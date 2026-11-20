#!/usr/bin/env python3
"""Small UDP-CAN observer/gateway for one native_sim pool node.

Client datagrams go to the gateway; CAN frames are forwarded unchanged to the
simulator. Frames emitted by the simulator are logged and returned to the most
recent client, allowing a single test-runner socket to observe CAN traffic and
perform SDO exchanges.
"""

import os
import socket
import struct

WIRE = struct.Struct("<IBB2x64s")
GATEWAY_HOST = os.environ.get("CAN_GATEWAY_HOST", "127.0.0.1")
GATEWAY_PORT = int(os.environ.get("CAN_GATEWAY_PORT", "46100"))
SIMULATOR_HOST = os.environ.get("CAN_SIMULATOR_HOST", "127.0.0.1")
SIMULATOR_PORT = int(os.environ.get("CAN_SIMULATOR_PORT", "46110"))


def main() -> None:
    gateway = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    gateway.bind((GATEWAY_HOST, GATEWAY_PORT))
    requester = None
    print(f"CAN observer listening on {GATEWAY_HOST}:{GATEWAY_PORT}", flush=True)
    try:
        while True:
            packet, source = gateway.recvfrom(2048)
            if len(packet) != WIRE.size:
                continue
            can_id, dlc, flags, data = WIRE.unpack(packet)
            if source[1] == SIMULATOR_PORT:
                print(
                    f"CAN tx id=0x{can_id:x} dlc={dlc} flags=0x{flags:02x} "
                    f"data={data[:dlc].hex()}",
                    flush=True,
                )
                if requester is not None:
                    gateway.sendto(packet, requester)
            else:
                requester = source
                print(
                    f"CAN rx id=0x{can_id:x} dlc={dlc} flags=0x{flags:02x} "
                    f"data={data[:dlc].hex()}",
                    flush=True,
                )
                gateway.sendto(packet, (SIMULATOR_HOST, SIMULATOR_PORT))
    finally:
        gateway.close()


if __name__ == "__main__":
    main()
