"""
千牛平台适配器。

提供千牛 PC 客户端的消息读取、草稿回填和受控发送能力。
"""
from .adapter import PLATFORM_QIANNIU, QianniuSidecarAdapter
from .detector import QianniuDetector
from .reader import QianniuReader
from .sender import QianniuSender
from .sessions import QianniuSessionReader

__all__ = [
    "PLATFORM_QIANNIU",
    "QianniuDetector",
    "QianniuReader",
    "QianniuSender",
    "QianniuSessionReader",
    "QianniuSidecarAdapter",
]
