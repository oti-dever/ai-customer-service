from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_DB = PROJECT_ROOT / "database" / "app.db"
SERVICE_DB = PROJECT_ROOT / "database" / "service.db"


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


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Split the legacy database/app.db into service and client cache databases."
    )
    parser.add_argument("--source", type=Path, default=LEGACY_DB)
    parser.add_argument("--service-db", type=Path, default=SERVICE_DB)
    parser.add_argument("--client-cache-db", type=Path, default=default_client_cache_db())
    parser.add_argument(
        "--target",
        choices=("service", "client", "both"),
        default="both",
        help="Which database copy to create.",
    )
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    result: dict[str, object] = {
        "source": str(args.source),
        "overwrite": bool(args.overwrite),
        "outputs": [],
    }
    outputs: list[dict[str, str | bool]] = []
    if args.target in {"service", "both"}:
        outputs.append(copy_db(args.source, args.service_db, overwrite=args.overwrite))
    if args.target in {"client", "both"}:
        outputs.append(copy_db(args.source, args.client_cache_db, overwrite=args.overwrite))
    result["outputs"] = outputs
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if any(item.get("copied") for item in outputs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
