"""Compatibility wrapper for the Qianniu writer.

The implementation lives in `rpa.platforms.qianniu.writer`.
"""
from __future__ import annotations

from ..platforms.qianniu import writer as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)


if __name__ == "__main__":
    run_writer()
