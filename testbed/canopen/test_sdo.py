import unittest

from sdo import CAN_WIRE, configure_calibration, UdpCanSdoClient


class FakeGateway:
    def __init__(self):
        self.sent = []
        self.pending = []

    def sendto(self, packet, address):
        self.sent.append((packet, address))
        request_id, dlc, flags, data = CAN_WIRE.unpack(packet)
        index = int.from_bytes(data[1:3], "little")
        subindex = data[3]
        if data[0] in (0x2F, 0x2B, 0x23):
            command = 0x60
            response_data = bytes((command,)) + data[1:4] + bytes(4)
        else:
            size = 1 if index == 0x2106 else 2
            command = {1: 0x4F, 2: 0x4B}[size]
            value = (
                (70 if subindex == 1 else 140)
                if index == 0x2106
                else (2048 if subindex == 1 else 3500)
                if index == 0x2107
                else 25
            )
            response_data = bytes((command,)) + data[1:4] + value.to_bytes(size, "little")
            response_data = response_data.ljust(8, b"\0")
        self.pending.append(
            (CAN_WIRE.pack(0x589, 8, flags, response_data.ljust(64, b"\0")), ("127.0.0.1", 1))
        )

    def recvfrom(self, _size):
        return self.pending.pop(0)

    def settimeout(self, _timeout):
        pass


class SdoTests(unittest.TestCase):
    def test_calibration_writes(self):
        gateway = FakeGateway()
        client = UdpCanSdoClient(gateway, ("127.0.0.1", 2))
        verified = configure_calibration(client)
        self.assertEqual(len(gateway.sent), 5)
        self.assertEqual(verified[(0x2106, 1)], 70)
        self.assertEqual(verified[(0x2107, 2)], 3500)
        self.assertEqual(verified[(0x2111, 0)], 25)


if __name__ == "__main__":
    unittest.main()
