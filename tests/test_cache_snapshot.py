import os
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.db.connection import resolved_default_db_path
from service.cache_snapshot import (
    DEFAULT_SERVICE_DB_PATH,
    build_cache_snapshot,
    build_conversation_list,
    resolved_snapshot_db_path,
)


def temporary_directory():
    root = REPO_ROOT / "Testing" / "tmp"
    root.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(dir=root)


class CacheSnapshotTests(unittest.TestCase):
    def test_empty_database_returns_empty_snapshot(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "empty.db"

            snapshot = build_cache_snapshot(db_path=db_path)

            self.assertEqual(snapshot["status"], "success")
            self.assertEqual(snapshot["source_role"], "python_service_db")
            self.assertEqual(snapshot["cursor"], "")
            self.assertEqual(snapshot["snapshot_cursor"], "")
            self.assertTrue(snapshot["full_snapshot"])
            self.assertEqual(snapshot["conversation_count"], 0)
            self.assertEqual(snapshot["message_count"], 0)
            self.assertEqual(snapshot["conversations"], [])

    def test_snapshot_filters_platform_and_embeds_messages(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "app.db"
            conn = sqlite3.connect(str(db_path))
            try:
                conn.executescript(
                    """
                    CREATE TABLE conversations (
                        id INTEGER PRIMARY KEY,
                        platform TEXT,
                        platform_conversation_id TEXT,
                        account_id TEXT,
                        customer_name TEXT,
                        last_message TEXT,
                        unread_count INTEGER,
                        status TEXT,
                        last_time TEXT,
                        created_at TEXT,
                        updated_at TEXT,
                        deleted_at TEXT
                    );
                    CREATE TABLE messages (
                        id INTEGER PRIMARY KEY,
                        conversation_id INTEGER,
                        direction TEXT,
                        content TEXT,
                        sender TEXT,
                        sender_name TEXT,
                        platform_msg_id TEXT,
                        sync_status INTEGER,
                        error_reason TEXT,
                        original_timestamp TEXT,
                        content_image_path TEXT,
                        source_type TEXT,
                        confidence INTEGER,
                        verification_status TEXT,
                        content_type TEXT,
                        observed_at TEXT,
                        client_message_id TEXT,
                        cache_scope TEXT,
                        cache_origin TEXT,
                        created_at TEXT
                    );
                    CREATE TABLE wechat_messages (
                        id INTEGER PRIMARY KEY,
                        message_id INTEGER UNIQUE,
                        conversation_id INTEGER,
                        platform_message_id TEXT,
                        content_image_path TEXT,
                        evidence_ref TEXT
                    );
                    """
                )
                conn.execute(
                    """
                    INSERT INTO conversations
                    (id, platform, platform_conversation_id, account_id, customer_name,
                     last_message, unread_count, status, last_time, created_at,
                     updated_at, deleted_at)
                    VALUES
                    (1, 'wechat', 'wechat-001', 'wechat', '张三',
                     '你好', 1, 'active',
                     '2026-06-03 12:00:00', '2026-06-03 11:59:59',
                     '2026-06-03 12:00:00', NULL),
                    (2, 'qianniu', 'qianniu-001', 'qianniu', '李四',
                     '在吗', 0, 'active',
                     '2026-06-03 12:01:00', '2026-06-03 12:00:59',
                     '2026-06-03 12:01:00', NULL)
                    """
                )
                conn.execute(
                    """
                    INSERT INTO messages
                    (id, conversation_id, direction, content, sender, sender_name,
                     platform_msg_id, sync_status, error_reason, original_timestamp,
                     content_image_path, source_type, confidence, verification_status,
                     content_type, observed_at, client_message_id, cache_scope,
                     cache_origin, created_at)
                    VALUES
                    (10, 1, 'in', '你好', 'customer', '张三',
                     'm-001', 1, '', '', '', 'ui_observed', 90,
                     'unverified', 'text', '2026-06-03 12:00:00', '',
                     'local_cache', 'platform_observed_cache', '2026-06-03 12:00:00')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO messages
                    (id, conversation_id, direction, content, sender, sender_name,
                     platform_msg_id, sync_status, error_reason, original_timestamp,
                     content_image_path, source_type, confidence, verification_status,
                     content_type, observed_at, client_message_id, cache_scope,
                     cache_origin, created_at)
                    VALUES
                    (11, 1, 'in', '文件 知识点.md 21.5K 微信电脑版', 'customer', '寮犱笁',
                     'm-002', 1, '', '', '', 'ui_observed', 90,
                     'unverified', 'file', '2026-06-03 12:00:01', '',
                     'local_cache', 'platform_observed_cache', '2026-06-03 12:00:01')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO wechat_messages
                    (message_id, conversation_id, platform_message_id, content_image_path, evidence_ref)
                    VALUES
                    (11, 1, 'm-002',
                     '',
                     'D:/ai-customer-service/python/rpa/_media/wechat/file/知识点.md')
                    """
                )
                conn.commit()
            finally:
                conn.close()

            snapshot = build_cache_snapshot(platform="wechat", db_path=db_path)

            self.assertEqual(snapshot["status"], "success")
            self.assertEqual(snapshot["source_role"], "python_service_db")
            self.assertEqual(snapshot["platform"], "wechat")
            self.assertEqual(snapshot["cursor"], "")
            self.assertEqual(snapshot["snapshot_cursor"], "2026-06-03 12:00:01")
            self.assertTrue(snapshot["full_snapshot"])
            self.assertEqual(snapshot["conversation_count"], 1)
            self.assertEqual(snapshot["message_count"], 2)
            conversation = snapshot["conversations"][0]
            self.assertEqual(conversation["platform_conversation_id"], "wechat-001")
            self.assertEqual(conversation["created_at"], "2026-06-03 11:59:59")
            self.assertIsNone(conversation["deleted_at"])
            self.assertEqual(conversation["messages"][0]["platform_message_id"], "m-001")
            self.assertEqual(conversation["messages"][1]["platform_message_id"], "m-002")
            self.assertEqual(conversation["messages"][1]["content_type"], "file")
            self.assertEqual(
                conversation["messages"][1]["content_image_path"],
                "D:/ai-customer-service/python/rpa/_media/wechat/file/知识点.md",
            )
            self.assertEqual(
                conversation["messages"][1]["evidence_ref"],
                "D:/ai-customer-service/python/rpa/_media/wechat/file/知识点.md",
            )

            incremental = build_cache_snapshot(
                platform="wechat",
                cursor="2026-06-03 12:00:00",
                db_path=db_path,
            )
            self.assertEqual(incremental["status"], "success")
            self.assertEqual(incremental["cursor"], "2026-06-03 12:00:00")
            self.assertFalse(incremental["full_snapshot"])
            self.assertEqual(incremental["conversation_count"], 0)

    def test_conversation_list_reads_service_rows_without_messages(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "app_data.db"
            conn = sqlite3.connect(str(db_path))
            try:
                conn.executescript(
                    """
                    CREATE TABLE conversations (
                        id INTEGER PRIMARY KEY,
                        platform TEXT,
                        platform_conversation_id TEXT,
                        account_id TEXT,
                        customer_name TEXT,
                        last_message TEXT,
                        unread_count INTEGER,
                        status TEXT,
                        last_time TEXT,
                        created_at TEXT,
                        updated_at TEXT,
                        deleted_at TEXT
                    );
                    CREATE TABLE messages (
                        id INTEGER PRIMARY KEY,
                        conversation_id INTEGER,
                        direction TEXT,
                        content TEXT,
                        sender TEXT,
                        message_time TEXT,
                        created_at TEXT,
                        deleted_at TEXT
                    );
                    """
                )
                conn.execute(
                    """
                    INSERT INTO conversations
                    (id, platform, platform_conversation_id, account_id, customer_name,
                     last_message, unread_count, status, last_time, created_at,
                     updated_at, deleted_at)
                    VALUES
                    (1, 'wechat', 'wechat-001', 'wechat', '张三',
                     '最后入站', 2, 'active',
                     '2026-06-03 12:00:00', '2026-06-03 11:00:00',
                     '2026-06-03 12:00:00', NULL),
                    (2, 'qianniu', 'qianniu-001', 'qianniu', '李四',
                     '千牛消息', 0, 'active',
                     '2026-06-03 12:01:00', '2026-06-03 11:01:00',
                     '2026-06-03 12:01:00', NULL),
                    (3, 'wechat', 'deleted-001', 'wechat', '删除会话',
                     '删除', 0, 'deleted',
                     '2026-06-03 12:02:00', '2026-06-03 11:02:00',
                     '2026-06-03 12:02:00', '2026-06-03 12:03:00')
                    """
                )
                conn.execute(
                    """
                    INSERT INTO messages
                    (id, conversation_id, direction, content, sender, message_time, created_at, deleted_at)
                    VALUES
                    (10, 1, 'out', '旧出站', 'agent',
                     '2026-06-03 11:59:00', '2026-06-03 11:59:00', NULL),
                    (11, 1, 'in', '最后入站', 'customer',
                     '2026-06-03 12:00:00', '2026-06-03 12:00:00', NULL)
                    """
                )
                conn.commit()
            finally:
                conn.close()

            listing = build_conversation_list(platform="wechat", db_path=db_path)

            self.assertEqual(listing["status"], "success")
            self.assertEqual(listing["source_role"], "python_service_db")
            self.assertEqual(listing["platform"], "wechat")
            self.assertEqual(listing["conversation_count"], 1)
            conversation = listing["conversations"][0]
            self.assertEqual(conversation["platform_conversation_id"], "wechat-001")
            self.assertEqual(conversation["customer_name"], "张三")
            self.assertEqual(conversation["last_message"], "最后入站")
            self.assertEqual(conversation["last_direction"], "in")

    def test_snapshot_db_path_uses_service_env_not_qt_appdata(self):
        with temporary_directory() as tmp:
            db_path = Path(tmp) / "service.db"
            old_server_db = os.environ.get("AI_CUSTOMER_SERVICE_SERVER_DB")
            old_snapshot_db = os.environ.get("AI_CUSTOMER_SERVICE_SNAPSHOT_DB")
            try:
                os.environ["AI_CUSTOMER_SERVICE_SERVER_DB"] = str(db_path)
                os.environ["AI_CUSTOMER_SERVICE_SNAPSHOT_DB"] = str(Path(tmp) / "fallback.db")
                self.assertEqual(resolved_snapshot_db_path(), db_path)
            finally:
                if old_server_db is None:
                    os.environ.pop("AI_CUSTOMER_SERVICE_SERVER_DB", None)
                else:
                    os.environ["AI_CUSTOMER_SERVICE_SERVER_DB"] = old_server_db
                if old_snapshot_db is None:
                    os.environ.pop("AI_CUSTOMER_SERVICE_SNAPSHOT_DB", None)
                else:
                    os.environ["AI_CUSTOMER_SERVICE_SNAPSHOT_DB"] = old_snapshot_db

    def test_default_python_db_paths_use_app_data_db(self):
        old_app_db = os.environ.get("AI_CUSTOMER_SERVICE_APP_DB")
        old_db = os.environ.get("AI_CUSTOMER_SERVICE_DB")
        old_server_db = os.environ.get("AI_CUSTOMER_SERVICE_SERVER_DB")
        old_snapshot_db = os.environ.get("AI_CUSTOMER_SERVICE_SNAPSHOT_DB")
        try:
            os.environ.pop("AI_CUSTOMER_SERVICE_APP_DB", None)
            os.environ.pop("AI_CUSTOMER_SERVICE_DB", None)
            os.environ.pop("AI_CUSTOMER_SERVICE_SERVER_DB", None)
            os.environ.pop("AI_CUSTOMER_SERVICE_SNAPSHOT_DB", None)
            self.assertEqual(resolved_default_db_path().name, "app_data.db")
            self.assertEqual(DEFAULT_SERVICE_DB_PATH.name, "app_data.db")
            self.assertEqual(resolved_snapshot_db_path().name, "app_data.db")
        finally:
            if old_app_db is None:
                os.environ.pop("AI_CUSTOMER_SERVICE_APP_DB", None)
            else:
                os.environ["AI_CUSTOMER_SERVICE_APP_DB"] = old_app_db
            if old_db is None:
                os.environ.pop("AI_CUSTOMER_SERVICE_DB", None)
            else:
                os.environ["AI_CUSTOMER_SERVICE_DB"] = old_db
            if old_server_db is None:
                os.environ.pop("AI_CUSTOMER_SERVICE_SERVER_DB", None)
            else:
                os.environ["AI_CUSTOMER_SERVICE_SERVER_DB"] = old_server_db
            if old_snapshot_db is None:
                os.environ.pop("AI_CUSTOMER_SERVICE_SNAPSHOT_DB", None)
            else:
                os.environ["AI_CUSTOMER_SERVICE_SNAPSHOT_DB"] = old_snapshot_db


if __name__ == "__main__":
    unittest.main()
