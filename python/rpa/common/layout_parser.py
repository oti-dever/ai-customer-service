"""
Layout analysis: classify OCR blocks as left (other) / right (self) / center (system).
Merge adjacent blocks of same side into single messages.
Extract structured info (sender_name, timestamp) for left-side messages.
"""
from __future__ import annotations

from dataclasses import dataclass
import re
from typing import List, Tuple, Optional

# (text, bbox, confidence) from ocr_engine
OCRBlock = Tuple[str, List[List[float]], float]

_SYSTEM_SPLIT_RE = re.compile(
    r"^\d{1,2}:\d{2}$|^\d{4}[年/\-]\d{1,2}[月/\-]\d{1,2}|^今天$|^昨天$|^星期[一二三四五六日天]$|^(昨天|前天|星期[一二三四五六日天])\s*\d{1,2}:\d{2}$"
)
_WECHAT_SYSTEM_TEXT_RE = re.compile(
    r"^(以下是新消息|查看更多消息|对方正在输入|以上是打招呼的内容.*|消息已发出.*|你已添加了.+现在可以开始聊天了)$"
)

# 千牛消息头部：名称 + 时间戳
# 名称格式：
#   - 店铺名:昵称 (如 "oppo平实专卖店:哗哗")
#   - 名字·昵称 (如 "被习惯遗忘的我·小朵")
#   - 纯昵称
# 时间格式：2026-3-24 15:33:39 或 2026/3/24 15:33:39
# 注意：\s 包含各种空白字符（空格、全角空格等）
_QN_HEADER_PATTERN = re.compile(
    r"^([\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+(?:[:：·•][\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+)?)\s+"
    r"(\d{4}[-/]\d{1,2}[-/]\d{1,2}[\s\u3000]+\d{1,2}[:：]\d{2}(?:[:：]\d{2})?)"
    r"(?:[\s\u3000]+tb\d+)?$"  # 可选的 tb 号
)

# 简化版：只匹配时间戳（用于从文本中提取）
# 支持普通空格、全角空格、或无空格（OCR 可能漏掉空格）
_TIMESTAMP_PATTERN = re.compile(
    r"(\d{4}[-/]\d{1,2}[-/]\d{1,2}[\s\u3000]*\d{1,2}[:：]\d{2}(?:[:：]\d{2})?)"
)


@dataclass
class ParsedMessage:
    """Single chat message after layout parsing."""
    content: str
    side: str  # "in" | "out" | "system"
    bbox: List[List[float]]  # union of block bboxes
    sender_name: str = ""  # OCR 识别的发送者名称
    original_timestamp: str = ""  # OCR 识别的原始时间字符串


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


def _is_wechat_system_text(text: str) -> bool:
    s = text.strip()
    return bool(_is_hard_split_system_text(s) or _WECHAT_SYSTEM_TEXT_RE.match(s))


def _extract_system_timestamp_text(text: str) -> str:
    """
    提取中间时间分隔文本，供后续第一条消息复用。

    微信常见形式：
    - "星期二 09:12"
    - "昨天 18:25"
    - "09:12"
    """
    s = text.strip()
    if not s:
        return ""
    return s if re.search(r"\d{1,2}:\d{2}", s) else ""


