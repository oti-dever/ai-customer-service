from __future__ import annotations

import re
import hashlib
import time
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any

from .config import AppConfig, load_config
from .detector import QQDetector
from .qq_logging import get_logger
from .uia import child_count, safe_prop, safe_rect_tuple, trim, uia_guard, walk_controls


logger = get_logger(__name__)


@dataclass(frozen=True)
class QQVisibleText:
    text: str
    depth: int
    source: str
    control_type: str
    class_name: str
    automation_id: str
    rect: str


@dataclass(frozen=True)
class QQMessageProbeResult:
    ok: bool
    source: str
    title: str
    texts: list[QQVisibleText]
    detail: str = ""


@dataclass(frozen=True)
class QQStructuredMessage:
    direction: str
    sender: str
    time_text: str
    text: str
    rect: str
    confidence: float
    raw_count: int


@dataclass(frozen=True)
class QQStructuredMessageResult:
    ok: bool
    source: str
    title: str
    messages: list[QQStructuredMessage]
    detail: str = ""


@dataclass(frozen=True)
class QQConversationItem:
    index: int
    title: str
    time_text: str
    preview: str
    rect: str
    unread_hint: str = ""
    is_current_candidate: bool = False


@dataclass(frozen=True)
class QQConversationListResult:
    ok: bool
    source: str
    current_title: str
    conversations: list[QQConversationItem]
    detail: str = ""


@dataclass(frozen=True)
class QQMediaLayoutItem:
    index: int
    score: int
    reason: str
    depth: int
    path: str
    control_type: str
    class_name: str
    automation_id: str
    name: str
    rect: str
    child_count: int
    patterns: str
    parent_type: str
    parent_name: str
    parent_rect: str
    nearby_text: str


@dataclass(frozen=True)
class QQMediaLayoutResult:
    ok: bool
    source: str
    title: str
    items: list[QQMediaLayoutItem]
    detail: str = ""


@dataclass(frozen=True)
class QQMediaMessage:
    index: int
    platform_msg_id: str
    content_type: str
    direction: str
    file_name: str
    file_size: str
    text: str
    rect: str
    media_rect: str
    image_count: int
    confidence: float
    metadata: dict[str, Any]
    content_image_path: str = ""
    evidence_ref: str = ""


@dataclass(frozen=True)
class QQMediaMessageResult:
    ok: bool
    source: str
    title: str
    messages: list[QQMediaMessage]
    detail: str = ""


@dataclass(frozen=True)
class QQMessageSnapshotResult:
    ok: bool
    source: str
    title: str
    text_messages: list[QQStructuredMessage]
    media_messages: list[QQMediaMessage]
    detail: str = ""


