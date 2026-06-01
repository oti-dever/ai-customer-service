import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.wechat.adapter import WechatSidecarAdapter


class FakeStore:
    def __init__(self):
        self.events = []

    def append(self, event):
        self.events.append(event)
        return len(self.events)


@dataclass(frozen=True)
class FakeUnread:
    name: str
    control: object = object()
    automation_id: str = "session_item_Alice"
    class_name: str = "mmui::ChatSessionCell"
    rect: str = "(0,0,100,30)"


class FakeDetector:
    def __init__(self, *, verified=True):
        self.clicked = []
        self.verified = verified

    def probe(self):
        return {"healthy": True, "reason": "ok"}

    def collect_visible_sessions(self, limit):
        raise AssertionError("initial visible session scan should not run")

    def find_main_window_control(self):
        return SimpleNamespace(NativeWindowHandle=123)

    def get_session_list(self, win):
        return object()

    def scan_unread_sessions_detailed(self, session_list):
        return SimpleNamespace(
            sessions=[FakeUnread("Alice\n2条新消息")],
            source="session-list-tree",
            scanned_items=5,
        )

    def extract_session_title(self, name):
        return name.splitlines()[0].strip()

    def click_session_detailed(self, unread, fallback_hwnd=0, allow_foreground=True):
        self.clicked.append((unread.name, fallback_hwnd, allow_foreground))
        return SimpleNamespace(ok=True, method="message_main_window_send", detail="")

    def verify_session_switch(self, display_name, win, *, session_list=None, timeout_ms=1200, interval_ms=120):
        return self.verified

    def diagnose_uia(self, *, candidate_limit=5):
        return {
            "probe": self.probe(),
            "window_snapshot": {"hwnd": 123, "title": "WeChat", "class_name": "mmui::MainWindow"},
            "session_candidates": [{"score": 100, "reason": "automationId"}][:candidate_limit],
            "message_candidates": [],
            "input_candidates": [],
            "send_button_candidates": [],
        }


class FakeReader:
    def read_visible_messages(self, *, display_name="", limit=30):
        sample = SimpleNamespace(
            name="hello",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="text",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_1",
            rect="(10,10,200,40)",
            left=10,
            right=200,
            left_variance=1.0,
            right_variance=2.0,
        )
        context = SimpleNamespace(
            platform="wechat_pc",
            chat_title=display_name,
            user_id=display_name,
            is_group=False,
            member_count=None,
            session_id=f"wechat_{display_name or 'current'}",
        )
        return SimpleNamespace(display_name=display_name, samples=[sample], context=context)


class FakeSender:
    pass


class WechatSidecarAdapterTests(unittest.TestCase):
    def test_connect_does_not_emit_initial_snapshot_by_default(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "1",
                "command": "connect",
                "platform": "wechat_pc",
                "parameters": {},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(len(store.events), 1)
        self.assertEqual(store.events[0]["event_type"], "account_health_changed")

    def test_scan_unread_and_fetch_emits_only_unread_conversation_messages(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector()
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()

        response = adapter.command(
            {
                "request_id": "2",
                "command": "scan_unread_and_fetch",
                "platform": "wechat_pc",
                "parameters": {
                    "session_limit": 1,
                    "message_limit": 10,
                    "allow_foreground": False,
                    "settle_ms": 0,
                },
            }
        )

        result = response["result"]
        self.assertEqual(response["status"], "success")
        self.assertEqual(result["unread_count"], 1)
        self.assertEqual(result["conversation_count"], 1)
        self.assertEqual(result["message_count"], 1)
        self.assertEqual([event["event_type"] for event in store.events], ["conversation_observed", "message_observed"])
        self.assertEqual(store.events[1]["payload"]["content"], "hello")
        self.assertEqual(result["processed"][0]["context"]["chat_title"], "Alice")
        self.assertEqual(store.events[0]["payload"]["session_id"], "wechat_Alice")
        self.assertEqual(store.events[1]["payload"]["metadata"]["chat_context"]["session_id"], "wechat_Alice")

    def test_scan_unread_and_fetch_skips_unverified_switch(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector(verified=False)
        adapter._reader = FakeReader()

        response = adapter.command(
            {
                "request_id": "3",
                "command": "scan_unread_and_fetch",
                "platform": "wechat_pc",
                "parameters": {"settle_ms": 0},
            }
        )

        result = response["result"]
        self.assertEqual(response["status"], "success")
        self.assertEqual(result["conversation_count"], 0)
        self.assertEqual(result["message_count"], 0)
        self.assertEqual(store.events, [])
        self.assertEqual(result["processed"][0]["error"], "session_switch_not_verified")

    def test_diagnose_wechat_uia_returns_probe_and_candidates(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "4",
                "command": "diagnose_wechat_uia",
                "platform": "wechat_pc",
                "parameters": {"candidate_limit": 1},
            }
        )

        self.assertEqual(response["status"], "success")
        result = response["result"]
        self.assertTrue(result["probe"]["healthy"])
        self.assertEqual(result["window_snapshot"]["hwnd"], 123)
        self.assertEqual(len(result["session_candidates"]), 1)

    def test_health_payload_carries_failure_state(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector()
        adapter._failures.record(False, "scan_unread")
        adapter._failures.record(False, "scan_unread")
        adapter._failures.record(False, "scan_unread")
        event = adapter._health_event(healthy=False, status="degraded", message="x", metadata={"reason": "y"})

        self.assertEqual(event["payload"]["metadata"]["failure_count"], 3)
        self.assertEqual(event["payload"]["metadata"]["last_stage"], "scan_unread")
        self.assertTrue(event["payload"]["metadata"]["should_cooldown"])

    def test_failure_metadata_reflects_stage(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._detector = FakeDetector()
        adapter._failures.record(False, "scan_unread")
        meta = adapter._failure_metadata("scan_unread", "boom")

        self.assertEqual(meta["stage"], "scan_unread")
        self.assertEqual(meta["detail"], "boom")
        self.assertEqual(meta["failure_count"], 1)


if __name__ == "__main__":
    unittest.main()
