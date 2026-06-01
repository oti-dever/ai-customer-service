"""Helpers for lightweight tool entrypoints."""
from __future__ import annotations

import runpy
import sys
from pathlib import Path


def run_legacy_script(relative_path: str) -> None:
    """Run an existing script while presenting the tool path as argv[0]."""
    rpa_dir = Path(__file__).resolve().parents[1]
    script_path = rpa_dir / relative_path
    sys.argv[0] = str(script_path)
    runpy.run_path(str(script_path), run_name="__main__")
