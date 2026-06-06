from __future__ import annotations

import sqlite3
import os
from pathlib import Path
from typing import Any

from rpa.db.connection import open_db


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SERVICE_DB_PATH = PROJECT_ROOT / "database" / "service.db"


def _clean(value: Any) -> str:
    return str(value or "").strip()


def _clamp_limit(value: Any, default: int, maximum: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return default
    return max(1, min(parsed, maximum))


def _table_exists(conn: sqlite3.Connection, table_name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ? LIMIT 1",
        (table_name,),
    ).fetchone()
    return row is not None


def _table_columns(conn: sqlite3.Connection, table_name: str) -> set[str]:
    return {str(row[1]) for row in conn.execute(f"PRAGMA table_info({table_name})").fetchall()}


def _row_to_dict(row: sqlite3.Row) -> dict[str, Any]:
    return {key: row[key] for key in row.keys()}


def resolved_snapshot_db_path() -> Path:
    raw = (
        os.environ.get("AI_CUSTOMER_SERVICE_SERVER_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_SNAPSHOT_DB")
        or ""
    ).strip()
    if raw:
        return Path(raw).expanduser()
    return DEFAULT_SERVICE_DB_PATH


def build_cache_snapshot(
    *,
    platform: str = "",
    cursor: str = "",
    conversation_limit: int = 100,
    message_limit: int = 200,
    db_path: Path | None = None,
) -> dict[str, Any]:
    """Build a read-only snapshot for rebuilding the C++ local cache."""
    conv_limit = _clamp_limit(conversation_limit, 100, 500)
    msg_limit = _clamp_limit(message_limit, 200, 1000)
    normalized_platform = _clean(platform).lower()
    snapshot_cursor = _clean(cursor)
    path = db_path or resolved_snapshot_db_path()

    conn = open_db(path)
    try:
        conn.row_factory = sqlite3.Row
        if not _table_exists(conn, "conversations") or not _table_exists(conn, "messages"):
            return {
                "status": "success",
                "source_role": "python_service_db",
                "db_path": str(path),
                "platform": normalized_platform,
                "cursor": snapshot_cursor,
                "snapshot_cursor": snapshot_cursor,
                "full_snapshot": not snapshot_cursor,
                "conversation_count": 0,
                "message_count": 0,
                "conversations": [],
            }

        where = ""
        params: list[Any] = []
        clauses: list[str] = []
        conversation_columns = _table_columns(conn, "conversations")
        if normalized_platform:
            clauses.append("platform = ?")
            params.append(normalized_platform)
        if "deleted_at" in conversation_columns:
            clauses.append("(deleted_at IS NULL OR deleted_at = '')")
        if snapshot_cursor:
            clauses.append(
                "(coalesce(updated_at, '') > ? OR coalesce(last_time, '') > ?)"
            )
            params.extend([snapshot_cursor, snapshot_cursor])
        if clauses:
            where = "WHERE " + " AND ".join(clauses)
        params.append(conv_limit)

        conversations = conn.execute(
            f"""
            SELECT id, platform, platform_conversation_id, account_id, customer_name,
                   last_message, unread_count, status, source_type, confidence,
                   cache_scope, cache_origin, last_time, updated_at
            FROM conversations
            {where}
            ORDER BY last_time DESC, id DESC
            LIMIT ?
            """,
            params,
        ).fetchall()

        snapshot_items: list[dict[str, Any]] = []
        total_messages = 0
        next_cursor = snapshot_cursor
        for conv in conversations:
            item = _row_to_dict(conv)
            for key in ("updated_at", "last_time"):
                value = _clean(item.get(key))
                if value and value > next_cursor:
                    next_cursor = value
            messages = conn.execute(
                """
                SELECT id, conversation_id, direction, content, sender, sender_name,
                       platform_msg_id, sync_status, error_reason, original_timestamp,
                       content_image_path, source_type, confidence, verification_status,
                       content_type, observed_at, client_message_id, cache_scope,
                       cache_origin, created_at
                FROM messages
                WHERE conversation_id = ?
                ORDER BY id DESC
                LIMIT ?
                """,
                (conv["id"], msg_limit),
            ).fetchall()
            ordered_messages = [_row_to_dict(row) for row in reversed(messages)]
            for row in ordered_messages:
                for key in ("observed_at", "created_at"):
                    value = _clean(row.get(key))
                    if value and value > next_cursor:
                        next_cursor = value
            item["messages"] = ordered_messages
            total_messages += len(ordered_messages)
            snapshot_items.append(item)
    finally:
        conn.close()

    return {
        "status": "success",
        "source_role": "python_service_db",
        "db_path": str(path),
        "platform": normalized_platform,
        "cursor": snapshot_cursor,
        "snapshot_cursor": next_cursor,
        "full_snapshot": not snapshot_cursor and len(snapshot_items) < conv_limit,
        "conversation_count": len(snapshot_items),
        "message_count": total_messages,
        "conversations": snapshot_items,
    }


__all__ = ["build_cache_snapshot", "resolved_snapshot_db_path"]
