import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service.app_database import ensure_app_database_schema
from service.migrate_legacy_databases import migrate_service_db_to_app_data


def temporary_directory():
    root = REPO_ROOT / "Testing" / "tmp"
    root.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=root)


class MigrateLegacyDatabasesTests(unittest.TestCase):
    def test_migrates_service_business_tables_to_app_data_db(self):
        with temporary_directory() as tmp:
            source = Path(tmp) / "service.db"
            target = Path(tmp) / "app_data.db"

            ensure_app_database_schema(source)
            conn = sqlite3.connect(str(source))
            try:
                conn.execute(
                    """
                    INSERT INTO conversations
                    (id, platform, platform_conversation_id, account_id, customer_name,
                     last_message, last_time, unread_count, status, updated_at)
                    VALUES
                    (1, 'wechat', 'wechat:张三', 'acct-1', '张三',
                     '你好', '2026-06-03 13:01:00', 1, 'active',
                     '2026-06-03 13:01:00')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO messages
                    (id, conversation_id, platform_message_id, client_message_id,
                     direction, sender, sender_name, content_type, content, status,
                     message_time, updated_at)
                    VALUES
                    (10, 1, 'wechat-msg-1', '', 'in', 'customer', '张三',
                     'text', '你好', 'observed',
                     '2026-06-03 13:01:00', '2026-06-03 13:01:00')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO wechat_conversations
                    (conversation_id, wechat_account_id, wechat_conversation_key,
                     display_name)
                    VALUES (1, 'acct-1', 'wechat:张三', '张三')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO wechat_messages
                    (message_id, conversation_id, wechat_account_id,
                     wechat_conversation_key, wechat_display_name,
                     platform_message_id, direction, sender_role, source_type,
                     confidence, verification_status)
                    VALUES
                    (10, 1, 'acct-1', 'wechat:张三', '张三',
                     'wechat-msg-1', 'inbound', 'customer', 'ui_observed',
                     90, 'unverified')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO rpa_events
                    (event_id, event_type, platform, account_id, conversation_key,
                     occurred_at, payload_json, raw_event_json)
                    VALUES
                    ('evt-1', 'message_observed', 'wechat', 'acct-1',
                     'wechat:张三', '2026-06-03T13:01:00', '{}', '{}')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO conversation_mutations
                    (platform, account_id, conversation_key, mutation_type,
                     effective_at, operator, reason)
                    VALUES
                    ('wechat', 'acct-1', 'wechat:张三', 'clear_messages',
                     '2026-06-03T13:02:00', 'tester', '')
                    """
                )
                conn.commit()
            finally:
                conn.close()

            result = migrate_service_db_to_app_data(source, target, overwrite=False)

            self.assertTrue(result["migrated"])
            self.assertEqual(result["inserted_counts"]["conversations"], 1)
            self.assertEqual(result["inserted_counts"]["messages"], 1)
            self.assertEqual(result["inserted_counts"]["wechat_conversations"], 1)
            self.assertEqual(result["inserted_counts"]["wechat_messages"], 1)
            self.assertEqual(result["inserted_counts"]["rpa_events"], 1)
            self.assertEqual(result["inserted_counts"]["conversation_mutations"], 1)

            conn = sqlite3.connect(str(target))
            try:
                conv = conn.execute(
                    """
                    SELECT platform, platform_conversation_id, customer_name,
                           last_message
                    FROM conversations
                    """
                ).fetchone()
                self.assertEqual(conv, ("wechat", "wechat:张三", "张三", "你好"))

                message = conn.execute(
                    """
                    SELECT conversation_id, platform_message_id, content, status
                    FROM messages
                    """
                ).fetchone()
                self.assertEqual(message, (1, "wechat-msg-1", "你好", "observed"))

                ui_tables = conn.execute(
                    """
                    SELECT name FROM sqlite_master
                    WHERE type = 'table' AND name = 'ui_conversation_drafts'
                    """
                ).fetchall()
                self.assertEqual(len(ui_tables), 1)
            finally:
                conn.close()

    def test_refuses_to_merge_into_non_empty_app_data_without_overwrite(self):
        with temporary_directory() as tmp:
            source = Path(tmp) / "service.db"
            target = Path(tmp) / "app_data.db"
            ensure_app_database_schema(source)
            ensure_app_database_schema(target)

            conn = sqlite3.connect(str(source))
            try:
                conn.execute(
                    """
                    INSERT INTO conversations
                    (platform, platform_conversation_id, customer_name)
                    VALUES ('wechat', 'source', 'source')
                    """
                )
                conn.commit()
            finally:
                conn.close()

            conn = sqlite3.connect(str(target))
            try:
                conn.execute(
                    """
                    INSERT INTO conversations
                    (platform, platform_conversation_id, customer_name)
                    VALUES ('wechat', 'target', 'target')
                    """
                )
                conn.commit()
            finally:
                conn.close()

            result = migrate_service_db_to_app_data(source, target, overwrite=False)

            self.assertFalse(result["migrated"])
            self.assertEqual(result["reason"], "target_business_data_exists")


if __name__ == "__main__":
    unittest.main()
