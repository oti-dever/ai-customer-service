"""
千牛平台适配器。

提供千牛 PC 客户端的消息读取和发送能力。
"""
from .reader import run_reader
from .writer import run_writer

__all__ = ["run_reader", "run_writer"]
