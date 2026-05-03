#ifndef SIM_COMMAND_LISTENER_H
#define SIM_COMMAND_LISTENER_H

#include <cstdint>

// Starts a background thread that listens on 127.0.0.1:<port> for a single
// byte and fires throw_hand_launched_aircraft() on receive. Call once at
// startup after globals are initialized. Idempotent — second call with the
// same port is a no-op. exit(1) on bind failure (loud, no fallback).
//
// If hand_launch_mode is false, bytes are accepted but the throw call is
// skipped (logged as a warning) so the harness interface is uniform across
// launch modes.
void StartSimCommandListener(uint16_t port);

void StopSimCommandListener(void);  // SIGINT handler hook

#endif
