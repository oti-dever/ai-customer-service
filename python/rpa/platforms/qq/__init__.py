"""QQ PC automation PoC.

The modules in this package are intentionally probe-oriented. They discover
QQ.exe windows, inspect the UIA tree, read visible text candidates, and fill a
draft without sending it.
"""

from .adapter import QQSidecarAdapter
from .messages import PLATFORM_QQ, QQVisibleMessage, QQVisibleMessageResult, read_visible_messages

__all__ = [
    "PLATFORM_QQ",
    "QQSidecarAdapter",
    "QQVisibleMessage",
    "QQVisibleMessageResult",
    "read_visible_messages",
]
