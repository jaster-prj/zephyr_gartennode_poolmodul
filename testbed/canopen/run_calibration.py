#!/usr/bin/env python3
"""Write configured CANopen SDO calibration values to the pool simulator."""

import socket
import sys

from sdo import UdpCanSdoClient, configure_calibration

GATEWAY_PORT = 46100
gateway = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    client = UdpCanSdoClient(gateway, ("127.0.0.1", GATEWAY_PORT), timeout=1.0)
    verified = configure_calibration(client)
    print(f"CANopen calibration values written: {verified}")
finally:
    gateway.close()
