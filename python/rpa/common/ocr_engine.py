"""Compatibility wrapper for `rpa.core.ocr_engine`."""
from __future__ import annotations

from ..core import ocr_engine as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
