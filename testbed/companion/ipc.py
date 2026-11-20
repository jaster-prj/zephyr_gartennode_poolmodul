"""Versioned datagram framing between the MQTT companion and native_sim."""

from dataclasses import dataclass
import struct

MAGIC = b"GNS1"
VERSION = 1
HEADER = struct.Struct("!4sBBH")
DEFAULT_MAX_DATAGRAM = 512


@dataclass(frozen=True)
class Frame:
    topic: str
    payload: bytes


def encode_frame(topic: str, payload: bytes, max_datagram: int = DEFAULT_MAX_DATAGRAM) -> bytes:
    topic_bytes = topic.encode("utf-8")
    if not topic_bytes or b"\0" in topic_bytes or len(topic_bytes) > 0xFF:
        raise ValueError("topic must be non-empty UTF-8 and fit in one length byte")
    if not isinstance(payload, bytes):
        raise TypeError("payload must be bytes")
    if len(payload) > 0xFFFF:
        raise ValueError("payload does not fit in the frame length field")
    total = HEADER.size + len(topic_bytes) + len(payload)
    if total > max_datagram:
        raise ValueError("frame exceeds configured datagram limit")
    return HEADER.pack(MAGIC, VERSION, len(topic_bytes), len(payload)) + topic_bytes + payload


def decode_frame(datagram: bytes, max_datagram: int = DEFAULT_MAX_DATAGRAM) -> Frame:
    if len(datagram) > max_datagram or len(datagram) < HEADER.size:
        raise ValueError("invalid datagram size")
    magic, version, topic_length, payload_length = HEADER.unpack_from(datagram)
    if magic != MAGIC or version != VERSION or topic_length == 0:
        raise ValueError("invalid frame header")
    expected = HEADER.size + topic_length + payload_length
    if len(datagram) != expected:
        raise ValueError("frame length does not match header")
    topic_bytes = datagram[HEADER.size : HEADER.size + topic_length]
    if b"\0" in topic_bytes:
        raise ValueError("topic contains NUL")
    try:
        topic = topic_bytes.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise ValueError("topic is not UTF-8") from exc
    return Frame(topic=topic, payload=datagram[HEADER.size + topic_length :])
