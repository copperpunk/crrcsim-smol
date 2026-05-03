#include "sim_command_listener.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "global.h"

extern void throw_hand_launched_aircraft();

static int        s_listen_fd = -1;
static pthread_t  s_thread = 0;
static bool       s_running = false;

static void* listener_loop(void* /*arg*/) {
    while (s_running) {
        int client = accept(s_listen_fd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) {
                continue;
            }
            if (s_running) {
                fprintf(stderr, "sim_command_listener: accept failed: %s\n",
                        strerror(errno));
            }
            return nullptr;
        }
        char byte;
        ssize_t n = recv(client, &byte, 1, 0);
        close(client);
        if (n != 1) {
            continue;
        }
        if (Global::hand_launch_mode) {
            printf("sim_command_listener: throw fired by command-port byte\n");
            // throw_hand_launched_aircraft() mutates FDM state (initAirplaneState).
            // fdm_thread holds Global::lockFDM around its update() — take the
            // same lock here so the listener thread does not race the FDM tick.
            Global::lockFDM();
            throw_hand_launched_aircraft();
            Global::unlockFDM();
        } else {
            fprintf(stderr,
                    "WARNING: sim_command_listener received byte but "
                    "hand_launch_mode is false; ignoring.\n");
        }
    }
    return nullptr;
}

void StartSimCommandListener(uint16_t port) {
    if (port == 0 || s_running) {
        return;
    }
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        fprintf(stderr, "CRRCSim cannot create command-port socket: %s\n",
                strerror(errno));
        exit(1);
    }
    int yes = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "CRRCSim cannot bind --command-port %u: %s\n",
                port, strerror(errno));
        exit(1);
    }
    if (listen(s_listen_fd, 1) < 0) {
        fprintf(stderr, "CRRCSim cannot listen on --command-port %u: %s\n",
                port, strerror(errno));
        exit(1);
    }
    s_running = true;
    pthread_create(&s_thread, nullptr, listener_loop, nullptr);
    printf("sim_command_listener: listening on 127.0.0.1:%u\n", port);
}

void StopSimCommandListener(void) {
    if (!s_running) {
        return;
    }
    s_running = false;
    shutdown(s_listen_fd, SHUT_RDWR);
    close(s_listen_fd);
    s_listen_fd = -1;
    pthread_join(s_thread, nullptr);
}
