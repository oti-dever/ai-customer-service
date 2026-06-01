import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.qianniu_chat_parser import parse_qianniu_chat_lines


class QianniuChatParserTests(unittest.TestCase):
    def test_parses_incoming_and_outgoing_sequence(self):
        rows = [
            "tb4947894539",
            "2026-4-18 13:53:35",
            "你好",
            "2026-4-18 13:53:51",
            "有求必应羊羊：王刚",
            "您好",
            "已读",
        ]

        result = parse_qianniu_chat_lines(rows)

        self.assertTrue(result.success)
        self.assertEqual(len(result.messages), 2)
        self.assertEqual(result.messages[0].side, "in")
        self.assertEqual(result.messages[0].sender_name, "tb4947894539")
        self.assertEqual(result.messages[0].original_timestamp, "2026-4-18 13:53:35")
        self.assertEqual(result.messages[0].content, "你好")
        self.assertEqual(result.messages[1].side, "out")
        self.assertEqual(result.messages[1].sender_name, "有求必应羊羊:王刚")
        self.assertEqual(result.messages[1].read_flag, "已读")

    def test_merges_multiline_incoming_message(self):
        rows = [
            "tb4947894539",
            "2026-4-18 13:53:35",
            "你好",
            "请问今天能发吗",
        ]

        result = parse_qianniu_chat_lines(rows)

        self.assertTrue(result.success)
        self.assertEqual(len(result.messages), 1)
        self.assertEqual(result.messages[0].side, "in")
        self.assertEqual(result.messages[0].content, "你好\n请问今天能发吗")

    def test_fails_when_only_outgoing_exists(self):
        rows = [
            "2026-4-18 13:53:51",
            "有求必应羊羊：王刚",
            "您好",
            "未读",
        ]

        result = parse_qianniu_chat_lines(rows)

        self.assertFalse(result.success)
        self.assertEqual(result.reason, "only_outgoing_matched")


if __name__ == "__main__":
    unittest.main()
