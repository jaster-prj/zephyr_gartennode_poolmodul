#!/usr/bin/env python3
import selectors
import socket
import struct
import subprocess
import sys
import time

endpoints = {
    46100: (46101, 46111),
    46101: (46100, 46110),
}
expected = {0x109: 9, 0x10A: 10, 0x209: 9, 0x20A: 10}
observed = set()
sockets = {}
selector = selectors.DefaultSelector()
for gateway_port in endpoints:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", gateway_port))
    sock.setblocking(False)
    sockets[gateway_port] = sock
    selector.register(sock, selectors.EVENT_READ, gateway_port)

processes = [subprocess.Popen([path], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
             for path in sys.argv[1:3]]
try:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and len(observed) < len(expected):
        for key, _ in selector.select(timeout=0.1):
            gateway_port = key.data
            packet, _ = key.fileobj.recvfrom(2048)
            if len(packet) < 9:
                raise AssertionError(f"short UDP-CAN frame: {len(packet)}")
            can_id, dlc = struct.unpack_from("<IB", packet)
            if dlc == 1 and can_id in expected and packet[8] == expected[can_id]:
                observed.add(can_id)
            _, remote_port = endpoints[gateway_port]
            sockets[endpoints[gateway_port][0]].sendto(
                packet, ("127.0.0.1", remote_port))
    if observed != set(expected):
        raise AssertionError(f"missing CAN frames {set(expected) - observed}")
    print("UDP-CAN gateway observed bidirectional node 9/node 10 frames")
finally:
    for process in processes:
        process.terminate()
    for process in processes:
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    for sock in sockets.values():
        sock.close()
