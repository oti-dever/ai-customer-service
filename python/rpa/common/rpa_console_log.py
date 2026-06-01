"""Compatibility wrapper for `rpa.core.rpa_console_log`."""
from __future__ import annotations

from ..core import rpa_console_log as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
