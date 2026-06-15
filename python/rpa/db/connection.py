"""SQLite connection management for Python RPA."""
from __future__ import annotations

import os
import sqlite3
from pathlib import Path

# db/connection.py -> parents[3] = repository root
PROJECT_ROOT = Path(__file__).resolve().parents[3]


def resolved_default_db_path() -> Path:
    raw = (
        os.environ.get("AI_CUSTOMER_SERVICE_APP_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_SERVER_DB")
        or os.environ.get("AI_CUSTOMER_SERVICE_DB")
        or ""
    ).strip()
    if raw:
        return Path(raw).expanduser()
    return PROJECT_ROOT / "database" / "app_data.db"


def open_db(db_path: Path | None = None) -> sqlite3.Connection:
    path = db_path or resolved_default_db_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(path), timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn

__all__ = ["open_db", "resolved_default_db_path"]
