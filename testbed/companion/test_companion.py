import threading
import unittest
from types import SimpleNamespace

from companion import MqttSocketCompanion
from ipc import decode_frame, encode_frame
from topics import ModuleTopics


class FakeSocket:
    def __init__(self):
        self.sent = []

    def sendto(self, data, address):
        self.sent.append((data, address))


class FakeClient:
    def __init__(self):
        self.subscriptions = []
        self.publications = []

    def subscribe(self, topics):
        self.subscriptions.append(topics)

    def publish(self, topic, payload, qos, retain):
        self.publications.append((topic, payload, qos, retain))


class CompanionTests(unittest.TestCase):
    def setUp(self):
        self.client = FakeClient()
        self.sock = FakeSocket()
        self.topics = ModuleTopics.for_module("g1", "pool1")
        self.relay = MqttSocketCompanion(
            self.client, self.sock, ("127.0.0.1", 46003), self.topics
        )

    def test_subscribes_again_after_reconnect(self):
        for _ in range(2):
            self.relay._on_connect(self.client, None, None, 0)
        self.assertEqual(len(self.client.subscriptions), 2)
        self.assertEqual(
            self.client.subscriptions[0],
            [
                (self.topics.ph_input, 1),
                (self.topics.orp_input, 1),
                (self.topics.temperature_environment, 1),
                (self.topics.temperature_rom_id, 1),
            ],
        )

    def test_connection_loss_and_reconnect_resubscribe(self):
        self.relay._on_connect(self.client, None, None, 0)
        self.relay._on_disconnect(self.client, None, None, 1)
        self.assertFalse(self.relay.connected)
        self.relay._on_connect(self.client, None, None, 0)
        self.assertTrue(self.relay.connected)
        self.assertEqual(len(self.client.subscriptions), 2)

    def test_forwards_payload_bytes_unchanged(self):
        payload = b"\x00 7.25 \xff"
        message = SimpleNamespace(topic=self.topics.ph_input, payload=payload)
        self.relay._on_message(self.client, None, message)
        frame = decode_frame(self.sock.sent[0][0])
        self.assertEqual(frame.topic, self.topics.ph_input)
        self.assertEqual(frame.payload, payload)
        self.assertEqual(self.sock.sent[0][1], ("127.0.0.1", 46003))

    def test_forwards_dashboard_temperature_to_simulator(self):
        payload = b"-4.75"
        self.relay._on_message(
            self.client, None,
            SimpleNamespace(topic=self.topics.temperature_environment, payload=payload),
        )
        frame = decode_frame(self.sock.sent[0][0])
        self.assertEqual(frame.topic, self.topics.temperature_environment)
        self.assertEqual(frame.payload, payload)

    def test_forwards_dashboard_rom_id_to_simulator(self):
        payload = b"2801000000000029"
        self.relay._on_message(
            self.client, None,
            SimpleNamespace(topic=self.topics.temperature_rom_id, payload=payload),
        )
        frame = decode_frame(self.sock.sent[0][0])
        self.assertEqual(frame.topic, self.topics.temperature_rom_id)
        self.assertEqual(frame.payload, payload)

    def test_replays_dashboard_rom_id_after_module_restart(self):
        payload = b"2801000000000029"
        self.relay._on_message(
            self.client, None,
            SimpleNamespace(topic=self.topics.temperature_rom_id, payload=payload),
        )
        self.sock.sent.clear()
        self.relay.connected = True

        stop = threading.Event()

        class RestartingSimulatorSocket(FakeSocket):
            def recvfrom(self, _size):
                stop.set()
                raise TimeoutError

        self.relay.ipc_socket = RestartingSimulatorSocket()
        self.relay.serve(stop)

        frame = decode_frame(self.relay.ipc_socket.sent[0][0])
        self.assertEqual(frame.topic, self.topics.temperature_rom_id)
        self.assertEqual(frame.payload, payload)
        self.assertEqual(
            self.relay.ipc_socket.sent[0][1], ("127.0.0.1", 46003)
        )

    def test_retained_and_malformed_sensor_payloads_remain_opaque(self):
        for payload in (b"", b"not-a-number", b"\x00\xff"):
            message = SimpleNamespace(
                topic=self.topics.orp_input, payload=payload, retain=True
            )
            self.relay._on_message(self.client, None, message)
        self.assertEqual(
            [decode_frame(datagram).payload for datagram, _ in self.sock.sent],
            [b"", b"not-a-number", b"\x00\xff"],
        )

    def test_ignores_unsubscribed_topics(self):
        self.relay._on_message(
            self.client, None, SimpleNamespace(topic="other/topic", payload=b"x")
        )
        self.assertEqual(self.sock.sent, [])

    def test_publishes_simulator_state_payload_unchanged(self):
        topic = self.topics.ph_state

        class ReceivingSocket:
            def __init__(self):
                self.first = True

            def recvfrom(self, _size):
                if self.first:
                    self.first = False
                    return encode_frame(topic, b"\x00 7.25 \xff"), ("127.0.0.1", 0)
                stop.set()
                raise TimeoutError

        # Exercise the state validation and publish path with a one-event socket.
        self.relay.ipc_socket = ReceivingSocket()
        stop = threading.Event()

        def serve_once():
            try:
                self.relay.serve(stop)
            except TimeoutError:
                pass

        serve_once()
        self.assertEqual(
            self.client.publications,
            [(self.topics.ph_state, b"\x00 7.25 \xff", 1, False)],
        )

    def test_retains_pump_actor_state(self):
        topic = f"{self.topics.actor_state.removesuffix('#')}waterpump"

        class ReceivingSocket:
            def __init__(self):
                self.first = True

            def recvfrom(self, _size):
                if self.first:
                    self.first = False
                    return encode_frame(topic, b"ON"), ("127.0.0.1", 0)
                stop.set()
                raise TimeoutError

        self.relay.ipc_socket = ReceivingSocket()
        stop = threading.Event()
        try:
            self.relay.serve(stop)
        except TimeoutError:
            pass
        self.assertEqual(self.client.publications, [(topic, b"ON", 1, True)])


if __name__ == "__main__":
    unittest.main()
