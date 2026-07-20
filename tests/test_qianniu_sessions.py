import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu import sessions
from rpa.platforms.qianniu.config import AppConfig


class FakeRect:
    def __init__(self, left, top, right, bottom):
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
        control_type="GroupControl",
        class_name="",
        rect=(0, 0, 100, 30),
        children=None,
    ):
        self.Name = name
        self.AutomationId = automation_id
        self.ControlTypeName = control_type
        self.ClassName = class_name
        self.BoundingRectangle = FakeRect(*rect)
        self._children = list(children or [])

    def GetChildren(self):
        return list(self._children)


class WindowOnlyDetector:
    q = AppConfig().qianniu

    def _score_chat_root_candidate(self, control, depth):
        if getattr(control, "AutomationId", "") == "UIWindow":
            return 120, "chat-root-aid"
        return 0, ""

    def _is_definitive_chat_root(self, control, score):
        return False

    def find_current_chat(self):
        raise AssertionError("find_current_chat should not be called when preferred hwnd is available")


class QianniuSessionReaderTests(unittest.TestCase):
    def test_read_visible_sessions_uses_preferred_window_without_global_chat_lookup(self):
        session_item = FakeControl(name="tb4947894539", control_type="TreeItemControl", rect=(20, 70, 220, 110))
        session_root = FakeControl(
            automation_id=f"UIWindow.centralwidget.{AppConfig().qianniu.reception_normal_list_suffix}",
            control_type="ListControl",
            rect=(10, 50, 280, 300),
            children=[session_item],
        )
        window_root = FakeControl(
            automation_id="UIWindow",
            class_name="MutilChatView",
            control_type="WindowControl",
            rect=(0, 0, 1200, 800),
            children=[session_root],
        )
        old_control_from_hwnd = sessions.control_from_hwnd
        try:
            sessions.control_from_hwnd = lambda hwnd: window_root if hwnd == 2468 else None
            reader = sessions.QianniuSessionReader(AppConfig())
            reader.detector = WindowOnlyDetector()
            reader.set_window_hwnd(2468)

            items = reader.read_visible_sessions(limit=10, detect_unread=False)
        finally:
            sessions.control_from_hwnd = old_control_from_hwnd

        self.assertEqual([item.title for item in items], ["tb4947894539"])
        self.assertIs(reader.current_chat_root(), window_root)

    def test_read_visible_sessions_does_not_fallback_global_when_preferred_window_missing(self):
        old_control_from_hwnd = sessions.control_from_hwnd
        try:
            sessions.control_from_hwnd = lambda hwnd: None
            reader = sessions.QianniuSessionReader(AppConfig())
            reader.detector = WindowOnlyDetector()
            reader.set_window_hwnd(2468)

            items = reader.read_visible_sessions(limit=10, detect_unread=False)
        finally:
            sessions.control_from_hwnd = old_control_from_hwnd

        self.assertEqual(items, [])

    def test_preferred_window_root_does_not_update_cache_by_default(self):
        window_root = FakeControl(
            automation_id="UIWindow",
            class_name="MutilChatView",
            control_type="WindowControl",
            rect=(0, 0, 1200, 800),
        )
        old_control_from_hwnd = sessions.control_from_hwnd
        try:
            sessions.control_from_hwnd = lambda hwnd: window_root if hwnd == 2468 else None
            reader = sessions.QianniuSessionReader(AppConfig())
            reader.set_window_hwnd(2468)

            root = reader.preferred_window_root()
        finally:
            sessions.control_from_hwnd = old_control_from_hwnd

        self.assertIs(root, window_root)
        self.assertIsNone(reader.current_chat_root())

    def test_preferred_window_root_can_update_cache_when_requested(self):
        window_root = FakeControl(
            automation_id="UIWindow",
            class_name="MutilChatView",
            control_type="WindowControl",
            rect=(0, 0, 1200, 800),
        )
        old_control_from_hwnd = sessions.control_from_hwnd
        try:
            sessions.control_from_hwnd = lambda hwnd: window_root if hwnd == 2468 else None
            reader = sessions.QianniuSessionReader(AppConfig())
            reader.set_window_hwnd(2468)

            root = reader.preferred_window_root(update_cache=True)
        finally:
            sessions.control_from_hwnd = old_control_from_hwnd

        self.assertIs(root, window_root)
        self.assertIs(reader.current_chat_root(), window_root)

    def test_find_session_root_accepts_multi_account_reception_list_view(self):
        session_item = FakeControl(name="tb4947894539", control_type="TreeItemControl", rect=(120, 380, 340, 430))
        reception_list = FakeControl(
            automation_id=(
                "UIWindow.mutilcentralwidget.stackedWidget.SingleChatView.centralwidget.stackedWidget."
                "SubChatView.ChatListWidget.ChatListView.centralwidget.list_widget.ReceptionListView"
            ),
            class_name="ReceptionListView",
            control_type="GroupControl",
            rect=(112, 371, 352, 1010),
            children=[session_item],
        )
        chat_list_view = FakeControl(
            automation_id=(
                "UIWindow.mutilcentralwidget.stackedWidget.SingleChatView.centralwidget.stackedWidget."
                "SubChatView.ChatListWidget.ChatListView"
            ),
            class_name="ChatListView",
            control_type="GroupControl",
            rect=(112, 313, 352, 1010),
            children=[reception_list],
        )
        chat_list_widget = FakeControl(
            automation_id=(
                "UIWindow.mutilcentralwidget.stackedWidget.SingleChatView.centralwidget.stackedWidget."
                "SubChatView.ChatListWidget"
            ),
            control_type="GroupControl",
            rect=(112, 207, 352, 1010),
            children=[chat_list_view],
        )
        chat_root = FakeControl(
            automation_id="UIWindow",
            class_name="MutilChatView",
            control_type="WindowControl",
            rect=(48, 127, 1358, 1010),
            children=[chat_list_widget],
        )

        root, source = sessions.find_session_root_from_chat(WindowOnlyDetector(), chat_root)
        items = sessions.extract_session_items(root, limit=10) if root is not None else []

        self.assertIs(root, reception_list)
        self.assertEqual(source, "reception_list_view_fallback")
        self.assertEqual([item.title for item in items], ["tb4947894539"])


if __name__ == "__main__":
    unittest.main()