class QQReader:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QQDetector(self.config)
        self._cached_conversation_root: Any | None = None
        self._cached_conversation_root_hwnd = 0
        self._cached_message_area_root: Any | None = None
        self._cached_message_area_hwnd = 0
        self._cached_message_area_source = ""

    def read_visible_texts(self, limit: int = 80, chat_root: Any | None = None) -> QQMessageProbeResult:
        limit = max(1, min(int(limit or 80), 300))
        started_at = time.perf_counter()
        with uia_guard("qq_read_visible_texts"):
            window_hwnd = 0
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return QQMessageProbeResult(
                        ok=False,
                        source="find_chat",
                        title="",
                        texts=[],
                        detail="qq_window_not_found",
                    )
                chat_root = handle.chat_root
                window_hwnd = handle.window.window.hwnd

            title = self._resolve_title(chat_root)
            scan_root, source, message_area_cache_hit = self._message_area_for(chat_root, window_hwnd)
            scan_depth = max(12, self.config.qq.max_tree_depth)
            texts = collect_visible_texts(scan_root, max_depth=scan_depth, max_nodes=self.config.qq.max_tree_nodes)
            texts = texts[-limit:]
            logger.info(
                "qq read_visible_texts ok=True source=%s title=%s text_count=%s elapsed_ms=%.1f message_area_cache_hit=%s",
                source,
                title,
                len(texts),
                (time.perf_counter() - started_at) * 1000.0,
                message_area_cache_hit,
            )
            return QQMessageProbeResult(ok=True, source=source, title=title, texts=texts)

    def read_structured_messages(self, limit: int = 40, chat_root: Any | None = None) -> QQStructuredMessageResult:
        limit = max(1, min(int(limit or 40), 120))
        started_at = time.perf_counter()
        with uia_guard("qq_read_structured_messages"):
            window_hwnd = 0
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return QQStructuredMessageResult(
                        ok=False,
                        source="find_chat",
                        title="",
                        messages=[],
                        detail="qq_window_not_found",
                    )
                chat_root = handle.chat_root
                window_hwnd = handle.window.window.hwnd

            title = self._resolve_title(chat_root)
            scan_root, source, message_area_cache_hit = self._message_area_for(chat_root, window_hwnd)
            message_rect = safe_rect_tuple(scan_root)
            scan_depth = max(12, self.config.qq.max_tree_depth)
            texts = collect_visible_texts(scan_root, max_depth=scan_depth, max_nodes=self.config.qq.max_tree_nodes)
            messages = structure_visible_messages(texts, message_rect)
            messages = messages[-limit:]
            logger.info(
                "qq read_structured_messages ok=True source=%s title=%s message_count=%s elapsed_ms=%.1f message_area_cache_hit=%s",
                source,
                title,
                len(messages),
                (time.perf_counter() - started_at) * 1000.0,
                message_area_cache_hit,
            )
            return QQStructuredMessageResult(ok=True, source=source, title=title, messages=messages)

    def read_message_snapshot(
        self,
        limit: int = 40,
        *,
        include_media: bool = True,
        capture_evidence: bool = False,
        evidence_dir: str | Path | None = None,
    ) -> QQMessageSnapshotResult:
        limit = max(1, min(int(limit or 40), 120))
        started_at = time.perf_counter()
        with uia_guard("qq_read_message_snapshot"):
            handle = self.detector.find_current_chat()
            if not handle:
                return QQMessageSnapshotResult(
                    ok=False,
                    source="find_chat",
                    title="",
                    text_messages=[],
                    media_messages=[],
                    detail="qq_window_not_found",
                )

            chat_root = handle.chat_root
            window_hwnd = handle.window.window.hwnd
            window_rect = handle.window.window.rect
            title = self._resolve_title(chat_root)
            scan_root, text_source, message_area_cache_hit = self._message_area_for(chat_root, window_hwnd)
            message_rect = safe_rect_tuple(scan_root)
            scan_depth = max(12, self.config.qq.max_tree_depth)

            stage_started_at = time.perf_counter()
            texts = collect_visible_texts(scan_root, max_depth=scan_depth, max_nodes=self.config.qq.max_tree_nodes)
            text_messages = structure_visible_messages(texts, message_rect)[-limit:]
            text_ms = (time.perf_counter() - stage_started_at) * 1000.0

            media_messages: list[QQMediaMessage] = []
            media_source = ""
            media_ms = 0.0
            row_count = 0
            candidate_count = 0
            if include_media:
                stage_started_at = time.perf_counter()
                rows = find_message_row_controls(scan_root)
                row_count = len(rows)
                candidate_rows = [
                    row
                    for row in rows
                    if row_has_media_candidate(row) or row_has_media_text_candidate_from_visible(row, texts)
                ]
                candidate_count = len(candidate_rows)
                if candidate_rows:
                    media_messages = collect_media_messages_from_rows(candidate_rows, message_rect=message_rect)
                    media_messages = ensure_media_message_ids(media_messages, conversation_title=title)
                    if capture_evidence:
                        media_messages = attach_media_evidence(
                            media_messages,
                            hwnd=window_hwnd,
                            window_rect=window_rect,
                            root_dir=evidence_dir or self.config.qq.media_artifact_dir,
                        )
                    media_messages = media_messages[-limit:]
                    media_source = text_source
                else:
                    media_source = "skipped:no_media_candidate"
                media_ms = (time.perf_counter() - stage_started_at) * 1000.0

            source = text_source if not include_media else f"{text_source}+{media_source}"
            logger.info(
                "qq read_message_snapshot ok=True source=%s title=%s text_count=%s media_count=%s row_count=%s media_candidate_count=%s elapsed_ms=%.1f text_ms=%.1f media_ms=%.1f include_media=%s evidence=%s message_area_cache_hit=%s",
                source,
                title,
                len(text_messages),
                len(media_messages),
                row_count,
                candidate_count,
                (time.perf_counter() - started_at) * 1000.0,
                text_ms,
                media_ms,
                include_media,
                capture_evidence,
                message_area_cache_hit,
            )
            return QQMessageSnapshotResult(
                ok=True,
                source=source,
                title=title,
                text_messages=text_messages,
                media_messages=media_messages,
            )

    def read_conversations(
        self,
        limit: int = 40,
        chat_root: Any | None = None,
        *,
        resolve_title: bool = True,
    ) -> QQConversationListResult:
        limit = max(1, min(int(limit or 40), 120))
        started_at = time.perf_counter()
        with uia_guard("qq_read_conversations"):
            window_hwnd = 0
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return QQConversationListResult(
                        ok=False,
                        source="find_chat",
                        current_title="",
                        conversations=[],
                        detail="qq_window_not_found",
                    )
                chat_root = handle.chat_root
                window_hwnd = handle.window.window.hwnd

            current_title = self._resolve_title(chat_root) if resolve_title else ""
            conversation_root = self._cached_conversation_root_for(window_hwnd)
            cache_hit = conversation_root is not None
            if conversation_root is None:
                conversation_root = find_conversation_list_root(chat_root)
                if conversation_root is not None:
                    self._cache_conversation_root(window_hwnd, conversation_root)
            if conversation_root is None:
                return QQConversationListResult(
                    ok=False,
                    source="conversation_list",
                    current_title=current_title,
                    conversations=[],
                    detail="conversation_list_not_found",
                )
            conversations = collect_conversation_items(conversation_root, current_title=current_title)
            conversations = conversations[:limit]
            logger.info(
                "qq read_conversations ok=True title=%s conversation_count=%s elapsed_ms=%.1f resolve_title=%s cache_hit=%s",
                current_title,
                len(conversations),
                (time.perf_counter() - started_at) * 1000.0,
                resolve_title,
                cache_hit,
            )
            return QQConversationListResult(
                ok=True,
                source="conversation_list",
                current_title=current_title,
                conversations=conversations,
            )

    def _cached_conversation_root_for(self, window_hwnd: int) -> Any | None:
        cached = self._cached_conversation_root
        if cached is None:
            return None
        if self._cached_conversation_root_hwnd and window_hwnd and self._cached_conversation_root_hwnd != int(window_hwnd):
            self._clear_conversation_root_cache()
            return None
        if not _is_cached_root_usable(cached):
            self._clear_conversation_root_cache()
            return None
        return cached

    def _cache_conversation_root(self, window_hwnd: int, conversation_root: Any) -> None:
        self._cached_conversation_root = conversation_root
        self._cached_conversation_root_hwnd = int(window_hwnd or 0)

    def _clear_conversation_root_cache(self) -> None:
        self._cached_conversation_root = None
        self._cached_conversation_root_hwnd = 0

    def _message_area_for(self, chat_root: Any, window_hwnd: int) -> tuple[Any, str, bool]:
        cached = self._cached_message_area_root_for(window_hwnd)
        if cached is not None:
            return cached, self._cached_message_area_source or "message_area:cached", True

        roots = self.detector.find_message_area_candidates(chat_root)
        if not roots:
            self._clear_message_area_cache()
            return chat_root, "chat_root", False

        source = f"message_area:{roots[0].reason}"
        self._cache_message_area_root(window_hwnd, roots[0].control, source)
        return roots[0].control, source, False

    def _cached_message_area_root_for(self, window_hwnd: int) -> Any | None:
        cached = self._cached_message_area_root
        if cached is None:
            return None
        if self._cached_message_area_hwnd and window_hwnd and self._cached_message_area_hwnd != int(window_hwnd):
            self._clear_message_area_cache()
            return None
        if not _is_cached_root_usable(cached):
            self._clear_message_area_cache()
            return None
        return cached

    def _cache_message_area_root(self, window_hwnd: int, message_area_root: Any, source: str) -> None:
        self._cached_message_area_root = message_area_root
        self._cached_message_area_hwnd = int(window_hwnd or 0)
        self._cached_message_area_source = source

    def _clear_message_area_cache(self) -> None:
        self._cached_message_area_root = None
        self._cached_message_area_hwnd = 0
        self._cached_message_area_source = ""

    def read_media_layout(self, limit: int = 80, chat_root: Any | None = None) -> QQMediaLayoutResult:
        limit = max(1, min(int(limit or 80), 300))
        started_at = time.perf_counter()
        with uia_guard("qq_read_media_layout"):
            window_hwnd = 0
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return QQMediaLayoutResult(
                        ok=False,
                        source="find_chat",
                        title="",
                        items=[],
                        detail="qq_window_not_found",
                    )
                chat_root = handle.chat_root
                window_hwnd = handle.window.window.hwnd

            title = self._resolve_title(chat_root)
            scan_root, source, message_area_cache_hit = self._message_area_for(chat_root, window_hwnd)
            message_rect = safe_rect_tuple(scan_root)
            scan_depth = max(12, self.config.qq.max_tree_depth)
            texts = collect_visible_texts(scan_root, max_depth=scan_depth, max_nodes=self.config.qq.max_tree_nodes)
            items = collect_media_layout_items(
                scan_root,
                message_rect=message_rect,
                visible_texts=texts,
                max_depth=scan_depth,
                max_nodes=self.config.qq.max_tree_nodes,
                limit=limit,
            )
            logger.info(
                "qq read_media_layout ok=True source=%s title=%s item_count=%s elapsed_ms=%.1f message_area_cache_hit=%s",
                source,
                title,
                len(items),
                (time.perf_counter() - started_at) * 1000.0,
                message_area_cache_hit,
            )
            return QQMediaLayoutResult(ok=True, source=source, title=title, items=items)

    def read_media_messages(
        self,
        limit: int = 40,
        chat_root: Any | None = None,
        *,
        capture_evidence: bool = False,
        evidence_dir: str | Path | None = None,
    ) -> QQMediaMessageResult:
        limit = max(1, min(int(limit or 40), 120))
        started_at = time.perf_counter()
        with uia_guard("qq_read_media_messages"):
            window_hwnd = 0
            window_rect: tuple[int, int, int, int] = (0, 0, 0, 0)
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return QQMediaMessageResult(
                        ok=False,
                        source="find_chat",
                        title="",
                        messages=[],
                        detail="qq_window_not_found",
                    )
                chat_root = handle.chat_root
                window_hwnd = handle.window.window.hwnd
                window_rect = handle.window.window.rect
            else:
                handle = self.detector.find_current_chat()
                if handle:
                    window_hwnd = handle.window.window.hwnd
                    window_rect = handle.window.window.rect

            title = self._resolve_title(chat_root)
            scan_root, source, message_area_cache_hit = self._message_area_for(chat_root, window_hwnd)
            message_rect = safe_rect_tuple(scan_root)
            rows = find_message_row_controls(scan_root)
            candidate_rows = [row for row in rows if row_has_media_candidate(row)]
            messages = collect_media_messages_from_rows(candidate_rows, message_rect=message_rect)
            messages = ensure_media_message_ids(messages, conversation_title=title)
            if capture_evidence:
                messages = attach_media_evidence(
                    messages,
                    hwnd=window_hwnd,
                    window_rect=window_rect,
                    root_dir=evidence_dir or self.config.qq.media_artifact_dir,
                )
            messages = messages[-limit:]
            logger.info(
                "qq read_media_messages ok=True source=%s title=%s row_count=%s media_candidate_count=%s media_count=%s elapsed_ms=%.1f message_area_cache_hit=%s",
                source,
                title,
                len(rows),
                len(candidate_rows),
                len(messages),
                (time.perf_counter() - started_at) * 1000.0,
                message_area_cache_hit,
            )
            return QQMediaMessageResult(ok=True, source=source, title=title, messages=messages)

    def _resolve_title(self, chat_root: Any) -> str:
        titles = self.detector.find_title_candidates(chat_root, limit=5)
        return titles[0][1] if titles else ""


