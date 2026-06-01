"""
千牛聊天区：气泡底色 + 几何侧别优先于「必须两行头」的序列解析。

- B：在 OCR 块 bbox 内及左右邻域采样像素，统计白底气泡 vs 浅蓝底气泡，与 layout 比例规则融合。
- A：合并为对方(in)后若仍无 sender_name 但有正文，则 sender_name = 当前买家名，时间留空。
"""
from __future__ import annotations

from typing import List, Optional, Tuple

import numpy as np

from ...core.layout_parser import (
    OCRBlock,
    ParsedMessage,
    _bbox_bottom,
    _bbox_center_x,
    _bbox_center_y,
    _bbox_left,
    _bbox_right,
    _bbox_top,
    _extract_system_timestamp_text,
    _is_hard_split_system_text,
    _is_wechat_system_text,
    _merge_blocks,
    _qn_should_flush_before_incoming_block,
    _qn_trailing_line_inherits_out,
    _sanitize_qianniu_incoming_tb_leak,
)


def _bgra_to_arr(bgra: bytes, w: int, h: int) -> np.ndarray:
    arr = np.frombuffer(bgra, dtype=np.uint8)
    if arr.size < w * h * 4:
        return np.zeros((0, 0, 4), dtype=np.uint8)
    return arr.reshape(h, w, 4)


def _pixel_kind(b: int, g: int, r: int) -> str:
    """粗分：白底 in 气泡、浅蓝 out 气泡、黑字、其它（含聊天灰底）。"""
    if r < 95 and g < 95 and b < 95:
        return "text"
    # 千牛己方浅蓝底：B 明显高于 R，避免浅灰底 (R≈G≈B) 误判为蓝
    if b > 215 and g > 185 and r > 140 and (b - r) >= 12:
        return "out"
    # 对方白底气泡：高亮近白；普通灰底 min 往往 <248，判为 neutral
    if r > 248 and g > 248 and b > 248 and max(r, g, b) - min(r, g, b) < 38:
        return "in"
    return "neutral"


