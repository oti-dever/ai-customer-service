import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu import reader


class FakeRect:
    left = 10
    top = 20
    right = 110
    bottom = 220


class FakeControl:
    BoundingRectangle = FakeRect()
    Name = "buyer 2026-06-04 12:00:00 hello"

    def SetFocus(self):
        return None

    def GetChildren(self):
        return []


class FakeWebControl(FakeControl):
    Name = ""


class FakeDetector:
    def __init__(self, message_web=None):
        self.find_current_chat_calls = 0
        self.find_message_display_calls = 0
        self.find_message_web_calls = 0
        self.message_web = message_web

    def find_current_chat(self):
        self.find_current_chat_calls += 1
        raise AssertionError("find_current_chat should not be called when chat_root is reusable")

    def find_message_display(self, chat_root):
        self.find_message_display_calls += 1
        return FakeControl()

    def find_message_web(self, chat_root):
        self.find_message_web_calls += 1
        return self.message_web


class QianniuReaderTests(unittest.TestCase):
    def test_parse_accessible_messages_keeps_visible_message_sequence(self):
        messages = reader.parse_accessible_messages(
            [
                "tb1001 2026-06-04 12:00:00 hello",
                "line two",
                "agent 2026-06-04 12:01:00 reply",
                reader.READ_STATUS,
                "tb1001 2026-06-04 12:02:00 next question",
            ]
        )

        self.assertEqual(len(messages), 3)
        self.assertEqual([item.sender for item in messages], ["tb1001", "agent", "tb1001"])
        self.assertEqual(
            [item.timestamp for item in messages],
            ["2026-06-04 12:00:00", "2026-06-04 12:01:00", "2026-06-04 12:02:00"],
        )
        self.assertEqual([item.direction for item in messages], ["inbound", "outbound", "inbound"])
        self.assertEqual(messages[0].text, "hello\nline two")
        self.assertEqual(messages[1].text, "reply")
        self.assertEqual(messages[1].status, reader.READ_STATUS)
        self.assertEqual(messages[2].text, "next question")

    def test_parse_copied_messages_keeps_visible_message_sequence(self):
        text = "\n".join(
            [
                "tb1001 2026-06-04 12:00:00",
                "hello",
                "line two",
                "agent 2026-06-04 12:01:00",
                "reply",
                reader.READ_STATUS,
                "tb1001 2026-06-04 12:02:00",
                "next question",
            ]
        )

        messages = reader.parse_copied_messages(text)

        self.assertEqual(len(messages), 3)
        self.assertEqual([item.sender for item in messages], ["tb1001", "agent", "tb1001"])
        self.assertEqual(
            [item.timestamp for item in messages],
            ["2026-06-04 12:00:00", "2026-06-04 12:01:00", "2026-06-04 12:02:00"],
        )
        self.assertEqual([item.direction for item in messages], ["inbound", "outbound", "inbound"])
        self.assertEqual(messages[0].text, "hello\nline two")
        self.assertEqual(messages[1].text, "reply")
        self.assertEqual(messages[1].status, reader.READ_STATUS)
        self.assertEqual(messages[2].text, "next question")

    def test_read_visible_messages_debug_parses_accessible_texts(self):
        q_reader = reader.QianniuReader()

        def fake_read_visible_texts(**_kwargs):
            return reader.MessageReadResult(
                ok=True,
                source="message_display_point_accessible",
                texts=[
                    "tb1001 2026-06-04 12:00:00 hello",
                    "agent 2026-06-04 12:01:00 reply",
                    reader.READ_STATUS,
                ],
            )

        q_reader.read_visible_texts = fake_read_visible_texts

        result, messages = q_reader.read_visible_messages_debug(limit=10, chat_root=FakeControl())

        self.assertTrue(result.ok)
        self.assertEqual(result.source, "message_display_point_accessible")
        self.assertEqual(len(messages), 2)
        self.assertEqual([item.text for item in messages], ["hello", "reply"])
        self.assertEqual([item.direction for item in messages], ["inbound", "outbound"])

    def test_parse_accessible_messages_splits_merged_text_node(self):
        merged_text = " ".join(
            [
                "tb1001 2026-06-04 12:00:00 hello",
                "line two",
                "agent 2026-06-04 12:01:00 reply",
                reader.READ_STATUS,
                "tb1001 2026-06-04 12:02:00 next question",
            ]
        )

        messages = reader.parse_accessible_messages([merged_text])

        self.assertEqual(len(messages), 3)
        self.assertEqual([item.sender for item in messages], ["tb1001", "agent", "tb1001"])
        self.assertEqual([item.text for item in messages], ["hello line two", "reply", "next question"])
        self.assertEqual([item.direction for item in messages], ["inbound", "outbound", "inbound"])

    def test_read_visible_texts_reuses_chat_root_and_message_display(self):
        q_reader = reader.QianniuReader()
        fake_detector = FakeDetector()
        q_reader.detector = fake_detector

        first = q_reader.read_visible_texts(limit=10, chat_root=FakeControl())
        second = q_reader.read_visible_texts(limit=10, chat_root=FakeControl())

        self.assertTrue(first.ok)
        self.assertTrue(second.ok)
        self.assertEqual(first.source, "message_display_accessible")
        self.assertEqual(fake_detector.find_current_chat_calls, 0)
        self.assertEqual(fake_detector.find_message_display_calls, 1)

    def test_read_visible_texts_prefers_web_copy_when_available(self):
        q_reader = reader.QianniuReader()
        fake_detector = FakeDetector(message_web=FakeWebControl())
        q_reader.detector = fake_detector
        calls = []
        original_get_clipboard_text = reader.get_clipboard_text
        original_set_clipboard_text = reader.set_clipboard_text
        original_click_point = reader.click_point
        original_send_ctrl_key = reader.send_ctrl_key

        clipboard_reads = iter(
            [
                "old clipboard",
                "\n".join(
                    [
                        "tb1001 2026-06-04 12:00:00",
                        "hello",
                        "agent 2026-06-04 12:01:00",
                        "reply",
                        reader.READ_STATUS,
                    ]
                ),
            ]
        )

        def fake_get_clipboard_text():
            calls.append(("get_clipboard_text",))
            return next(clipboard_reads)

        def fake_set_clipboard_text(text):
            calls.append(("set_clipboard_text", text))

        def fake_click_point(x, y):
            calls.append(("click_point", x, y))

        def fake_send_ctrl_key(letter):
            calls.append(("send_ctrl_key", letter))

        try:
            reader.get_clipboard_text = fake_get_clipboard_text
            reader.set_clipboard_text = fake_set_clipboard_text
            reader.click_point = fake_click_point
            reader.send_ctrl_key = fake_send_ctrl_key

            result = q_reader.read_visible_texts(limit=10, chat_root=FakeControl())
        finally:
            reader.get_clipboard_text = original_get_clipboard_text
            reader.set_clipboard_text = original_set_clipboard_text
            reader.click_point = original_click_point
            reader.send_ctrl_key = original_send_ctrl_key

        self.assertTrue(result.ok)
        self.assertEqual(result.source, "message_web_copy")
        self.assertEqual(len(result.texts), 1)
        self.assertIn("tb1001 2026-06-04 12:00:00", result.texts[0])
        self.assertEqual(fake_detector.find_message_display_calls, 1)
        self.assertEqual(fake_detector.find_message_web_calls, 1)

    def test_copy_text_clicks_message_area_again_after_ctrl_a_c(self):
        calls = []
        clipboard_reads = iter(["old clipboard", "copied text"])
        original_get_clipboard_text = reader.get_clipboard_text
        original_set_clipboard_text = reader.set_clipboard_text
        original_click_point = reader.click_point
        original_send_ctrl_key = reader.send_ctrl_key

        def fake_get_clipboard_text():
            calls.append(("get_clipboard_text",))
            return next(clipboard_reads)

        def fake_set_clipboard_text(text):
            calls.append(("set_clipboard_text", text))

        def fake_click_point(x, y):
            calls.append(("click_point", x, y))

        def fake_send_ctrl_key(letter):
            calls.append(("send_ctrl_key", letter))

        class CopyRectControl:
            Name = ""

            def __init__(self):
                self.rects = [
                    type("FirstRect", (), {"left": 10, "top": 20, "right": 110, "bottom": 220})(),
                    type("SecondRect", (), {"left": 20, "top": 40, "right": 120, "bottom": 240})(),
                ]

            @property
            def BoundingRectangle(self):
                calls.append(("get_rect",))
                if self.rects:
                    return self.rects.pop(0)
                return type("SecondRect", (), {"left": 20, "top": 40, "right": 120, "bottom": 240})()

            def SetFocus(self):
                return None

        try:
            reader.get_clipboard_text = fake_get_clipboard_text
            reader.set_clipboard_text = fake_set_clipboard_text
            reader.click_point = fake_click_point
            reader.send_ctrl_key = fake_send_ctrl_key

            self.assertEqual(reader.copy_text_from_control(CopyRectControl()), "copied text")
        finally:
            reader.get_clipboard_text = original_get_clipboard_text
            reader.set_clipboard_text = original_set_clipboard_text
            reader.click_point = original_click_point
            reader.send_ctrl_key = original_send_ctrl_key

        self.assertEqual(
            calls,
            [
                ("get_clipboard_text",),
                ("set_clipboard_text", ""),
                ("get_rect",),
                ("click_point", 60, 120),
                ("send_ctrl_key", "A"),
                ("send_ctrl_key", "C"),
                ("get_clipboard_text",),
                ("get_rect",),
                ("click_point", 70, 140),
                ("set_clipboard_text", "old clipboard"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