def collect_visible_texts(root: Any, max_depth: int, max_nodes: int) -> list[QQVisibleText]:
    found: list[QQVisibleText] = []
    seen: set[str] = set()
    for depth, control in walk_controls(root, max_depth=max_depth, max_nodes=max_nodes):
        for source, value in _control_text_values(control):
            text = normalize_text(value)
            if not text or _should_skip_text(text):
                continue
            rect = _safe_rect(control)
            key = f"{text}\n{safe_prop(control, 'AutomationId')}\n{safe_prop(control, 'ClassName')}\n{rect}"
            if key in seen:
                continue
            seen.add(key)
            found.append(
                QQVisibleText(
                    text=text,
                    depth=depth,
                    source=source,
                    control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
                    class_name=safe_prop(control, "ClassName"),
                    automation_id=safe_prop(control, "AutomationId"),
                    rect=rect,
                )
            )
    return found


def collect_media_layout_items(
    root: Any,
    *,
    message_rect: tuple[int, int, int, int] | None,
    visible_texts: list[QQVisibleText],
    max_depth: int,
    max_nodes: int,
    limit: int,
) -> list[QQMediaLayoutItem]:
    rows = _walk_controls_with_paths(root, max_depth=max_depth, max_nodes=max_nodes)
    candidates: list[tuple[int, int, object, object | None, str, int, str]] = []
    for order, (depth, control, parent, path) in enumerate(rows):
        score, reason = score_media_layout_candidate(control, message_rect)
        if score <= 0:
            continue
        candidates.append((score, order, control, parent, path, depth, reason))

    # Keep visual order for the final probe output; score is still printed for diagnosis.
    candidates = sorted(candidates, key=lambda item: (_control_visual_sort_key(item[2]), -item[0], item[1]))[:limit]
    items: list[QQMediaLayoutItem] = []
    for index, (score, _order, control, parent, path, depth, reason) in enumerate(candidates, start=1):
        rect = _safe_rect(control)
        items.append(
            QQMediaLayoutItem(
                index=index,
                score=score,
                reason=reason,
                depth=depth,
                path=path,
                control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
                class_name=safe_prop(control, "ClassName"),
                automation_id=safe_prop(control, "AutomationId"),
                name=clean_text(safe_prop(control, "Name")),
                rect=rect,
                child_count=child_count(control),
                patterns=",".join(_available_patterns(control)) or "-",
                parent_type=(safe_prop(parent, "ControlTypeName") or safe_prop(parent, "LocalizedControlType")) if parent else "",
                parent_name=clean_text(safe_prop(parent, "Name")) if parent else "",
                parent_rect=_safe_rect(parent) if parent else "-",
                nearby_text=nearby_text_for_rect(rect, visible_texts),
            )
        )
    return items


