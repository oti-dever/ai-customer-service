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
from rpa.platforms.wechat.media_context_menu import ContextMenuFileResult
from rpa.platforms.wechat.media_evidence import MediaEvidenceResult


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
        return SimpleNamespace(ok=True, method="message_main_window_send", detail="", foreground=False)

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
    def read_visible_messages(self, *, display_name="", limit=30, tail_only=False):
        self.tail_only = tail_only
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
            platform="wechat",
            chat_title=display_name,
            user_id=display_name,
            is_group=False,
            member_count=None,
            session_id=f"wechat_{display_name or 'current'}",
        )
        return SimpleNamespace(display_name=display_name, samples=[sample], context=context)


class FakeSender:
    pass


class FakeMediaEvidenceWriter:
    def __init__(self, result=None):
        self.result = result
        self.calls = []

    def capture(self, content_type, platform_msg_id, sample):
        self.calls.append((content_type, platform_msg_id, sample))
        return self.result


class WechatSidecarAdapterTests(unittest.TestCase):
    def test_connect_does_not_emit_initial_snapshot_by_default(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "1",
                "command": "connect",
                "platform": "wechat",
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
                "platform": "wechat",
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
        self.assertEqual(store.events[1]["payload"]["display_name"], "Alice")
        self.assertEqual(store.events[1]["payload"]["direction"], "inbound")
        self.assertEqual(store.events[1]["payload"]["sender_role"], "customer")
        self.assertEqual(result["processed"][0]["context"]["chat_title"], "Alice")
        self.assertEqual(store.events[0]["payload"]["session_id"], "wechat_Alice")
        self.assertEqual(store.events[1]["payload"]["metadata"]["chat_context"]["session_id"], "wechat_Alice")

    def test_message_event_maps_media_kind_to_content_type(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter()
        for kind in ("image", "emoji", "video", "file"):
            with self.subTest(kind=kind):
                sample = SimpleNamespace(
                    name=f"[{kind}]",
                    direction="in",
                    direction_method="bubble_position",
                    role_confidence=0.8,
                    kind=kind,
                    class_name="mmui::ChatTextItemView",
                    automation_id=f"chat_bubble_item_view_{kind}",
                    rect=f"(10,10,200,{kind})",
                    left_variance=1.0,
                    right_variance=2.0,
                )

                event = adapter._message_event("Alice", sample)

                self.assertEqual(event["payload"]["content_type"], kind)
                self.assertEqual(event["payload"]["metadata"]["kind"], kind)

    def test_message_event_defaults_unknown_kind_to_text(self):
        adapter = WechatSidecarAdapter(FakeStore())
        sample = SimpleNamespace(
            name="hello",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="bubble_ref",
            class_name="mmui::ChatBubbleReferItemView",
            automation_id="chat_bubble_item_view_ref",
            rect="(10,10,200,40)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(event["payload"]["content_type"], "text")
        self.assertEqual(event["payload"]["metadata"]["kind"], "bubble_ref")

    def test_message_event_platform_msg_id_ignores_rect(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter()

        base = {
            "name": "hello",
            "direction": "in",
            "direction_method": "bubble_position",
            "role_confidence": 0.8,
            "kind": "text",
            "class_name": "mmui::ChatTextItemView",
            "automation_id": "chat_bubble_item_view_text",
            "left_variance": 1.0,
            "right_variance": 2.0,
        }
        first = SimpleNamespace(**base, rect="(10,10,200,40)")
        second = SimpleNamespace(**base, rect="(10,60,200,90)")

        first_event = adapter._message_event("Alice", first)
        second_event = adapter._message_event("Alice", second)

        self.assertEqual(
            first_event["payload"]["platform_msg_id"],
            second_event["payload"]["platform_msg_id"],
        )
        self.assertNotEqual(
            first_event["payload"]["metadata"]["rect"],
            second_event["payload"]["metadata"]["rect"],
        )

    def test_message_event_platform_msg_id_uses_visible_sequence_for_scanned_text(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter()

        base = {
            "name": "hello",
            "direction": "in",
            "direction_method": "bubble_position",
            "role_confidence": 0.8,
            "kind": "text",
            "class_name": "mmui::ChatTextItemView",
            "automation_id": "chat_bubble_item_view_text",
            "left_variance": 1.0,
            "right_variance": 2.0,
        }
        first = SimpleNamespace(**base, rect="(10,10,200,40)")
        second = SimpleNamespace(**base, rect="(10,60,200,90)")

        first_event = adapter._message_event("Alice", first, sequence_index=0)
        second_event = adapter._message_event("Alice", second, sequence_index=1)

        self.assertNotEqual(
            first_event["payload"]["platform_msg_id"],
            second_event["payload"]["platform_msg_id"],
        )
        self.assertEqual(first_event["payload"]["metadata"]["visible_sequence_index"], 0)
        self.assertEqual(second_event["payload"]["metadata"]["visible_sequence_index"], 1)

    def test_message_event_platform_msg_id_includes_media_rect(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter()

        base = {
            "name": "图片",
            "direction": "in",
            "direction_method": "bubble_position",
            "role_confidence": 0.8,
            "kind": "image",
            "class_name": "mmui::ChatBubbleReferItemView",
            "automation_id": "chat_bubble_item_view_image",
            "left_variance": 1.0,
            "right_variance": 2.0,
        }
        first = SimpleNamespace(**base, rect="(10,10,200,40)")
        second = SimpleNamespace(**base, rect="(10,60,200,90)")

        first_event = adapter._message_event("Alice", first)
        second_event = adapter._message_event("Alice", second)

        self.assertNotEqual(
            first_event["payload"]["platform_msg_id"],
            second_event["payload"]["platform_msg_id"],
        )

    def test_message_event_platform_msg_id_includes_content_type(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter()

        text_sample = SimpleNamespace(
            name="图片",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="text",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_text",
            rect="(10,10,200,40)",
            left_variance=1.0,
            right_variance=2.0,
        )
        image_sample = SimpleNamespace(
            name="图片",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="image",
            class_name="mmui::ChatBubbleReferItemView",
            automation_id="chat_bubble_item_view_image",
            rect="(10,10,200,40)",
            left_variance=1.0,
            right_variance=2.0,
        )

        text_event = adapter._message_event("Alice", text_sample)
        image_event = adapter._message_event("Alice", image_sample)

        self.assertNotEqual(
            text_event["payload"]["platform_msg_id"],
            image_event["payload"]["platform_msg_id"],
        )

    def test_message_event_writes_media_evidence_path(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        writer = FakeMediaEvidenceWriter(
            MediaEvidenceResult(status="saved", path="D:/evidence/wechat_image.png")
        )
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[image]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="image",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_image",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(len(writer.calls), 1)
        self.assertEqual(event["payload"]["content_image_path"], "D:/evidence/wechat_image.png")
        self.assertEqual(event["payload"]["evidence_ref"], "D:/evidence/wechat_image.png")
        self.assertEqual(event["payload"]["metadata"]["evidence_method"], "bubble_screenshot")
        self.assertEqual(event["payload"]["metadata"]["evidence_status"], "saved")

    def test_message_event_prefers_copied_media_file_over_screenshot(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: ContextMenuFileResult(
            status="copied",
            source_paths=["D:/source/order.png"],
            artifact_paths=["D:/artifact/order.png"],
            menu_name="复制",
        )
        writer = FakeMediaEvidenceWriter(
            MediaEvidenceResult(status="saved", path="D:/evidence/wechat_image.png")
        )
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[image]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="image",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_image",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(writer.calls, [])
        self.assertEqual(event["payload"]["content_image_path"], "D:/artifact/order.png")
        self.assertEqual(event["payload"]["evidence_ref"], "D:/artifact/order.png")
        self.assertEqual(event["payload"]["metadata"]["file_copy_status"], "copied")
        self.assertEqual(event["payload"]["metadata"]["file_copy_menu_name"], "复制")

    def test_message_event_writes_file_copy_as_evidence_without_image_path(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: ContextMenuFileResult(
            status="copied",
            source_paths=["D:/source/order.pdf"],
            artifact_paths=["D:/artifact/order.pdf"],
            menu_name="复制",
        )
        writer = FakeMediaEvidenceWriter()
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[file]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="file",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_file",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(writer.calls, [])
        self.assertEqual(event["payload"]["content_type"], "file")
        self.assertNotIn("content_image_path", event["payload"])
        self.assertEqual(event["payload"]["evidence_ref"], "D:/artifact/order.pdf")
        self.assertEqual(event["payload"]["metadata"]["file_source_paths"], ["D:/source/order.pdf"])
        self.assertEqual(event["payload"]["metadata"]["file_artifact_paths"], ["D:/artifact/order.pdf"])
        self.assertEqual(event["payload"]["metadata"]["file_copy_status"], "copied")

    def test_message_event_uses_file_screenshot_when_copy_fails(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: ContextMenuFileResult(
            status="failed",
            error="clipboard_empty",
            menu_name="复制",
        )
        writer = FakeMediaEvidenceWriter(
            MediaEvidenceResult(status="saved", path="D:/evidence/wechat_file.png")
        )
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[file]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="file",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_file",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(len(writer.calls), 1)
        self.assertEqual(writer.calls[0][0], "file")
        self.assertEqual(event["payload"]["content_type"], "file")
        self.assertEqual(event["payload"]["content_image_path"], "D:/evidence/wechat_file.png")
        self.assertEqual(event["payload"]["evidence_ref"], "D:/evidence/wechat_file.png")
        self.assertEqual(event["payload"]["metadata"]["file_copy_status"], "failed")
        self.assertEqual(event["payload"]["metadata"]["file_copy_error"], "clipboard_empty")
        self.assertEqual(event["payload"]["metadata"]["evidence_status"], "saved")

    def test_message_event_writes_video_copy_as_evidence_without_image_path(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: ContextMenuFileResult(
            status="copied",
            source_paths=["D:/source/demo.mp4"],
            artifact_paths=["D:/artifact/demo.mp4"],
            menu_name="复制",
        )
        writer = FakeMediaEvidenceWriter()
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[video]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="video",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_video",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(writer.calls, [])
        self.assertEqual(event["payload"]["content_type"], "video")
        self.assertNotIn("content_image_path", event["payload"])
        self.assertEqual(event["payload"]["evidence_ref"], "D:/artifact/demo.mp4")
        self.assertEqual(event["payload"]["metadata"]["file_source_paths"], ["D:/source/demo.mp4"])
        self.assertEqual(event["payload"]["metadata"]["file_artifact_paths"], ["D:/artifact/demo.mp4"])
        self.assertEqual(event["payload"]["metadata"]["file_copy_status"], "copied")

    def test_message_event_uses_video_screenshot_when_copy_fails(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: ContextMenuFileResult(
            status="failed",
            error="copy_menu_not_found",
            menu_name="",
        )
        writer = FakeMediaEvidenceWriter(
            MediaEvidenceResult(status="saved", path="D:/evidence/wechat_video.png")
        )
        adapter._media_evidence_writer = writer
        sample = SimpleNamespace(
            name="[video]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="video",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_video",
            rect="(10,10,200,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(len(writer.calls), 1)
        self.assertEqual(writer.calls[0][0], "video")
        self.assertEqual(event["payload"]["content_type"], "video")
        self.assertEqual(event["payload"]["content_image_path"], "D:/evidence/wechat_video.png")
        self.assertEqual(event["payload"]["evidence_ref"], "D:/evidence/wechat_video.png")
        self.assertEqual(event["payload"]["metadata"]["file_copy_status"], "failed")
        self.assertEqual(event["payload"]["metadata"]["file_copy_error"], "copy_menu_not_found")
        self.assertEqual(event["payload"]["metadata"]["evidence_status"], "saved")

    def test_message_event_keeps_media_message_when_evidence_capture_fails(self):
        adapter = WechatSidecarAdapter(FakeStore())
        adapter._copy_media_file_if_available = lambda _content_type, _platform_msg_id, _sample: None
        adapter._media_evidence_writer = FakeMediaEvidenceWriter(
            MediaEvidenceResult(status="failed", error="bubble_capture_failed")
        )
        sample = SimpleNamespace(
            name="[emoji]",
            direction="in",
            direction_method="bubble_position",
            role_confidence=0.8,
            kind="emoji",
            class_name="mmui::ChatTextItemView",
            automation_id="chat_bubble_item_view_emoji",
            rect="(10,10,100,100)",
            left_variance=1.0,
            right_variance=2.0,
        )

        event = adapter._message_event("Alice", sample)

        self.assertEqual(event["payload"]["content_type"], "emoji")
        self.assertNotIn("content_image_path", event["payload"])
        self.assertNotIn("evidence_ref", event["payload"])
        self.assertEqual(event["payload"]["metadata"]["evidence_status"], "failed")
        self.assertEqual(event["payload"]["metadata"]["evidence_error"], "bubble_capture_failed")

    def test_scan_unread_and_fetch_skips_unverified_switch(self):
        store = FakeStore()
        adapter = WechatSidecarAdapter(store)
        adapter._detector = FakeDetector(verified=False)
        adapter._reader = FakeReader()

        response = adapter.command(
            {
                "request_id": "3",
                "command": "scan_unread_and_fetch",
                "platform": "wechat",
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
                "platform": "wechat",
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
