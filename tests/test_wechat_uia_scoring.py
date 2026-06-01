import unittest
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.wechat.uia_scoring import (
    find_input_candidates,
    find_message_list_candidates,
    find_send_button_candidates,
    find_session_list_candidates,
)


class FakeRect:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeControl:
    def __init__(
        self,
        *,
        name="",
        automation_id="",
        class_name="",
        control_type="",
        rect=None,
        children=None,
    ):
        self.Name = name
        self.AutomationId = automation_id
        self.ClassName = class_name
        self.ControlTypeName = control_type
        self.LocalizedControlType = control_type
        self.BoundingRectangle = rect or FakeRect()
        self._children = children or []

    def GetChildren(self):
        return list(self._children)


class WechatUiaScoringTests(unittest.TestCase):
    def test_scores_session_list_by_session_children(self):
        session_item = FakeControl(
            name="Alice",
            automation_id="session_item_Alice",
            class_name="mmui::ChatSessionCell",
        )
        session_list = FakeControl(
            automation_id="fallback_list",
            class_name="mmui::XTableView",
            control_type="List",
            children=[session_item],
        )
        root = FakeControl(children=[FakeControl(name="noise"), session_list])

        candidates = find_session_list_candidates(root)

        self.assertTrue(candidates)
        self.assertIs(candidates[0].control, session_list)
        self.assertGreaterEqual(candidates[0].score, 60)

    def test_scores_message_list_by_chat_text_children(self):
        message = FakeControl(
            name="hello",
            automation_id="chat_bubble_item_view_1",
            class_name="mmui::ChatTextItemView",
        )
        message_list = FakeControl(
            class_name="mmui::RecyclerListView",
            control_type="List",
            children=[message],
        )
        root = FakeControl(children=[message_list])

        candidates = find_message_list_candidates(root)

        self.assertTrue(candidates)
        self.assertIs(candidates[0].control, message_list)
        self.assertGreaterEqual(candidates[0].score, 60)

    def test_scores_input_below_message_list(self):
        message_list = FakeControl(
            automation_id="chat_message_list",
            class_name="mmui::RecyclerListView",
            rect=FakeRect(250, 80, 900, 500),
        )
        input_field = FakeControl(
            automation_id="fallback_input",
            class_name="mmui::ChatInputField",
            control_type="Edit",
            rect=FakeRect(260, 510, 880, 620),
        )
        root = FakeControl(children=[message_list, input_field])

        candidates = find_input_candidates(root)

        self.assertTrue(candidates)
        self.assertIs(candidates[0].control, input_field)
        self.assertGreaterEqual(candidates[0].score, 60)

    def test_scores_send_button_near_input(self):
        input_field = FakeControl(
            automation_id="chat_input_field",
            class_name="mmui::ChatInputField",
            rect=FakeRect(260, 510, 760, 620),
        )
        send_button = FakeControl(
            name="\u53d1\u9001",
            class_name="mmui::XTextView",
            control_type="Button",
            rect=FakeRect(780, 560, 850, 610),
        )
        root = FakeControl(children=[input_field, send_button])

        candidates = find_send_button_candidates(root)

        self.assertTrue(candidates)
        self.assertIs(candidates[0].control, send_button)
        self.assertGreaterEqual(candidates[0].score, 60)


if __name__ == "__main__":
    unittest.main()
