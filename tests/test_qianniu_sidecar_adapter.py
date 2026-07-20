import sys
import threading
import time
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu.adapter import QianniuConversationTask, QianniuSidecarAdapter
from rpa.platforms.qianniu.accounts import AccountTab
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


class WindowAwareFakeSessions(FakeSessions):
    def __init__(self):
        super().__init__()
        self.window_hwnds = []

    def set_window_hwnd(self, hwnd):
        self.window_hwnds.append(hwnd)


def make_account(account_id, display_name, *, selected=False, unread=False, unread_score=0.0):
    return AccountTab(
        account_id=account_id,
        display_name=display_name,
        selected=selected,
        rect="(0,0,100,30)",
        rect_tuple=(0, 0, 100, 30),
        control=object(),
        source="uia",
        confidence=0.9,
        raw_texts=[display_name],
        automation_id=f"account_{account_id}",
        class_name="TabItem",
        control_type="TabItemControl",
        has_unread_hint=unread,
        unread_score=unread_score,
    )


class FakeAccounts:
    def __init__(self, accounts, *, hwnd=0):
        self.accounts = list(accounts)
        self.hwnd = hwnd
        self.switch_calls = []
        self.list_calls = 0
        self.invalidate_calls = 0

    def list_accounts(self, fresh=False):
        self.list_calls += 1
        return list(self.accounts)

    def cached_window_hwnd(self):
        return self.hwnd

    def selected_account(self, fresh=False):
        selected = [item for item in self.accounts if item.selected]
        return selected[0] if selected else self.accounts[0] if self.accounts else None

    def switch_account(self, query):
        self.switch_calls.append(query)
        target = None
        for item in self.accounts:
            if item.account_id == query or item.display_name == query:
                target = item
                break
        if target is None:
            return False, "account_not_found"
        if target.selected:
            return True, "already_selected"
        updated = []
        for item in self.accounts:
            updated.append(AccountTab(**{**item.__dict__, "selected": item.account_id == target.account_id}))
        self.accounts = updated
        return True, "rect_click"

    def invalidate_cache(self):
        self.invalidate_calls += 1


class AccountAwareUnreadSessions:
    def __init__(self, account_id_getter, unread_by_account):
        self.account_id_getter = account_id_getter
        self.unread_by_account = dict(unread_by_account)
        self.select_calls = []

    def _item(self):
        account_id = self.account_id_getter()
        unread = bool(self.unread_by_account.get(account_id, False))
        return SessionItem(
            title="寮犱笁",
            control=object(),
            rect="(0,0,100,30)",
            rect_tuple=(0, 0, 100, 30),
            automation_id=f"session_{account_id}",
            class_name="TreeItem",
            control_type="TreeItem",
            raw_texts=["寮犱笁"],
            unread=unread,
            unread_score=0.02 if unread else 0.0,
        )

    def read_visible_sessions(self, limit=50, detect_unread=False):
        return [self._item()]

    def select_session(self, item):
        self.select_calls.append((self.account_id_getter(), item.title))
        return True, "rect_click"

    def current_chat_root(self):
        return object()


class RepeatingUnreadSessions:
    def __init__(self, title="Alice"):
        self.title = title
        self.unread = True
        self.select_calls = []
        self.read_calls = 0

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
        self.read_calls += 1
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
        self.sent_chat_roots = []
        self.sent_kwargs = []

    def prepare_reply_draft(self, text):
        return SendResult(ok=True, stage="prepared", method="value_pattern")

    def send_text(self, text, dry_run=True, chat_root=None, **kwargs):
        self.sent_texts.append(text)
        self.sent_chat_roots.append(chat_root)
        self.sent_kwargs.append(dict(kwargs))
        return SendResult(ok=True, stage="sent", method="value_pattern+enter_key")

    def send_media(self, file_path, content_type, dry_run=False, chat_root=None, **_kwargs):
        self.sent_media.append((file_path, content_type))
        self.sent_chat_roots.append(chat_root)
        return SendResult(ok=True, stage="sent", method="clipboard_file+enter_key")


class ResolvingFakeSender(FakeSender):
    def __init__(self):
        super().__init__()
        self.input_field = object()
        self.resolve_calls = []

    def resolve_input_field(self, chat_root, input_field=None):
        self.resolve_calls.append((chat_root, input_field))
        return self.input_field


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


