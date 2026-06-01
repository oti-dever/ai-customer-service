"""Compatibility wrapper for `rpa.core.input_sim`."""
from __future__ import annotations

from ..core import input_sim as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
