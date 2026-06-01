"""Recommended entrypoint for Qianniu conversation list calibration."""
from __future__ import annotations

import sys
from pathlib import Path

PYTHON_DIR = Path(__file__).resolve().parents[3]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.tools._entrypoint import run_legacy_script


if __name__ == "__main__":
    run_legacy_script("calibrate_qianniu_conversation_list.py")