class PreferredWindowTargetSessions(TargetSessions):
    def __init__(self, selected_title="Alice", titles=None):
        super().__init__(selected_title=selected_title, titles=titles)
        self.preferred_window_root_control = object()
        self.preferred_chat_root = object()
        self.current_chat_root_calls = 0
        self.preferred_window_root_calls = 0
        self.preferred_window_root_update_cache_values = []
        self.preferred_chat_root_calls = 0
        self.preferred_chat_root_update_cache_values = []

    def current_chat_root(self):
        self.current_chat_root_calls += 1
        return None

    def preferred_window_root(self, update_cache=False):
        self.preferred_window_root_calls += 1
        self.preferred_window_root_update_cache_values.append(update_cache)
        return self.preferred_window_root_control

    def chat_root_from_preferred_window(self, update_cache=True):
        self.preferred_chat_root_calls += 1
        self.preferred_chat_root_update_cache_values.append(update_cache)
        return self.preferred_chat_root


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


def wait_until(predicate, timeout=2.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.02)
    return bool(predicate())


def make_conversation_task(account_id="qnacct_wanggang", display_name="Alice", task_id="worker-task"):
    return QianniuConversationTask(
        task_id=task_id,
        dedupe_key=f"{account_id}:{display_name.lower()}",
        account_id=account_id,
        account_display_name="wanggang",
        session_title=display_name,
        conversation_key=f"qianniu:{account_id}:{display_name}",
        source="current_session_unread",
        created_at=time.time(),
    )


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
        processed = response["result"]["processed"][0]
        self.assertTrue(processed["task_id"].startswith("qnq_"))
        self.assertEqual(processed["dedupe_key"], "local_qianniu:张三")
        self.assertGreaterEqual(processed["queue_wait_ms"], 0.0)
        self.assertGreaterEqual(processed["process_ms"], 0.0)
        self.assertEqual([event["event_type"] for event in store.events], ["conversation_observed", "message_observed"])
        self.assertEqual(store.events[1]["payload"]["direction"], "inbound")
        self.assertEqual(store.events[1]["payload"]["sender_role"], "customer")

    def test_scan_unread_with_worker_enqueues_and_worker_processes_async(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._sessions = FakeSessions()
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()
        adapter._connected = True
        adapter._start_conversation_worker()
        try:
            response = adapter.command(
                {
                    "request_id": "worker-scan",
                    "command": "scan_unread_and_fetch",
                    "platform": "qianniu",
                    "parameters": {"message_limit": 10},
                }
            )

            self.assertEqual(response["status"], "success")
            self.assertEqual(response["result"]["queued_count"], 1)
            self.assertEqual(response["result"]["conversation_count"], 0)
            self.assertEqual(response["result"]["message_count"], 0)
            self.assertTrue(wait_until(lambda: len(store.events) == 2))
            self.assertEqual([event["event_type"] for event in store.events], ["conversation_observed", "message_observed"])
            snapshot = adapter._conversation_worker_snapshot()
            self.assertEqual(snapshot["queue_queued_count"], 0)
            self.assertEqual(snapshot["queue_processing_count"], 0)
        finally:
            adapter._connected = False
            adapter._stop_conversation_worker()

    def test_worker_busy_pauses_scanner(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._connected = True
        adapter._start_conversation_worker()
        try:
            adapter._conversation_worker._set_processing(True)
            pause, snapshot = adapter._conversation_worker_should_pause_scanner()

            self.assertTrue(pause)
            self.assertTrue(snapshot["worker_running"])
            self.assertTrue(snapshot["worker_processing"])
        finally:
            adapter._conversation_worker._set_processing(False)
            adapter._connected = False
            adapter._stop_conversation_worker()

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

    def test_queued_worker_uses_session_hint_without_repeating_session_scan(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = RepeatingUnreadSessions(title="Alice")
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "unread-hint",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["processed_count"], 1)
        self.assertEqual(response["result"]["processed"][0]["session_find_source"], "task_hint")
        self.assertEqual(sessions.read_calls, 1)
        self.assertEqual(sessions.select_calls, ["Alice"])

    def test_worker_caches_input_field_for_auto_reply_context(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = RepeatingUnreadSessions(title="Alice")
        sender = ResolvingFakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "worker-cache-input",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        context = adapter._worker_auto_reply_context_snapshot()
        self.assertEqual(response["status"], "success")
        self.assertIsNotNone(context)
        self.assertIs(context.input_field, sender.input_field)
        self.assertEqual(len(sender.resolve_calls), 1)

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

    def test_scan_unread_switches_to_top_unread_account_before_session_scan(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._accounts = FakeAccounts(
            [
                make_account("qnacct_robot", "萌动彼岸岸:机器人", selected=True),
                make_account("qnacct_wanggang", "有求必应羊羊:王刚", unread=True, unread_score=0.8),
            ]
        )
        adapter._sessions = AccountAwareUnreadSessions(
            lambda: adapter._account_id,
            {"qnacct_robot": False, "qnacct_wanggang": True},
        )
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        response = adapter.command(
            {
                "request_id": "scan-account-unread",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(adapter._accounts.switch_calls, ["qnacct_wanggang"])
        self.assertEqual(response["result"]["account_stage"], "queue_task_account_already_selected")
        self.assertEqual(response["result"]["account_id"], "qnacct_wanggang")
        self.assertEqual(response["result"]["account_scan_count"], 2)
        self.assertEqual(response["result"]["conversation_count"], 1)
        self.assertEqual(store.events[0]["account_id"], "qnacct_wanggang")
        self.assertEqual(store.events[0]["conversation_key"], "qianniu:qnacct_wanggang:寮犱笁")
        self.assertEqual(
            store.events[0]["payload"]["metadata"]["account_display_name"],
            "有求必应羊羊:王刚",
        )

    def test_scan_unread_queues_current_and_top_unread_accounts_serially(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._accounts = FakeAccounts(
            [
                make_account("qnacct_robot", "robot", selected=True),
                make_account("qnacct_wanggang", "wanggang", unread=True, unread_score=0.8),
            ]
        )
        sessions = AccountAwareUnreadSessions(
            lambda: adapter._account_id,
            {"qnacct_robot": True, "qnacct_wanggang": True},
        )
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = FakeSender()
        adapter._detector = FakeDetector()

        first = adapter.command(
            {
                "request_id": "scan-current-and-top-unread",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )
        second = adapter.command(
            {
                "request_id": "scan-current-and-top-unread-next",
                "command": "scan_unread_and_fetch",
                "platform": "qianniu",
                "parameters": {"message_limit": 10},
            }
        )

        self.assertEqual(first["status"], "success")
        self.assertEqual(second["status"], "success")
        self.assertEqual(first["result"]["account_scan_count"], 2)
        self.assertEqual(first["result"]["queued_count"], 2)
        self.assertEqual(first["result"]["conversation_count"], 1)
        self.assertEqual(first["result"]["message_count"], 1)
        self.assertEqual(first["result"]["queue_queued_count"], 1)
        self.assertEqual(second["result"]["conversation_count"], 1)
        self.assertEqual(second["result"]["message_count"], 1)
        self.assertEqual(second["result"]["queue_queued_count"], 0)
        self.assertEqual(adapter._accounts.switch_calls, ["qnacct_wanggang", "qnacct_robot", "qnacct_wanggang"])
        self.assertEqual(
            [event["account_id"] for event in store.events if event["event_type"] == "conversation_observed"],
            ["qnacct_robot", "qnacct_wanggang"],
        )
        self.assertEqual(sessions.select_calls, [("qnacct_robot", "寮犱笁"), ("qnacct_wanggang", "寮犱笁")])

    def test_list_accounts_returns_current_qianniu_account_names(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._accounts = FakeAccounts(
            [
                make_account("qnacct_robot", "萌动彼岸岸:机器人", selected=True),
                make_account("qnacct_wanggang", "有求必应羊羊:王刚"),
            ]
        )

        response = adapter.command(
            {
                "request_id": "list-accounts",
                "command": "list_accounts",
                "platform": "qianniu",
                "parameters": {},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["account_count"], 2)
        self.assertEqual(
            [item["display_name"] for item in response["result"]["accounts"]],
            ["萌动彼岸岸:机器人", "有求必应羊羊:王刚"],
        )
        self.assertEqual(response["result"]["active_display_name"], "萌动彼岸岸:机器人")

    def test_account_scan_syncs_cached_window_hwnd_to_session_reader(self):
        adapter = QianniuSidecarAdapter(FakeStore())
        sessions = WindowAwareFakeSessions()
        adapter._sessions = sessions
        adapter._accounts = FakeAccounts(
            [make_account("qnacct_wanggang", "有求必应羊羊:王刚", selected=True)],
            hwnd=2468,
        )

        listed = adapter._list_accounts_safely(fresh=True)

        self.assertEqual(len(listed), 1)
        self.assertEqual(sessions.window_hwnds, [2468])

    def test_async_health_probe_schedule_is_disabled_during_listening(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._connected = True
        adapter._schedule_async_health_probe("test", "health-stop", delay_sec=5.0)

        self.assertIsNone(adapter._health_probe_thread)
        self.assertIsNone(adapter._health_probe_stop)

    def test_stop_health_probe_keeps_reference_when_thread_is_still_alive(self):
        class StubbornThread:
            def __init__(self):
                self.join_calls = 0

            def is_alive(self):
                return True

            def join(self, timeout=None):
                self.join_calls += 1

        adapter = QianniuSidecarAdapter(FakeStore())
        thread = StubbornThread()
        stop_event = threading.Event()
        adapter._health_probe_thread = thread
        adapter._health_probe_stop = stop_event

        adapter._stop_health_probe("stop-stubborn", reason="test", join_timeout=0.0)

        self.assertTrue(stop_event.is_set())
        self.assertEqual(thread.join_calls, 1)
        self.assertIs(adapter._health_probe_thread, thread)
        self.assertIs(adapter._health_probe_stop, stop_event)

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

    def test_send_message_switches_to_target_and_verifies_by_selected_session(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Bob", titles=["Bob", "Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["today", "unpaid", "paid"])

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
        self.assertEqual(response["result"]["target_session_stage"], "selected_verified")
        self.assertEqual(response["result"]["target_session_method"], "fresh_scan+rect_click+verify_selected")
        self.assertEqual(sessions.select_calls, ["Alice"])
        self.assertEqual(sessions.invalidate_calls, 2)
        self.assertEqual(sender.sent_texts, ["ok"])

    def test_send_message_fast_path_uses_active_context_without_account_or_session_scan(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        accounts = FakeAccounts(
            [make_account("qnacct_wanggang", "鏈夋眰蹇呭簲缇婄緤:鐜嬪垰", selected=True)]
        )
        sessions = TargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._accounts = accounts
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["today", "unpaid", "paid"])
        adapter._remember_active_session(
            "Alice",
            account_id="qnacct_wanggang",
            account_display_name="鏈夋眰蹇呭簲缇婄緤:鐜嬪垰",
            conversation_key="qianniu:qnacct_wanggang:Alice",
            source="test_active_context",
        )

        response = adapter.command(
            {
                "request_id": "send-fast-context",
                "command": "send_message",
                "platform": "qianniu",
                "account_id": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-fast-context",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_account_stage"], "active_context")
        self.assertEqual(response["result"]["target_account_id"], "qnacct_wanggang")
        self.assertEqual(response["result"]["target_session_stage"], "active_context")
        self.assertEqual(response["result"]["target_session_method"], "memory_context")
        self.assertEqual(accounts.list_calls, 0)
        self.assertEqual(accounts.switch_calls, [])
        self.assertEqual(sessions.select_calls, [])
        self.assertEqual(sessions.invalidate_calls, 0)
        self.assertEqual(sender.sent_texts, ["ok"])
        self.assertEqual(store.events[-1]["account_id"], "qnacct_wanggang")
        self.assertEqual(store.events[-1]["conversation_key"], "qianniu:qnacct_wanggang:Alice")

    def test_send_message_fast_path_uses_preferred_window_root_without_deep_scan(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = PreferredWindowTargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])
        adapter._remember_active_session(
            "Alice",
            account_id="qnacct_wanggang",
            account_display_name="wanggang",
            conversation_key="qianniu:qnacct_wanggang:Alice",
            source="test_active_context",
        )

        response = adapter.command(
            {
                "request_id": "send-fast-preferred-window",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-fast-preferred-window",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "active_context")
        self.assertEqual(sessions.select_calls, [])
        self.assertEqual(sessions.current_chat_root_calls, 1)
        self.assertEqual(sessions.preferred_window_root_calls, 1)
        self.assertEqual(sessions.preferred_window_root_update_cache_values, [False])
        self.assertEqual(sessions.preferred_chat_root_calls, 0)
        self.assertIs(sender.sent_chat_roots[0], sessions.preferred_window_root_control)
        self.assertEqual(sender.sent_kwargs[0], {"allow_global_find": False})

    def test_auto_reply_send_uses_worker_direct_context(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = PreferredWindowTargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])
        adapter._account_id = "qnacct_wanggang"
        adapter._account_display_name = "wanggang"
        adapter._remember_worker_auto_reply_context(make_conversation_task(), "Alice")

        response = adapter.command(
            {
                "request_id": "send-worker-direct",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "auto ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "send_source": "auto_reply",
                    "task_id": "auto-reply-task",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_account_stage"], "worker_auto_reply_context")
        self.assertEqual(response["result"]["target_session_stage"], "worker_auto_reply_context")
        self.assertTrue(response["result"]["send_direct_path"])
        self.assertTrue(response["result"]["worker_direct_path"])
        self.assertEqual(sessions.select_calls, [])
        self.assertEqual(sessions.invalidate_calls, 0)
        self.assertEqual(sessions.preferred_window_root_calls, 1)
        self.assertIs(sender.sent_chat_roots[0], sessions.preferred_window_root_control)
        self.assertIsNone(adapter._worker_auto_reply_context_snapshot())

    def test_auto_reply_send_uses_worker_cached_input_field(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = PreferredWindowTargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])
        adapter._account_id = "qnacct_wanggang"
        adapter._account_display_name = "wanggang"
        chat_root = object()
        input_field = object()
        adapter._remember_worker_auto_reply_context(
            make_conversation_task(),
            "Alice",
            chat_root=chat_root,
            input_field=input_field,
            input_source="test_cache",
        )

        response = adapter.command(
            {
                "request_id": "send-worker-direct-cached-input",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "auto ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "send_source": "auto_reply",
                    "task_id": "auto-reply-cached-input-task",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertTrue(response["result"]["worker_direct_path"])
        self.assertEqual(sessions.preferred_window_root_calls, 0)
        self.assertIs(sender.sent_chat_roots[0], chat_root)
        self.assertIs(sender.sent_kwargs[0]["input_field"], input_field)
        self.assertFalse(sender.sent_kwargs[0]["allow_global_find"])

    def test_manual_send_does_not_use_worker_direct_context(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = TargetSessions(selected_title="Alice", titles=["Alice", "Bob"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])
        adapter._account_id = "qnacct_wanggang"
        adapter._account_display_name = "wanggang"
        adapter._remember_worker_auto_reply_context(make_conversation_task(), "Alice")

        response = adapter.command(
            {
                "request_id": "send-manual-safe",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "manual ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "manual-task",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "current_selected")
        self.assertFalse(response["result"]["worker_direct_path"])
        self.assertIsNotNone(adapter._worker_auto_reply_context_snapshot())

    def test_send_message_switches_target_account_before_target_session(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        adapter._accounts = FakeAccounts(
            [
                make_account("qnacct_robot", "萌动彼岸岸:机器人", selected=True),
                make_account("qnacct_wanggang", "有求必应羊羊:王刚"),
            ]
        )
        sessions = TargetSessions(selected_title="Bob", titles=["Bob", "Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["Alice"])

        response = adapter.command(
            {
                "request_id": "send-account-switch",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:qnacct_wanggang:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-account-switch",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(adapter._accounts.switch_calls, ["qnacct_wanggang"])
        self.assertEqual(response["result"]["target_account_stage"], "target_account_selected")
        self.assertEqual(response["result"]["target_account_id"], "qnacct_wanggang")
        self.assertEqual(sessions.select_calls, ["Alice"])
        self.assertEqual(sender.sent_texts, ["ok"])
        self.assertEqual(store.events[-1]["account_id"], "qnacct_wanggang")
        self.assertEqual(store.events[-1]["conversation_key"], "qianniu:qnacct_wanggang:Alice")

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
        self.assertEqual(sessions.invalidate_calls, 2)
        self.assertEqual(sender.sent_texts, ["ok"])

    def test_send_message_allows_click_assumed_when_selected_and_header_unavailable(self):
        store = FakeStore()
        adapter = QianniuSidecarAdapter(store)
        sessions = HeaderOnlyTargetSessions(selected_title="Bob", titles=["Bob", "Alice"])
        sender = FakeSender()
        adapter._sessions = sessions
        adapter._reader = FakeReader()
        adapter._sender = sender
        adapter._detector = FakeDetector(header_names=["today", "unpaid", "paid"])

        response = adapter.command(
            {
                "request_id": "send-click-assumed",
                "command": "send_message",
                "platform": "qianniu",
                "parameters": {
                    "display_name": "Alice",
                    "conversation_key": "qianniu:local_qianniu:Alice",
                    "text": "ok",
                    "confirm_token": "manual_confirmed_by_agent",
                    "task_id": "task-click-assumed",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["target_session_stage"], "click_assumed")
        self.assertEqual(response["result"]["target_session_method"], "fresh_scan+rect_click+assume_clicked")
        self.assertEqual(sessions.select_calls, ["Alice"])
        self.assertEqual(sessions.invalidate_calls, 2)
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
