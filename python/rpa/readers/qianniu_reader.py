"""Compatibility wrapper for the Qianniu reader.

The implementation lives in `rpa.platforms.qianniu.reader`.
"""
from __future__ import annotations

from ..platforms.qianniu import reader as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)


if __name__ == "__main__":
    run_reader()
