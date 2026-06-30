from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class QQConfig:
    process_name: str = "QQ.exe"
    max_tree_depth: int = 12
    max_tree_nodes: int = 5000
    min_window_area: int = 160_000
    media_capture_evidence: bool = True
    media_artifact_dir: str = "python/rpa/_media/qq"
    window_class_hints: list[str] = field(
        default_factory=lambda: [
            "TXGuiFoundation",
            "Chrome_WidgetWin_1",
            "Qt5152QWindowIcon",
            "QQ",
        ]
    )
    title_blocklist: list[str] = field(
        default_factory=lambda: [
            "截图",
            "图片查看",
            "设置",
            "安全",
            "登录",
        ]
    )
    generic_text_blocklist: list[str] = field(
        default_factory=lambda: [
            "",
            "QQ",
            "消息",
            "联系人",
            "群聊",
            "搜索",
            "发送",
            "关闭",
            "最小化",
            "最大化",
            "还原",
            "表情",
            "图片",
            "文件",
            "语音",
            "视频",
            "更多",
        ]
    )


@dataclass(frozen=True)
class AppConfig:
    qq: QQConfig = field(default_factory=QQConfig)


def load_config(path: str | Path = "settings.json") -> AppConfig:
    config_path = Path(path)
    if not config_path.exists():
        return AppConfig()

    with config_path.open("r", encoding="utf-8") as file:
        raw = json.load(file)

    return AppConfig(qq=_load_dataclass(QQConfig, raw.get("qq", {})))


def _load_dataclass(cls: type[Any], values: dict[str, Any]) -> Any:
    defaults = cls()
    accepted: dict[str, Any] = {}
    for field_name in defaults.__dataclass_fields__:
        if field_name in values:
            accepted[field_name] = values[field_name]
    return cls(**accepted)
