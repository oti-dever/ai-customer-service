"""SQLite connection management for Python RPA."""
from __future__ import annotations

import os
import sqlite3
import sys
from pathlib import Path
from typing import Optional

# db/connection.py -> parents[3] = repository root
PROJECT_ROOT = Path(__file__).resolve().parents[3]

# Keep in sync with src/utils/appsettings.h configureApplication.
_QT_ORG = "YangYangAI"
_QT_APP = "CustomerServiceDemo"
_QT_DB_NAME = "app.db"


def _qt_appdata_db_candidate() -> Optional[Path]:
    """Return the Qt AppData database candidate if the platform can provide one."""
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

__all__ = ["open_db", "resolved_default_db_path"]
