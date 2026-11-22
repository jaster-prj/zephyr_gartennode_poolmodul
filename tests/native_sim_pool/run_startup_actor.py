"""Standalone pool startup/actor regression; no MQTT or running garden required.

Build with testbed/canopen/pool_can.overlay and pass the native executable.
Run in an isolated network namespace: uses UDP 46100/46110 and 46003/46004.
"""

from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "testbed/companion"))
from ipc import decode_frame, encode_frame

CAN_FRAME = struct.Struct("<IBB2x64s")
NODE = 9
MODULE = ("127.0.0.1", 46110)
BASE = "garden/native-test/modules/pool-1"


def check(binary):
    binary = str(Path(binary).resolve())
    temporary_root = "/tmp/opencode" if Path("/tmp/opencode").is_dir() else None
    with (
        tempfile.TemporaryDirectory(prefix="pool-smoke-", dir=temporary_root) as directory,
        tempfile.TemporaryFile() as log,
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as can,
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as ipc,
    ):
        can.bind(("127.0.0.1", 46100))
        ipc.bind(("127.0.0.1", 46004))
        process = subprocess.Popen([binary], stdout=log, stderr=log, cwd=directory)
        heartbeats = []
        actors = []

        def send(can_id, payload):
            can.sendto(CAN_FRAME.pack(can_id, len(payload), 0, payload), MODULE)

        def wait_for(predicate, request=None, timeout=8):
            deadline = time.monotonic() + timeout
            next_request = 0
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise AssertionError(f"Simulator exited: {process.returncode}")
                if request and time.monotonic() >= next_request:
                    request()
                    next_request = time.monotonic() + 0.5
                for incoming in select.select([can, ipc], [], [], 0.1)[0]:
                    packet, _peer = incoming.recvfrom(2048)
                    if incoming is can:
                        assert len(packet) == CAN_FRAME.size, len(packet)
                        can_id, dlc, _flags, data = CAN_FRAME.unpack(packet)
                        data = data[:dlc]
                        if can_id == 0x700 + NODE and len(data) == 1:
                            heartbeats.append(data[0])
                        event = ("can", can_id, data)
                    else:
                        frame = decode_frame(packet)
                        event = ("state", frame.topic, frame.payload)
                        actors.append(event)
                    if predicate(event):
                        return event
            raise AssertionError("Timed out waiting for native pool feedback")

        def sdo(value=None):
            request = struct.pack(
                "<BHB4s", 0x40 if value is None else 0x2F, 0x2104, 0,
                bytes(4) if value is None else bytes([value, 0, 0, 0]),
            )
            response = wait_for(
                lambda event: event[0] == "can" and event[1] == 0x580 + NODE
                and event[2][1:4] == request[1:4],
                lambda: send(0x600 + NODE, request),
            )[2]
            assert response[0] == (0x4F if value is None else 0x60), response.hex()
            return response[4] if value is None else None

        def actor(value, since=0):
            expected = ("state", BASE + "/state/actor/waterpump", value)
            if expected not in actors[since:]:
                wait_for(lambda event: event == expected)

        try:
            wait_for(lambda event: event == ("can", 0x700 + NODE, b"\x05"))
            assert heartbeats[0] == 0, f"Boot-up must precede Operational: {heartbeats}"
            # No CAN requests: timers must keep working on an idle standalone bus.
            before = len(heartbeats)
            wait_for(lambda _event: len(heartbeats) >= before + 2, timeout=4)
            assert all(state == 5 for state in heartbeats[1:]), heartbeats
            print("PASS: boot-up then recurring Operational heartbeat on idle bus", flush=True)
            assert sdo() == 0, "Sampling pump must initially be disabled"

            # The firmware cannot publish until a valid input establishes its root.
            # Initial OFF must be retried, even though no GPIO transition occurs.
            ipc.sendto(encode_frame(BASE + "/input/ph", b"7.0"), ("127.0.0.1", 46003))
            actor(b"OFF")
            print("PASS: initial OFF actor state after input/companion handshake", flush=True)

            start = len(actors)
            sdo(1)
            actor(b"ON", start)
            assert sdo() == 1
            start = len(actors)
            sdo(0)
            actor(b"OFF", start)
            assert sdo() == 0
            print("PASS: SDO pump control produces ON/OFF actor feedback", flush=True)
        except Exception:
            log.seek(0)
            sys.stderr.write(log.read().decode(errors="replace"))
            raise
        finally:
            send(0x600 + NODE, bytes.fromhex("2f04210000000000"))
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)


if __name__ == "__main__":
    check(sys.argv[1] if len(sys.argv) > 1 else "build/native_sim/zephyr/zephyr.exe")
