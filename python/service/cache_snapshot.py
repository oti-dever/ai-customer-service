from __future__ import annotations

import sqlite3
import os
from pathlib import Path
from typing import Any

from rpa.db.connection import open_db


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SERVICE_DB_PATH = PROJECT_ROOT / "database" / "app_data.db"


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


def _column_expr(columns: set[str], name: str, fallback: str = "''") -> str:
    if name in columns:
        return name
    return f"{fallback} AS {name}"


def _coalesce_nonempty_expr(terms: list[str], alias: str) -> str:
    if not terms:
        return f"'' AS {alias}"
    nonempty_terms = ", ".join([f"NULLIF({term}, '')" for term in terms])
    return f"COALESCE({nonempty_terms}, '') AS {alias}"


def resolved_snapshot_db_path() -> Path:
    raw = (
        os.environ.get("AI_CUSTOMER_SERVICE_APP_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_SERVER_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_SNAPSHOT_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_DB")
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
        created_at_expr = "created_at" if "created_at" in conversation_columns else "'' AS created_at"
        deleted_at_expr = "deleted_at" if "deleted_at" in conversation_columns else "NULL AS deleted_at"

        conversations = conn.execute(
            f"""
            SELECT id, platform, platform_conversation_id, account_id, customer_name,
                   last_message, unread_count, status, last_time, {created_at_expr},
                   updated_at, {deleted_at_expr}
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
        message_columns = _table_columns(conn, "messages")
        platform_message_id_expr = (
            "m.platform_message_id AS platform_message_id"
            if "platform_message_id" in message_columns
            else "m.platform_msg_id AS platform_message_id"
        )
        message_status_expr = (
            "m.status AS status"
            if "status" in message_columns
            else "CASE m.sync_status WHEN 10 THEN 'pending' WHEN 11 THEN 'sent' WHEN 12 THEN 'failed' ELSE 'observed' END AS status"
        )
        message_time_expr = (
            "m.message_time AS message_time"
            if "message_time" in message_columns
            else "COALESCE(NULLIF(m.observed_at, ''), NULLIF(m.original_timestamp, ''), m.created_at) AS message_time"
        )
        client_message_id_expr = (
            "m.client_message_id AS client_message_id"
            if "client_message_id" in message_columns
            else "'' AS client_message_id"
        )
        error_reason_expr = (
            "m.error_reason AS error_reason" if "error_reason" in message_columns else "'' AS error_reason"
        )
        sender_name_expr = (
            "m.sender_name AS sender_name" if "sender_name" in message_columns else "'' AS sender_name"
        )
        content_type_expr = (
            "m.content_type AS content_type" if "content_type" in message_columns else "'text' AS content_type"
        )
        message_updated_at_expr = (
            "m.updated_at AS updated_at" if "updated_at" in message_columns else "m.created_at AS updated_at"
        )
        message_deleted_at_expr = (
            "m.deleted_at AS deleted_at" if "deleted_at" in message_columns else "NULL AS deleted_at"
        )
        message_content_image_terms: list[str] = []
        message_evidence_terms: list[str] = []
        message_joins: list[str] = []
        if _table_exists(conn, "wechat_messages"):
            wechat_columns = _table_columns(conn, "wechat_messages")
            message_joins.append("LEFT JOIN wechat_messages wm ON wm.message_id = m.id")
            if "content_image_path" in wechat_columns:
                message_content_image_terms.append("wm.content_image_path")
            if "evidence_ref" in wechat_columns:
                message_evidence_terms.append("wm.evidence_ref")
        if _table_exists(conn, "qianniu_messages"):
            qianniu_columns = _table_columns(conn, "qianniu_messages")
            message_joins.append("LEFT JOIN qianniu_messages qm ON qm.message_id = m.id")
            if "content_image_path" in qianniu_columns:
                message_content_image_terms.append("qm.content_image_path")
            if "evidence_ref" in qianniu_columns:
                message_evidence_terms.append("qm.evidence_ref")
        if "content_image_path" in message_columns:
            message_content_image_terms.append("m.content_image_path")
        if "evidence_ref" in message_columns:
            message_evidence_terms.append("m.evidence_ref")
        content_image_expr = _coalesce_nonempty_expr(
            message_content_image_terms + message_evidence_terms,
            "content_image_path",
        )
        evidence_ref_expr = _coalesce_nonempty_expr(message_evidence_terms, "evidence_ref")
        message_join_sql = "\n                ".join(message_joins)
        for conv in conversations:
            item = _row_to_dict(conv)
            for key in ("updated_at", "last_time"):
                value = _clean(item.get(key))
                if value and value > next_cursor:
                    next_cursor = value
            messages = conn.execute(
                f"""
                SELECT m.id AS id, m.conversation_id AS conversation_id,
                       {platform_message_id_expr}, {client_message_id_expr},
                       m.direction AS direction, m.sender AS sender, {sender_name_expr},
                       {content_type_expr}, m.content AS content,
                       {message_status_expr}, {error_reason_expr}, {message_time_expr},
                       m.created_at AS created_at, {message_updated_at_expr}, {message_deleted_at_expr},
                       {content_image_expr}, {evidence_ref_expr}
                FROM messages m
                {message_join_sql}
                WHERE m.conversation_id = ?
                ORDER BY id DESC
                LIMIT ?
                """,
                (conv["id"], msg_limit),
            ).fetchall()
            ordered_messages = [_row_to_dict(row) for row in reversed(messages)]
            for row in ordered_messages:
                for key in ("updated_at", "message_time", "created_at"):
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

def build_conversation_list(
    *,
    platform: str = "",
    conversation_limit: int = 100,
    db_path: Path | None = None,
) -> dict[str, Any]:
    conv_limit = _clamp_limit(conversation_limit, 100, 1000)
    normalized_platform = _clean(platform).lower()
    path = db_path or resolved_snapshot_db_path()
    if not path.exists():
        return {
            "status": "success",
            "source_role": "python_service_db",
            "db_path": str(path),
            "platform": normalized_platform,
            "conversation_count": 0,
            "conversations": [],
        }

    conn = open_db(path)
    conn.row_factory = sqlite3.Row
    try:
        if not _table_exists(conn, "conversations"):
            return {
                "status": "success",
                "source_role": "python_service_db",
                "db_path": str(path),
                "platform": normalized_platform,
                "conversation_count": 0,
                "conversations": [],
            }

        conversation_columns = _table_columns(conn, "conversations")
        where = ""
        params: list[Any] = []
        clauses: list[str] = []
        if normalized_platform:
            clauses.append("c.platform = ?")
            params.append(normalized_platform)
        if "deleted_at" in conversation_columns:
            clauses.append("(c.deleted_at IS NULL OR c.deleted_at = '')")
        if clauses:
            where = "WHERE " + " AND ".join(clauses)

        created_at_expr = "c.created_at" if "created_at" in conversation_columns else "'' AS created_at"
        deleted_at_expr = "c.deleted_at" if "deleted_at" in conversation_columns else "NULL AS deleted_at"
        updated_at_expr = "c.updated_at" if "updated_at" in conversation_columns else "c.last_time AS updated_at"
        updated_at_order_expr = "c.updated_at" if "updated_at" in conversation_columns else "c.last_time"
        if _table_exists(conn, "messages"):
            message_columns = _table_columns(conn, "messages")
            message_deleted_clause = (
                "AND (m.deleted_at IS NULL OR m.deleted_at = '')"
                if "deleted_at" in message_columns
                else ""
            )
            message_time_order_expr = (
                "COALESCE(NULLIF(m.message_time, ''), m.created_at)"
                if "message_time" in message_columns
                else "m.created_at"
            )
            last_direction_expr = """
                   (
                     SELECT m.direction
                     FROM messages m
                     WHERE m.conversation_id = c.id
                       {message_deleted_clause}
                     ORDER BY {message_time_order_expr} DESC, m.id DESC
                     LIMIT 1
                   ) AS last_direction
            """.format(
                message_deleted_clause=message_deleted_clause,
                message_time_order_expr=message_time_order_expr,
            )
        else:
            last_direction_expr = "'' AS last_direction"
        params.append(conv_limit)
        rows = conn.execute(
            f"""
            SELECT c.id, c.platform, c.platform_conversation_id, c.account_id,
                   c.customer_name, c.last_message, c.unread_count, c.status,
                   c.last_time, {created_at_expr}, {updated_at_expr}, {deleted_at_expr},
                   {last_direction_expr}
            FROM conversations c
            {where}
            ORDER BY COALESCE(NULLIF(c.last_time, ''), {updated_at_order_expr}, c.created_at) DESC, c.id DESC
            LIMIT ?
            """,
            params,
        ).fetchall()
    finally:
        conn.close()

    conversations = [_row_to_dict(row) for row in rows]
    for item in conversations:
        item["last_direction"] = _clean(item.get("last_direction"))
    return {
        "status": "success",
        "source_role": "python_service_db",
        "db_path": str(path),
        "platform": normalized_platform,
        "conversation_count": len(conversations),
        "conversations": conversations,
    }

def build_conversation_messages(
    *,
    platform: str,
    conversation_key: str,
    message_limit: int = 300,
    db_path: Path | None = None,
) -> dict[str, Any]:
    normalized_platform = _clean(platform).lower()
    normalized_key = _clean(conversation_key)
    msg_limit = _clamp_limit(message_limit, 300, 2000)
    path = db_path or resolved_snapshot_db_path()
    if not path.exists():
        return {
            "status": "success",
            "source_role": "python_service_db",
            "db_path": str(path),
            "platform": normalized_platform,
            "conversation_key": normalized_key,
            "message_count": 0,
            "messages": [],
        }

    conn = open_db(path)
    conn.row_factory = sqlite3.Row
    try:
        if not _table_exists(conn, "conversations") or not _table_exists(conn, "messages"):
            return {
                "status": "success",
                "source_role": "python_service_db",
                "db_path": str(path),
                "platform": normalized_platform,
                "conversation_key": normalized_key,
                "message_count": 0,
                "messages": [],
            }

        conversation_columns = _table_columns(conn, "conversations")
        deleted_clause = "AND (deleted_at IS NULL OR deleted_at = '')" if "deleted_at" in conversation_columns else ""
        conv = conn.execute(
            f"""
            SELECT id, platform, platform_conversation_id, account_id, customer_name
            FROM conversations
            WHERE platform = ? AND platform_conversation_id = ?
              {deleted_clause}
            LIMIT 1
            """,
            (normalized_platform, normalized_key),
        ).fetchone()
        if conv is None:
            return {
                "status": "success",
                "source_role": "python_service_db",
                "db_path": str(path),
                "platform": normalized_platform,
                "conversation_key": normalized_key,
                "message_count": 0,
                "messages": [],
            }

        message_columns = _table_columns(conn, "messages")
        platform_message_id_expr = (
            "m.platform_message_id AS platform_message_id"
            if "platform_message_id" in message_columns
            else "m.platform_msg_id AS platform_message_id"
        )
        message_status_expr = (
            "m.status AS status"
            if "status" in message_columns
            else "CASE m.sync_status WHEN 10 THEN 'pending' WHEN 11 THEN 'sent' WHEN 12 THEN 'failed' ELSE 'observed' END AS status"
        )
        message_time_expr = (
            "m.message_time AS message_time"
            if "message_time" in message_columns
            else "COALESCE(NULLIF(m.observed_at, ''), NULLIF(m.original_timestamp, ''), m.created_at) AS message_time"
        )
        client_message_id_expr = (
            "m.client_message_id AS client_message_id"
            if "client_message_id" in message_columns
            else "'' AS client_message_id"
        )
        error_reason_expr = (
            "m.error_reason AS error_reason" if "error_reason" in message_columns else "'' AS error_reason"
        )
        sender_name_expr = (
            "m.sender_name AS sender_name" if "sender_name" in message_columns else "'' AS sender_name"
        )
        content_type_expr = (
            "m.content_type AS content_type" if "content_type" in message_columns else "'text' AS content_type"
        )
        message_updated_at_expr = (
            "m.updated_at AS updated_at" if "updated_at" in message_columns else "m.created_at AS updated_at"
        )
        message_deleted_at_expr = (
            "m.deleted_at AS deleted_at" if "deleted_at" in message_columns else "NULL AS deleted_at"
        )
        message_content_image_terms: list[str] = []
        message_evidence_terms: list[str] = []
        message_joins: list[str] = []
        if _table_exists(conn, "wechat_messages"):
            wechat_columns = _table_columns(conn, "wechat_messages")
            message_joins.append("LEFT JOIN wechat_messages wm ON wm.message_id = m.id")
            if "content_image_path" in wechat_columns:
                message_content_image_terms.append("wm.content_image_path")
            if "evidence_ref" in wechat_columns:
                message_evidence_terms.append("wm.evidence_ref")
        if _table_exists(conn, "qianniu_messages"):
            qianniu_columns = _table_columns(conn, "qianniu_messages")
            message_joins.append("LEFT JOIN qianniu_messages qm ON qm.message_id = m.id")
            if "content_image_path" in qianniu_columns:
                message_content_image_terms.append("qm.content_image_path")
            if "evidence_ref" in qianniu_columns:
                message_evidence_terms.append("qm.evidence_ref")
        if "content_image_path" in message_columns:
            message_content_image_terms.append("m.content_image_path")
        if "evidence_ref" in message_columns:
            message_evidence_terms.append("m.evidence_ref")
        content_image_expr = _coalesce_nonempty_expr(
            message_content_image_terms + message_evidence_terms,
            "content_image_path",
        )
        evidence_ref_expr = _coalesce_nonempty_expr(message_evidence_terms, "evidence_ref")
        message_join_sql = "\n                ".join(message_joins)
        messages = conn.execute(
            f"""
            SELECT m.id AS id, m.conversation_id AS conversation_id,
                   {platform_message_id_expr}, {client_message_id_expr},
                   m.direction AS direction, m.sender AS sender, {sender_name_expr},
                   {content_type_expr}, m.content AS content,
                   {message_status_expr}, {error_reason_expr}, {message_time_expr},
                   m.created_at AS created_at, {message_updated_at_expr}, {message_deleted_at_expr},
                   {content_image_expr}, {evidence_ref_expr}
            FROM messages m
            {message_join_sql}
            WHERE m.conversation_id = ?
            ORDER BY id DESC
            LIMIT ?
            """,
            (int(conv["id"]), msg_limit),
        ).fetchall()
        ordered_messages = [_row_to_dict(row) for row in reversed(messages)]
    finally:
        conn.close()

    return {
        "status": "success",
        "source_role": "python_service_db",
        "db_path": str(path),
        "platform": normalized_platform,
        "conversation_key": normalized_key,
        "message_count": len(ordered_messages),
        "messages": ordered_messages,
    }


__all__ = [
    "build_cache_snapshot",
    "build_conversation_list",
    "build_conversation_messages",
    "resolved_snapshot_db_path",
]
