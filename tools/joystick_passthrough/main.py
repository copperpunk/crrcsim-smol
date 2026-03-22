#!/usr/bin/env python3
"""Joystick passthrough for CRRCSim smol protocol.

Sits in the lockstep loop in place of the smol flight controller:
  1. Send SimActuatorMsg (joystick → controls)
  2. Sim advances FDM, sends sensor messages
  3. Drain all sensor messages, extract IMU sequence
  4. Repeat

Usage:
    cd crrcsim-ardupilot/tools
    python -m joystick_passthrough.main [--config passthrough.yaml] [--port 9002]
"""
import argparse
import ctypes
import importlib.util
import socket
import sys
import time
from pathlib import Path

import pygame
import yaml


def _find_repo_root() -> Path:
    d = Path(__file__).resolve().parent
    while d != d.parent:
        if (d / ".git").exists():
            return d
        d = d.parent
    raise RuntimeError("Could not find repo root")


_sil_proto_path = (
    _find_repo_root() / "src" / "mod_inputdev" / "inputdev_apm" / "sil-protocol" / "sil_protocol.py"
)
_spec = importlib.util.spec_from_file_location("sil_protocol", _sil_proto_path)
sil_protocol = importlib.util.module_from_spec(_spec)
sys.modules["sil_protocol"] = sil_protocol
_spec.loader.exec_module(sil_protocol)

SimMsgHeader = sil_protocol.SimMsgHeader
SimImuMsg = sil_protocol.SimImuMsg
SimActuatorMsg = sil_protocol.SimActuatorMsg
SIM_MSG_IMU = sil_protocol.SIM_MSG_IMU
SIM_MSG_ACTUATOR = sil_protocol.SIM_MSG_ACTUATOR

HEADER_SIZE = ctypes.sizeof(SimMsgHeader)
DEFAULT_CONFIG = Path(__file__).resolve().parent / "passthrough.yaml"
SIM_PORT = 9002
CHANNEL_NAMES = ["aileron", "elevator", "throttle", "rudder"]


def load_config(path: Path) -> dict:
    with open(path) as f:
        return yaml.safe_load(f)


def calibrate(raw: float, ch: dict) -> float:
    center = ch["center"]
    min_val = ch["min"]
    max_val = ch["max"]

    if ch["range"] == "unidir":
        normalized = (raw - min_val) / (max_val - min_val)
        if ch["inverted"]:
            normalized = 1.0 - normalized
        return max(0.0, min(1.0, normalized))

    if raw >= center:
        denom = max_val - center
        normalized = (raw - center) / denom if denom != 0.0 else 0.0
    else:
        denom = center - min_val
        normalized = (raw - center) / denom if denom != 0.0 else 0.0

    if ch["inverted"]:
        normalized = -normalized

    return max(-1.0, min(1.0, normalized))


def deadzone(value: float, dz: float) -> float:
    if abs(value) < dz:
        return 0.0
    sign = 1.0 if value > 0.0 else -1.0
    return sign * (abs(value) - dz) / (1.0 - dz)


def make_actuator(
    sequence: int, aileron: float, elevator: float, throttle: float, rudder: float
) -> bytes:
    msg = SimActuatorMsg()
    msg.header.msg_type = SIM_MSG_ACTUATOR
    msg.header.payload_len = ctypes.sizeof(SimActuatorMsg) - HEADER_SIZE
    msg.sequence = sequence
    msg.aileron = aileron
    msg.elevator = elevator
    msg.throttle = throttle
    msg.rudder = rudder
    return bytes(msg)


def read_channels(joystick: pygame.joystick.JoystickType, cfg: dict) -> dict[str, float]:
    pygame.event.pump()
    raw_axes = [joystick.get_axis(i) for i in range(joystick.get_numaxes())]
    dz = cfg["deadzone"]
    result = {}
    for name in CHANNEL_NAMES:
        ch = cfg["channels"][name]
        raw = raw_axes[ch["axis"]]
        val = calibrate(raw, ch)
        if ch["range"] == "bidir":
            val = deadzone(val, dz)
        result[name] = val
    return result


def drain_until_imu(sock: socket.socket) -> tuple[int, float]:
    imu_size = ctypes.sizeof(SimImuMsg)
    sequence = 0
    time_sec = 0.0

    while True:
        data, _ = sock.recvfrom(512)
        if len(data) < HEADER_SIZE:
            continue
        if data[0] == SIM_MSG_IMU and len(data) >= imu_size:
            imu = SimImuMsg.from_buffer_copy(data)
            sequence = imu.sequence
            time_sec = imu.time_sec
            sock.setblocking(False)
            try:
                while True:
                    extra, _ = sock.recvfrom(512)
                    if extra[0] == SIM_MSG_IMU and len(extra) >= imu_size:
                        imu = SimImuMsg.from_buffer_copy(extra)
                        sequence = imu.sequence
                        time_sec = imu.time_sec
            except BlockingIOError:
                pass
            finally:
                sock.setblocking(True)
            return sequence, time_sec


def run(config_path: Path, port: int) -> None:
    cfg = load_config(config_path)
    print(f"Config: {config_path}")

    pygame.init()
    pygame.joystick.init()
    if pygame.joystick.get_count() == 0:
        print("No joystick found. Connect a controller and try again.")
        sys.exit(1)
    joystick = pygame.joystick.Joystick(0)
    joystick.init()
    print(f"Joystick: {joystick.get_name()} ({joystick.get_numaxes()} axes)")

    for name in CHANNEL_NAMES:
        ch = cfg["channels"][name]
        inv = "inv" if ch["inverted"] else "   "
        print(f"  {name:10s}  axis={ch['axis']}  {inv}  {ch['range']}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 0))
    sim_addr = ("127.0.0.1", port)

    sock.sendto(make_actuator(0, 0.0, 0.0, 0.0, 0.0), sim_addr)
    print(f"Connected to sim on UDP port {port}, waiting for first IMU...")

    cycle = 0
    t_start = time.monotonic()

    try:
        while True:
            sequence, sim_time = drain_until_imu(sock)
            ch = read_channels(joystick, cfg)

            sock.sendto(
                make_actuator(
                    sequence,
                    ch["aileron"] * 0.5,
                    ch["elevator"] * 0.5,
                    ch["throttle"],
                    ch["rudder"] * 0.5,
                ),
                sim_addr,
            )

            cycle += 1
            if cycle % 100 == 0:
                wall = time.monotonic() - t_start
                print(
                    f"[{cycle:6d}] sim_t={sim_time:.2f}s  wall={wall:.1f}s  "
                    f"A={ch['aileron']:+.2f} E={ch['elevator']:+.2f} "
                    f"T={ch['throttle']:.2f} R={ch['rudder']:+.2f}"
                )

    except KeyboardInterrupt:
        print(f"\nStopped after {cycle} cycles.")
    finally:
        sock.close()
        pygame.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Joystick passthrough for CRRCSim SIL")
    parser.add_argument(
        "--config", type=Path, default=DEFAULT_CONFIG, help="Path to passthrough.yaml"
    )
    parser.add_argument(
        "--port", type=int, default=SIM_PORT, help=f"Sim UDP port (default: {SIM_PORT})"
    )
    args = parser.parse_args()
    run(args.config, args.port)
