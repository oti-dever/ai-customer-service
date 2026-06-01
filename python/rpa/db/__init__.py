"""RPA 数据访问推荐入口。"""
from __future__ import annotations

from .connection import open_db, resolved_default_db_path
from .inbox_dao import (
    count_pending_send,
    fetch_pending_send,
    insert_send_event,
    mark_sent_failed,
    mark_sent_ok,
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
