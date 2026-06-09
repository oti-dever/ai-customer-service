"""Outbound send-status DAO.

The legacy rpa_inbox_messages queue has been retired. Incoming platform data is
persisted through service.truth_store and rpa_events instead.
"""
from __future__ import annotations

import sqlite3
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
    """Deprecated: the legacy rpa_inbox_messages table is no longer used."""
    return 0


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
    """Deprecated: the legacy rpa_inbox_messages table is no longer used."""
    return False


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
          AND m.status = 'pending'
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
          AND m.status = 'pending'
          AND c.platform = ?
        """,
        (platform,),
    )
    row = cur.fetchone()
    return int(row[0]) if row and row[0] is not None else 0


def mark_sent_ok(conn: sqlite3.Connection, msg_id: int) -> None:
    cur = conn.cursor()
    cur.execute(
        "UPDATE messages SET status = 'sent', error_reason = '', updated_at = CURRENT_TIMESTAMP WHERE id = ?",
        (msg_id,),
    )
    conn.commit()


def mark_sent_failed(conn: sqlite3.Connection, msg_id: int, reason: str) -> None:
    cur = conn.cursor()
    cur.execute(
        "UPDATE messages SET status = 'failed', error_reason = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?",
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
