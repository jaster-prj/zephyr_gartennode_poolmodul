"""Opaque MQTT-to-UDP and UDP-to-MQTT relay for the native-sim testbed."""

import logging
import socket
from typing import Any

from ipc import decode_frame, encode_frame
from topics import ModuleTopics

LOG = logging.getLogger("pool-test-companion")


def _reason_value(reason_code: Any) -> int:
    return int(getattr(reason_code, "value", reason_code))


class MqttSocketCompanion:
    def __init__(
        self,
        client: Any,
        ipc_socket: socket.socket,
        simulator_address: tuple[str, int],
        topics: ModuleTopics,
    ) -> None:
        self.client = client
        self.ipc_socket = ipc_socket
        self.simulator_address = simulator_address
        self.topics = topics
        self.input_topics = {topics.ph_input, topics.orp_input}
        self.state_topics = {topics.ph_state, topics.orp_state}
        self.connected = False
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if _reason_value(reason_code) != 0:
            self.connected = False
            LOG.warning("MQTT connection failed: %s", reason_code)
            return
        self.connected = True
        # A clean session and a new subscription cause the broker to replay
        # retained input values after startup or a connection loss.
        client.subscribe([(self.topics.ph_input, 1), (self.topics.orp_input, 1)])
        LOG.info("Subscribed to module pH and ORP inputs")

    def _on_disconnect(
        self, client: Any, userdata: Any, disconnect_flags: Any, reason_code: Any, properties: Any = None
    ) -> None:
        del client, userdata, disconnect_flags, properties
        self.connected = False
        if _reason_value(reason_code) != 0:
            LOG.info("MQTT connection lost; client will reconnect: %s", reason_code)

    def _on_message(self, client: Any, userdata: Any, message: Any) -> None:
        del client, userdata
        if message.topic not in self.input_topics:
            return
        try:
            datagram = encode_frame(message.topic, bytes(message.payload))
            self.ipc_socket.sendto(datagram, self.simulator_address)
        except (OSError, TypeError, ValueError):
            LOG.exception("Could not forward MQTT input topic %s", message.topic)

    def serve(self, stop: Any) -> None:
        """Publish simulator state events until the supplied stop event is set."""
        while not stop.is_set():
            try:
                datagram, _ = self.ipc_socket.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                if stop.is_set():
                    break
                raise
            try:
                frame = decode_frame(datagram)
            except ValueError:
                LOG.warning("Ignoring malformed simulator datagram")
                continue
            if frame.topic not in self.state_topics and not frame.topic.startswith(
                self.topics.actor_state.removesuffix("#")
            ):
                LOG.warning("Ignoring unsupported simulator state topic %s", frame.topic)
                continue
            # Measurement ownership remains in the firmware. Do not decode,
            # normalize, or otherwise transform the MQTT payload.
            is_actor_state = frame.topic.startswith(
                self.topics.actor_state.removesuffix("#")
            )
            self.client.publish(
                frame.topic,
                frame.payload,
                qos=1,
                retain=is_actor_state,
            )


def run_from_environment() -> None:
    import os
    import threading

    import paho.mqtt.client as mqtt

    garden_id = os.environ["GARDEN_ID"]
    module_id = os.environ["MODULE_ID"]
    broker_host = os.environ.get("MQTT_HOST", "mosquitto")
    broker_port = int(os.environ.get("MQTT_PORT", "1883"))
    sim_host = os.environ.get("SIMULATOR_HOST", "127.0.0.1")
    sim_port = int(os.environ.get("SIMULATOR_IPC_PORT", "46003"))
    companion_port = int(os.environ.get("COMPANION_IPC_PORT", "46004"))
    client_id = os.environ.get("MQTT_CLIENT_ID", f"pool-{module_id}-test")

    ipc_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ipc_socket.bind(("0.0.0.0", companion_port))
    ipc_socket.settimeout(0.2)
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id, clean_session=True)
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    relay = MqttSocketCompanion(
        client,
        ipc_socket,
        (sim_host, sim_port),
        ModuleTopics.for_module(garden_id, module_id),
    )
    stop = threading.Event()
    try:
        client.connect(broker_host, broker_port, keepalive=30)
        client.loop_start()
        relay.serve(stop)
    finally:
        stop.set()
        client.loop_stop()
        client.disconnect()
        ipc_socket.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    run_from_environment()