def _sample_block_bubble_votes(
    arr: np.ndarray,
    bbox: List[List[float]],
    w: int,
    h: int,
) -> Tuple[int, int, int]:
    """返回 (in_bg_votes, out_bg_votes, total_samples)。"""
    if arr.size == 0 or w <= 0 or h <= 0:
        return 0, 0, 0
    x0i = int(max(0, min(_bbox_left(bbox), w - 1)))
    x1i = int(max(0, min(_bbox_right(bbox), w - 1)))
    y0i = int(max(0, min(_bbox_top(bbox), h - 1)))
    y1i = int(max(0, min(_bbox_bottom(bbox), h - 1)))
    if x1i <= x0i or y1i <= y0i:
        return 0, 0, 0
    in_v = out_v = 0
    total = 0
    xs_base = [x0i, (x0i + x1i) // 2, x1i]
    # 向左多采：白气泡在文字左侧
    for dx in (2, 8, 16, 24):
        xs_base.append(max(0, x0i - dx))
    # 向右多采：蓝气泡在文字右侧
    for dx in (2, 8, 16):
        xs_base.append(min(w - 1, x1i + dx))
    ys = [y0i, (y0i + y1i) // 2, y1i]
    seen: set[Tuple[int, int]] = set()
    for y in ys:
        for x in xs_base:
            xi = int(max(0, min(x, w - 1)))
            yi = int(max(0, min(y, h - 1)))
            if (xi, yi) in seen:
                continue
            seen.add((xi, yi))
            b, g, r, _a = (int(arr[yi, xi, j]) for j in range(4))
            k = _pixel_kind(b, g, r)
            total += 1
            if k == "in":
                in_v += 1
            elif k == "out":
                out_v += 1
    return in_v, out_v, max(1, total)


def _classify_side_geometry_qianniu(
    bbox: List[List[float]], region_width: float, left_threshold: float, right_threshold: float
) -> str:
    lx = _bbox_left(bbox)
    rx = _bbox_right(bbox)
    cx = _bbox_center_x(bbox)
    left_ratio = lx / region_width
    right_ratio = rx / region_width
    center_ratio = cx / region_width
    if right_ratio > right_threshold:
        return "out"
    if left_ratio < left_threshold:
        return "in"
    if center_ratio >= (left_threshold + right_threshold) / 2.0:
        return "out"
    return "in"


def _classify_side_bubble_first(
    text: str,
    bbox: List[List[float]],
    arr: np.ndarray,
    w: int,
    h: int,
    region_width: float,
    left_threshold: float,
    right_threshold: float,
) -> str:
    """色带投票优先；样本不足或平局时回退千牛几何规则。"""
    in_v, out_v, tot = _sample_block_bubble_votes(arr, bbox, w, h)
    if tot >= 6 and max(in_v, out_v) >= 3:
        if in_v >= out_v + 2:
            return "in"
        if out_v >= in_v + 2:
            return "out"
    return _classify_side_geometry_qianniu(bbox, region_width, left_threshold, right_threshold)


def parse_qianniu_bubble_first(
    blocks: List[OCRBlock],
    bgra: bytes,
    w: int,
    h: int,
    *,
    default_incoming_name: str,
    left_threshold: float = 0.4,
    right_threshold: float = 0.6,
    merge_y_gap: float = 15.0,
    debug: bool = False,
) -> List[ParsedMessage]:
    """
    与 parse_chat_layout(platform=qianniu) 相同的合并/切段逻辑，
    但侧别由气泡底色 + 几何共同决定；合并后对缺头的 incoming 补 sender（A）。
    """
    if not blocks or w <= 0 or h <= 0 or len(bgra) < w * h * 4:
        return []

    region_width = float(w)
    arr = _bgra_to_arr(bgra, w, h)
    if arr.size == 0:
        return []

    sorted_blocks = sorted(blocks, key=lambda b: _bbox_center_y(b[1]))
    messages: List[ParsedMessage] = []
    current: List[OCRBlock] = []
    current_side: Optional[str] = None
    pending_system_timestamp = ""

    def _flush_current() -> None:
        nonlocal current, current_side, pending_system_timestamp
        if not current or not current_side:
            return
        merged = _merge_blocks(current, current_side, platform="qianniu", debug=debug)
        if pending_system_timestamp and not merged.original_timestamp:
            merged.original_timestamp = pending_system_timestamp
            pending_system_timestamp = ""
        if (
            merged.side == "in"
            and default_incoming_name.strip()
            and not (merged.sender_name or "").strip()
            and merged.content.strip()
        ):
            merged.sender_name = default_incoming_name.strip()
        # 补默认 sender 发生在 _merge_blocks 的 tb 清洗之后；再洗一次避免尾部重复 tb 残留。
        if merged.side == "in":
            merged.content = _sanitize_qianniu_incoming_tb_leak(
                merged.sender_name, merged.content
            )
        messages.append(merged)
        current = []
        current_side = None

    def _classify_side(text: str, bbox: List[List[float]]) -> str:
        if _is_wechat_system_text(text):
            return "system"
        return _classify_side_bubble_first(
            text,
            bbox,
            arr,
            w,
            h,
            region_width,
            left_threshold,
            right_threshold,
        )

    def _same_message(prev_block: OCRBlock, next_block: OCRBlock, side: str) -> bool:
        prev_box = prev_block[1]
        next_box = next_block[1]
        top = _bbox_top(next_box)
        last_bottom = _bbox_bottom(prev_box)
        if top - last_bottom > merge_y_gap:
            return False
        return True

    qianniu_sides: Optional[List[str]] = None
    if len(sorted_blocks) >= 2:
        raw_sides = [_classify_side(b[0], b[1]) for b in sorted_blocks]
        for i in range(1, len(sorted_blocks)):
            if raw_sides[i] != "in" or raw_sides[i - 1] != "out":
                continue
            if _qn_trailing_line_inherits_out(
                sorted_blocks[i - 1],
                sorted_blocks[i],
                region_width,
                merge_y_gap,
                left_threshold,
                right_threshold,
            ):
                raw_sides[i] = "out"
        qianniu_sides = raw_sides

    for idx, block in enumerate(sorted_blocks):
        text, bbox, _ = block
        if qianniu_sides is not None:
            side = qianniu_sides[idx]
        else:
            side = _classify_side(text, bbox)

        if side == "system":
            if _is_hard_split_system_text(text):
                _flush_current()
                pending_system_timestamp = _extract_system_timestamp_text(text)
            continue

        if current and current_side == side:
            if side == "in" and _qn_should_flush_before_incoming_block(text, current):
                _flush_current()
                current = [block]
                current_side = side
                continue
            if _same_message(current[-1], block, side):
                current.append(block)
                continue
            _flush_current()
        elif current and current_side != side:
            _flush_current()

        current = [block]
        current_side = side

    _flush_current()
    return messages


def bubble_first_has_incoming_body(
    messages: List[ParsedMessage],
    *,
    min_content_chars: int = 1,
) -> bool:
    """是否至少有一条对方消息带可见正文（用于决定是否采用气泡优先结果）。"""
    for m in messages:
        if m.side != "in":
            continue
        body = (m.content or "").replace(" ", "").strip()
        if len(body) >= max(1, min_content_chars):
            return True
    return False
