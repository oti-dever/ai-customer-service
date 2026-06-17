import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service.rpa_bridge import RpaEventStore
import service.truth_store as truth_store_module
from service.truth_store import PythonServiceTruthStore


def temporary_directory():
    root = REPO_ROOT / "Testing" / "tmp"
    root.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=root)


class ServiceTruthStoreTests(unittest.TestCase):
    def test_event_store_persists_observed_conversation_and_message(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))

            conv_event = {
                "event_id": "evt-conv-1",
                "event_type": "conversation_observed",
                "platform": "wechat",
                "account_id": "acct-1",
                "conversation_key": "wechat:张三",
                "occurred_at": "2026-06-03T13:00:00",
                "payload": {
                    "display_name": "张三",
                    "source_type": "ui_observed",
                    "confidence": 80,
                },
            }
            msg_event = {
                "event_id": "evt-msg-1",
                "event_type": "message_observed",
                "platform": "wechat",
                "account_id": "acct-1",
                "conversation_key": "wechat:张三",
                "occurred_at": "2026-06-03T13:01:00",
                "payload": {
                    "platform_msg_id": "wechat-msg-1",
                    "direction": "inbound",
                    "sender_role": "customer",
                    "sender_name": "张三",
                    "content_type": "text",
                    "content": "你好",
                    "source_type": "ui_observed",
                    "confidence": 82,
                    "verification_status": "unverified",
                },
            }

            store.append(conv_event)
            store.append(msg_event)
            store.append(msg_event)

            events = store.list_after("", platform="wechat", limit=10)["events"]
            self.assertTrue(all(item["truth_persisted"] for item in events))

            conn = sqlite3.connect(str(db_path))
            try:
                conv = conn.execute(
                    """
                    SELECT id, platform, platform_conversation_id, customer_name,
                           last_message
                    FROM conversations
                    """
                ).fetchone()
                self.assertIsNotNone(conv)
                self.assertEqual(conv[1], "wechat")
                self.assertEqual(conv[2], "wechat:张三")
                self.assertEqual(conv[3], "张三")
                self.assertEqual(conv[4], "你好")

                messages = conn.execute(
                    """
                    SELECT direction, content, sender, sender_name, platform_message_id,
                           status
                    FROM messages
                    """
                ).fetchall()
                self.assertEqual(len(messages), 1)
                self.assertEqual(messages[0][0], "in")
                self.assertEqual(messages[0][1], "你好")
                self.assertEqual(messages[0][2], "customer")
                self.assertEqual(messages[0][3], "张三")
                self.assertEqual(messages[0][4], "wechat-msg-1")
                self.assertEqual(messages[0][5], "observed")
            finally:
                conn.close()

    def test_outbound_message_without_display_name_does_not_overwrite_customer_name_with_key(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))

            store.append(
                {
                    "event_id": "evt-conv-outbound-name",
                    "event_type": "conversation_observed",
                    "platform": "wechat",
                    "account_id": "wechat",
                    "conversation_key": "wechat:wechat:念忘",
                    "occurred_at": "2026-06-06T09:40:34",
                    "payload": {
                        "display_name": "念忘",
                        "source_type": "ui_observed",
                        "confidence": 75,
                    },
                }
            )
            store.append(
                {
                    "event_id": "evt-msg-outbound-no-name",
                    "event_type": "message_observed",
                    "platform": "wechat",
                    "account_id": "wechat",
                    "conversation_key": "wechat:wechat:念忘",
                    "occurred_at": "2026-06-06T09:40:35",
                    "payload": {
                        "platform_msg_id": "wechat-outbound-no-name",
                        "direction": "outbound",
                        "sender_role": "agent",
                        "sender_name": "",
                        "content_type": "text",
                        "content": "1",
                        "source_type": "ui_observed",
                        "confidence": 90,
                        "verification_status": "unverified",
                    },
                }
            )

            conn = sqlite3.connect(str(db_path))
            try:
                conv = conn.execute(
                    "SELECT platform_conversation_id, customer_name, last_message FROM conversations"
                ).fetchone()
                self.assertEqual(conv[0], "wechat:wechat:念忘")
                self.assertEqual(conv[1], "念忘")
                self.assertEqual(conv[2], "1")
            finally:
                conn.close()

    def test_outbound_command_records_pending_then_sent(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            truth_store = PythonServiceTruthStore(db_path)
            store = RpaEventStore(truth_store=truth_store)
            original_local_now_iso = truth_store_module._local_now_iso

            try:
                truth_store_module._local_now_iso = lambda: "2026-06-03T21:01:59+08:00"
                truth_store.persist_outbound_command(
                {
                    "command": "send_message",
                    "platform": "qianniu",
                    "account_id": "acct-2",
                    "client_message_id": "cmid-1",
                    "parameters": {
                        "conversation_key": "qianniu:李四",
                        "display_name": "李四",
                        "text": "您好",
                        "client_message_id": "cmid-1",
                    },
                }
                )
            finally:
                truth_store_module._local_now_iso = original_local_now_iso

            store.append(
                {
                    "event_id": "evt-sent-1",
                    "event_type": "message_sent",
                    "platform": "qianniu",
                    "account_id": "acct-2",
                    "conversation_key": "qianniu:李四",
                    "occurred_at": "2026-06-03T13:02:00",
                    "payload": {
                        "client_message_id": "cmid-1",
                        "status": "sent",
                    },
                }
            )

            conn = sqlite3.connect(str(db_path))
            try:
                row = conn.execute(
                    """
                    SELECT c.platform, c.platform_conversation_id, m.direction,
                           m.content, m.sender, m.client_message_id,
                           m.status, m.error_reason, m.message_time,
                           m.created_at, c.last_time
                    FROM messages m
                    JOIN conversations c ON c.id = m.conversation_id
                    """
                ).fetchone()
                self.assertIsNotNone(row)
                self.assertEqual(row[0], "qianniu")
                self.assertEqual(row[1], "qianniu:李四")
                self.assertEqual(row[2], "out")
                self.assertEqual(row[3], "您好")
                self.assertEqual(row[4], "agent")
                self.assertEqual(row[5], "cmid-1")
                self.assertEqual(row[6], "sent")
                self.assertEqual(row[7], "")
                self.assertEqual(row[8], "2026-06-03 21:01:59")
                self.assertEqual(row[9], "2026-06-03 21:01:59")
                self.assertEqual(row[10], "2026-06-03 21:01:59")
            finally:
                conn.close()

    def test_replay_events_reads_persisted_event_log_after_restart(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))

            store.append(
                {
                    "event_id": "evt-replay-conv",
                    "event_type": "conversation_observed",
                    "platform": "wechat",
                    "account_id": "acct-1",
                    "conversation_key": "wechat:张三",
                    "occurred_at": "2026-06-03T13:00:00",
                    "payload": {
                        "display_name": "张三",
                        "source_type": "ui_observed",
                        "confidence": 80,
                    },
                }
            )
            store.append(
                {
                    "event_id": "evt-replay-msg",
                    "event_type": "message_observed",
                    "platform": "wechat",
                    "account_id": "acct-1",
                    "conversation_key": "wechat:张三",
                    "occurred_at": "2026-06-03T13:01:00",
                    "payload": {
                        "platform_msg_id": "wechat-msg-replay",
                        "direction": "inbound",
                        "sender_role": "customer",
                        "sender_name": "张三",
                        "content_type": "text",
                        "content": "重放消息",
                    },
                }
            )

            restarted_truth_store = PythonServiceTruthStore(db_path)
            first_page = restarted_truth_store.replay_events(platform="wechat", cursor="0", limit=1)
            self.assertEqual(first_page["status"], "success")
            self.assertEqual(first_page["source_role"], "python_service_truth_replay")
            self.assertEqual(first_page["event_count"], 1)
            self.assertEqual(first_page["events"][0]["event_type"], "conversation_observed")
            self.assertTrue(first_page["events"][0]["replayed"])

            second_page = restarted_truth_store.replay_events(
                platform="wechat",
                cursor=first_page["cursor"],
                limit=10,
            )
            self.assertEqual(second_page["event_count"], 1)
            self.assertEqual(second_page["events"][0]["event_type"], "message_observed")
            self.assertEqual(second_page["events"][0]["payload"]["content"], "重放消息")


    def test_filter_observed_message_events_bootstraps_empty_conversation(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))
            store.append(
                {
                    "event_id": "evt-filter-conv",
                    "event_type": "conversation_observed",
                    "platform": "qianniu",
                    "account_id": "acct-1",
                    "conversation_key": "qianniu:acct-1:buyer-1",
                    "occurred_at": "2026-06-03T13:00:00",
                    "payload": {
                        "display_name": "buyer-1",
                        "source_type": "ui_observed",
                        "confidence": 70,
                    },
                }
            )

            events = [
                _message_event("msg-new-in", "inbound", "customer", "hello"),
                _message_event("msg-new-out", "outbound", "agent", "reply"),
            ]

            filtered = store.filter_observed_message_events(events, bootstrap_limit=10, incremental_limit=1)
            self.assertEqual([item["payload"]["platform_msg_id"] for item in filtered], ["msg-new-in", "msg-new-out"])

    def test_filter_observed_message_events_emits_new_sequence_for_existing_conversation(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))
            store.append(_message_event("msg-existing", "inbound", "customer", "old"))

            events = [
                _message_event("msg-existing", "inbound", "customer", "old"),
                _message_event("msg-new-out", "outbound", "agent", "reply"),
                _message_event("msg-new-in-1", "inbound", "customer", "new 1"),
                _message_event("msg-new-in-2", "inbound", "customer", "new 2"),
            ]

            filtered = store.filter_observed_message_events(events, bootstrap_limit=10, incremental_limit=10)
            self.assertEqual(
                [item["payload"]["platform_msg_id"] for item in filtered],
                ["msg-new-out", "msg-new-in-1", "msg-new-in-2"],
            )

    def test_filter_observed_message_events_aligns_tail_when_message_ids_change(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))
            history = [
                _message_event("msg-old-1", "inbound", "customer", "我去"),
                _message_event("msg-old-2", "inbound", "customer", "我没注意到"),
                _message_event("msg-old-3", "inbound", "customer", "让代领人签呗"),
                _message_event("msg-old-4", "outbound", "agent", "来两把"),
            ]
            for event in history:
                store.append(event)

            events = [
                _message_event("msg-new-rect-1", "inbound", "customer", "我去"),
                _message_event("msg-new-rect-2", "inbound", "customer", "我没注意到"),
                _message_event("msg-new-rect-3", "inbound", "customer", "让代领人签呗"),
                _message_event("msg-new-rect-4", "outbound", "agent", "来两把"),
                _message_event("msg-new-in", "inbound", "customer", "行"),
            ]

            filtered = store.filter_observed_message_events(events, bootstrap_limit=10, incremental_limit=10)
            self.assertEqual([item["payload"]["platform_msg_id"] for item in filtered], ["msg-new-in"])

    def test_filter_observed_message_events_keeps_new_media_after_sequence_overlap_even_with_duplicate_id(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            store = RpaEventStore(truth_store=PythonServiceTruthStore(db_path))
            store.append(_message_event("weak-media-id", "inbound", "customer", "图片", content_type="image"))

            events = [
                _message_event("weak-media-id", "inbound", "customer", "图片", content_type="image"),
                _message_event("weak-media-id", "inbound", "customer", "图片", content_type="image"),
            ]

            filtered = store.filter_observed_message_events(events, bootstrap_limit=10, incremental_limit=10)
            self.assertEqual([item["payload"]["platform_msg_id"] for item in filtered], ["weak-media-id"])

    def test_clear_messages_records_barrier_and_ignores_old_visible_messages(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            truth_store = PythonServiceTruthStore(db_path)
            event_store = RpaEventStore(truth_store=truth_store)
            event_store.append(_message_event("msg-before-clear", "inbound", "customer", "before clear"))
            _insert_send_event_for_platform_msg(db_path, "msg-before-clear")

            result = truth_store.clear_conversation_messages(
                "qianniu",
                "qianniu:acct-1:buyer-1",
                account_id="acct-1",
                operator="tester",
            )
            self.assertEqual(result["status"], "success")

            event_store.append(
                {
                    **_message_event("msg-old-after-clear", "inbound", "customer", "old visible"),
                    "occurred_at": "2000-01-01T00:00:00",
                }
            )
            event_store.append(
                {
                    **_message_event("msg-new-after-clear", "inbound", "customer", "new visible"),
                    "occurred_at": "2999-01-01T00:00:00",
                }
            )

            conn = sqlite3.connect(str(db_path))
            try:
                messages = conn.execute(
                    "SELECT platform_message_id, content FROM messages ORDER BY id"
                ).fetchall()
                self.assertEqual(messages, [("msg-new-after-clear", "new visible")])
                conv = conn.execute(
                    "SELECT last_message, unread_count, deleted_at FROM conversations"
                ).fetchone()
                self.assertEqual(conv[0], "new visible")
                self.assertEqual(conv[1], 0)
                self.assertIn(conv[2], (None, ""))
                platform_rows = conn.execute(
                    "SELECT platform_message_id FROM qianniu_messages ORDER BY id"
                ).fetchall()
                self.assertEqual(platform_rows, [("msg-new-after-clear",)])
                send_event_count = conn.execute(
                    "SELECT COUNT(*) FROM message_send_events"
                ).fetchone()[0]
                self.assertEqual(send_event_count, 0)
            finally:
                conn.close()

    def test_delete_conversation_reopens_only_for_new_inbound_message(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            truth_store = PythonServiceTruthStore(db_path)
            event_store = RpaEventStore(truth_store=truth_store)
            event_store.append(_message_event("msg-before-delete", "inbound", "customer", "before delete"))
            _insert_send_event_for_platform_msg(db_path, "msg-before-delete")

            result = truth_store.delete_conversation(
                "qianniu",
                "qianniu:acct-1:buyer-1",
                account_id="acct-1",
                operator="tester",
            )
            self.assertEqual(result["status"], "success")

            event_store.append(
                {
                    **_message_event("msg-old-in", "inbound", "customer", "old inbound"),
                    "occurred_at": "2000-01-01T00:00:00",
                }
            )
            event_store.append(
                {
                    **_message_event("msg-new-out", "outbound", "agent", "new outbound"),
                    "occurred_at": "2999-01-01T00:00:00",
                }
            )
            event_store.append(
                {
                    **_message_event("msg-new-in", "inbound", "customer", "new inbound"),
                    "occurred_at": "2999-01-01T00:01:00",
                }
            )

            conn = sqlite3.connect(str(db_path))
            try:
                messages = conn.execute(
                    "SELECT platform_message_id, direction, content FROM messages ORDER BY id"
                ).fetchall()
                self.assertEqual(messages, [("msg-new-in", "in", "new inbound")])
                conv = conn.execute(
                    "SELECT status, deleted_at, last_message FROM conversations"
                ).fetchone()
                self.assertEqual(conv[0], "active")
                self.assertIn(conv[1], (None, ""))
                self.assertEqual(conv[2], "new inbound")
                platform_rows = conn.execute(
                    "SELECT platform_message_id FROM qianniu_messages ORDER BY id"
                ).fetchall()
                self.assertEqual(platform_rows, [("msg-new-in",)])
                send_event_count = conn.execute(
                    "SELECT COUNT(*) FROM message_send_events"
                ).fetchone()[0]
                self.assertEqual(send_event_count, 0)
            finally:
                conn.close()


def _message_event(
    platform_msg_id: str,
    direction: str,
    sender_role: str,
    content: str,
    *,
    content_type: str = "text",
) -> dict:
    return {
        "event_id": f"evt-{platform_msg_id}",
        "event_type": "message_observed",
        "platform": "qianniu",
        "account_id": "acct-1",
        "conversation_key": "qianniu:acct-1:buyer-1",
        "occurred_at": "2026-06-03T13:01:00",
        "payload": {
            "platform_msg_id": platform_msg_id,
            "direction": direction,
            "sender_role": sender_role,
            "sender_name": "buyer-1" if sender_role == "customer" else "agent",
            "content_type": content_type,
            "content": content,
            "source_type": "ui_observed",
            "confidence": 70,
            "verification_status": "unverified",
        },
    }


def _insert_send_event_for_platform_msg(db_path: Path, platform_msg_id: str) -> None:
    conn = sqlite3.connect(str(db_path))
    try:
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS message_send_events (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              message_id INTEGER,
              conversation_id INTEGER,
              phase TEXT DEFAULT '',
              detail TEXT DEFAULT ''
            )
            """
        )
        row = conn.execute(
            """
            SELECT id, conversation_id
            FROM messages
            WHERE platform_message_id = ?
            LIMIT 1
            """,
            (platform_msg_id,),
        ).fetchone()
        assert row is not None
        conn.execute(
            """
            INSERT INTO message_send_events (message_id, conversation_id, phase, detail)
            VALUES (?, ?, 'test', 'test')
            """,
            (int(row[0]), int(row[1])),
        )
        conn.commit()
    finally:
        conn.close()


if __name__ == "__main__":
    unittest.main()
