from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_BLACKLIST = [
    "\u516c\u4f17\u53f7",
    "\u670d\u52a1\u53f7",
    "\u670d\u52a1\u901a\u77e5",
    "\u5fae\u4fe1\u5b89\u5168\u4e2d\u5fc3",
    "\u8ba2\u9605\u53f7",
    "\u6587\u4ef6\u4f20\u8f93\u52a9\u624b",
    "\u5fae\u4fe1\u56e2\u961f",
    "\u817e\u8baf\u65b0\u95fb",
    "\u5fae\u4fe1\u8fd0\u52a8",
    "\u5fae\u4fe1\u652f\u4ed8",
    "QQ\u5b89\u5168\u4e2d\u5fc3",
    "\u6298\u53e0\u7684\u7fa4\u804a",
]


@dataclass(frozen=True)
class WechatAutomationConfig:
    process_name: str = "Weixin.exe"
    process_names: list[str] = field(default_factory=lambda: ["Weixin.exe", "WeChat.exe", "weixin.exe", "wechat.exe"])
    window_classes: list[str] = field(
        default_factory=lambda: ["mmui::MainWindow", "Qt51514QWindowIcon", "WeChatMainWndForPC", "ChatWnd"]
    )
    window_title_keywords: list[str] = field(default_factory=lambda: ["\u5fae\u4fe1", "Weixin", "WeChat"])
    session_list_automation_id: str = "session_list"
    unread_markers: list[str] = field(
        default_factory=lambda: ["\u672a\u8bfb", "\u6761\u672a\u8bfb", "\u6761\u65b0\u6d88\u606f", "new message", "new messages"]
    )
    unread_patterns: list[str] = field(
        default_factory=lambda: [
            r"\[\d+\u6761\]",
            r"\d+\u6761\u672a\u8bfb",
            r"\d+\u6761\u65b0\u6d88\u606f",
            r"\b\d+\s*new messages?\b",
        ]
    )
    blacklist: list[str] = field(default_factory=lambda: list(DEFAULT_BLACKLIST))
    max_consecutive_failures: int = 8
    failure_cooldown_seconds: float = 30.0
    debug_save_artifacts: bool = False
    debug_artifact_dir: str = "python/rpa/_debug/wechat"
    debug_save_window_png: bool = True


def load_wechat_config(path: str | Path | None = None) -> WechatAutomationConfig:
    raw_path = path or os.getenv("WECHAT_RPA_CONFIG", "")
    if not raw_path:
        return WechatAutomationConfig()

    config_path = Path(raw_path)
    if not config_path.exists():
        return WechatAutomationConfig()

    with config_path.open("r", encoding="utf-8") as file:
        raw = json.load(file)
    if not isinstance(raw, dict):
        return WechatAutomationConfig()
    values = raw.get("wechat") if isinstance(raw.get("wechat"), dict) else raw
    return _load_dataclass(WechatAutomationConfig, values)


def _load_dataclass(cls: type[Any], values: dict[str, Any]) -> Any:
    defaults = cls()
    accepted: dict[str, Any] = {}
    for field_name in defaults.__dataclass_fields__:
        if field_name in values:
            accepted[field_name] = values[field_name]
    return cls(**accepted)
