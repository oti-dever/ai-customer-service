"""Compatibility wrapper for `rpa.platforms.qianniu.header`."""
from __future__ import annotations

from ..platforms.qianniu import header as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
