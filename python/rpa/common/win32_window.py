"""Compatibility wrapper for `rpa.core.win32_window`."""
from __future__ import annotations

from ..core import win32_window as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