def find_message_row_controls(message_root: Any) -> list[Any]:
    ml_root = find_ml_root(message_root) or message_root
    containers = _safe_children(ml_root)
    if len(containers) == 1:
        nested = _safe_children(containers[0])
        if nested:
            containers = nested
    rows: list[Any] = []
    for control in containers:
        rect = safe_rect_tuple(control)
        if not _is_visible_rect(rect):
            continue
        width = rect[2] - rect[0] if rect else 0
        height = rect[3] - rect[1] if rect else 0
        aid = safe_prop(control, "AutomationId")
        if re.fullmatch(r"\d{8,}", aid) or (width >= 300 and height >= 36 and child_count(control) >= 1):
            rows.append(control)
    return sorted(rows, key=lambda control: (safe_rect_tuple(control) or (0, 0, 0, 0))[1])


def find_ml_root(root: Any) -> Any | None:
    for _depth, control in walk_controls(root, max_depth=8, max_nodes=1200):
        if safe_prop(control, "AutomationId") == "ml-root":
            return control
    return None


def collect_media_messages_from_rows(
    rows: list[Any],
    *,
    message_rect: tuple[int, int, int, int] | None,
) -> list[QQMediaMessage]:
    messages: list[QQMediaMessage] = []
    for row in rows:
        media = media_message_from_row(row, message_rect=message_rect)
        if media is not None:
            messages.append(media)
    return [
        QQMediaMessage(
            index=index,
            platform_msg_id=item.platform_msg_id,
            content_type=item.content_type,
            direction=item.direction,
            file_name=item.file_name,
            file_size=item.file_size,
            text=item.text,
            rect=item.rect,
            media_rect=item.media_rect,
            image_count=item.image_count,
            confidence=item.confidence,
            metadata=item.metadata,
        )
        for index, item in enumerate(messages, start=1)
    ]


def row_has_media_candidate(row: Any) -> bool:
    for _depth, control in walk_controls(row, max_depth=5, max_nodes=260):
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        if control_type == "ImageControl" and _is_visible_rect(rect):
            width = rect[2] - rect[0] if rect else 0
            height = rect[3] - rect[1] if rect else 0
            if width >= 16 and height >= 16:
                return True

        name = clean_text(safe_prop(control, "Name"))
        automation_id = clean_text(safe_prop(control, "AutomationId"))
        class_name = clean_text(safe_prop(control, "ClassName"))
        if _has_media_text_hint(name):
            return True
        if _has_media_control_hint(" ".join([name, automation_id, class_name, control_type])):
            return True
        for _source, value in _control_text_values(control):
            if _has_media_text_hint(value):
                return True
    return any(_has_media_text_hint(text) for text in _message_row_texts(row))


def row_has_media_text_candidate_from_visible(row: Any, visible_texts: list[QQVisibleText]) -> bool:
    row_rect = safe_rect_tuple(row)
    if not _is_visible_rect(row_rect):
        return False
    for item in visible_texts:
        item_rect = parse_rect(item.rect)
        if not _is_visible_rect(item_rect):
            continue
        if not rects_overlap_for_media_candidate(row_rect, item_rect):
            continue
        if _has_media_text_hint(item.text):
            return True
    return False


