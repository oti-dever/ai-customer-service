"""千牛平台布局解析配置。"""
from __future__ import annotations

import re
from typing import Any

from ...core.layout_parser import LayoutParserConfig


QIANNIU_HEADER_PATTERN = re.compile(
    r"^([\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+(?:[:：·•][\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+)?)\s+"
    r"(\d{4}[-/]\d{1,2}[-/]\d{1,2}[\s\u3000]+\d{1,2}[:：]\d{2}(?:[:：]\d{2})?)"
    r"(?:[\s\u3000]+tb\d+)?$"
)

QIANNIU_LAYOUT_CONFIG = LayoutParserConfig(
    platform="qianniu",
    header_pattern=QIANNIU_HEADER_PATTERN,
    left_threshold=0.4,
    right_threshold=0.6,
    merge_y_gap=40.0,
)


def build_layout_parser_config(layout_cfg: dict[str, Any] | None = None) -> LayoutParserConfig:
    """Build a 千牛 layout parser config with JSON overrides applied."""
    layout_cfg = layout_cfg or {}
    return QIANNIU_LAYOUT_CONFIG.with_overrides(
        left_threshold=float(layout_cfg.get("left_threshold", QIANNIU_LAYOUT_CONFIG.left_threshold)),
        right_threshold=float(layout_cfg.get("right_threshold", QIANNIU_LAYOUT_CONFIG.right_threshold)),
        merge_y_gap=float(layout_cfg.get("merge_y_gap", QIANNIU_LAYOUT_CONFIG.merge_y_gap)),
    )