def parse_chat_layout(
    blocks: List[OCRBlock],
    region_width: float,
    platform: str = "generic",
    left_threshold: float = 0.4,
    right_threshold: float = 0.6,
    merge_y_gap: float = 15,
    debug: bool = False,
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
    pending_system_timestamp = ""

    def _flush_current() -> None:
        nonlocal current, current_side, pending_system_timestamp
        if not current or not current_side:
            return
        merged = _merge_blocks(current, current_side, platform=platform, debug=debug)
        if pending_system_timestamp and not merged.original_timestamp:
            merged.original_timestamp = pending_system_timestamp
            pending_system_timestamp = ""
        messages.append(merged)
        current = []
        current_side = None

    def _classify_side(text: str, bbox: List[List[float]]) -> str:
        lx = _bbox_left(bbox)
        rx = _bbox_right(bbox)
        cx = _bbox_center_x(bbox)
        left_ratio = lx / region_width
        right_ratio = rx / region_width
        center_ratio = cx / region_width

        if platform == "wechat" and _is_wechat_system_text(text):
            return "system"

        # 千牛：己方长气泡会大幅向左延伸，整块 OCR 的左缘可能落在左侧比例区内；
        # 若仍先判 left_ratio，会把「右对齐宽气泡」误判为对方(in)。因此先判右缘贴右为 out。
        if platform == "qianniu":
            if right_ratio > right_threshold:
                return "out"
            if left_ratio < left_threshold:
                return "in"
            if center_ratio >= (left_threshold + right_threshold) / 2.0:
                return "out"
            return "in"

        if left_ratio < left_threshold:
            return "in"
        if right_ratio > right_threshold:
            return "out"

        if platform == "wechat":
            if center_ratio <= 0.48 and lx < region_width * 0.58:
                return "in"
            if center_ratio >= 0.52 and rx > region_width * 0.42:
                return "out"
        return "system"

    def _same_message(prev_block: OCRBlock, next_block: OCRBlock, side: str) -> bool:
        prev_box = prev_block[1]
        next_box = next_block[1]
        top = _bbox_top(next_box)
        last_bottom = _bbox_bottom(prev_box)
        if top - last_bottom > merge_y_gap:
            return False
        if platform != "wechat":
            return True

        max_anchor_drift = max(42.0, region_width * 0.07)
        if side == "in":
            anchor_delta = abs(_bbox_left(next_box) - _bbox_left(prev_box))
        else:
            anchor_delta = abs(_bbox_right(next_box) - _bbox_right(prev_box))
        if anchor_delta <= max_anchor_drift:
            return True

        prev_width = max(1.0, _bbox_right(prev_box) - _bbox_left(prev_box))
        next_width = max(1.0, _bbox_right(next_box) - _bbox_left(next_box))
        narrow_block = min(prev_width, next_width) <= region_width * 0.18
        vertical_gap = top - last_bottom
        return narrow_block and vertical_gap <= max(10.0, merge_y_gap * 0.5)

    qianniu_sides: Optional[list[str]] = None
    if platform == "qianniu" and len(sorted_blocks) >= 2:
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
            # 中间列块并不总是“消息分隔符”（例如混入昵称/装饰文本）。
            # 仅在明显是时间/日期/今天等分隔词时，才强制切断当前消息。
            if _is_hard_split_system_text(text):
                _flush_current()
                pending_system_timestamp = _extract_system_timestamp_text(text)
            continue

        if current and current_side == side:
            if (
                platform == "qianniu"
                and side == "in"
                and _qn_should_flush_before_incoming_block(text, current)
            ):
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


def _extract_header_info(text: str, debug: bool = False) -> Tuple[str, str, str]:
    """
    从文本中提取发送者名称、时间戳和剩余内容。
    
    千牛消息头部格式：
    - "oppo平实专卖店:哗哗 2026-3-24 15:33:39"
    - "升妆旗舰店:土豆03 2026-3-23 09:34:35"
    - "被习惯遗忘的我·小朵 2026-3-6 19:09:35"
    
    Returns: (sender_name, timestamp, remaining_content)
    """
    text = text.strip()
    
    # 尝试完整匹配头部模式
    m = _QN_HEADER_PATTERN.match(text)
    if m:
        sender = m.group(1).strip()
        ts = m.group(2).strip()
        if debug:
            print(f"[layout_parser] 完整匹配成功: sender={sender!r}, ts={ts!r}")
        return sender, ts, ""
    
    # 尝试从文本中提取时间戳
    ts_match = _TIMESTAMP_PATTERN.search(text)
    if ts_match:
        ts = ts_match.group(1).strip()
        before_ts = text[:ts_match.start()].strip()
        after_ts = text[ts_match.end():].strip()
        
        # before_ts 可能是发送者名称
        if before_ts and len(before_ts) <= 30:
            # 移除可能的 tb 号后缀
            after_ts = re.sub(r"^tb\d+\s*", "", after_ts).strip()
            if debug:
                print(f"[layout_parser] 时间戳匹配: sender={before_ts!r}, ts={ts!r}, remaining={after_ts!r}")
            return before_ts, ts, after_ts
    
    if debug:
        print(f"[layout_parser] 未匹配到头部信息: text={text[:50]!r}")
    return "", "", text


def _is_pure_timestamp(text: str) -> bool:
    """检查文本是否是纯时间戳（如 '2026-3-619:12:41'）"""
    text = text.strip()
    return bool(re.match(r"^\d{4}[-/]\d{1,2}[-/]\d{1,2}[\s\u3000]*\d{1,2}[:：]\d{2}(?:[:：]\d{2})?$", text))


_QN_TB_ACCOUNT_LINE = re.compile(r"^tb\d{5,24}$", re.IGNORECASE)


def _qn_is_tb_account_line(text: str) -> bool:
    """千牛买家侧常见一行仅含淘宝账号（如 tb810776366），用于版式切段。"""
    return bool(_QN_TB_ACCOUNT_LINE.match(text.strip()))


def _sanitize_qianniu_incoming_tb_leak(sender_name: str, content: str) -> str:
    """
    OCR 常把头像旁的 tb 号拆成独立块，合并后与 sender_name 重复并混入 content。
    去掉与 sender 相同的独立 tb token 及首尾粘连的重复账号，避免聚合里「正文含会话名」。
    """
    s = (content or "").strip()
    if not s:
        return ""
    sender = (sender_name or "").strip()
    if sender and _qn_is_tb_account_line(sender):
        parts = re.split(r"\s+", s)
        acc = sender.lower()
        parts = [p for p in parts if p.strip().lower() != acc]
        s = " ".join(parts).strip()
        pref = sender + " "
        while s.lower().startswith(pref.lower()):
            s = s[len(pref) :].strip()
        suff = " " + sender
        while s.lower().endswith(suff.lower()):
            s = s[: -len(suff)].strip()
        # OCR 偶发「中文+tb」无空格粘连（如 一条消息tb4947894539）
        esc = re.escape(sender)
        s = re.sub(rf"(?i){esc}$", "", s).strip()
        s = re.sub(rf"(?i)^{esc}", "", s).strip()
        return s.strip()
    # sender 尚未写入时：去掉正文开头的独立 tb 行（与头行重复）
    m = re.match(r"^(tb\d{5,24})\s+(.+)$", s, re.IGNORECASE)
    if m and _qn_is_tb_account_line(m.group(1)):
        return m.group(2).strip()
    if _qn_is_tb_account_line(s):
        return ""
    return s


def _qn_should_flush_before_incoming_block(text: str, current: List[OCRBlock]) -> bool:
    """
    千牛左侧：在合并前根据版式强制切段，使一条气泡对应一条 ParsedMessage。
    - 再次出现独立 tb 行：通常表示新一条买家消息头部。
    - 在已有较完整的一条（>=3 块）后出现的纯时间戳行：常为下一气泡的元信息行（先于 tb 出现）。
    """
    if not current:
        return False
    t = text.strip()
    if _qn_is_tb_account_line(t) and len(current) >= 2:
        return True
    if _is_pure_timestamp(t) and len(current) >= 3:
        return True
    return False


def _qn_trailing_line_inherits_out(
    prev_block: OCRBlock,
    cur_block: OCRBlock,
    region_width: float,
    merge_y_gap: float,
    left_threshold: float,
    right_threshold: float,
) -> bool:
    """
    己方多行浅蓝气泡里，Paddle 常把每一行拆成独立 OCR 块。
    较短的后一行右缘可能达不到 right_threshold，会被误标为 in；
    若紧挨在已判为 out 的行下方且仍在对话区中右带，则视为同一条己方消息的续行。
    """
    if region_width <= 0:
        return False
    _, pbox, _ = prev_block
    _, cbox, _ = cur_block
    vgap = _bbox_top(cbox) - _bbox_bottom(pbox)
    max_line_gap = min(max(merge_y_gap * 1.35, 22.0), 52.0)
    if vgap < -2.0 or vgap > max_line_gap:
        return False

    prx = _bbox_right(pbox) / region_width
    if prx < max(0.50, right_threshold * 0.78):
        return False

    lx = _bbox_left(cbox) / region_width
    rx = _bbox_right(cbox) / region_width
    cx = _bbox_center_x(cbox) / region_width

    if rx >= right_threshold * 0.68:
        return True
    if cx >= left_threshold + 0.10 and lx >= left_threshold * 0.35:
        return True
    return False


def _is_pure_name(text: str) -> bool:
    """检查文本是否是纯名称（如 '被习惯遗忘的我：小朵'），不含时间戳"""
    text = text.strip()
    if not text or len(text) > 40:
        return False
    # 不包含数字序列（时间戳特征）
    if re.search(r"\d{4}[-/]\d", text):
        return False
    # 像是名称格式
    return bool(re.match(r"^[\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+(?:[:：·•][\u4e00-\u9fa5A-Za-z0-9·•\-_（）()]+)?$", text))


def _merge_blocks(
    blocks: List[OCRBlock],
    side: str,
    platform: str = "generic",
    debug: bool = False,
) -> ParsedMessage:
    """
    合并同侧的 OCR 块为一条消息。
    对于左侧消息（in），尝试提取发送者名称和时间戳。
    
    支持两种 OCR 输出格式：
    1. 名称+时间戳在同一个 block：'被习惯遗忘的我·小朵 2026-3-6 19:12:41'
    2. 名称和时间戳分开在两个 block：['被习惯遗忘的我：小朵', '2026-3-619:12:41', ...]
    """
    if not blocks:
        return ParsedMessage(content="", side=side, bbox=[[0, 0], [0, 0], [0, 0], [0, 0]])
    
    all_boxes = [b[1] for b in blocks]
    xs = [p[0] for box in all_boxes for p in box]
    ys = [p[1] for box in all_boxes for p in box]
    bbox = [[min(xs), min(ys)], [max(xs), min(ys)], [max(xs), max(ys)], [min(xs), max(ys)]]
    
    sender_name = ""
    original_timestamp = ""
    content_parts: List[str] = []
    content_start_idx = 0
    
    if debug:
        print(f"[layout_parser] _merge_blocks: side={side}, blocks_count={len(blocks)}")
        for i, b in enumerate(blocks):
            print(f"  block[{i}]: {b[0][:60]!r}")
    
    if platform == "wechat":
        content_parts = [b[0] for b in blocks if b[0].strip()]
    elif side == "in" and len(blocks) >= 1:
        first_text = blocks[0][0].strip()
        content_start_idx = 0

        if platform == "qianniu" and len(blocks) >= 2:
            second_text = blocks[1][0].strip()
            if _qn_is_tb_account_line(first_text) and _is_pure_timestamp(second_text):
                sender_name = first_text
                original_timestamp = second_text
                content_start_idx = 2
            elif _is_pure_timestamp(first_text) and _qn_is_tb_account_line(second_text):
                sender_name = second_text
                original_timestamp = first_text
                content_start_idx = 2

        if content_start_idx == 0:
            # 尝试方式1：第一个 block 包含名称+时间戳
            sender, ts, remaining = _extract_header_info(first_text, debug=debug)

            if sender or ts:
                sender_name = sender
                original_timestamp = ts
                if remaining:
                    content_parts.append(remaining)
                content_start_idx = 1
            elif len(blocks) >= 2:
                # 尝试方式2：名称和时间戳分开在两个 block
                second_text = blocks[1][0].strip()

                if _is_pure_name(first_text) and _is_pure_timestamp(second_text):
                    sender_name = first_text
                    original_timestamp = second_text
                    content_start_idx = 2
                    if debug:
                        print(
                            f"[layout_parser] 分离模式匹配: sender={sender_name!r}, ts={original_timestamp!r}"
                        )

        # 剩余 blocks 是消息内容
        for b in blocks[content_start_idx:]:
            content_parts.append(b[0])
    else:
        # 右侧消息或其他，直接合并
        content_parts = [b[0] for b in blocks]
    
    content = " ".join(content_parts).replace("  ", " ").strip()
    if platform == "qianniu" and side == "in":
        content = _sanitize_qianniu_incoming_tb_leak(sender_name, content)

    return ParsedMessage(
        content=content,
        side=side,
        bbox=bbox,
        sender_name=sender_name,
        original_timestamp=original_timestamp,
    )
