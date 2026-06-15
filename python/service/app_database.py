from __future__ import annotations

import logging
from pathlib import Path

from rpa.db.connection import open_db

from .cache_snapshot import resolved_snapshot_db_path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
APP_DATA_SCHEMA_PATH = PROJECT_ROOT / "database" / "app_data_schema.sql"


def ensure_app_database_schema(db_path: Path | None = None) -> Path:
    path = db_path or resolved_snapshot_db_path()
    if not APP_DATA_SCHEMA_PATH.exists():
        logging.warning("app data schema file missing: %s", APP_DATA_SCHEMA_PATH)
        return path

    conn = open_db(path)
    try:
        conn.executescript(APP_DATA_SCHEMA_PATH.read_text(encoding="utf-8"))
        conn.commit()
    finally:
        conn.close()
    return path


__all__ = ["ensure_app_database_schema", "APP_DATA_SCHEMA_PATH"]
