import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.wechat.config import WechatAutomationConfig
from rpa.platforms.wechat.detector import WechatDetector, extract_session_title
from rpa.platforms.wechat.uia_scoring import find_session_candidate


class FakeRect:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeControl:
    def __init__(self, *, name="", automation_id="", class_name="", rect=None, children=None, selected=False):
        self.Name = name
        self.AutomationId = automation_id
        self.ClassName = class_name
        self.ControlTypeName = "ListItem"
        self.LocalizedControlType = "list item"
        self.BoundingRectangle = rect or FakeRect()
        self._children = children or []
        self._selected = selected

    def GetChildren(self):
        return list(self._children)

    def GetSelectionItemPattern(self):
        class Pattern:
            def __init__(self, selected):
                self.IsSelected = selected

        return Pattern(self._selected)


class WechatSessionV2Tests(unittest.TestCase):
    def test_find_session_candidate_uses_session_item_name(self):
        session = FakeControl(
            name="Alice",
            automation_id="session_item_Alice",
            class_name="mmui::ChatSessionCell",
            rect=FakeRect(0, 10, 200, 40),
        )
        session_list = FakeControl(
            automation_id="session_list",
            class_name="mmui::XTableView",
            children=[session],
        )
        root = FakeControl(children=[session_list])

        result = find_session_candidate(root, "Alice")

        self.assertIs(result, session)

    def test_detector_scans_unread_sessions_from_session_list(self):
        unread = FakeControl(name="Alice\n2条新消息", automation_id="session_item_Alice")
        service = FakeControl(name="服务号\n3条新消息", automation_id="session_item_service")
        session_list = FakeControl(children=[unread, service])

        result = WechatDetector().scan_unread_sessions_detailed(session_list)

        self.assertEqual(result.source, "session-list-tree")
        self.assertEqual(len(result.sessions), 1)
        self.assertIs(result.sessions[0].control, unread)

    def test_extract_session_title_uses_first_non_empty_line(self):
        self.assertEqual(extract_session_title("\n Alice \n2条新消息"), "Alice")

    def test_detector_uses_configured_unread_markers_and_blacklist(self):
        detector = WechatDetector(
            WechatAutomationConfig(
                blacklist=["服务号"],
                unread_markers=["2条未读"],
                unread_patterns=[r"\[\d+条\]"],
            )
        )
        self.assertTrue(detector._is_unread_session_name("Alice\n2条未读"))
        self.assertFalse(detector._is_unread_session_name("服务号\n2条未读"))

    def test_detector_prefers_win32_window_discovery(self):
        detector = WechatDetector(
            WechatAutomationConfig(
                process_names=["WeChat.exe"],
                window_classes=["ChatWnd"],
                window_title_keywords=["微信"],
            )
        )

        fake_psutil = SimpleNamespace(
            process_iter=lambda fields: [SimpleNamespace(info={"name": "WeChat.exe", "pid": 42})],
            NoSuchProcess=Exception,
            AccessDenied=Exception,
        )

        class FakeWin32Gui:
            @staticmethod
            def IsWindowVisible(hwnd):
                return True

            @staticmethod
            def GetClassName(hwnd):
                return "ChatWnd"

            @staticmethod
            def GetWindowText(hwnd):
                return "微信"

            @staticmethod
            def EnumWindows(callback, param):
                callback(1001, param)

        fake_win32process = SimpleNamespace(GetWindowThreadProcessId=lambda hwnd: (0, 42))
        fake_pywintypes = SimpleNamespace(error=Exception)
        fake_auto = SimpleNamespace(
            ControlFromHandle=lambda hwnd: SimpleNamespace(
                NativeWindowHandle=hwnd,
                Name="微信",
                ClassName="ChatWnd",
            )
        )

        with patch.dict(
            sys.modules,
            {
                "psutil": fake_psutil,
                "win32gui": FakeWin32Gui,
                "win32process": fake_win32process,
                "pywintypes": fake_pywintypes,
                "uiautomation": fake_auto,
            },
        ):
            window = detector._find_main_window_by_win32()

        self.assertIsNotNone(window)
        self.assertEqual(window.NativeWindowHandle, 1001)


if __name__ == "__main__":
    unittest.main()
