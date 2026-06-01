"""Compatibility wrapper for `rpa.core.window_lock`."""
from __future__ import annotations

from ..core import window_lock as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
