#!/usr/bin/env python3
import selectors
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

HEADER = struct.Struct("!4sBBH")
PORT = 46003

def frame(topic, payload, version=1):
    return HEADER.pack(b"GNS1", version, len(topic), len(payload)) + topic + payload

process = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, bufsize=1)
output = []
selector = selectors.DefaultSelector()
selector.register(process.stdout, selectors.EVENT_READ)
peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
companion = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
companion.bind(("127.0.0.1", 46004))
companion.settimeout(2)
deadline = time.monotonic() + 8
sent = False
ph_readback = None
passed = False
try:
    while time.monotonic() < deadline:
        for key, _ in selector.select(timeout=0.2):
            line = key.fileobj.readline()
            output.append(line)
            if "native-sim input receiver ready" in line and not sent:
                peer.sendto(frame(b"garden/garden-1/modules/pool-1/input/ph", b"8.0"), ("127.0.0.1", PORT))
                peer.sendto(frame(b"garden/garden-1/modules/pool-1/input/orp", b"250"), ("127.0.0.1", PORT))
                peer.sendto(frame(b"garden/garden-1/modules/pool-1/input/ph", b"15.0"), ("127.0.0.1", PORT))
                peer.sendto(frame(b"garden/garden-1/modules/pool-1/input/orp", b"9999"), ("127.0.0.1", PORT))
                peer.sendto(frame(b"garden/garden-1/modules/pool-1/input/ph", b"8.0", version=2), ("127.0.0.1", PORT))
                sent = True
            if "receiver pH mv=" in line:
                ph_readback = int(line.split("receiver pH mv=", 1)[1])
                if not 1798 <= ph_readback <= 1801:
                    raise AssertionError("unexpected pH ADC reading: " + line)
            if "receiver ORP mv=" in line:
                orp_readback = int(line.split("receiver ORP mv=", 1)[1])
                if ph_readback is None or not 1897 <= orp_readback <= 1902:
                    raise AssertionError("unexpected ORP ADC reading: " + line)
                passed = True
                break
            if "failed" in line.lower() or "incorrect" in line.lower():
                raise AssertionError("".join(output))
        if passed or process.poll() is not None:
            break
    if process.poll() is not None:
        output.append(process.stdout.read())
    if not passed:
        raise AssertionError("Native-sim ADC/model/receiver test failed:\\n" + "".join(output))
    expected = {
        (b"garden/garden-1/modules/pool-1/state/measurement/ph", b"7.25"),
        (b"garden/garden-1/modules/pool-1/state/actor/waterpump", b"ON"),
    }
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "testbed" / "companion"))
    from ipc import decode_frame
    observed = set()
    for _ in range(2):
        datagram, _ = companion.recvfrom(512)
        event = decode_frame(datagram)
        observed.add((event.topic.encode("utf-8"), event.payload))
    if observed != expected:
        raise AssertionError(f"unexpected simulator state events: {observed}")
    print("ADC emulator, sensor models, receiver, and state-event checks passed")
finally:
    companion.close()
    peer.close()
    if process.poll() is None:
        process.kill()
        process.wait(timeout=2)
