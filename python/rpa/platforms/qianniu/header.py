"""
千牛接待中心：从标题区 OCR 结果中挑选「对端店名/昵称」，过滤系统提示与列表噪声。
"""
from __future__ import annotations

import re
from typing import Any, List, Optional, Tuple

# 常见误识别：会话列表里的占位行、模式提示等（非真实联系人名）
_DEFAULT_EXCLUDE_SUBSTRINGS = (
    "这类消息",
    "手机查看",
    "模拟消息",
    "人工接待",
    "搜索会话",
    "搜索人、群",
    "全部会话",
    "全部买家",
    "全部联系人",
    "待处理",
    "正在接待",
    "其他消息",
    "联系人",
    "响应率",
    "分钟响应",
    "旺旺满意",
    "平均响应",
    "询单转化",
    "(qianniu)",
    "qianniu)",
)


def _normalize_peer_title(raw: str) -> str:
    s = raw.strip()
    for suf in (" (qianniu)", "(qianniu)", " (千牛)", "(千牛)"):
        if s.endswith(suf):
            s = s[: -len(suf)].strip()
            break
    return s


def peer_name_is_noise(name: str, extra_substrings: Optional[List[str]] = None) -> bool:
    """是否为应忽略的标题文本（系统 UI / 列表占位）。"""
    s = _normalize_peer_title(name)
    if not s or len(s) < 2:
        return True
    if len(s) > 40:
        return True
    if re.match(r"^\d{1,2}:\d{2}$", s):
        return True
    if re.match(r"^模式\s*[:：]", s):
        return True
    combined = list(_DEFAULT_EXCLUDE_SUBSTRINGS)
    if extra_substrings:
        combined.extend(str(x) for x in extra_substrings if x)
    for sub in combined:
        if sub and sub in s:
            return True
    return False


def pick_peer_name_from_ocr_blocks(
    blocks: List[Tuple[str, Any, float]],
    extra_substrings: Optional[List[str]] = None,
) -> Optional[str]:
    """
    在标题区多个 OCR 块中，排除噪声后取置信度最高的一条。
    blocks: (text, bbox, confidence)
    """
    candidates: List[Tuple[float, str]] = []
    for item in blocks:
        if len(item) < 3:
            continue
        text, _bbox, conf = item[0], item[1], float(item[2])
        s = _normalize_peer_title(str(text))
        if peer_name_is_noise(s, extra_substrings):
            continue
        candidates.append((conf, s))
    if not candidates:
        return None
    candidates.sort(key=lambda x: x[0], reverse=True)
    return candidates[0][1]
