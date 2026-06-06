from __future__ import annotations

import json
import logging
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from rpa.db.connection import open_db

from .cache_snapshot import resolved_snapshot_db_path


def _clean(value: Any) -> str:
    return str(value or "").strip()


def _json(value: Any) -> str:
    return json.dumps(value or {}, ensure_ascii=False, sort_keys=True)


def _json_loads(value: Any) -> dict[str, Any]:
    try:
        parsed = json.loads(_clean(value))
    except json.JSONDecodeError:
        return {}
    return parsed if isinstance(parsed, dict) else {}


def _sqlite_time(value: Any) -> str:
    text = _clean(value)
    if not text:
        return ""
    return text.replace("T", " ").replace("Z", "")[:19]


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _parse_time(value: Any) -> datetime | None:
    text = _clean(value)
    if not text:
        return None
    normalized = text.replace("Z", "+00:00")
    if " " in normalized and "T" not in normalized:
        normalized = normalized.replace(" ", "T", 1)
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError:
        try:
            parsed = datetime.strptime(text[:19], "%Y-%m-%d %H:%M:%S")
        except ValueError:
            return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def _direction(value: Any, sender_role: Any = "") -> str:
    raw = _clean(value).lower()
    role = _clean(sender_role).lower()
    if raw in {"in", "inbound", "incoming", "received"} or role == "customer":
        return "in"
    if raw in {"out", "outbound", "outgoing", "sent"} or role in {"agent", "assistant"}:
        return "out"
    if raw == "system" or role == "system":
        return "system"
    return "in"


def _sender(direction: str, sender_role: Any) -> str:
    role = _clean(sender_role).lower()
    if role in {"customer", "agent", "assistant", "system"}:
        return "agent" if role == "assistant" else role
    if direction == "out":
        return "agent"
    if direction == "system":
        return "system"
    return "customer"


def _message_fingerprint(direction: Any, sender_role: Any, content: Any) -> str:
    normalized_content = " ".join(_clean(content).split())
    return f"{_direction(direction, sender_role)}\n{normalized_content}"


def _event_message_fingerprint(event: dict[str, Any]) -> str:
    payload = event.get("payload") or {}
    return _message_fingerprint(
        payload.get("direction"),
        payload.get("sender_role"),
        payload.get("content"),
    )


def _longest_tail_overlap(history: list[str], current: list[str]) -> int:
    max_len = min(len(history), len(current))
    for size in range(max_len, 0, -1):
        if history[-size:] == current[:size]:
            return size
    return 0


