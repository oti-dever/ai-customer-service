"""Compatibility wrapper for the RPA database API.

The implementation lives in `rpa.db`. Keep this module only for legacy imports.
New code should import from `rpa.db`.
"""
from __future__ import annotations

from ..db import (
    count_pending_send,
    fetch_pending_send,
    insert_send_event,
    mark_sent_failed,
    mark_sent_ok,
    open_db,
    resolved_default_db_path,
    write_inbox_batch,
    write_inbox_message,
)

__all__ = [
    "open_db",
    "resolved_default_db_path",
    "count_pending_send",
    "fetch_pending_send",
    "insert_send_event",
    "mark_sent_failed",
    "mark_sent_ok",
    "write_inbox_batch",
    "write_inbox_message",
]
