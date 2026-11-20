"""Broker-backed retained/reconnect test; MQTT_HOST/MQTT_PORT select Mosquitto.

Run with RUN_MQTT_INTEGRATION=1 when a broker is available.
"""

import os
import socket
import threading
import unittest
import uuid

import paho.mqtt.client as mqtt

from companion import MqttSocketCompanion, _reason_value
from ipc import decode_frame
from topics import ModuleTopics


class BrokerRelayTest(unittest.TestCase):
    @unittest.skipUnless(
        os.environ.get("RUN_MQTT_INTEGRATION") == "1",
        "set RUN_MQTT_INTEGRATION=1 when a broker is available",
    )
    def test_retained_input_replay_and_reconnect(self):
        host = os.environ.get("MQTT_HOST", "127.0.0.1")
        port = int(os.environ.get("MQTT_PORT", "1883"))
        identity = uuid.uuid4().hex[:10]
        topics = ModuleTopics.for_module("test-garden", f"pool-{identity}")
        simulator = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        simulator.bind(("127.0.0.1", 0))
        simulator.settimeout(5)
        relay_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        relay_socket.bind(("127.0.0.1", 0))
        relay_socket.settimeout(0.2)

        publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"pub-{identity}")
        publisher.connect(host, port, keepalive=20)
        publisher.loop_start()
        retained_payload = b"7.25 \x00 opaque"
        publisher.publish(topics.ph_input, retained_payload, qos=1, retain=True).wait_for_publish()

        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"relay-{identity}",
            clean_session=True,
        )
        relay = MqttSocketCompanion(
            client, relay_socket, simulator.getsockname(), topics
        )
        connected = threading.Event()
        disconnected = threading.Event()
        prior_on_connect = client.on_connect
        prior_on_disconnect = client.on_disconnect

        def on_connect(*args):
            prior_on_connect(*args)
            if _reason_value(args[3]) == 0:
                connected.set()

        def on_disconnect(*args):
            prior_on_disconnect(*args)
            disconnected.set()

        client.on_connect = on_connect
        client.on_disconnect = on_disconnect
        try:
            client.connect(host, port, keepalive=20)
            client.loop_start()
            self.assertTrue(connected.wait(5), "relay did not connect to broker")
            first, _ = simulator.recvfrom(512)
            self.assertEqual(decode_frame(first).payload, retained_payload)

            # Reconnect with a clean session; the retained environment input
            # must be delivered again after the companion re-subscribes.
            connected.clear()
            client.disconnect()
            self.assertTrue(disconnected.wait(5), "relay disconnect callback not observed")
            disconnected.clear()
            client.reconnect()
            self.assertTrue(connected.wait(5), "relay did not reconnect")
            replay, _ = simulator.recvfrom(512)
            replayed = decode_frame(replay)
            self.assertEqual((replayed.topic, replayed.payload), (topics.ph_input, retained_payload))

            malformed = b"\xffnot-numeric\x00"
            publisher.publish(topics.orp_input, malformed, qos=1, retain=False).wait_for_publish()
            forwarded, _ = simulator.recvfrom(512)
            self.assertEqual(decode_frame(forwarded).payload, malformed)
        finally:
            client.loop_stop()
            client.disconnect()
            publisher.loop_stop()
            publisher.disconnect()
            relay_socket.close()
            simulator.close()


if __name__ == "__main__":
    unittest.main()