class PythonServiceTruthStore:
    """Persist platform events into the Python service truth database."""

    def __init__(self, db_path: Path | None = None) -> None:
        self._db_path = db_path

    def persist_event(self, event: dict[str, Any]) -> bool:
        event_type = _clean(event.get("event_type"))
        if event_type not in {"conversation_observed", "message_observed", "message_sent", "send_failed"}:
            return True

        path = self._db_path or resolved_snapshot_db_path()
        conn = open_db(path)
        try:
            self._ensure_schema(conn)
            if not self._should_accept_observed_event(conn, event):
                return False
            self._append_event_log(conn, event)
            if event_type == "conversation_observed":
                self._upsert_conversation(conn, event)
            elif event_type == "message_observed":
                conv_id = self._upsert_conversation(conn, event)
                self._upsert_message(conn, conv_id, event)
            elif event_type == "message_sent":
                self._mark_outbound_result(conn, event, sent=True)
            elif event_type == "send_failed":
                self._mark_outbound_result(conn, event, sent=False)
            conn.commit()
            return True
        finally:
            conn.close()

    def clear_conversation_messages(
        self,
        platform: str,
        conversation_key: str,
        *,
        account_id: str = "",
        operator: str = "",
        reason: str = "",
    ) -> dict[str, Any]:
        return self._apply_conversation_mutation(
            "clear_messages",
            platform,
            conversation_key,
            account_id=account_id,
            operator=operator,
            reason=reason,
        )

    def delete_conversation(
        self,
        platform: str,
        conversation_key: str,
        *,
        account_id: str = "",
        operator: str = "",
        reason: str = "",
    ) -> dict[str, Any]:
        return self._apply_conversation_mutation(
            "delete_conversation",
            platform,
            conversation_key,
            account_id=account_id,
            operator=operator,
            reason=reason,
        )

    def replay_events(self, platform: str = "", cursor: str | int | None = None, limit: int = 50) -> dict[str, Any]:
        try:
            since = int(cursor or 0)
        except (TypeError, ValueError):
            since = 0
        safe_limit = max(1, min(int(limit or 50), 200))
        normalized_platform = _clean(platform).lower()
        path = self._db_path or resolved_snapshot_db_path()

        conn = open_db(path)
        try:
            self._ensure_schema(conn)
            clauses = ["id > ?"]
            params: list[Any] = [since]
            if normalized_platform:
                clauses.append("platform = ?")
                params.append(normalized_platform)
            params.append(safe_limit)
            rows = conn.execute(
                f"""
                SELECT id, event_type, platform, account_id, conversation_key,
                       occurred_at, payload_json, raw_event_json
                FROM rpa_events
                WHERE {" AND ".join(clauses)}
                ORDER BY id ASC
                LIMIT ?
                """,
                params,
            ).fetchall()
            latest_row = conn.execute("SELECT COALESCE(MAX(id), 0) FROM rpa_events").fetchone()
        finally:
            conn.close()

        events: list[dict[str, Any]] = []
        last_cursor = since
        for row in rows:
            event_id = int(row[0])
            raw_event = _json_loads(row[7])
            event = raw_event or {
                "event_id": f"replay_{event_id}",
                "event_type": _clean(row[1]),
                "platform": _clean(row[2]),
                "account_id": _clean(row[3]),
                "conversation_key": _clean(row[4]),
                "occurred_at": _clean(row[5]),
                "payload": _json_loads(row[6]),
            }
            event["cursor"] = str(event_id)
            event["replay_cursor"] = str(event_id)
            event["replayed"] = True
            event["source_role"] = "python_service_truth_replay"
            events.append(event)
            last_cursor = event_id

        latest = int(latest_row[0]) if latest_row and latest_row[0] is not None else since
        return {
            "status": "success",
            "source_role": "python_service_truth_replay",
            "platform": normalized_platform,
            "cursor": str(last_cursor if events else since),
            "latest_cursor": str(latest),
            "event_count": len(events),
            "events": events,
        }

    def filter_observed_message_events(
        self,
        events: list[dict[str, Any]],
        *,
        bootstrap_limit: int = 100,
        incremental_limit: int = 10,
    ) -> list[dict[str, Any]]:
        candidates = [event for event in events if _clean(event.get("event_type")) == "message_observed"]
        if len(candidates) != len(events):
            return list(events)
        if not candidates:
            return []

        first = candidates[0]
        platform = _clean(first.get("platform")).lower()
        conversation_key = _clean(first.get("conversation_key"))
        if not platform or not conversation_key:
            return list(events)

        bootstrap_limit = max(1, min(int(bootstrap_limit or 100), 500))
        incremental_limit = max(1, min(int(incremental_limit or 10), 100))
        path = self._db_path or resolved_snapshot_db_path()

        conn = open_db(path)
        try:
            self._ensure_schema(conn)
            row = conn.execute(
                """
                SELECT id FROM conversations
                WHERE platform = ? AND platform_conversation_id = ?
                LIMIT 1
                """,
                (platform, conversation_key),
            ).fetchone()
            mutation = self._latest_mutation(conn, platform, conversation_key)
            if not row:
                return self._filter_candidates_by_mutation(candidates[-bootstrap_limit:], mutation)

            conversation_id = int(row[0])
            message_count = int(
                conn.execute(
                    "SELECT COUNT(*) FROM messages WHERE conversation_id = ?",
                    (conversation_id,),
                ).fetchone()[0]
                or 0
            )
            if message_count <= 0:
                return self._filter_candidates_by_mutation(candidates[-bootstrap_limit:], mutation)

            rows = conn.execute(
                """
                SELECT direction, sender, content FROM messages
                WHERE conversation_id = ?
                ORDER BY id DESC
                LIMIT ?
                """,
                (conversation_id, bootstrap_limit),
            ).fetchall()
            history_fingerprints = [
                _message_fingerprint(row[0], row[1], row[2])
                for row in reversed(rows)
            ]

            ids = [
                _clean((event.get("payload") or {}).get("platform_msg_id"))
                for event in candidates
                if isinstance(event.get("payload"), dict)
            ]
            ids = [value for value in ids if value]
            existing_ids: set[str] = set()
            if ids:
                placeholders = ",".join("?" for _ in ids)
                rows = conn.execute(
                    f"""
                    SELECT platform_msg_id FROM messages
                    WHERE conversation_id = ?
                      AND platform_msg_id IN ({placeholders})
                    """,
                    [conversation_id, *ids],
                ).fetchall()
                existing_ids = {_clean(item[0]) for item in rows}
        finally:
            conn.close()

        candidates = self._filter_candidates_by_mutation(candidates, mutation)
        if not candidates:
            return []
        candidate_fingerprints = [_event_message_fingerprint(event) for event in candidates]
        overlap = _longest_tail_overlap(history_fingerprints, candidate_fingerprints)
        if overlap > 0:
            selected: list[dict[str, Any]] = []
            seen_new_ids: set[str] = set()
            for event in candidates[overlap:]:
                payload = event.get("payload")
                if not isinstance(payload, dict):
                    continue
                platform_msg_id = _clean(payload.get("platform_msg_id"))
                if platform_msg_id and (platform_msg_id in existing_ids or platform_msg_id in seen_new_ids):
                    continue
                if _direction(payload.get("direction"), payload.get("sender_role")) != "in":
                    continue
                selected.append(event)
                if platform_msg_id:
                    seen_new_ids.add(platform_msg_id)
            return selected[-incremental_limit:]

        selected: list[dict[str, Any]] = []
        seen_new_ids: set[str] = set()
        for event in candidates:
            payload = event.get("payload")
            if not isinstance(payload, dict):
                continue
            platform_msg_id = _clean(payload.get("platform_msg_id"))
            if not platform_msg_id or platform_msg_id in existing_ids or platform_msg_id in seen_new_ids:
                continue
            if _direction(payload.get("direction"), payload.get("sender_role")) != "in":
                continue
            selected.append(event)
            seen_new_ids.add(platform_msg_id)
        return selected[-incremental_limit:]

    def _filter_candidates_by_mutation(
        self,
        candidates: list[dict[str, Any]],
        mutation: dict[str, Any] | None,
    ) -> list[dict[str, Any]]:
        if not mutation:
            return list(candidates)
        mutation_time = _parse_time(mutation.get("effective_at"))
        if mutation_time is None:
            return []
        mutation_type = mutation.get("mutation_type")
        selected: list[dict[str, Any]] = []
        for event in candidates:
            event_time = _parse_time(event.get("occurred_at"))
            if event_time is None or event_time <= mutation_time:
                continue
            if mutation_type == "delete_conversation":
                payload = event.get("payload")
                if not isinstance(payload, dict):
                    continue
                if _direction(payload.get("direction"), payload.get("sender_role")) != "in":
                    continue
            selected.append(event)
        return selected

    def persist_outbound_command(self, payload: dict[str, Any]) -> None:
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}
        command = _clean(payload.get("command"))
        if command != "send_message":
            return

        path = self._db_path or resolved_snapshot_db_path()
        conn = open_db(path)
        try:
            self._ensure_schema(conn)
            event = {
                "event_type": "message_observed",
                "platform": _clean(payload.get("platform")).lower(),
                "account_id": _clean(payload.get("account_id")),
                "conversation_key": _clean(params.get("conversation_key") or params.get("display_name")),
                "occurred_at": "",
                "client_message_id": _clean(payload.get("client_message_id") or params.get("client_message_id")),
                "payload": {
                    "direction": "outbound",
                    "sender_role": "agent",
                    "sender_name": "",
                    "content_type": "text",
                    "content": _clean(params.get("text")),
                    "source_type": "manual_confirmed",
                    "confidence": 100,
                    "verification_status": "manual_verified",
                    "client_message_id": _clean(payload.get("client_message_id") or params.get("client_message_id")),
                },
            }
            conv_id = self._upsert_conversation(conn, event)
            self._upsert_message(conn, conv_id, event, sync_status=10)
            conn.commit()
        finally:
            conn.close()

    def mark_outbound_command_failed(self, payload: dict[str, Any], reason: str) -> None:
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}
        event = {
            "event_type": "send_failed",
            "platform": _clean(payload.get("platform")).lower(),
            "account_id": _clean(payload.get("account_id")),
            "conversation_key": _clean(params.get("conversation_key") or params.get("display_name")),
            "client_message_id": _clean(payload.get("client_message_id") or params.get("client_message_id")),
            "payload": {
                "client_message_id": _clean(payload.get("client_message_id") or params.get("client_message_id")),
                "error_message": _clean(reason),
            },
        }
        self.persist_event(event)

    def _ensure_schema(self, conn: sqlite3.Connection) -> None:
        conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS conversations (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              platform TEXT NOT NULL,
              platform_conversation_id TEXT,
              account_id TEXT DEFAULT '',
              customer_name TEXT NOT NULL,
              last_message TEXT DEFAULT '',
              last_time DATETIME,
              unread_count INTEGER DEFAULT 0,
              status TEXT DEFAULT 'new',
              source_type TEXT NOT NULL DEFAULT 'mock',
              confidence INTEGER NOT NULL DEFAULT 100,
              cache_scope TEXT NOT NULL DEFAULT 'local_cache',
              cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime',
              updated_at DATETIME,
              created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
              UNIQUE(platform, platform_conversation_id)
            );
            CREATE TABLE IF NOT EXISTS messages (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              conversation_id INTEGER NOT NULL,
              direction TEXT NOT NULL,
              content TEXT NOT NULL,
              sender TEXT NOT NULL,
              sender_name TEXT DEFAULT '',
              created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
              platform_msg_id TEXT,
              sync_status INTEGER NOT NULL DEFAULT 1,
              error_reason TEXT DEFAULT '',
              original_timestamp TEXT DEFAULT '',
              content_image_path TEXT DEFAULT '',
              source_type TEXT NOT NULL DEFAULT 'mock',
              confidence INTEGER NOT NULL DEFAULT 100,
              verification_status TEXT NOT NULL DEFAULT 'unverified',
              content_type TEXT NOT NULL DEFAULT 'text',
              observed_at DATETIME,
              client_message_id TEXT DEFAULT '',
              cache_scope TEXT NOT NULL DEFAULT 'local_cache',
              cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime',
              FOREIGN KEY(conversation_id) REFERENCES conversations(id)
            );
            CREATE INDEX IF NOT EXISTS idx_messages_conv_id ON messages(conversation_id);
            CREATE TABLE IF NOT EXISTS rpa_events (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              event_id TEXT UNIQUE,
              event_type TEXT NOT NULL,
              platform TEXT NOT NULL,
              account_id TEXT DEFAULT '',
              conversation_key TEXT DEFAULT '',
              occurred_at DATETIME,
              payload_json TEXT DEFAULT '{}',
              raw_event_json TEXT DEFAULT '{}',
              source_role TEXT NOT NULL DEFAULT 'python_service_truth',
              created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_rpa_events_platform_id ON rpa_events(platform, id);
            CREATE TABLE IF NOT EXISTS conversation_mutations (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              platform TEXT NOT NULL,
              account_id TEXT DEFAULT '',
              conversation_key TEXT NOT NULL,
              mutation_type TEXT NOT NULL,
              effective_at DATETIME NOT NULL,
              operator TEXT DEFAULT '',
              reason TEXT DEFAULT '',
              created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_conversation_mutations_target
              ON conversation_mutations(platform, conversation_key, id);
            """
        )
        optional_migrations = [
            "ALTER TABLE conversations ADD COLUMN account_id TEXT DEFAULT ''",
            "ALTER TABLE conversations ADD COLUMN source_type TEXT NOT NULL DEFAULT 'mock'",
            "ALTER TABLE conversations ADD COLUMN confidence INTEGER NOT NULL DEFAULT 100",
            "ALTER TABLE conversations ADD COLUMN cache_scope TEXT NOT NULL DEFAULT 'local_cache'",
            "ALTER TABLE conversations ADD COLUMN cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime'",
            "ALTER TABLE conversations ADD COLUMN updated_at DATETIME",
            "ALTER TABLE messages ADD COLUMN sender_name TEXT DEFAULT ''",
            "ALTER TABLE messages ADD COLUMN original_timestamp TEXT DEFAULT ''",
            "ALTER TABLE messages ADD COLUMN content_image_path TEXT DEFAULT ''",
            "ALTER TABLE messages ADD COLUMN source_type TEXT NOT NULL DEFAULT 'mock'",
            "ALTER TABLE messages ADD COLUMN confidence INTEGER NOT NULL DEFAULT 100",
            "ALTER TABLE messages ADD COLUMN verification_status TEXT NOT NULL DEFAULT 'unverified'",
            "ALTER TABLE messages ADD COLUMN content_type TEXT NOT NULL DEFAULT 'text'",
            "ALTER TABLE messages ADD COLUMN observed_at DATETIME",
            "ALTER TABLE messages ADD COLUMN client_message_id TEXT DEFAULT ''",
            "ALTER TABLE messages ADD COLUMN cache_scope TEXT NOT NULL DEFAULT 'local_cache'",
            "ALTER TABLE messages ADD COLUMN cache_origin TEXT NOT NULL DEFAULT 'legacy_runtime'",
            "ALTER TABLE conversations ADD COLUMN deleted_at DATETIME",
        ]
        for migration in optional_migrations:
            try:
                conn.execute(migration)
            except sqlite3.DatabaseError:
                pass

    def _append_event_log(self, conn: sqlite3.Connection, event: dict[str, Any]) -> None:
        event_type = _clean(event.get("event_type"))
        platform = _clean(event.get("platform")).lower()
        if not event_type or not platform:
            return
        payload = event.get("payload")
        if not isinstance(payload, dict):
            payload = {}
        conn.execute(
            """
            INSERT OR IGNORE INTO rpa_events
            (event_id, event_type, platform, account_id, conversation_key,
             occurred_at, payload_json, raw_event_json, source_role)
            VALUES (?, ?, ?, ?, ?, NULLIF(?, ''), ?, ?, 'python_service_truth')
            """,
            (
                _clean(event.get("event_id")),
                event_type,
                platform,
                _clean(event.get("account_id")),
                _clean(event.get("conversation_key")),
                _sqlite_time(event.get("occurred_at")),
                _json(payload),
                _json(event),
            ),
        )

    def _apply_conversation_mutation(
        self,
        mutation_type: str,
        platform: str,
        conversation_key: str,
        *,
        account_id: str = "",
        operator: str = "",
        reason: str = "",
    ) -> dict[str, Any]:
        normalized_platform = _clean(platform).lower()
        normalized_key = _clean(conversation_key)
        if not normalized_platform or not normalized_key:
            return {
                "status": "error",
                "error": "missing_platform_or_conversation_key",
                "platform": normalized_platform,
                "conversation_key": normalized_key,
            }

        event_type = (
            "conversation_messages_cleared"
            if mutation_type == "clear_messages"
            else "conversation_deleted"
        )
        effective_at = _utc_now()
        path = self._db_path or resolved_snapshot_db_path()
        conn = open_db(path)
        try:
            self._ensure_schema(conn)
            cur = conn.execute(
                """
                INSERT INTO conversation_mutations
                (platform, account_id, conversation_key, mutation_type, effective_at, operator, reason)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    normalized_platform,
                    _clean(account_id),
                    normalized_key,
                    mutation_type,
                    _sqlite_time(effective_at),
                    _clean(operator),
                    _clean(reason),
                ),
            )
            mutation_id = int(cur.lastrowid)

            row = conn.execute(
                """
                SELECT id FROM conversations
                WHERE platform = ? AND platform_conversation_id = ?
                LIMIT 1
                """,
                (normalized_platform, normalized_key),
            ).fetchone()
            conversation_id = int(row[0]) if row else 0
            if conversation_id > 0:
                conn.execute("DELETE FROM messages WHERE conversation_id = ?", (conversation_id,))
                if mutation_type == "delete_conversation":
                    conn.execute(
                        """
                        UPDATE conversations
                        SET deleted_at = ?, status = 'deleted', last_message = '',
                            unread_count = 0, updated_at = ?
                        WHERE id = ?
                        """,
                        (_sqlite_time(effective_at), _sqlite_time(effective_at), conversation_id),
                    )
                else:
                    conn.execute(
                        """
                        UPDATE conversations
                        SET last_message = '', unread_count = 0,
                            last_time = ?, updated_at = ?, deleted_at = NULL
                        WHERE id = ?
                        """,
                        (_sqlite_time(effective_at), _sqlite_time(effective_at), conversation_id),
                    )

            event = {
                "event_id": f"{event_type}:{normalized_platform}:{normalized_key}:{mutation_id}",
                "event_type": event_type,
                "platform": normalized_platform,
                "account_id": _clean(account_id),
                "conversation_key": normalized_key,
                "occurred_at": effective_at,
                "payload": {
                    "mutation_id": mutation_id,
                    "mutation_type": mutation_type,
                    "effective_at": effective_at,
                    "operator": _clean(operator),
                    "reason": _clean(reason),
                    "reopen_policy": "new_message_only",
                },
            }
            self._append_event_log(conn, event)
            conn.commit()
        finally:
            conn.close()

        return {
            "status": "success",
            "platform": normalized_platform,
            "conversation_key": normalized_key,
            "mutation_type": mutation_type,
            "mutation_id": mutation_id,
            "event": event,
        }

    def _latest_mutation(
        self,
        conn: sqlite3.Connection,
        platform: str,
        conversation_key: str,
    ) -> dict[str, Any] | None:
        row = conn.execute(
            """
            SELECT id, mutation_type, effective_at
            FROM conversation_mutations
            WHERE platform = ? AND conversation_key = ?
            ORDER BY id DESC
            LIMIT 1
            """,
            (platform, conversation_key),
        ).fetchone()
        if not row:
            return None
        return {
            "id": int(row[0]),
            "mutation_type": _clean(row[1]),
            "effective_at": _clean(row[2]),
        }

    def _should_accept_observed_event(self, conn: sqlite3.Connection, event: dict[str, Any]) -> bool:
        event_type = _clean(event.get("event_type"))
        if event_type not in {"conversation_observed", "message_observed"}:
            return True
        platform = _clean(event.get("platform")).lower()
        conversation_key = _clean(event.get("conversation_key"))
        if not platform or not conversation_key:
            return True
        mutation = self._latest_mutation(conn, platform, conversation_key)
        if not mutation:
            return True

        mutation_time = _parse_time(mutation.get("effective_at"))
        event_time = _parse_time(event.get("occurred_at"))
        if mutation_time is None or event_time is None:
            return False
        if event_time <= mutation_time:
            return False

        mutation_type = mutation.get("mutation_type")
        if mutation_type == "clear_messages":
            return True
        if mutation_type == "delete_conversation":
            if event_type != "message_observed":
                return False
            payload = event.get("payload")
            if not isinstance(payload, dict):
                return False
            return _direction(payload.get("direction"), payload.get("sender_role")) == "in"
        return True

    def _upsert_conversation(self, conn: sqlite3.Connection, event: dict[str, Any]) -> int:
        payload = event.get("payload") or {}
        platform = _clean(event.get("platform")).lower()
        conversation_key = _clean(event.get("conversation_key"))
        account_id = _clean(event.get("account_id"))
        display_name = _clean(payload.get("display_name") or payload.get("sender_name") or conversation_key)
        observed_at = _sqlite_time(event.get("occurred_at"))
        source_type = _clean(payload.get("source_type")) or "ui_observed"
        confidence = int(payload.get("confidence") or 70)
        content = _clean(payload.get("content"))

        row = conn.execute(
            """
            SELECT id FROM conversations
            WHERE platform = ? AND platform_conversation_id = ?
            LIMIT 1
            """,
            (platform, conversation_key),
        ).fetchone()
        if row:
            conv_id = int(row[0])
            conn.execute(
                """
                UPDATE conversations
                SET account_id = COALESCE(NULLIF(?, ''), account_id),
                    customer_name = COALESCE(NULLIF(?, ''), customer_name),
                    last_message = COALESCE(NULLIF(?, ''), last_message),
                    last_time = COALESCE(NULLIF(?, ''), last_time),
                    status = 'active',
                    deleted_at = NULL,
                    source_type = ?,
                    confidence = ?,
                    cache_scope = 'local_cache',
                    cache_origin = 'python_service_truth',
                    updated_at = COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP)
                WHERE id = ?
                """,
                (
                    account_id,
                    display_name,
                    content,
                    observed_at,
                    source_type,
                    confidence,
                    observed_at,
                    conv_id,
                ),
            )
            return conv_id

        cur = conn.execute(
            """
            INSERT INTO conversations
            (platform, platform_conversation_id, account_id, customer_name,
             last_message, last_time, unread_count, status, source_type, confidence,
             cache_scope, cache_origin, updated_at)
            VALUES (?, ?, ?, ?, ?, NULLIF(?, ''), 0, 'active', ?, ?,
                    'local_cache', 'python_service_truth', COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP))
            """,
            (
                platform,
                conversation_key,
                account_id,
                display_name or conversation_key,
                content,
                observed_at,
                source_type,
                confidence,
                observed_at,
            ),
        )
        return int(cur.lastrowid)

    def _upsert_message(
        self,
        conn: sqlite3.Connection,
        conversation_id: int,
        event: dict[str, Any],
        *,
        sync_status: int = 1,
    ) -> None:
        payload = event.get("payload") or {}
        platform_msg_id = _clean(payload.get("platform_msg_id"))
        client_message_id = _clean(event.get("client_message_id") or payload.get("client_message_id"))
        if not platform_msg_id and not client_message_id:
            logging.warning("skip message_observed without platform/client id event_id=%s", event.get("event_id"))
            return

        direction = _direction(payload.get("direction"), payload.get("sender_role"))
        sender = _sender(direction, payload.get("sender_role"))
        observed_at = _sqlite_time(event.get("occurred_at"))
        content = _clean(payload.get("content"))
        source_type = _clean(payload.get("source_type")) or "ui_observed"
        confidence = int(payload.get("confidence") or 70)
        sender_name = _clean(payload.get("sender_name"))

        existing = None
        if platform_msg_id:
            existing = conn.execute(
                "SELECT id FROM messages WHERE platform_msg_id = ? LIMIT 1",
                (platform_msg_id,),
            ).fetchone()
        if existing is None and client_message_id:
            existing = conn.execute(
                "SELECT id FROM messages WHERE client_message_id = ? LIMIT 1",
                (client_message_id,),
            ).fetchone()

        if existing:
            conn.execute(
                """
                UPDATE messages
                SET conversation_id = ?,
                    direction = ?,
                    content = ?,
                    sender = ?,
                    sender_name = ?,
                    platform_msg_id = COALESCE(NULLIF(?, ''), platform_msg_id),
                    original_timestamp = COALESCE(NULLIF(?, ''), original_timestamp),
                    source_type = ?,
                    confidence = ?,
                    verification_status = ?,
                    content_type = ?,
                    observed_at = COALESCE(NULLIF(?, ''), observed_at),
                    client_message_id = COALESCE(NULLIF(?, ''), client_message_id),
                    cache_scope = 'local_cache',
                    sync_status = CASE
                        WHEN ? = 1 AND sync_status IN (10, 11, 12) THEN sync_status
                        ELSE ?
                    END,
                    cache_origin = 'python_service_truth'
                WHERE id = ?
                """,
                (
                    conversation_id,
                    direction,
                    content,
                    sender,
                    sender_name,
                    platform_msg_id,
                    observed_at,
                    source_type,
                    confidence,
                    _clean(payload.get("verification_status")) or "unverified",
                    _clean(payload.get("content_type")) or "text",
                    observed_at,
                    client_message_id,
                    sync_status,
                    sync_status,
                    int(existing[0]),
                ),
            )
            return

        conn.execute(
            """
            INSERT INTO messages
            (conversation_id, direction, content, sender, sender_name, platform_msg_id,
             sync_status, error_reason, original_timestamp, content_image_path,
             source_type, confidence, verification_status, content_type, observed_at,
             client_message_id, cache_scope, cache_origin, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?, ?, NULLIF(?, ''),
                    ?, 'local_cache', 'python_service_truth', COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP))
            """,
            (
                conversation_id,
                direction,
                content,
                sender,
                sender_name,
                platform_msg_id,
                sync_status,
                observed_at,
                _clean(payload.get("content_image_path") or payload.get("evidence_ref")),
                source_type,
                confidence,
                _clean(payload.get("verification_status")) or "unverified",
                _clean(payload.get("content_type")) or "text",
                observed_at,
                client_message_id,
                observed_at,
            ),
        )

    def _mark_outbound_result(self, conn: sqlite3.Connection, event: dict[str, Any], *, sent: bool) -> None:
        payload = event.get("payload") or {}
        client_message_id = _clean(event.get("client_message_id") or payload.get("client_message_id"))
        if not client_message_id:
            return

        sync_status = 11 if sent else 12
        reason = "" if sent else (_clean(payload.get("error_message")) or _clean(payload.get("status")) or "send_failed")
        conn.execute(
            """
            UPDATE messages
            SET sync_status = ?,
                error_reason = ?,
                cache_scope = 'local_cache',
                cache_origin = 'python_service_truth'
            WHERE client_message_id = ?
            """,
            (sync_status, reason[:500], client_message_id),
        )


__all__ = ["PythonServiceTruthStore"]