def rects_overlap_for_media_candidate(
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


def _has_media_text_hint(value: Any) -> bool:
    text = clean_text(value).replace("\u2009", " ")
    if not text:
        return False
    return looks_like_file_name(text) or bool(infer_file_size([text]))


def _has_media_control_hint(value: str) -> bool:
    haystack = clean_text(value).lower()
    if not haystack:
        return False
    return any(
        token in haystack
        for token in (
            "image",
            "img",
            "pic",
            "photo",
            "video",
            "file",
            "media",
            "emoji",
            "thumb",
            "鍥剧墖",
            "瑙嗛",
            "鏂囦欢",
            "琛ㄦ儏",
        )
    )


def attach_media_evidence(
    messages: list[QQMediaMessage],
    *,
    hwnd: int,
    window_rect: tuple[int, int, int, int],
    root_dir: str | Path,
) -> list[QQMediaMessage]:
    if not messages:
        return messages
    from .media_extractor import QQMediaExtractor

    extractor = QQMediaExtractor(root_dir=root_dir)
    captured: list[QQMediaMessage] = []
    for item in messages:
        extracted = extractor.extract(
            item,
            hwnd=hwnd,
            window_rect=window_rect,
        )
        metadata = dict(item.metadata)
        metadata.update(extracted.metadata_fields)
        captured.append(
            replace(
                item,
                content_image_path=str(extracted.payload_fields.get("content_image_path", "")),
                evidence_ref=str(extracted.payload_fields.get("evidence_ref", "")),
                metadata=metadata,
            )
        )
    return captured


def ensure_media_message_ids(
    messages: list[QQMediaMessage],
    *,
    conversation_title: str,
) -> list[QQMediaMessage]:
    stable: list[QQMediaMessage] = []
    for sequence_index, item in enumerate(messages, start=1):
        metadata = dict(item.metadata)
        platform_msg_id = clean_text(item.platform_msg_id)
        if platform_msg_id:
            metadata.setdefault("platform_msg_id_source", "uia_automation_id")
            stable.append(replace(item, platform_msg_id=_normalize_qq_media_id(platform_msg_id), metadata=metadata))
            continue

        fingerprint = "|".join(
            [
                "qq",
                clean_text(conversation_title),
                clean_text(item.direction),
                clean_text(item.content_type),
                clean_text(item.file_name),
                clean_text(item.file_size),
                clean_text(item.text),
                _rect_size_signature(item.media_rect or item.rect),
                str(sequence_index),
            ]
        )
        generated_id = "qq_media_" + hashlib.sha1(fingerprint.encode("utf-8", errors="ignore")).hexdigest()[:24]
        metadata.update(
            {
                "platform_msg_id_source": "fallback_hash",
                "platform_msg_id_fingerprint_fields": [
                    "conversation_title",
                    "direction",
                    "content_type",
                    "file_name",
                    "file_size",
                    "text",
                    "media_rect_size",
                    "sequence_index",
                ],
            }
        )
        stable.append(replace(item, platform_msg_id=generated_id, metadata=metadata))
    return stable


def _normalize_qq_media_id(value: str) -> str:
    text = clean_text(value)
    if not text:
        return ""
    return text if text.startswith("qq_") else f"qq_media_{text}"


def _rect_size_signature(value: str) -> str:
    rect = parse_rect(value)
    if rect is None:
        return clean_text(value)
    return f"{max(0, rect[2] - rect[0])}x{max(0, rect[3] - rect[1])}"


def media_message_from_row(
    row: Any,
    *,
    message_rect: tuple[int, int, int, int] | None,
) -> QQMediaMessage | None:
    row_rect = safe_rect_tuple(row)
    if not _is_visible_rect(row_rect):
        return None
    row_rect_text = _safe_rect(row)
    platform_msg_id = clean_text(safe_prop(row, "AutomationId"))
    texts = _message_row_texts(row)
    image_controls = _message_row_image_controls(row)
    image_rects = [safe_rect_tuple(control) for control in image_controls]
    image_rects = [rect for rect in image_rects if _is_visible_rect(rect)]

    file_index, file_name = infer_file_name(texts)
    file_size = infer_file_size(texts)
    content_type = ""
    confidence = 0.72
    if file_name:
        content_type = "video" if is_video_file_name(file_name) else "file"
        confidence += 0.12
    elif image_rects:
        content_type = "image"
        confidence += 0.1
    if not content_type:
        return None

    media_rect = infer_media_rect(row, content_type=content_type, image_rects=image_rects) or row_rect
    media_rect_text = _format_rect(media_rect)

    if content_type == "video" and image_rects:
        confidence += 0.08
    if content_type == "file" and file_size:
        confidence += 0.08
    if platform_msg_id:
        confidence += 0.05

    direction = infer_row_direction(media_rect, message_rect)
    text = media_display_text(content_type, file_name, file_size)
    metadata = {
        "raw_texts": texts,
        "row_child_count": child_count(row),
        "image_rects": [_format_rect(rect) for rect in image_rects],
    }
    if file_index >= 0:
        metadata["file_name_text_index"] = file_index
    return QQMediaMessage(
        index=0,
        platform_msg_id=platform_msg_id,
        content_type=content_type,
        direction=direction,
        file_name=file_name,
        file_size=file_size,
        text=text,
        rect=row_rect_text,
        media_rect=media_rect_text,
        image_count=len(image_rects),
        confidence=min(confidence, 0.95),
        metadata=metadata,
    )


def _message_row_texts(row: Any) -> list[str]:
    items = collect_visible_texts(row, max_depth=12, max_nodes=1200)
    items = [item for item in items if _is_visible_rect(parse_rect(item.rect))]
    items = sorted(items, key=_visible_text_sort_key)
    values: list[str] = []
    seen: set[str] = set()
    for item in items:
        text = clean_text(item.text).replace("\u2009", " ")
        if not text or _is_chrome_text(text) or is_time_text(text):
            continue
        if text in seen:
            continue
        seen.add(text)
        values.append(text)
    return values


def _message_row_image_controls(row: Any) -> list[Any]:
    controls: list[Any] = []
    for _depth, control in walk_controls(row, max_depth=12, max_nodes=1200):
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        if control_type != "ImageControl":
            continue
        rect = safe_rect_tuple(control)
        if not _is_visible_rect(rect):
            continue
        width = rect[2] - rect[0]
        height = rect[3] - rect[1]
        if width < 16 or height < 16:
            continue
        controls.append(control)
    return sorted(controls, key=lambda control: _rect_area(safe_rect_tuple(control)), reverse=True)


def infer_media_rect(
    row: Any,
    *,
    content_type: str,
    image_rects: list[tuple[int, int, int, int]],
) -> tuple[int, int, int, int] | None:
    if content_type == "image":
        return _largest_rect(image_rects)

    row_rect = safe_rect_tuple(row)
    candidates: list[tuple[int, tuple[int, int, int, int]]] = []
    for _depth, control in walk_controls(row, max_depth=8, max_nodes=600):
        rect = safe_rect_tuple(control)
        if not _is_visible_rect(rect) or rect is None:
            continue
        if row_rect and rect == row_rect:
            continue
        width = rect[2] - rect[0]
        height = rect[3] - rect[1]
        if width < 80 or height < 45 or width > 520 or height > 420:
            continue
        if safe_prop(control, "AutomationId").startswith("msg-extra"):
            continue
        score = _rect_area(rect)
        if content_type == "video" and image_rects and any(_rect_contains(rect, image_rect) for image_rect in image_rects):
            score += 80_000
        if content_type == "file":
            control_texts = _message_row_texts(control)
            if not any(looks_like_file_name(text) or infer_file_size([text]) for text in control_texts):
                continue
            score += 60_000
        candidates.append((score, rect))
    if candidates:
        return max(candidates, key=lambda item: item[0])[1]
    return _largest_rect(image_rects)


def infer_file_name(texts: list[str]) -> tuple[int, str]:
    for index, text in enumerate(texts):
        if not looks_like_file_name(text):
            continue
        previous = texts[index - 1] if index > 0 else ""
        if previous and _should_join_file_prefix(previous, text):
            separator = " " if is_video_file_name(text) else ""
            return index, clean_text(f"{previous}{separator}{text}")
        return index, text
    return -1, ""


def infer_file_size(texts: list[str]) -> str:
    for text in texts:
        normalized = clean_text(text).replace("\u2009", " ")
        if re.fullmatch(r"\d+(?:\.\d+)?\s*(?:B|KB|MB|GB|TB)", normalized, re.IGNORECASE):
            return normalized
    return ""


def looks_like_file_name(value: str) -> bool:
    text = clean_text(value)
    if not text or len(text) > 160:
        return False
    return bool(re.search(r"\.[A-Za-z0-9]{2,8}(?:$|\s)", text))


def is_video_file_name(value: str) -> bool:
    return bool(re.search(r"\.(?:mp4|mov|avi|mkv|wmv|flv|webm|m4v)(?:$|\s)", clean_text(value), re.IGNORECASE))


def _should_join_file_prefix(previous: str, current: str) -> bool:
    if not previous or looks_like_file_name(previous) or infer_file_size([previous]) or is_time_text(previous):
        return False
    if len(previous) > 80 or len(current) > 120:
        return False
    return True


def infer_row_direction(
    media_rect: tuple[int, int, int, int],
    message_rect: tuple[int, int, int, int] | None,
) -> str:
    if message_rect is None:
        return "unknown"
    midpoint = (message_rect[0] + message_rect[2]) / 2.0
    return "outgoing" if _rect_center_x(media_rect) > midpoint else "incoming"


def media_display_text(content_type: str, file_name: str, file_size: str) -> str:
    if content_type == "image":
        return "[图片]"
    if content_type == "video":
        return " ".join(part for part in ("[视频]", file_name, file_size) if part)
    if content_type == "file":
        return " ".join(part for part in ("[文件]", file_name, file_size) if part)
    return f"[{content_type}]"


def _largest_rect(rects: list[tuple[int, int, int, int]]) -> tuple[int, int, int, int] | None:
    return max(rects, key=_rect_area) if rects else None


def _rect_area(rect: tuple[int, int, int, int] | None) -> int:
    if rect is None:
        return 0
    return max(0, rect[2] - rect[0]) * max(0, rect[3] - rect[1])


def _format_rect(rect: tuple[int, int, int, int] | None) -> str:
    if rect is None:
        return "-"
    return f"({rect[0]},{rect[1]},{rect[2]},{rect[3]})"


def _rect_contains(
    outer: tuple[int, int, int, int],
    inner: tuple[int, int, int, int],
    *,
    margin: int = 2,
) -> bool:
    return (
        inner[0] >= outer[0] - margin
        and inner[1] >= outer[1] - margin
        and inner[2] <= outer[2] + margin
        and inner[3] <= outer[3] + margin
    )


def _safe_children(control: Any) -> list[Any]:
    try:
        return list(control.GetChildren())
    except Exception:
        return []


def score_media_layout_candidate(
    control: Any,
    message_rect: tuple[int, int, int, int] | None,
) -> tuple[int, str]:
    rect = safe_rect_tuple(control)
    if not _is_visible_rect(rect) or rect is None:
        return 0, ""
    if message_rect and not _rects_overlap(rect, message_rect):
        return 0, ""

    left, top, right, bottom = rect
    width = right - left
    height = bottom - top
    if width < 12 or height < 12:
        return 0, ""
    if message_rect:
        message_width = max(1, message_rect[2] - message_rect[0])
        message_height = max(1, message_rect[3] - message_rect[1])
        if width > message_width * 0.96 and height > message_height * 0.80:
            return 0, ""

    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    name = clean_text(safe_prop(control, "Name"))
    class_name = safe_prop(control, "ClassName")
    automation_id = safe_prop(control, "AutomationId")
    haystack = " ".join([name, class_name, automation_id, control_type]).lower()
    children = child_count(control)

    score = 0
    reasons: list[str] = []
    if control_type in {"ImageControl"}:
        score += 120
        reasons.append("image-control")
    if control_type in {"ButtonControl", "CustomControl", "DataItemControl", "ListItemControl"}:
        score += 45
        reasons.append("media-card-type")
    if control_type in {"GroupControl", "PaneControl"} and children <= 8:
        score += 24
        reasons.append("small-container")
    if any(token in haystack for token in ("image", "img", "pic", "photo", "video", "file", "voice", "audio", "media", "emoji", "thumb")):
        score += 90
        reasons.append("media-keyword")
    if any(token in name for token in ("图片", "图像", "视频", "文件", "语音", "音频", "表情", "动画", "预览")):
        score += 90
        reasons.append("media-cn-keyword")
    if not name and 28 <= width <= 420 and 28 <= height <= 320:
        score += 22
        reasons.append("anonymous-visible-shape")
    if 36 <= width <= 360 and 28 <= height <= 260:
        score += 18
        reasons.append("bubble-sized")
    if children > 0 and 24 <= width <= 480 and 24 <= height <= 360:
        score += 12
        reasons.append(f"children={children}")
    if control_type == "TextControl":
        score -= 200
        reasons.append("text-penalty")
    if name in {"消息列表", "QQ", "true", "false"}:
        score -= 120
        reasons.append("chrome-penalty")
    if score < 40:
        return 0, ""
    return score, ",".join(reasons)


def nearby_text_for_rect(rect_value: str, visible_texts: list[QQVisibleText], *, max_items: int = 5) -> str:
    rect = parse_rect(rect_value)
    if rect is None:
        return ""
    left, top, right, bottom = rect
    center_y = (top + bottom) // 2
    nearby: list[tuple[int, int, str]] = []
    for item in visible_texts:
        item_rect = parse_rect(item.rect)
        if item_rect is None:
            continue
        text = clean_text(item.text)
        if not text or _is_chrome_text(text):
            continue
        item_center_y = (item_rect[1] + item_rect[3]) // 2
        vertical_distance = abs(item_center_y - center_y)
        if vertical_distance > 180:
            continue
        horizontal_gap = min(abs(item_rect[0] - right), abs(left - item_rect[2]), abs(item_rect[0] - left))
        nearby.append((vertical_distance, horizontal_gap, text))
    seen: set[str] = set()
    values: list[str] = []
    for _vertical, _horizontal, text in sorted(nearby)[:max_items]:
        if text in seen:
            continue
        seen.add(text)
        values.append(text)
    return " | ".join(values)


def _walk_controls_with_paths(root: Any, max_depth: int, max_nodes: int) -> list[tuple[int, object, object | None, str]]:
    rows: list[tuple[int, object, object | None, str]] = []
    queue: list[tuple[int, object, object | None, str]] = [(0, root, None, "0")]
    while queue and len(rows) < max_nodes:
        depth, control, parent, path = queue.pop(0)
        rows.append((depth, control, parent, path))
        if depth >= max_depth:
            continue
        try:
            children = list(control.GetChildren())
        except Exception:
            continue
        for child_offset, child in enumerate(children):
            queue.append((depth + 1, child, control, f"{path}/{child_offset}"))
    return rows


def _available_patterns(control: Any) -> list[str]:
    found: list[str] = []
    for name, getter_name in (
        ("Invoke", "GetInvokePattern"),
        ("SelectionItem", "GetSelectionItemPattern"),
        ("LegacyIAccessible", "GetLegacyIAccessiblePattern"),
        ("Value", "GetValuePattern"),
        ("Text", "GetTextPattern"),
    ):
        try:
            getter = getattr(control, getter_name, None)
            if getter and getter():
                found.append(name)
        except Exception:
            continue
    return found


def _control_visual_sort_key(control: object) -> tuple[int, int, int]:
    rect = safe_rect_tuple(control)
    if rect is None:
        return (10**9, 10**9, 10**9)
    left, top, _right, _bottom = rect
    return (top // 8, top, left)


def _rects_overlap(
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


def structure_visible_messages(
    texts: list[QQVisibleText],
    message_rect: tuple[int, int, int, int] | None,
) -> list[QQStructuredMessage]:
    visible = [item for item in texts if _is_visible_rect(parse_rect(item.rect))]
    visible = [item for item in visible if not _is_chrome_text(item.text)]
    visible = sorted(visible, key=_visible_text_sort_key)
    if not visible:
        return []

    midpoint = _message_midpoint(message_rect, visible)
    groups: list[dict[str, Any]] = []
    pending_time = ""
    pending_time_top = 0
    pending_sender = ""
    pending_sender_top = 0
    current: dict[str, Any] | None = None

    for item in visible:
        rect = parse_rect(item.rect)
        if rect is None:
            continue
        text = clean_text(item.text)
        if not text:
            continue
        if is_time_text(text):
            pending_time = text
            pending_time_top = rect[1]
            current = None
            continue
        if is_sender_candidate(item, text):
            pending_sender = text
            pending_sender_top = rect[1]
            continue
        if not is_message_body_candidate(item, text):
            continue

        inferred_direction = "outgoing" if _rect_center_x(rect) > midpoint else "incoming"
        continues_current = (
            current is not None
            and rect[1] - current["bottom"] <= 36
            and not (pending_time and pending_time_top > current["top"])
            and (text.startswith("@") or rect[1] - current["top"] <= 48 or current["direction"] == inferred_direction)
        )
        direction = current["direction"] if continues_current and current is not None else inferred_direction
        if not continues_current:
            current = {
                "direction": direction,
                "sender": pending_sender if 0 <= rect[1] - pending_sender_top <= 90 else "",
                "time_text": pending_time if 0 <= rect[1] - pending_time_top <= 260 else "",
                "parts": [],
                "rects": [],
                "top": rect[1],
                "bottom": rect[3],
            }
            groups.append(current)
        current["parts"].append(text)
        current["rects"].append(rect)
        current["bottom"] = max(current["bottom"], rect[3])
        current["top"] = min(current["top"], rect[1])

    return [_group_to_message(group) for group in groups if group["parts"]]


def find_conversation_list_root(chat_root: Any) -> Any | None:
    candidates: list[tuple[int, Any]] = []
    root_rect = safe_rect_tuple(chat_root)
    for _depth, control in walk_controls(chat_root, max_depth=14, max_nodes=3000):
        name = clean_text(safe_prop(control, "Name"))
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        if not rect:
            continue
        score = 0
        if name == "会话列表":
            score += 200
        if control_type == "WindowControl":
            score += 20
        if root_rect:
            root_width = max(1, root_rect[2] - root_rect[0])
            if rect[0] < root_rect[0] + root_width * 0.35:
                score += 35
            if rect[2] < root_rect[0] + root_width * 0.45:
                score += 25
        width = rect[2] - rect[0]
        height = rect[3] - rect[1]
        if 140 <= width <= 360 and height >= 250:
            score += 35
        if score > 0:
            if score >= 260:
                return control
            candidates.append((score, control))
    return max(candidates, key=lambda item: item[0])[1] if candidates else None


def collect_conversation_items(conversation_root: Any, current_title: str = "") -> list[QQConversationItem]:
    item_controls = _conversation_item_controls(conversation_root)
    items: list[QQConversationItem] = []
    for index, control in enumerate(item_controls, start=1):
        texts = collect_visible_texts(control, max_depth=8, max_nodes=400)
        text_items = [
            item
            for item in texts
            if item.control_type == "TextControl" and _is_visible_rect(parse_rect(item.rect))
        ]
        text_items = sorted(text_items, key=_visible_text_sort_key)
        if not text_items:
            continue
        time_items = [item for item in text_items if is_time_text(item.text)]
        title_item = next((item for item in text_items if item not in time_items), None)
        if title_item is None:
            continue
        item_rect = _safe_rect(control)
        unread_item = infer_unread_badge_item(text_items, item_rect)
        preview_items = [
            item
            for item in text_items
            if item is not title_item
            and item is not unread_item
            and item not in time_items
            and not _is_chrome_text(item.text)
            and not is_group_count_marker(item.text)
        ]
        title = clean_text(title_item.text)
        preview = " ".join(clean_text(item.text) for item in preview_items)
        items.append(
            QQConversationItem(
                index=index,
                title=title,
                time_text=clean_text(time_items[0].text) if time_items else "",
                preview=preview,
                rect=item_rect,
                unread_hint=clean_text(unread_item.text) if unread_item else "",
                is_current_candidate=bool(current_title and title == clean_text(current_title)),
            )
        )
    return items


def _is_cached_root_usable(control: Any) -> bool:
    rect = safe_rect_tuple(control)
    if not _is_visible_rect(rect):
        return False
    return bool(_safe_children(control))


def _conversation_item_controls(conversation_root: Any) -> list[Any]:
    controls: list[Any] = []
    root_rect = safe_rect_tuple(conversation_root)
    if not root_rect:
        return controls
    try:
        children = list(conversation_root.GetChildren())
    except Exception:
        return controls
    for child in children:
        rect = safe_rect_tuple(child)
        if not rect:
            continue
        width = rect[2] - rect[0]
        height = rect[3] - rect[1]
        if width >= (root_rect[2] - root_rect[0]) * 0.75 and 36 <= height <= 90:
            controls.append(child)
    return sorted(controls, key=lambda control: (safe_rect_tuple(control) or (0, 0, 0, 0))[1])


def infer_unread_hint(text_items: list[QQVisibleText], item_rect: str = "") -> str:
    item = infer_unread_badge_item(text_items, item_rect)
    return clean_text(item.text) if item else ""


def infer_unread_badge_item(text_items: list[QQVisibleText], item_rect: str = "") -> QQVisibleText | None:
    item_bounds = parse_rect(item_rect)
    candidates: list[tuple[int, QQVisibleText]] = []
    for item in text_items:
        text = clean_text(item.text)
        if not re.fullmatch(r"\d{1,3}", text):
            continue
        rect = parse_rect(item.rect)
        if not rect:
            continue
        width = rect[2] - rect[0]
        height = rect[3] - rect[1]
        if width > 36 or height > 24:
            continue
        if item_bounds is not None:
            _left, top, right, bottom = item_bounds
            center_x = (rect[0] + rect[2]) // 2
            center_y = (rect[1] + rect[3]) // 2
            if center_y < top or center_y > bottom:
                continue
            if center_x < right - 64:
                continue
        candidates.append((rect[2], item))
    if not candidates:
        return None
    return max(candidates, key=lambda pair: pair[0])[1]


def is_group_count_marker(value: str) -> bool:
    return bool(re.fullmatch(r"\(\d{1,4}\)", clean_text(value)))


def _group_to_message(group: dict[str, Any]) -> QQStructuredMessage:
    rects = group["rects"]
    left = min(rect[0] for rect in rects)
    top = min(rect[1] for rect in rects)
    right = max(rect[2] for rect in rects)
    bottom = max(rect[3] for rect in rects)
    sender, text = split_sender_and_text(group["sender"], group["parts"])
    confidence = 0.75
    if group["time_text"]:
        confidence += 0.1
    if sender:
        confidence += 0.1
    return QQStructuredMessage(
        direction=group["direction"],
        sender=sender,
        time_text=group["time_text"],
        text=text,
        rect=f"({left},{top},{right},{bottom})",
        confidence=min(confidence, 0.95),
        raw_count=len(group["parts"]),
    )


def split_sender_and_text(existing_sender: str, parts: list[str]) -> tuple[str, str]:
    cleaned_parts = [clean_text(part) for part in parts if clean_text(part)]
    if existing_sender:
        return clean_text(existing_sender), " ".join(remove_badge_parts(cleaned_parts))
    if len(cleaned_parts) < 2:
        return "", " ".join(remove_badge_parts(cleaned_parts))

    first = cleaned_parts[0]
    if is_inline_sender_name(first):
        return first, " ".join(remove_badge_parts(cleaned_parts[1:]))
    return "", " ".join(remove_badge_parts(cleaned_parts))


def remove_badge_parts(parts: list[str]) -> list[str]:
    return [part for part in parts if not is_group_badge(part)]


def is_inline_sender_name(value: str) -> bool:
    text = clean_text(value)
    if not text or text.startswith("@") or is_time_text(text) or _is_chrome_text(text) or is_group_badge(text):
        return False
    if len(text) > 24:
        return False
    if re.search(r"[，。！？、,.!?/:：；;（）()\[\]{}]", text):
        return False
    return True


def is_group_badge(value: str) -> bool:
    return clean_text(value) in {
        "管理员",
        "群主",
        "群管理员",
        "owner",
        "admin",
    }


def parse_rect(value: str) -> tuple[int, int, int, int] | None:
    try:
        left, top, right, bottom = [int(part) for part in value.strip("()").split(",")]
    except ValueError:
        return None
    return left, top, right, bottom


def _is_visible_rect(rect: tuple[int, int, int, int] | None) -> bool:
    if rect is None:
        return False
    left, top, right, bottom = rect
    return right > left and bottom > top and (left, top, right, bottom) != (0, 0, 0, 0)


def _visible_text_sort_key(item: QQVisibleText) -> tuple[int, int, int, str]:
    rect = parse_rect(item.rect)
    if rect is None:
        return (10**9, 10**9, 10**9, item.text)
    left, top, _right, _bottom = rect
    return (top // 8, top, left, item.text)


def _message_midpoint(
    message_rect: tuple[int, int, int, int] | None,
    visible: list[QQVisibleText],
) -> float:
    if message_rect:
        return (message_rect[0] + message_rect[2]) / 2.0
    rects = [parse_rect(item.rect) for item in visible]
    rects = [rect for rect in rects if rect is not None]
    if not rects:
        return 0.0
    return (min(rect[0] for rect in rects) + max(rect[2] for rect in rects)) / 2.0


def _rect_center_x(rect: tuple[int, int, int, int]) -> float:
    return (rect[0] + rect[2]) / 2.0


def clean_text(value: str) -> str:
    return re.sub(r"\s+", " ", value.replace("\x7f", "")).strip()


def is_time_text(value: str) -> bool:
    text = clean_text(value)
    return bool(
        re.fullmatch(r"\d{1,4}/\d{1,2}/\d{1,2}\s+\d{1,2}:\d{2}", text)
        or re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text)
        or re.fullmatch(r"\d{1,2}/\d{1,2}", text)
        or re.fullmatch(r"(星期|周)[一二三四五六日天](\s+\d{1,2}:\d{2})?", text)
    )


def is_sender_candidate(item: QQVisibleText, text: str) -> bool:
    if item.control_type != "GroupControl":
        return False
    if text.startswith("@") or is_time_text(text) or _is_chrome_text(text):
        return False
    return 1 <= len(text) <= 40


def is_message_body_candidate(item: QQVisibleText, text: str) -> bool:
    if item.control_type != "TextControl":
        return False
    if is_time_text(text) or _is_chrome_text(text):
        return False
    return True


def _is_chrome_text(value: str) -> bool:
    text = clean_text(value)
    return text in {"QQ", "消息列表", "会话", "表情", "true", "false"}


def normalize_text(value: Any) -> str:
    text = str(value or "").replace("\r", "\n")
    lines = [line.strip() for line in text.split("\n")]
    lines = [line for line in lines if line]
    return "\n".join(lines)


def _control_text_values(control: Any) -> list[tuple[str, str]]:
    values: list[tuple[str, str]] = []
    for attr in ("Name", "Value", "LegacyIAccessibleName", "HelpText"):
        value = safe_prop(control, attr)
        if value:
            values.append((attr, value))
    for source, reader in (("ValuePattern", _read_value_pattern), ("TextPattern", _read_text_pattern)):
        value = reader(control)
        if value:
            values.append((source, value))
    return values


def _read_value_pattern(control: Any) -> str:
    try:
        pattern = control.GetValuePattern()
        if not pattern:
            return ""
        return str(getattr(pattern, "Value", "") or getattr(pattern, "CurrentValue", "") or "")
    except Exception:
        return ""


def _read_text_pattern(control: Any) -> str:
    try:
        pattern = control.GetTextPattern()
        if not pattern:
            return ""
        document_range = getattr(pattern, "DocumentRange", None)
        get_text = getattr(document_range, "GetText", None) if document_range else None
        if not get_text:
            return ""
        return str(get_text(-1) or "")
    except Exception:
        return ""


def _safe_rect(control: Any) -> str:
    try:
        rect = control.BoundingRectangle
        return f"({rect.left},{rect.top},{rect.right},{rect.bottom})"
    except Exception:
        return "-"


def _should_skip_text(value: str) -> bool:
    if not value:
        return True
    if len(value) > 1000:
        return True
    normalized = trim(re.sub(r"\s+", " ", value), 120)
    blocked = {
        "QQ",
        "消息",
        "联系人",
        "群聊",
        "搜索",
        "关闭",
        "最小化",
        "最大化",
        "还原",
    }
    return normalized in blocked
