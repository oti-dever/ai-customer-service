from __future__ import annotations

import argparse
import json
import os
import shutil
import sqlite3
import sys
from pathlib import Path
from typing import Any

from .app_database import ensure_app_database_schema


PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_DB = PROJECT_ROOT / "database" / "app.db"
SERVICE_DB = PROJECT_ROOT / "database" / "service.db"
APP_DATA_DB = PROJECT_ROOT / "database" / "app_data.db"

BUSINESS_FACT_TABLES = [
    "conversations",
    "messages",
    "wechat_conversations",
    "qianniu_conversations",
    "wechat_messages",
    "qianniu_messages",
    "rpa_events",
    "conversation_mutations",
]

DELETE_ORDER = [
    "wechat_messages",
    "qianniu_messages",
    "messages",
    "wechat_conversations",
    "qianniu_conversations",
    "conversations",
    "rpa_events",
    "conversation_mutations",
]


def default_client_cache_db() -> Path:
    if os.name == "nt":
        base = os.environ.get("APPDATA", "").strip()
        if base:
            return Path(base) / "YangYangAI" / "CustomerServiceDemo" / "client_cache.db"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "YangYangAI" / "CustomerServiceDemo" / "client_cache.db"
    base = Path(os.environ.get("XDG_DATA_HOME", "").strip() or Path.home() / ".local" / "share")
    return base / "YangYangAI" / "CustomerServiceDemo" / "client_cache.db"


def copy_db(source: Path, target: Path, *, overwrite: bool) -> dict[str, str | bool]:
    if not source.is_file():
        return {"target": str(target), "copied": False, "reason": "source_missing"}
    if target.exists() and not overwrite:
        return {"target": str(target), "copied": False, "reason": "target_exists"}

    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    copied_sidecars: list[str] = []
    for suffix in ("-wal", "-shm", "-journal"):
        source_sidecar = Path(str(source) + suffix)
        target_sidecar = Path(str(target) + suffix)
        if source_sidecar.is_file():
            if target_sidecar.exists() and not overwrite:
                continue
            shutil.copy2(source_sidecar, target_sidecar)
            copied_sidecars.append(suffix)

    return {
        "target": str(target),
        "copied": True,
        "source": str(source),
        "sidecars": ",".join(copied_sidecars),
    }


def _table_exists(conn: sqlite3.Connection, table_name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ? LIMIT 1",
        (table_name,),
    ).fetchone()
    return row is not None


def _table_columns(conn: sqlite3.Connection, table_name: str) -> list[str]:
    return [str(row[1]) for row in conn.execute(f"PRAGMA table_info({table_name})").fetchall()]


def _table_count(conn: sqlite3.Connection, table_name: str) -> int:
    if not _table_exists(conn, table_name):
        return 0
    row = conn.execute(f"SELECT COUNT(*) FROM {table_name}").fetchone()
    return int(row[0] or 0) if row else 0


def _copy_table_rows(
    source_conn: sqlite3.Connection,
    target_conn: sqlite3.Connection,
    table_name: str,
) -> int:
    if not _table_exists(source_conn, table_name) or not _table_exists(target_conn, table_name):
        return 0

    source_columns = set(_table_columns(source_conn, table_name))
    target_columns = _table_columns(target_conn, table_name)
    columns = [column for column in target_columns if column in source_columns]
    if not columns:
        return 0

    quoted_columns = ", ".join([f'"{column}"' for column in columns])
    placeholders = ", ".join(["?" for _ in columns])
    rows = source_conn.execute(
        f"SELECT {quoted_columns} FROM {table_name} ORDER BY rowid"
    ).fetchall()
    if not rows:
        return 0

    target_conn.executemany(
        f"INSERT INTO {table_name} ({quoted_columns}) VALUES ({placeholders})",
        rows,
    )
    return len(rows)


def migrate_service_db_to_app_data(
    source: Path,
    target: Path,
    *,
    overwrite: bool,
    dry_run: bool = False,
) -> dict[str, Any]:
    if not source.is_file():
        return {
            "target": str(target),
            "migrated": False,
            "reason": "source_missing",
            "source": str(source),
        }

    if not dry_run:
        ensure_app_database_schema(target)

    source_conn = sqlite3.connect(str(source))
    target_conn = sqlite3.connect(str(target)) if target.exists() or not dry_run else None
    try:
        source_counts = {
            table: _table_count(source_conn, table)
            for table in BUSINESS_FACT_TABLES
        }
        target_counts = {}
        if target_conn is not None:
            target_counts = {
                table: _table_count(target_conn, table)
                for table in BUSINESS_FACT_TABLES
            }
        existing_target_rows = sum(target_counts.values())
        if existing_target_rows > 0 and not overwrite:
            return {
                "target": str(target),
                "migrated": False,
                "reason": "target_business_data_exists",
                "source": str(source),
                "source_counts": source_counts,
                "target_counts": target_counts,
            }

        if dry_run:
            return {
                "target": str(target),
                "migrated": False,
                "dry_run": True,
                "source": str(source),
                "source_counts": source_counts,
                "target_counts": target_counts,
            }

        assert target_conn is not None
        target_conn.execute("PRAGMA foreign_keys = OFF;")
        if overwrite:
            for table in DELETE_ORDER:
                if _table_exists(target_conn, table):
                    target_conn.execute(f"DELETE FROM {table}")

        inserted_counts: dict[str, int] = {}
        for table in BUSINESS_FACT_TABLES:
            inserted_counts[table] = _copy_table_rows(source_conn, target_conn, table)

        target_conn.commit()
        return {
            "target": str(target),
            "migrated": True,
            "source": str(source),
            "overwrite": overwrite,
            "inserted_counts": inserted_counts,
        }
    except Exception:
        if target_conn is not None:
            target_conn.rollback()
        raise
    finally:
        source_conn.close()
        if target_conn is not None:
            target_conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Migrate legacy databases used by the customer service app."
    )
    parser.add_argument("--source", type=Path, default=LEGACY_DB)
    parser.add_argument("--service-db", type=Path, default=SERVICE_DB)
    parser.add_argument("--app-data-db", type=Path, default=APP_DATA_DB)
    parser.add_argument("--client-cache-db", type=Path, default=default_client_cache_db())
    parser.add_argument(
        "--target",
        choices=("service", "client", "both", "app-data"),
        default="both",
        help="Which migration target to create.",
    )
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Inspect source and target counts without writing app-data.",
    )
    args = parser.parse_args()

    result: dict[str, object] = {
        "source": str(args.source),
        "overwrite": bool(args.overwrite),
        "outputs": [],
    }
    outputs: list[dict[str, str | bool]] = []
    if args.target == "app-data":
        source = args.service_db if args.source == LEGACY_DB else args.source
        output = migrate_service_db_to_app_data(
            source,
            args.app_data_db,
            overwrite=args.overwrite,
            dry_run=args.dry_run,
        )
        result["source"] = str(source)
        result["outputs"] = [output]
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if output.get("migrated") or output.get("dry_run") else 1

    if args.target in {"service", "both"}:
        outputs.append(copy_db(args.source, args.service_db, overwrite=args.overwrite))
    if args.target in {"client", "both"}:
        outputs.append(copy_db(args.source, args.client_cache_db, overwrite=args.overwrite))
    result["outputs"] = outputs
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if any(item.get("copied") for item in outputs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
