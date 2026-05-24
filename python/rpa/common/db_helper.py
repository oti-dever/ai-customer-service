"""
SQLite helper for rpa_inbox_messages and messages.
Shared by Reader (write inbox) and Writer (read/update messages).

默认库路径与 Qt `Database::open()` 对齐（与 AppSettings 中 Organization/Application 一致）：
- Windows：%APPDATA%\\YangYangAI\\CustomerServiceDemo\\app.db（若该文件已存在则优先使用）
- 否则：仓库根下 database/app.db（便于未安装 Qt、仅跑 RPA 的开发/CI）

仍可通过环境变量 AI_CUSTOMER_SERVICE_DB 覆盖为任意绝对路径。
"""
from __future__ import annotations

import os
import sqlite3
import sys
from datetime import datetime
from pathlib import Path
from typing import List, Optional

# common/db_helper.py -> parents[3] = 仓库根
PROJECT_ROOT = Path(__file__).resolve().parents[3]

# 与 src/utils/appsettings.h 中 configureApplication 一致
_QT_ORG = "YangYangAI"
_QT_APP = "CustomerServiceDemo"
_QT_DB_NAME = "app.db"


def _qt_appdata_db_candidate() -> Optional[Path]:
    """与 Qt QStandardPaths::AppDataLocation + app.db 等价的候选路径（文件存在时才采用）。"""
    if os.name == "nt":
        roaming = os.environ.get("APPDATA", "").strip()
        if not roaming:
            return None
        return Path(roaming) / _QT_ORG / _QT_APP / _QT_DB_NAME
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / _QT_ORG / _QT_APP / _QT_DB_NAME
    xdg = os.environ.get("XDG_DATA_HOME", "").strip()
    base = Path(xdg) if xdg else Path.home() / ".local" / "share"
    return base / _QT_ORG / _QT_APP / _QT_DB_NAME


def resolved_default_db_path() -> Path:
    raw = (os.environ.get("AI_CUSTOMER_SERVICE_DB") or "").strip()
    if raw:
        return Path(raw).expanduser()
    cand = _qt_appdata_db_candidate()
    if cand is not None and cand.is_file():
        return cand
    return PROJECT_ROOT / "database" / "app.db"


