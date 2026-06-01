"""
平台适配器基类。

定义平台适配器的统一接口，与 C++ 侧 IPlatformAdapter 对齐。
MVP 阶段优先实现 start/stop/health_check/fetch_visible_* 和 prepare_reply_draft；
真实发送能力（send_message）作为非 MVP 扩展，需要确认凭证和二次校验。
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any


class PlatformAdapter(ABC):
    """
    平台适配器抽象基类。
    
    与架构文档插件协议的映射：
    - connect() -> start()
    - disconnect() -> stop()
    - sync() -> fetch_visible_conversations() + fetch_visible_messages()
    - send_message() -> MVP 阶段优先 prepare_reply_draft()
    - fetch_history() -> 后续扩展
    - get_health() -> health_check()
    """

    @abstractmethod
    def start(self) -> None:
        """启动平台 reader / writer，检查窗口或浏览器上下文。"""
        ...

    @abstractmethod
    def stop(self) -> None:
        """停止采集和自动化任务，释放窗口锁。"""
        ...

    @abstractmethod
    def health_check(self) -> dict[str, Any]:
        """
        返回平台健康状态。
        
        Returns:
            包含以下字段的字典：
            - status: "healthy" | "degraded" | "error"
            - window_found: bool
            - login_state: "logged_in" | "logged_out" | "unknown"
            - ocr_available: bool
            - last_error: str | None
            - metadata: dict
        """
        ...

    @abstractmethod
    def fetch_visible_conversations(self) -> list[dict[str, Any]]:
        """
        获取当前可见的会话列表。
        
        Returns:
            会话列表，每个会话包含：
            - conversation_key: str
            - display_name: str
            - unread_count: int
            - last_message_at: str | None
            - metadata: dict
        """
        ...

    @abstractmethod
    def fetch_visible_messages(self, conv_id: str) -> list[dict[str, Any]]:
        """
        获取指定会话的可见消息。
        
        Args:
            conv_id: 会话标识符
            
        Returns:
            消息列表，每条消息包含：
            - platform_msg_id: str
            - direction: "in" | "out" | "system"
            - content: str
            - timestamp: str
            - sender_name: str | None
            - source_type: str
            - metadata: dict
        """
        ...

    @abstractmethod
    def prepare_reply_draft(self, conv_id: str, text: str) -> dict[str, Any]:
        """
        准备回复草稿（回填到平台输入框）。
        
        MVP 阶段默认只做草稿回填，最终发送由客服在 C++ 工作台确认。
        
        Args:
            conv_id: 目标会话标识符
            text: 待回填的文本
            
        Returns:
            包含以下字段的字典：
            - task_id: str
            - status: "success" | "failed"
            - error_message: str | None
            - evidence: dict | None  # 截图、输入框状态等
        """
        ...

    def send_message(
        self, conv_id: str, text: str, *, confirm_token: str
    ) -> dict[str, Any]:
        """
        真实发送消息（非 MVP 默认能力）。
        
        只有在人工确认和风控边界完整后再启用。
        需要校验窗口标题、账号、会话对象、输入框内容，避免焦点劫持和误发。
        
        Args:
            conv_id: 目标会话标识符
            text: 待发送的文本
            confirm_token: 确认凭证（来自 C++ 侧人工确认）
            
        Returns:
            包含以下字段的字典：
            - task_id: str
            - status: "sent" | "failed"
            - platform_msg_id: str | None
            - error_message: str | None
        """
        raise NotImplementedError(
            "send_message 是非 MVP 默认能力，需要确认凭证和二次校验后再启用"
        )
