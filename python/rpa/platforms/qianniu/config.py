from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class QianniuConfig:
    process_name: str = "AliWorkbench.exe"
    window_class: str = "Qt5152QWindowIcon"
    window_title_keyword: str = ""
    window_class_hints: list[str] = field(
        default_factory=lambda: [
            "ChatView",
            "Qt5152QWindowIcon",
            "Qt5152QWindowlcon",
            "Qt5152QWindowToolSaveBits",
            "Qt5152QWindowPopupSaveBits",
        ]
    )
    chat_root_automation_id: str = "UIWindow"
    chat_container_automation_ids: list[str] = field(
        default_factory=lambda: [
            "UIWindow.centralwidget",
            "UIWindow.centralwidget.chat_Widget",
            "UIWindow.centralwidget.SubChatView_Widget",
            "UIWindow.centralwidget.SubChatView.ChatDisplayWidget.ChatContentView",
            "UIWindow.centralwidget.SubChatView.ChatExtendView.tabWidget",
        ]
    )
    message_display_suffix: str = "ChatContentView.splitter.msgDisplayWidget"
    message_web_suffix: str = "ChatContentView.splitter.msgDisplayWidget.widget"
    input_suffix: str = "ChatContentView.splitter.sendMsgWidget.chatInputArea.plainTextEdit"
    send_button_suffix: str = "ChatContentView.splitter.sendMsgWidget.enterAreaKeyWidget.sendMsg"
    chat_list_suffix: str = "UIWindow.centralwidget.SubChatView.ChatListWidget"
    chat_list_view_suffix: str = "UIWindow.centralwidget.SubChatView.ChatListWidget.ChatListView.centralwidget"
    chat_list_items_suffix: str = "UIWindow.centralwidget.SubChatView.ChatListWidget.ChatListView.centralwidget.list_widget"
    reception_normal_list_suffix: str = "ChatListView.centralwidget.list_widget.ReceptionListView.stackedWidget.normalList"
    chat_search_input_suffix: str = "UIWindow.centralwidget.SubChatView.ChatListWidget.ChatSearchView.centralwidget.lineEdit"
    max_tree_depth: int = 12
    max_tree_nodes: int = 3000


@dataclass(frozen=True)
class AppConfig:
    qianniu: QianniuConfig = field(default_factory=QianniuConfig)


def load_config(path: str | Path = "settings.json") -> AppConfig:
    config_path = Path(path)
    if not config_path.exists():
        return AppConfig()

    with config_path.open("r", encoding="utf-8") as file:
        raw = json.load(file)

    return AppConfig(
        qianniu=_load_dataclass(QianniuConfig, raw.get("qianniu", {})),
    )


def _load_dataclass(cls: type[Any], values: dict[str, Any]) -> Any:
    defaults = cls()
    accepted: dict[str, Any] = {}
    for field_name in defaults.__dataclass_fields__:
        if field_name in values:
            accepted[field_name] = values[field_name]
    return cls(**accepted)