def open_db(db_path: Path | None = None) -> sqlite3.Connection:
    path = db_path or resolved_default_db_path()
    conn = sqlite3.connect(str(path), timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn


def write_inbox_batch(
    conn: sqlite3.Connection,
    platform: str,
    platform_conversation_id: str,
    customer_name: str,
    items: List[tuple],  # [(content, platform_msg_id), ...] or [(content, platform_msg_id, sender_name, original_timestamp), ...]
    sender_name: str = "",
    original_timestamp: str = "",
    at_time: Optional[str] = None,
) -> int:
    """
    Insert batch into rpa_inbox_messages. Uses INSERT OR IGNORE for idempotency.
    Returns count of inserted rows.
    
    Args:
        items: [(content, platform_msg_id), ...]
        sender_name: OCR 识别的发送者名称（如 "店铺:昵称"）
        original_timestamp: 写入行级 original_timestamp 的默认值（条目不包含第 4 元时）
        at_time: 若提供，本批所有行的 created_at 使用该时间（与 reader 侧写入的入库时间对齐）
    """
    if not items:
        return 0
    now = at_time or datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cur = conn.cursor()
    count = 0
    for item in items:
        if len(item) >= 4:
            content, platform_msg_id, item_sender_name, item_original_timestamp = item[:4]
        else:
            content, platform_msg_id = item[:2]
            item_sender_name = sender_name
            item_original_timestamp = original_timestamp
        cur.execute(
            """
            INSERT OR IGNORE INTO rpa_inbox_messages
            (platform, platform_conversation_id, customer_name, content, created_at, platform_msg_id, consume_status, error_reason, sender_name, original_timestamp)
            VALUES (?, ?, ?, ?, ?, ?, 0, '', ?, ?)
            """,
            (
                platform,
                platform_conversation_id,
                customer_name,
                content,
                now,
                platform_msg_id,
                item_sender_name,
                item_original_timestamp,
            ),
        )
        if cur.rowcount > 0:
            count += 1
    conn.commit()
    return count


def write_inbox_message(
    conn: sqlite3.Connection,
    platform: str,
    platform_conversation_id: str,
    customer_name: str,
    content: str,
    platform_msg_id: str,
    sender_name: str = "",
    original_timestamp: str = "",
    content_image_path: str = "",
) -> bool:
    """
    Insert single message into rpa_inbox_messages.
    Returns True if inserted, False if duplicate.
    content_image_path: 千牛 chat_content_mode=image_only 时聊天区 PNG 绝对路径；空表示纯文本。
    """
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cur = conn.cursor()
    cur.execute(
        """
        INSERT OR IGNORE INTO rpa_inbox_messages
        (platform, platform_conversation_id, customer_name, content, created_at, platform_msg_id, consume_status, error_reason, sender_name, original_timestamp, content_image_path)
        VALUES (?, ?, ?, ?, ?, ?, 0, '', ?, ?, ?)
        """,
        (
            platform,
            platform_conversation_id,
            customer_name,
            content,
            now,
            platform_msg_id,
            sender_name,
            original_timestamp,
            content_image_path or "",
        ),
    )
    conn.commit()
    return cur.rowcount > 0


def fetch_pending_send(
    conn: sqlite3.Connection,
    platform: str,
    limit: int = 20,
) -> List[tuple[int, int, str, str]]:
    """
    Fetch messages with sync_status=10 for given platform.
    Returns [(msg_id, conversation_id, content, platform_conversation_id), ...]
    """
    cur = conn.cursor()
    cur.execute(
        """
        SELECT m.id, m.conversation_id, m.content, c.platform_conversation_id
        FROM messages m
        JOIN conversations c ON c.id = m.conversation_id
        WHERE m.direction = 'out'
          AND m.sync_status = 10
          AND c.platform = ?
        ORDER BY m.id ASC
        LIMIT ?
        """,
        (platform, limit),
    )
    return cur.fetchall()


def count_pending_send(conn: sqlite3.Connection, platform: str) -> int:
    """
    待发 outbound 条数（sync_status=10），供 Reader 与 Writer 协调时判断。
    """
    cur = conn.cursor()
    cur.execute(
        """
        SELECT COUNT(*)
        FROM messages m
        JOIN conversations c ON c.id = m.conversation_id
        WHERE m.direction = 'out'
          AND m.sync_status = 10
          AND c.platform = ?
        """,
        (platform,),
    )
    row = cur.fetchone()
    return int(row[0]) if row and row[0] is not None else 0


def mark_sent_ok(conn: sqlite3.Connection, msg_id: int) -> None:
    cur = conn.cursor()
    cur.execute(
        "UPDATE messages SET sync_status = 11, error_reason = '' WHERE id = ?",
        (msg_id,),
    )
    conn.commit()


def mark_sent_failed(conn: sqlite3.Connection, msg_id: int, reason: str) -> None:
    cur = conn.cursor()
    cur.execute(
        "UPDATE messages SET sync_status = 12, error_reason = ? WHERE id = ?",
        (reason[:500], msg_id),
    )
    conn.commit()


def insert_send_event(
    conn: sqlite3.Connection,
    message_id: int,
    conversation_id: int,
    phase: str,
    detail: str = "",
) -> None:
    """
    Append one fine-grained send phase row for UI timeline / observability.
    phase: stable codes e.g. dequeued, lock_acquired, switch_chat, send_text,
    receipt_check, receipt_result, success, failed, lock_timeout.
    """
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO message_send_events (message_id, conversation_id, phase, detail)
        VALUES (?, ?, ?, ?)
        """,
        (message_id, conversation_id, phase[:64], (detail or "")[:500]),
    )
    conn.commit()
