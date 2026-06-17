import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu.adapter import QianniuSidecarAdapter
from rpa.platforms.qianniu.reader import MessageReadResult, MessageRecord
from rpa.platforms.qianniu.sender import SendResult
from rpa.platforms.qianniu.sessions import SessionItem


class FakeStore:
    def __init__(self):
        self.events = []

    def append(self, event):
        self.events.append(event)
        return len(self.events)


class FilteringStore(FakeStore):
    def __init__(self):
        super().__init__()
        self.filter_calls = []

    def filter_observed_message_events(self, events, *, bootstrap_limit=100, incremental_limit=10):
        self.filter_calls.append((list(events), bootstrap_limit, incremental_limit))
        return list(events[:1])


class FakeSessions:
    def __init__(self):
        self.unread = SessionItem(
            title="张三",
            control=object(),
            rect="(0,0,100,30)",
            rect_tuple=(0, 0, 100, 30),
            automation_id="session_zhangsan",
            class_name="TreeItem",
            control_type="TreeItem",
            raw_texts=["张三", "2条新消息"],
            unread=True,
            unread_score=0.02,
        )

    def read_visible_sessions(self, limit=50, detect_unread=False):
        return [self.unread]

    def select_first_unread(self):
        return self.unread, True, "rect_click"


class RepeatingUnreadSessions:
    def __init__(self, title="Alice"):
        self.title = title
        self.unread = True
        self.select_calls = []

    def _item(self):
        return SessionItem(
            title=self.title,
            control=object(),
            rect="(0,0,100,30)",
            rect_tuple=(0, 0, 100, 30),
            automation_id=f"session_{self.title}",
            class_name="TreeItem",
            control_type="TreeItem",
            raw_texts=[self.title],
            unread=self.unread,
            unread_score=0.02 if self.unread else 0.0,
        )

    def read_visible_sessions(self, limit=50, detect_unread=False):
        return [self._item()]

    def select_session(self, item):
        self.select_calls.append(item.title)
        return True, "rect_click"

    def current_chat_root(self):
        return object()


class FakeReader:
    def read_visible_messages_debug(self, limit=50):
        return (
            MessageReadResult(ok=True, source="message_display", texts=["hello"]),
            [
                MessageRecord(
                    sender="张三",
                    timestamp="2026-06-02 17:00:00",
                    text="你好",
                    raw="张三 2026-06-02 17:00:00\n你好",
                    direction="inbound",
                    status="",
                )
            ],
        )


class FakeReaderTwoMessages:
    def read_visible_messages_debug(self, limit=50, **_kwargs):
        return (
            MessageReadResult(ok=True, source="message_display", texts=["hello", "reply"]),
            [
                MessageRecord(
                    sender="寮犱笁",
                    timestamp="2026-06-02 17:00:00",
                    text="浣犲ソ",
                    raw="寮犱笁 2026-06-02 17:00:00\n浣犲ソ",
                    direction="inbound",
                    status="",
                ),
                MessageRecord(
                    sender="客服",
                    timestamp="2026-06-02 17:01:00",
                    text="鎮ㄥソ",
                    raw="客服 2026-06-02 17:01:00\n鎮ㄥソ\n已读",
                    direction="outbound",
                    status="已读",
                ),
            ],
        )


class FakeSender:
    def __init__(self):
        self.sent_texts = []
        self.sent_media = []

    def prepare_reply_draft(self, text):
        return SendResult(ok=True, stage="prepared", method="value_pattern")

    def send_text(self, text, dry_run=True):
        self.sent_texts.append(text)
        return SendResult(ok=True, stage="sent", method="value_pattern+enter_key")

    def send_media(self, file_path, content_type, dry_run=False):
        self.sent_media.append((file_path, content_type))
        return SendResult(ok=True, stage="sent", method="clipboard_file+enter_key")


class TargetSessions:
    def __init__(self, selected_title="Alice", titles=None):
        self.selected_title = selected_title
        self.titles = list(titles or ["Alice", "Bob"])
        self.select_calls = []
        self.invalidate_calls = 0

    def _item(self, title):
        return SessionItem(
            title=title,
            control=object(),
            rect="(0,0,100,30)",
            rect_tuple=(0, 0, 100, 30),
            automation_id=f"session_{title}",
            class_name="TreeItem",
            control_type="TreeItem",
            raw_texts=[title],
            selected=title == self.selected_title,
        )

    def read_visible_sessions(self, limit=50, detect_unread=False):
        return [self._item(title) for title in self.titles[:limit]]

    def selected_session(self, fresh=False):
        if fresh:
            self.invalidate_cache()
        for item in self.read_visible_sessions(limit=100, detect_unread=False):
            if item.selected:
                return item
        return None

    def invalidate_cache(self):
        self.invalidate_calls += 1

    def select_session(self, item):
        self.select_calls.append(item.title)
        self.selected_title = item.title
        return True, "rect_click"

    def current_chat_root(self):
        return object()


class HeaderOnlyTargetSessions(TargetSessions):
    def selected_session(self, fresh=False):
        if fresh:
            self.invalidate_cache()
        return None


class FakeDetector:
    def __init__(self, header_names=None):
        self.header_names = list(header_names or [])

    def find_process_ids(self):
        return [123]

    def find_best_window(self):
        return type("Window", (), {"title": "千牛", "class_name": "ChatView"})()

    def find_current_chat(self):
        return type("Handle", (), {"chat_root": object()})()

    def find_message_display(self, chat_root):
        return object()

    def find_message_web(self, chat_root):
        return object()

    def find_input_field(self, chat_root):
        return object()

    def find_send_button(self, chat_root):
        return object()

    def find_chat_header_names(self, chat_root):
        return list(self.header_names)


