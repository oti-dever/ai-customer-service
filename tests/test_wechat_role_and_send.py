import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.wechat.adapter import _event_confidence
from rpa.platforms.wechat.click_strategy import click_session_item_detailed
from rpa.platforms.wechat.role_judgement import judge_by_position


class FakeRect:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeControl:
    def __init__(self, rect):
        self.BoundingRectangle = rect

    def Click(self, simulateMove=False, waitTime=0):
        self.clicked = True

    def GetInvokePattern(self):
        return None

    def GetSelectionItemPattern(self):
        return None

    def GetParentControl(self):
        return None


class WechatRoleAndSendTests(unittest.TestCase):
    def test_role_by_position_inbound_left_of_message_list_center(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        bubble = FakeControl(FakeRect(260, 180, 520, 230))

        result = judge_by_position(bubble, message_list)

        self.assertEqual(result.direction, "in")
        self.assertEqual(result.role, "customer")
        self.assertEqual(result.method, "bubble_position")

    def test_role_by_position_outbound_right_of_message_list_center(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        bubble = FakeControl(FakeRect(700, 180, 960, 230))

        result = judge_by_position(bubble, message_list)

        self.assertEqual(result.direction, "out")
        self.assertEqual(result.role, "agent")
        self.assertEqual(result.method, "bubble_position")

    def test_event_confidence_normalizes_ratio(self):
        self.assertEqual(_event_confidence(0.73, default=65), 73)
        self.assertEqual(_event_confidence(87, default=65), 87)
        self.assertEqual(_event_confidence(None, default=65), 65)

    def test_click_strategy_returns_result_object(self):
        result = click_session_item_detailed(FakeControl(FakeRect(0, 0, 10, 10)), allow_foreground=False)
        self.assertIsInstance(result.ok, bool)
        self.assertIsInstance(result.method, str)
        self.assertTrue(result.method)


if __name__ == "__main__":
    unittest.main()
