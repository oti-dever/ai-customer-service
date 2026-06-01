"""Compatibility wrapper for `rpa.platforms.qianniu.coords`."""
from __future__ import annotations

from ..platforms.qianniu import coords as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
