import unittest

from ipc import decode_frame, encode_frame
from topics import ModuleTopics


class FrameTests(unittest.TestCase):
    def test_round_trip_keeps_opaque_payload(self):
        payload = b"\x00 7.25 \xff"
        frame = decode_frame(encode_frame("garden/g1/modules/p1/input/ph", payload))
        self.assertEqual(frame.topic, "garden/g1/modules/p1/input/ph")
        self.assertEqual(frame.payload, payload)

    def test_rejects_bad_magic_version_and_lengths(self):
        valid = encode_frame("topic", b"value")
        with self.assertRaises(ValueError):
            decode_frame(b"BAD!" + valid[4:])
        with self.assertRaises(ValueError):
            decode_frame(valid[:4] + b"\x02" + valid[5:])
        with self.assertRaises(ValueError):
            decode_frame(valid[:-1])
        with self.assertRaises(ValueError):
            decode_frame(valid, max_datagram=8)

    def test_module_scoped_topics(self):
        topics = ModuleTopics.for_module("garden-1", "pool_2")
        self.assertEqual(topics.ph_input, "garden/garden-1/modules/pool_2/input/ph")
        self.assertEqual(topics.orp_input, "garden/garden-1/modules/pool_2/input/orp")
        self.assertEqual(topics.ph_state, "garden/garden-1/modules/pool_2/state/measurement/ph")
        with self.assertRaises(ValueError):
            ModuleTopics.for_module("../bad", "pool")


if __name__ == "__main__":
    unittest.main()
