from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import Iterable, List

from .layout_parser import ParsedMessage
from .ocr_engine import OCRBlock

_TIMESTAMP_RE = re.compile(
    r"^\d{4}[-/]\d{1,2}[-/]\d{1,2}\s*\d{1,2}[:：]\d{2}(?:[:：]\d{2})?$"
)
_TB_BUYER_RE = re.compile(r"^tb\d+$", re.I)
_READ_FLAG_RE = re.compile(r"^(已读|未读)$")


@dataclass
class QianniuChatMessage:
    side: str  # in | out | system
    sender_name: str = ""
    original_timestamp: str = ""
    content_lines: list[str] = field(default_factory=list)
    read_flag: str = ""
    raw_lines: list[str] = field(default_factory=list)

    @property
    def content(self) -> str:
        return "\n".join(x for x in self.content_lines if x).strip()


@dataclass
class QianniuChatParseResult:
    messages: list[QianniuChatMessage]
    success: bool
    reason: str
    matched_headers: int = 0
    total_lines: int = 0
    incoming_count: int = 0
    outgoing_count: int = 0
    normalized_lines: list[str] = field(default_factory=list)


def _bbox_top(box: list[list[float]]) -> float:
    return min(p[1] for p in box)


def normalize_ocr_lines(lines: Iterable[str]) -> list[str]:
    out: list[str] = []
    last_noise = ""
    for raw in lines:
        if raw is None:
            continue
        for piece in str(raw).splitlines():
            s = piece.strip().replace("：", ":")
            if not s:
                continue
            if len(s) == 1 and not s.isalnum() and s not in {"好", "嗯"}:
                continue
            if s == last_noise and len(s) <= 2:
                continue
            out.append(s)
            last_noise = s
    return out


def ocr_blocks_to_lines(blocks: list[OCRBlock]) -> list[str]:
    if not blocks:
        return []
    ordered = sorted(blocks, key=lambda b: _bbox_top(b[1]))
    return normalize_ocr_lines([b[0] for b in ordered])


def is_timestamp_line(line: str) -> bool:
    return bool(_TIMESTAMP_RE.match(line.strip()))


def is_tb_buyer_line(line: str) -> bool:
    return bool(_TB_BUYER_RE.match(line.strip()))


def is_read_flag_line(line: str) -> bool:
    return bool(_READ_FLAG_RE.match(line.strip()))


def is_noise_line(line: str) -> bool:
    s = line.strip()
    if not s:
        return True
    if s in {"已读", "未读"}:
        return False
    if re.match(r"^\d{1,2}:\d{2}$", s):
        return True
    if s in {"今天", "昨天"}:
        return True
    return False


def looks_like_agent_name(line: str) -> bool:
    s = line.strip()
    if not s:
        return False
    if is_timestamp_line(s) or is_tb_buyer_line(s) or is_read_flag_line(s):
        return False
    if re.match(r"^[\d\W_]+$", s):
        return False
    return True


def _flush(messages: list[QianniuChatMessage], current: QianniuChatMessage | None) -> None:
    if current is None:
        return
    if not current.content:
        return
    messages.append(current)


def post_normalize(
    messages: list[QianniuChatMessage],
    *,
    drop_empty_messages: bool = True,
    min_content_chars: int = 1,
) -> list[QianniuChatMessage]:
    out: list[QianniuChatMessage] = []
    for msg in messages:
        content = msg.content.strip()
        if drop_empty_messages and len(content) < min_content_chars:
            continue
        if msg.side not in ("in", "out"):
            continue
        out.append(msg)
    return out


def parse_qianniu_chat_lines(
    lines: list[str],
    *,
    min_incoming_messages_for_success: int = 1,
    drop_empty_messages: bool = True,
    min_content_chars: int = 1,
) -> QianniuChatParseResult:
    rows = normalize_ocr_lines(lines)
    messages: list[QianniuChatMessage] = []
    current: QianniuChatMessage | None = None
    state = "idle"
    matched_headers = 0
    i = 0

    while i < len(rows):
        line = rows[i]
        next_line = rows[i + 1] if i + 1 < len(rows) else ""

        if state == "idle":
            if is_tb_buyer_line(line) and is_timestamp_line(next_line):
                current = QianniuChatMessage(
                    side="in",
                    sender_name=line,
                    original_timestamp=next_line,
                    raw_lines=[line, next_line],
                )
                matched_headers += 1
                state = "reading_incoming"
                i += 2
                continue
            if is_timestamp_line(line) and looks_like_agent_name(next_line):
                current = QianniuChatMessage(
                    side="out",
                    sender_name=next_line,
                    original_timestamp=line,
                    raw_lines=[line, next_line],
                )
                matched_headers += 1
                state = "reading_outgoing"
                i += 2
                continue
            i += 1
            continue

        if state == "reading_incoming":
            if is_tb_buyer_line(line) and is_timestamp_line(next_line):
                _flush(messages, current)
                current = None
                state = "idle"
                continue
            if is_timestamp_line(line) and looks_like_agent_name(next_line):
                _flush(messages, current)
                current = None
                state = "idle"
                continue
            if not is_noise_line(line):
                current.content_lines.append(line)
            current.raw_lines.append(line)
            i += 1
            continue

        if state == "reading_outgoing":
            if is_read_flag_line(line):
                current.read_flag = line.strip()
                current.raw_lines.append(line)
                i += 1
                continue
            if is_tb_buyer_line(line) and is_timestamp_line(next_line):
                _flush(messages, current)
                current = None
                state = "idle"
                continue
            if is_timestamp_line(line) and looks_like_agent_name(next_line):
                _flush(messages, current)
                current = None
                state = "idle"
                continue
            if not is_noise_line(line):
                current.content_lines.append(line)
            current.raw_lines.append(line)
            i += 1
            continue

    _flush(messages, current)
    messages = post_normalize(
        messages,
        drop_empty_messages=drop_empty_messages,
        min_content_chars=min_content_chars,
    )
    incoming_count = sum(1 for m in messages if m.side == "in")
    outgoing_count = sum(1 for m in messages if m.side == "out")
    success = incoming_count >= max(1, min_incoming_messages_for_success)
    if success:
        reason = "ok"
    elif not rows:
        reason = "empty_lines"
    elif matched_headers <= 0:
        reason = "no_header_matched"
    elif outgoing_count > 0 and incoming_count <= 0:
        reason = "only_outgoing_matched"
    else:
        reason = "no_valid_incoming"
    return QianniuChatParseResult(
        messages=messages,
        success=success,
        reason=reason,
        matched_headers=matched_headers,
        total_lines=len(rows),
        incoming_count=incoming_count,
        outgoing_count=outgoing_count,
        normalized_lines=list(rows),
    )


def parse_qianniu_chat_blocks(
    blocks: list[OCRBlock],
    *,
    min_incoming_messages_for_success: int = 1,
    drop_empty_messages: bool = True,
    min_content_chars: int = 1,
) -> QianniuChatParseResult:
    return parse_qianniu_chat_lines(
        ocr_blocks_to_lines(blocks),
        min_incoming_messages_for_success=min_incoming_messages_for_success,
        drop_empty_messages=drop_empty_messages,
        min_content_chars=min_content_chars,
    )


def to_parsed_messages(messages: list[QianniuChatMessage]) -> list[ParsedMessage]:
    out: list[ParsedMessage] = []
    for msg in messages:
        out.append(
            ParsedMessage(
                content=msg.content,
                side=msg.side,
                bbox=[[0, 0], [100, 0], [100, 20], [0, 20]],
                sender_name=msg.sender_name,
                original_timestamp=msg.original_timestamp,
            )
        )
    return out
