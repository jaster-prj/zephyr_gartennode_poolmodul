"""Live MQTT-to-ADC-to-CANopen checks, enabled by the Compose test profile."""

import os
import socket
import struct
import time
import unittest

import paho.mqtt.client as mqtt

from expected_measurements import SensorTransfer, expected_ph, expected_orp
from sdo import UdpCanSdoClient, configure_calibration
from topics import ModuleTopics


@unittest.skipUnless(
    os.environ.get("RUN_CAN_INTEGRATION") == "1",
    "requires live Compose testbed",
)
class LiveCanIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.topics = ModuleTopics.for_module(
            os.getenv("GARDEN_ID", "garden-1"),
            os.getenv("MODULE_ID", "pool-1"),
        )
        cls.mqtt = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id="can-e2e-test",
            clean_session=True,
        )
        cls.mqtt.connect(os.getenv("MQTT_HOST", "mosquitto"), int(os.getenv("MQTT_PORT", "1883")), 10)
        cls.mqtt.loop_start()
        deadline = time.monotonic() + 5
        while not cls.mqtt.is_connected() and time.monotonic() < deadline:
            time.sleep(0.05)
        if not cls.mqtt.is_connected():
            raise RuntimeError("MQTT connection did not become ready")
        cls.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        cls.client = UdpCanSdoClient(cls.sock, ("127.0.0.1", 46100), timeout=2)
        cls.client.download_unconfirmed(0x2102, 0, 1, 1)
        time.sleep(1.0)

    @classmethod
    def tearDownClass(cls):
        cls.sock.close()
        cls.mqtt.loop_stop()
        cls.mqtt.disconnect()

    def publish_condition(self, topic, value):
        result = self.mqtt.publish(topic, str(value), qos=1, retain=False)
        result.wait_for_publish(timeout=3)
        self.assertTrue(result.is_published(), "MQTT condition publish timed out")
        time.sleep(1.2)

    def test_calibration_writes_are_sent_without_readback(self):
        values = configure_calibration(self.client)
        self.assertEqual(values[(0x2106, 1)], 70)
        self.assertEqual(values[(0x2107, 2)], 3500)
        self.assertEqual(values[(0x2111, 0)], 25)

    def test_ph_mqtt_condition_matches_canopen_measurement(self):
        configure_calibration(self.client)
        value = 7.0
        self.publish_condition(self.topics.ph_input, value)
        expected = expected_ph(value)
        actual = struct.unpack(
            "<f", self.client.upload(0x2109, 0, 4).to_bytes(4, "little")
        )[0]
        self.assertAlmostEqual(actual, expected, delta=0.03)

    def test_orp_mqtt_condition_matches_canopen_measurement(self):
        configure_calibration(self.client)
        value = -470
        self.publish_condition(self.topics.orp_input, value)
        expected = expected_orp(value, transfer=SensorTransfer(1, 800))
        deadline = time.monotonic() + 2.0
        actual = 0
        while actual == 0 and time.monotonic() < deadline:
            encoded = self.client.upload(0x2113, 0, 2)
            actual = encoded - 0x10000 if encoded & 0x8000 else encoded
            if actual == 0:
                time.sleep(0.05)
        self.assertAlmostEqual(actual, expected, delta=1.0)
