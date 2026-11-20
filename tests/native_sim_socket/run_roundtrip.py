#!/usr/bin/env python3
import socket
import struct
import subprocess
import sys
import time

MAGIC = b"GNS1"
VERSION = 1
HEADER = struct.Struct("!4sBBH")
INPUT_TOPIC = b"garden/garden-1/modules/pool-1/input/ph"
STATE_TOPIC = b"garden/garden-1/modules/pool-1/state/sensor/ph"
PAYLOAD = b"7.25"

def frame(topic, payload):
    return HEADER.pack(MAGIC, VERSION, len(topic), len(payload)) + topic + payload

def parse(packet):
    magic, version, topic_len, payload_len = HEADER.unpack(packet[:HEADER.size])
    assert magic == MAGIC and version == VERSION
    topic_start = HEADER.size
    payload_start = topic_start + topic_len
    assert len(packet) == payload_start + payload_len
    return packet[topic_start:payload_start], packet[payload_start:]

executable = sys.argv[1]
peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
peer.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
peer.bind(("127.0.0.1", 46002))
peer.settimeout(8)
process = subprocess.Popen([executable], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
try:
    packet, simulator = peer.recvfrom(512)
    assert parse(packet) == (INPUT_TOPIC, PAYLOAD)
    peer.sendto(frame(INPUT_TOPIC, PAYLOAD), simulator)
    packet, _ = peer.recvfrom(512)
    assert parse(packet) == (STATE_TOPIC, PAYLOAD)
    time.sleep(0.5)
    process.terminate()
    output, _ = process.communicate(timeout=4)
    assert "UDP framed socket roundtrip passed" in output, output
    print("UDP framed socket roundtrip passed")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    peer.close()
