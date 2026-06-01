"""
微信平台适配器。

提供微信 PC 客户端的消息读取和发送能力。
"""
from .adapter import PLATFORM_WECHAT, WechatSidecarAdapter
from .detector import WechatDetector
from .reader_v2 import WechatVisibleMessageReader
from .sender_v2 import WechatMessageSender
from .uia_scoring import ControlCandidate
from .role_judgement import MessageRoleJudgement

__all__ = [
    "PLATFORM_WECHAT",
    "ControlCandidate",
    "MessageRoleJudgement",
    "WechatDetector",
    "WechatMessageSender",
    "WechatSidecarAdapter",
    "WechatVisibleMessageReader",
]
