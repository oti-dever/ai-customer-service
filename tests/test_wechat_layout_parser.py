import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.layout_parser import parse_chat_layout


def block(text: str, left: float, top: float, right: float, bottom: float, conf: float = 0.99):
    return (
        text,
        [[left, top], [right, top], [right, bottom], [left, bottom]],
        conf,
    )


class WechatLayoutParserTests(unittest.TestCase):
    def test_merges_split_left_message_in_wechat_mode(self):
        blocks = [
            block("你好，这里是第一段", 24, 20, 210, 45),
            block("这里是第二段，应该并成同一条", 28, 48, 260, 72),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="wechat", merge_y_gap=18)

        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].side, "in")
        self.assertEqual(messages[0].content, "你好，这里是第一段 这里是第二段，应该并成同一条")

    def test_wechat_system_timestamp_is_attached_to_next_message(self):
        blocks = [
            block("昨天 18:25", 255, 20, 345, 44),
            block("你好，后续联系我", 24, 60, 190, 86),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="wechat", merge_y_gap=18)

        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].original_timestamp, "昨天 18:25")
        self.assertEqual(messages[0].content, "你好，后续联系我")

    def test_wechat_system_hint_is_filtered_out(self):
        blocks = [
            block("以下是新消息", 250, 20, 350, 42),
            block("请问今天能发货吗", 30, 60, 210, 86),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="wechat", merge_y_gap=18)

        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].content, "请问今天能发货吗")

    def test_wechat_alignment_guard_splits_distinct_messages(self):
        blocks = [
            block("第一条消息", 24, 20, 200, 44),
            block("第二条消息", 120, 50, 320, 76),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="wechat", merge_y_gap=18)

        self.assertEqual(len(messages), 2)
        self.assertEqual([m.content for m in messages], ["第一条消息", "第二条消息"])

    def test_qianniu_header_parsing_still_works(self):
        blocks = [
            block("店铺A:张三 2026-3-24 15:33:39", 20, 20, 260, 44),
            block("您好，这里是客服回复", 26, 48, 230, 74),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="qianniu", merge_y_gap=18)

        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].sender_name, "店铺A:张三")
        self.assertEqual(messages[0].original_timestamp, "2026-3-24 15:33:39")
        self.assertEqual(messages[0].content, "您好，这里是客服回复")

    def test_qianniu_splits_multiple_tb_bubbles(self):
        """版式切段：多条买家气泡（tb 行重复出现）应拆成多条 ParsedMessage。"""
        blocks = [
            block("tb810776366", 20, 20, 120, 40),
            block("2026-4-10 12:59:50", 22, 44, 200, 64),
            block("哈哈嗝", 24, 68, 100, 88),
            block("tb810776366", 20, 92, 120, 112),
            block("2026-4-10 12:59:51", 22, 116, 200, 136),
            block("喝吧苏北", 24, 140, 120, 160),
        ]

        messages = parse_chat_layout(blocks, region_width=600, platform="qianniu", merge_y_gap=18)

        self.assertEqual(len(messages), 2)
        self.assertEqual(messages[0].sender_name, "tb810776366")
        self.assertEqual(messages[0].original_timestamp, "2026-4-10 12:59:50")
        self.assertEqual(messages[0].content, "哈哈嗝")
        self.assertEqual(messages[1].sender_name, "tb810776366")
        self.assertEqual(messages[1].original_timestamp, "2026-4-10 12:59:51")
        self.assertEqual(messages[1].content, "喝吧苏北")


if __name__ == "__main__":
    unittest.main()