class QianniuSidecarAdapterTests(unittest.TestCase):
    def test_fetch_visible_conversations_emits_events(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = FakeSessions()
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "1",
                "command": "fetch_visible_conversations",
                "platform": "qianniu",
                "parameters": {"limit": 10, "detect_unread": True},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["count"], 1)
        self.assertEqual(store.events[0]["event_type"], "conversation_observed")
        self.assertTrue(store.events[0]["payload"]["metadata"]["unread"])

    def test_scan_unread_and_fetch_emits_conversation_and_message(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = FakeSessions()
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "2",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["unread_count"], 1)
        self.assertEqual(response["result"]["conversation_count"], 1)
        self.assertEqual(response["result"]["message_count"], 1)
        self.assertEqual([event["event_type"] for event in store.events], ["conversation_observed", "message_observed"])
        self.assertEqual(store.events[1]["payload"]["direction"], "inbound")
        self.assertEqual(store.events[1]["payload"]["sender_role"], "customer")

    def test_scan_unread_clicks_same_unread_session_once_until_badge_clears(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = RepeatingUnreadSessions(title="Alice")
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        first = adapter.command(
            {
                "request_id": "unread-first",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )
        second = adapter.command(
            {
                "request_id": "unread-repeat",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )
        sessions.unread = False
        cleared = adapter.command(
            {
                "request_id": "unread-cleared",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )
        sessions.unread = True
        third = adapter.command(
            {
                "request_id": "unread-new",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(first["result"]["processed_count"], 1)
        self.assertEqual(second["result"]["processed_count"], 0)
        self.assertEqual(second["result"]["skipped_count"], 1)
        self.assertEqual(cleared["result"]["unread_count"], 0)
        self.assertEqual(third["result"]["processed_count"], 1)
        self.assertEqual(sessions.select_calls, ["Alice", "Alice"])

    def test_scan_unread_and_fetch_uses_store_message_filter(self):
        store = FilteringStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = FakeSessions()
        adapter._reader = FakeReaderTwoMessages()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "2-filtered",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["message_count"], 1)
        self.assertEqual(response["result"]["processed"][0]["parsed_messages"], 2)
        self.assertEqual(response["result"]["processed"][0]["filtered_messages"], 1)
        self.assertEqual(len(store.filter_calls), 1)
        self.assertEqual([event["event_type"] for event in store.events], ["conversation_observed", "message_observed"])

    def test_fetch_visible_messages_uses_store_message_filter_and_visible_sequence(self):
        store = FilteringStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = TargetSessions(selected_title="Alice", titles=["Alice"])
        adapter._reader = FakeReaderTwoMessages()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "fetch-filtered",
                "command": "fetch_visible_messages",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "limit": 10,
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["count"], 1)
        self.assertEqual(len(store.filter_calls), 1)
        raw_events = store.filter_calls[0][0]
        self.assertEqual(
            [event["payload"]["metadata"]["visible_sequence_index"] for event in raw_events],
            [0, 1],
        )
        self.assertEqual(len(store.events), 1)
        self.assertEqual(store.events[0]["payload"]["metadata"]["visible_sequence_index"], 0)

    def test_send_message_emits_send_result_event(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = FakeSessions()
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "3",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "text": "收到，马上处理",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-1",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["sent"], True)
        self.assertEqual([event["event_type"] for event in store.events], ["send_result_observed", "message_sent"])

    def test_send_message_uses_selected_target_without_switching(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "send-selected",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "qianniu:local_qianniu:Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-selected",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "current_selected")
        self.assertEqual(sessions.select_calls, [])
        self.assertEqual(sender.sent_texts, ["ok"])

    def test_send_media_uses_target_verification_and_emits_media_payload(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Alice", titles=["Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._sender = sender
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "send-media",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "content_type": "video",
                    "file_path": "D:/media/demo.mp4",
                    "file_name": "demo.mp4",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-media",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["content_type"], "video")
        self.assertEqual(sender.sent_media, [("D:/media/demo.mp4", "video")])
        self.assertEqual(store.events[-1]["payload"]["content_type"], "video")
        self.assertEqual(store.events[-1]["payload"]["file_path"], "D:/media/demo.mp4")

    def test_send_message_switches_to_target_and_verifies_by_header(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Bob", titles=["Bob", "Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])

        response = adapter.command(
            {
                "request_id": "send-switch",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-switch",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "header_verified")
        self.assertEqual(sessions.select_calls, ["Alice"])
        self.assertEqual(sessions.invalidate_calls, 1)
        self.assertEqual(sender.sent_texts, ["ok"])

    def test_send_message_verifies_switched_target_by_header_when_selected_unavailable(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = HeaderOnlyTargetSessions(selected_title="Bob", titles=["Bob", "Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])

        response = adapter.command(
            {
                "request_id": "send-header",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-header",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "header_verified")
        self.assertEqual(sessions.select_calls, ["Alice"])
        self.assertEqual(sessions.invalidate_calls, 1)
        self.assertEqual(sender.sent_texts, ["ok"])

    def test_send_message_fails_when_target_session_not_found(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Bob", titles=["Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "send-missing",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-missing",
                },
            }
        )

        self.assertEqual(response["status"], "error")
        self.assertEqual(response["error"], "target_session_not_found")
        self.assertEqual(sender.sent_texts, [])


if __name__ == "__main__":
    unittest.main()
