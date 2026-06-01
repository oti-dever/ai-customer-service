"""RPA inbox and outbound send-status DAO."""
from __future__ import annotations

import sqlite3
from datetime import datetime
from typing import List, Optional


def write_inbox_batch(
    conn: sqlite3.Connection,
    platform: str,
    platform_conversation_id: str,
    customer_name: str,
    items: List[tuple],
    sender_name: str = "",
    original_timestamp: str = "",
    at_time: Optional[str] = None,
) -> int:
    """Insert a batch into rpa_inbox_messages using INSERT OR IGNORE."""
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
    """Insert one message into rpa_inbox_messages."""
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
    """Fetch outbound messages waiting for platform delivery."""
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
    """Return outbound message count waiting for platform delivery."""
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
    """Append one fine-grained send phase row for UI timeline / observability."""
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO message_send_events (message_id, conversation_id, phase, detail)
        VALUES (?, ?, ?, ?)
        """,
        (message_id, conversation_id, phase[:64], (detail or "")[:500]),
    )
    conn.commit()

__all__ = [
    "count_pending_send",
    "fetch_pending_send",
    "insert_send_event",
    "mark_sent_failed",
    "mark_sent_ok",
    "write_inbox_batch",
    "write_inbox_message",
]
