"""
Layout analysis: classify OCR blocks as left (other) / right (self) / center (system).
Merge adjacent blocks of same side into single messages.
"""
from __future__ import annotations

from dataclasses import dataclass
import re
from typing import List, Tuple

# (text, bbox, confidence) from ocr_engine
OCRBlock = Tuple[str, List[List[float]], float]

_SYSTEM_SPLIT_RE = re.compile(
    r"^\d{1,2}:\d{2}$|^\d{4}[年/\-]\d{1,2}[月/\-]\d{1,2}|^今天$|^昨天$|^星期[一二三四五六日天]$"
)


@dataclass
class ParsedMessage:
    """Single chat message after layout parsing."""
    content: str
    side: str  # "in" | "out" | "system"
    bbox: List[List[float]]  # union of block bboxes


def _bbox_center_x(box: List[List[float]]) -> float:
    xs = [p[0] for p in box]
    return (min(xs) + max(xs)) / 2


def _bbox_center_y(box: List[List[float]]) -> float:
    ys = [p[1] for p in box]
    return (min(ys) + max(ys)) / 2


def _bbox_left(box: List[List[float]]) -> float:
    return min(p[0] for p in box)


def _bbox_right(box: List[List[float]]) -> float:
    return max(p[0] for p in box)


def _bbox_top(box: List[List[float]]) -> float:
    return min(p[1] for p in box)


def _bbox_bottom(box: List[List[float]]) -> float:
    return max(p[1] for p in box)


def _is_hard_split_system_text(text: str) -> bool:
    return bool(_SYSTEM_SPLIT_RE.match(text.strip()))


def parse_chat_layout(
    blocks: List[OCRBlock],
    region_width: float,
    left_threshold: float = 0.4,
    right_threshold: float = 0.6,
    merge_y_gap: float = 15,
) -> List[ParsedMessage]:
    """
    Parse OCR blocks into messages by layout.
    - left (x/width < left_threshold) -> "in" (other)
    - right (x/width > right_threshold) -> "out" (self)
    - center -> "system" (skip)
    Adjacent blocks with same side and close y are merged.
    """
    if not blocks or region_width <= 0:
        return []

    # Sort by y
    sorted_blocks = sorted(blocks, key=lambda b: _bbox_center_y(b[1]))

    messages: List[ParsedMessage] = []
    current: List[OCRBlock] = []
    current_side: str | None = None

    for block in sorted_blocks:
        text, bbox, _ = block
        lx = _bbox_left(bbox)
        rx = _bbox_right(bbox)
        left_ratio = lx / region_width
        right_ratio = rx / region_width

        # 千牛长消息（含 URL）常导致文本框很宽，中心点会落到中间，从而误判 system。
        # 这里改为按边界判定：左边界足够靠左 => in；右边界足够靠右 => out。
        if left_ratio < left_threshold:
            side = "in"
        elif right_ratio > right_threshold:
            side = "out"
        else:
            side = "system"

        if side == "system":
            # 中间列块并不总是“消息分隔符”（例如混入昵称/装饰文本）。
            # 仅在明显是时间/日期/今天等分隔词时，才强制切断当前消息。
            if current and current_side and _is_hard_split_system_text(text):
                messages.append(_merge_blocks(current, current_side))
                current = []
                current_side = None
            continue

        top = _bbox_top(bbox)
        if current and current_side == side:
            last_bottom = _bbox_bottom(current[-1][1])
            if top - last_bottom <= merge_y_gap:
                current.append(block)
                continue
            else:
                messages.append(_merge_blocks(current, current_side))
                current = []

        current = [block]
        current_side = side

    if current:
        messages.append(_merge_blocks(current, current_side))

    return messages


def _merge_blocks(blocks: List[OCRBlock], side: str) -> ParsedMessage:
    contents = [b[0] for b in blocks]
    content = " ".join(contents).replace("  ", " ").strip()
    all_boxes = [b[1] for b in blocks]
    xs = [p[0] for box in all_boxes for p in box]
    ys = [p[1] for box in all_boxes for p in box]
    bbox = [[min(xs), min(ys)], [max(xs), min(ys)], [max(xs), max(ys)], [min(xs), max(ys)]]
    return ParsedMessage(content=content, side=side, bbox=bbox)
