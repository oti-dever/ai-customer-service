from __future__ import annotations

from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

from .reader import QQMediaMessage, QQReader, QQStructuredMessage, parse_rect


PLATFORM_QQ = "qq"


@dataclass(frozen=True)
class QQVisibleMessage:
    index: int
    platform: str = PLATFORM_QQ
    platform_msg_id: str = ""
    conversation_title: str = ""
    content_type: str = "text"
    direction: str = ""
    sender: str = ""
    time_text: str = ""
    text: str = ""
    file_name: str = ""
    file_size: str = ""
    content_image_path: str = ""
    evidence_ref: str = ""
    rect: str = ""
    media_rect: str = ""
    confidence: float = 0.0
    raw_metadata: dict[str, Any] = field(default_factory=dict)

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class QQVisibleMessageResult:
    ok: bool
    source: str
    title: str
    messages: list[QQVisibleMessage]
    detail: str = ""
    media_count: int = 0


def read_visible_messages(
    *,
    limit: int = 40,
    include_media: bool = True,
    capture_evidence: bool = False,
    evidence_dir: str | Path | None = None,
    reader: QQReader | None = None,
) -> QQVisibleMessageResult:
    reader = reader or QQReader()
    if include_media:
        snapshot = reader.read_message_snapshot(
            limit=limit,
            include_media=True,
            capture_evidence=capture_evidence,
            evidence_dir=evidence_dir,
        )
        messages = merge_visible_messages(
            snapshot.text_messages,
            snapshot.media_messages,
            conversation_title=snapshot.title,
            limit=limit,
        )
        return QQVisibleMessageResult(
            ok=snapshot.ok,
            source=snapshot.source,
            title=snapshot.title,
            messages=messages,
            detail=snapshot.detail,
            media_count=len(snapshot.media_messages),
        )

    text_result = reader.read_structured_messages(limit=limit)
    title = text_result.title
    messages = merge_visible_messages(
        text_result.messages,
        [],
        conversation_title=title,
        limit=limit,
    )
    return QQVisibleMessageResult(
        ok=text_result.ok,
        source=text_result.source,
        title=title,
        messages=messages,
        detail=text_result.detail,
        media_count=0,
    )


def merge_visible_messages(
    text_messages: list[QQStructuredMessage],
    media_messages: list[QQMediaMessage],
    *,
    conversation_title: str,
    limit: int = 40,
) -> list[QQVisibleMessage]:
    media_rects = [parse_rect(item.rect) for item in media_messages]
    media_rects = [rect for rect in media_rects if rect is not None]
    messages: list[QQVisibleMessage] = []

    for item in text_messages:
        rect = parse_rect(item.rect)
        if rect is not None and any(rects_overlap(rect, media_rect) for media_rect in media_rects):
            continue
        messages.append(_from_text_message(item, conversation_title=conversation_title))

    for item in media_messages:
        messages.append(_from_media_message(item, conversation_title=conversation_title))

    sorted_messages = sorted(messages, key=visible_message_sort_key)
    if limit > 0:
        sorted_messages = sorted_messages[-limit:]
    return [
        QQVisibleMessage(
            index=index,
            platform=item.platform,
            platform_msg_id=item.platform_msg_id,
            conversation_title=item.conversation_title,
            content_type=item.content_type,
            direction=item.direction,
            sender=item.sender,
            time_text=item.time_text,
            text=item.text,
            file_name=item.file_name,
            file_size=item.file_size,
            content_image_path=item.content_image_path,
            evidence_ref=item.evidence_ref,
            rect=item.rect,
            media_rect=item.media_rect,
            confidence=item.confidence,
            raw_metadata=item.raw_metadata,
        )
        for index, item in enumerate(sorted_messages, start=1)
    ]


def visible_message_sort_key(item: QQVisibleMessage) -> tuple[int, int, int, str]:
    rect = parse_rect(item.rect)
    if rect is None:
        return (10**9, 10**9, 10**9, item.text)
    left, top, _right, _bottom = rect
    return (top // 8, top, left, item.text)


def rects_overlap(
    left: tuple[int, int, int, int],
    right: tuple[int, int, int, int],
    *,
    margin: int = 2,
) -> bool:
    return not (
        left[2] < right[0] - margin
        or left[0] > right[2] + margin
        or left[3] < right[1] - margin
        or left[1] > right[3] + margin
    )


def _from_text_message(item: QQStructuredMessage, *, conversation_title: str) -> QQVisibleMessage:
    return QQVisibleMessage(
        index=0,
        conversation_title=conversation_title,
        content_type="text",
        direction=item.direction,
        sender=item.sender,
        time_text=item.time_text,
        text=item.text,
        rect=item.rect,
        confidence=item.confidence,
        raw_metadata={"raw_count": item.raw_count},
    )


def _from_media_message(item: QQMediaMessage, *, conversation_title: str) -> QQVisibleMessage:
    metadata = dict(item.metadata)
    metadata["image_count"] = item.image_count
    return QQVisibleMessage(
        index=0,
        platform_msg_id=item.platform_msg_id,
        conversation_title=conversation_title,
        content_type=item.content_type,
        direction=item.direction,
        text=item.text,
        file_name=item.file_name,
        file_size=item.file_size,
        content_image_path=item.content_image_path,
        evidence_ref=item.evidence_ref,
        rect=item.rect,
        media_rect=item.media_rect,
        confidence=item.confidence,
        raw_metadata=metadata,
    )


def _join_detail(*parts: str) -> str:
    return " | ".join(part for part in parts if part)
