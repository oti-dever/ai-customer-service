from __future__ import annotations

import re
import threading
import time
from dataclasses import dataclass
from typing import Any

from .config import AppConfig, load_config
from .detector import QianniuDetector
from .qianniu_logging import get_logger
from .uia import control_from_point, find_process_ids, is_control_available, safe_prop, safe_rect_tuple, walk_controls

READ_STATUS = "\u5df2\u8bfb"
UNREAD_STATUS = "\u672a\u8bfb"
TIMESTAMP_PATTERN = r"\d{4}-\d{1,2}-\d{1,2}\s+\d{1,2}:\d{2}:\d{2}"

logger = get_logger(__name__)


@dataclass(frozen=True)
class MessageReadResult:
    ok: bool
    source: str
    texts: list[str]
    detail: str = ""


@dataclass(frozen=True)
class MessageRecord:
    sender: str
    timestamp: str
    text: str
    raw: str
    direction: str = "unknown"
    status: str = ""


class QianniuReader:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QianniuDetector(self.config)
        self.cached_chat_root: Any | None = None
        self.cached_message_display: Any | None = None
        self.cached_message_web: Any | None = None
        self._cache_thread_id: int | None = None

    def read_visible_texts(
        self,
        limit: int = 80,
        chat_root: Any | None = None,
        message_display: Any | None = None,
        message_web: Any | None = None,
    ) -> MessageReadResult:
        total_started_at = time.perf_counter()
        self._ensure_cache_thread()
        stage_started_at = time.perf_counter()
        reused_chat_root = False
        if is_control_available(chat_root):
            active_chat_root = chat_root
            reused_chat_root = True
        elif is_control_available(self.cached_chat_root):
            active_chat_root = self.cached_chat_root
            reused_chat_root = True
        else:
            handle = self.detector.find_current_chat()
            active_chat_root = handle.chat_root if handle else None
        find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if not active_chat_root:
            self.cached_chat_root = None
            self.cached_message_display = None
            self.cached_message_web = None
            logger.info(
                "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f ok=False source=find_chat reused_chat_root=%s text_count=0",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                reused_chat_root,
            )
            return MessageReadResult(ok=False, source="find_chat", texts=[], detail="chat window not found")
        self.cached_chat_root = active_chat_root
        self._cache_thread_id = threading.get_ident()

        stage_started_at = time.perf_counter()
        message_display, reused_message_display = self._resolve_message_display(active_chat_root, message_display)
        find_display_ms = (time.perf_counter() - stage_started_at) * 1000.0

        stage_started_at = time.perf_counter()
        message_web, reused_message_web = self._resolve_message_web(active_chat_root, message_web)
        find_web_ms = (time.perf_counter() - stage_started_at) * 1000.0

        display_collect_ms = 0.0
        web_collect_ms = 0.0
        display_point_ms = 0.0
        web_point_ms = 0.0
        web_rect_ms = 0.0
        copy_ms = 0.0

        if message_web:
            stage_started_at = time.perf_counter()
            copied = copy_text_from_control(message_web)
            copy_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if copied:
                logger.info(
                    "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f display_rect_ms=%.1f display_point_ms=%.1f find_web_ms=%.1f web_collect_ms=%.1f web_rect_ms=%.1f web_point_ms=%.1f copy_ms=%.1f ok=True source=message_web_copy reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=1",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    find_display_ms,
                    display_collect_ms,
                    0.0,
                    display_point_ms,
                    find_web_ms,
                    web_collect_ms,
                    web_rect_ms,
                    web_point_ms,
                    copy_ms,
                    reused_chat_root,
                    reused_message_display,
                    reused_message_web,
                )
                return MessageReadResult(ok=True, source="message_web_copy", texts=[copied])

        if message_display:
            stage_started_at = time.perf_counter()
            texts = collect_texts(message_display, max_depth=24, max_nodes=20000, preorder=True)
            display_collect_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if texts:
                logger.info(
                    "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f ok=True source=message_display_accessible reused_chat_root=%s reused_message_display=%s text_count=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    find_display_ms,
                    display_collect_ms,
                    reused_chat_root,
                    reused_message_display,
                    len(texts[-limit:]),
                )
                return MessageReadResult(ok=True, source="message_display_accessible", texts=texts[-limit:])

        if message_web:
            stage_started_at = time.perf_counter()
            texts = collect_texts(message_web, max_depth=24, max_nodes=20000, preorder=True)
            web_collect_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if texts:
                logger.info(
                    "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f find_web_ms=%.1f web_collect_ms=%.1f ok=True source=message_web_accessible reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    find_display_ms,
                    display_collect_ms,
                    find_web_ms,
                    web_collect_ms,
                    reused_chat_root,
                    reused_message_display,
                    reused_message_web,
                    len(texts[-limit:]),
                )
                return MessageReadResult(ok=True, source="message_web_accessible", texts=texts[-limit:])

        stage_started_at = time.perf_counter()
        display_rect = safe_rect_tuple(message_display) if message_display else None
        display_rect_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if display_rect:
            stage_started_at = time.perf_counter()
            texts = collect_point_accessible_texts(display_rect)
            display_point_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if texts:
                logger.info(
                    "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f display_rect_ms=%.1f display_point_ms=%.1f find_web_ms=%.1f web_collect_ms=%.1f ok=True source=message_display_point_accessible reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    find_display_ms,
                    display_collect_ms,
                    display_rect_ms,
                    display_point_ms,
                    find_web_ms,
                    web_collect_ms,
                    reused_chat_root,
                    reused_message_display,
                    reused_message_web,
                    len(texts[-limit:]),
                )
                return MessageReadResult(ok=True, source="message_display_point_accessible", texts=texts[-limit:])

        if not message_web:
            logger.info(
                "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f display_rect_ms=%.1f display_point_ms=%.1f find_web_ms=%.1f ok=False source=message_web reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=0",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                find_display_ms,
                display_collect_ms,
                display_rect_ms,
                display_point_ms,
                find_web_ms,
                reused_chat_root,
                reused_message_display,
                reused_message_web,
            )
            return MessageReadResult(ok=False, source="message_web", texts=[], detail="message web not found")

        stage_started_at = time.perf_counter()
        web_rect = safe_rect_tuple(message_web)
        web_rect_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if web_rect:
            stage_started_at = time.perf_counter()
            texts = collect_point_accessible_texts(web_rect)
            web_point_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if texts:
                logger.info(
                    "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f display_rect_ms=%.1f display_point_ms=%.1f find_web_ms=%.1f web_collect_ms=%.1f web_rect_ms=%.1f web_point_ms=%.1f ok=True source=message_web_point_accessible reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    find_display_ms,
                    display_collect_ms,
                    display_rect_ms,
                    display_point_ms,
                    find_web_ms,
                    web_collect_ms,
                    web_rect_ms,
                    web_point_ms,
                    reused_chat_root,
                    reused_message_display,
                    reused_message_web,
                    len(texts[-limit:]),
                )
                return MessageReadResult(ok=True, source="message_web_point_accessible", texts=texts[-limit:])

        logger.info(
            "qianniu read_messages_timing stage=total ms=%.1f find_chat_ms=%.1f find_display_ms=%.1f display_collect_ms=%.1f display_rect_ms=%.1f display_point_ms=%.1f find_web_ms=%.1f web_collect_ms=%.1f web_rect_ms=%.1f web_point_ms=%.1f copy_ms=%.1f ok=False source=message_web reused_chat_root=%s reused_message_display=%s reused_message_web=%s text_count=0",
            (time.perf_counter() - total_started_at) * 1000.0,
            find_chat_ms,
            find_display_ms,
            display_collect_ms,
            display_rect_ms,
            display_point_ms,
            find_web_ms,
            web_collect_ms,
            web_rect_ms,
            web_point_ms,
            copy_ms,
            reused_chat_root,
            reused_message_display,
            reused_message_web,
        )
        return MessageReadResult(ok=False, source="message_web", texts=[], detail="no UIA/copy text exposed")

    def read_visible_messages(self, limit: int = 50, chat_root: Any | None = None) -> list[MessageRecord]:
        result, messages = self.read_visible_messages_debug(limit=limit, chat_root=chat_root)
        return messages

    def read_visible_messages_debug(
        self,
        limit: int = 50,
        chat_root: Any | None = None,
        message_display: Any | None = None,
        message_web: Any | None = None,
    ) -> tuple[MessageReadResult, list[MessageRecord]]:
        started_at = time.perf_counter()
        result = self.read_visible_texts(
            limit=max(120, limit * 8),
            chat_root=chat_root,
            message_display=message_display,
            message_web=message_web,
        )
        if not result.ok or not result.texts:
            logger.info(
                "qianniu parse_messages_timing total_ms=%.1f source=%s ok=%s text_count=%s message_count=0",
                (time.perf_counter() - started_at) * 1000.0,
                result.source,
                result.ok,
                len(result.texts),
            )
            return result, []
        parse_started_at = time.perf_counter()
        if "point_accessible" in result.source:
            messages = parse_accessible_messages(result.texts)[-limit:]
            parser = "accessible"
        elif result.source.endswith("_copy"):
            raw_text = result.texts[-1]
            messages = parse_copied_messages(raw_text)[-limit:]
            parser = "copied"
        else:
            raw_text = "\n".join(result.texts)
            messages = parse_copied_messages(raw_text)[-limit:]
            parser = "joined"
        parse_ms = (time.perf_counter() - parse_started_at) * 1000.0
        logger.info(
            "qianniu parse_messages_timing total_ms=%.1f parse_ms=%.1f source=%s parser=%s ok=%s text_count=%s message_count=%s",
            (time.perf_counter() - started_at) * 1000.0,
            parse_ms,
            result.source,
            parser,
            result.ok,
            len(result.texts),
            len(messages),
        )
        return result, messages

    def _resolve_message_display(self, chat_root: Any, message_display: Any | None = None) -> tuple[Any | None, bool]:
        if is_control_available(message_display):
            self.cached_message_display = message_display
            self._cache_thread_id = threading.get_ident()
            return message_display, True
        if is_control_available(self.cached_message_display):
            return self.cached_message_display, True
        self.cached_message_display = None
        resolved = self.detector.find_message_display(chat_root)
        if resolved:
            self.cached_message_display = resolved
            self._cache_thread_id = threading.get_ident()
        return resolved, False

    def _resolve_message_web(self, chat_root: Any, message_web: Any | None = None) -> tuple[Any | None, bool]:
        if is_control_available(message_web):
            self.cached_message_web = message_web
            self._cache_thread_id = threading.get_ident()
            return message_web, True
        if is_control_available(self.cached_message_web):
            return self.cached_message_web, True
        self.cached_message_web = None
        resolved = self.detector.find_message_web(chat_root)
        if resolved:
            self.cached_message_web = resolved
            self._cache_thread_id = threading.get_ident()
        return resolved, False

    def _ensure_cache_thread(self) -> None:
        current_thread_id = threading.get_ident()
        if self._cache_thread_id in {None, current_thread_id}:
            return
        self.cached_chat_root = None
        self.cached_message_display = None
        self.cached_message_web = None
        self._cache_thread_id = None


def collect_texts(root: Any, max_depth: int, max_nodes: int, preorder: bool = False) -> list[str]:
    values: list[str] = []
    nodes = walk_controls_preorder(root, max_depth=max_depth, max_nodes=max_nodes) if preorder else walk_controls(
        root, max_depth=max_depth, max_nodes=max_nodes
    )
    last_value = ""
    for _, control in nodes:
        for value in collect_control_texts(control):
            if not value or should_skip_accessible_text(value):
                continue
            if value == last_value:
                continue
            values.append(value)
            last_value = value
    return values


def collect_control_texts(control: Any) -> list[str]:
    values: list[str] = []
    for attr in ("Name", "Value", "LegacyIAccessibleName", "HelpText"):
        value = normalize_text(safe_prop(control, attr))
        if value:
            values.append(value)

    for reader in (_read_value_pattern, _read_text_pattern):
        value = reader(control)
        if value:
            values.append(value)
    values.extend(_read_legacy_pattern(control))
    return _dedupe(values)


def collect_point_accessible_texts(
    rect: tuple[int, int, int, int],
    step_x: int = 90,
    step_y: int = 28,
    parent_hops: int = 4,
    child_depth: int = 3,
) -> list[str]:
    ali_render_pids = set(find_process_ids("AliRender.exe"))
    controls: list[Any] = []
    seen_runtime: set[str] = set()

    left, top, right, bottom = rect
    points = sample_points(left, top, right, bottom, step_x=step_x, step_y=step_y)
    for x, y in points:
        control = control_from_point(x, y)
        for item in iter_control_and_parents(control, max_hops=parent_hops):
            if not item:
                continue
            if not is_likely_chrome_accessible_control(item, ali_render_pids, rect):
                continue
            key = control_identity(item)
            if key in seen_runtime:
                continue
            seen_runtime.add(key)
            controls.append(item)

    values: list[str] = []
    last_value = ""
    for control in sort_controls_by_rect(controls):
        for _, item in walk_controls_preorder(control, max_depth=child_depth, max_nodes=200):
            if not is_rect_inside(safe_rect_tuple(item), rect):
                continue
            for value in collect_control_texts(item):
                if not value or should_skip_accessible_text(value) or value == last_value:
                    continue
                values.append(value)
                last_value = value
    return values


def sample_points(left: int, top: int, right: int, bottom: int, step_x: int, step_y: int) -> list[tuple[int, int]]:
    width = max(1, right - left)
    height = max(1, bottom - top)
    x_values = list(range(left + 10, right - 10, step_x))
    y_values = list(range(top + 10, bottom - 10, step_y))
    x_values.extend([left + width // 4, left + width // 2, left + width * 3 // 4])
    y_values.extend([top + height // 4, top + height // 2, top + height * 3 // 4])
    points = [(x, y) for y in sorted(set(y_values)) for x in sorted(set(x_values))]
    return [(x, y) for x, y in points if left <= x <= right and top <= y <= bottom]


def iter_control_and_parents(control: Any | None, max_hops: int) -> list[Any]:
    result: list[Any] = []
    current = control
    for _ in range(max_hops + 1):
        if not current:
            break
        result.append(current)
        try:
            current = current.GetParentControl()
        except Exception:
            break
    return result


def is_likely_chrome_accessible_control(
    control: Any,
    ali_render_pids: set[int],
    container_rect: tuple[int, int, int, int],
) -> bool:
    rect = safe_rect_tuple(control)
    if not is_rect_inside(rect, container_rect):
        return False
    framework = safe_prop(control, "FrameworkId")
    class_name = safe_prop(control, "ClassName")
    process_id = safe_prop(control, "ProcessId")
    pid_match = process_id.isdigit() and int(process_id) in ali_render_pids
    return pid_match or framework == "Chrome" or "Chrome" in class_name


def control_identity(control: Any) -> str:
    runtime_id = safe_prop(control, "RuntimeId")
    if runtime_id:
        return f"runtime:{runtime_id}"
    rect = safe_rect_tuple(control)
    return "|".join(
        [
            safe_prop(control, "ProcessId"),
            safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
            safe_prop(control, "ClassName"),
            safe_prop(control, "Name"),
            str(rect),
        ]
    )


def sort_controls_by_rect(controls: list[Any]) -> list[Any]:
    def key(control: Any) -> tuple[int, int, int, str]:
        rect = safe_rect_tuple(control)
        if not rect:
            return (10**9, 10**9, 10**9, control_identity(control))
        return (rect[1], rect[0], rect[3] - rect[1], control_identity(control))

    return sorted(controls, key=key)


def is_rect_inside(rect: tuple[int, int, int, int] | None, container: tuple[int, int, int, int]) -> bool:
    if not rect:
        return False
    left, top, right, bottom = rect
    c_left, c_top, c_right, c_bottom = container
    if right <= left or bottom <= top:
        return False
    return left >= c_left - 4 and right <= c_right + 4 and top >= c_top - 4 and bottom <= c_bottom + 4


def should_skip_accessible_text(value: str) -> bool:
    value = normalize_accessible_line(value)
    if not value:
        return True
    if value in {"\u6700\u65b0\u6d88\u606f", "\u5343\u725b\u6d88\u606f\u804a\u5929", "\u83dc\u5355"}:
        return True
    if value.startswith("http://") or value.startswith("https://"):
        return True
    if "web_chat-packer/recent.html" in value:
        return True
    return False


def normalize_accessible_line(value: str) -> str:
    value = normalize_text(value)
    value = value.replace("\ufffd", " ")
    value = value.replace("\u25c6", " ")
    value = value.replace("\u25ca", " ")
    value = re.sub(r"\s+", " ", value).strip()
    return value


def walk_controls_preorder(root: Any, max_depth: int, max_nodes: int) -> list[tuple[int, Any]]:
    found: list[tuple[int, Any]] = []
    stack: list[tuple[int, Any]] = [(0, root)]

    while stack and len(found) < max_nodes:
        depth, control = stack.pop()
        found.append((depth, control))
        if depth >= max_depth:
            continue
        try:
            children = list(control.GetChildren())
        except Exception:
            continue
        for child in reversed(children):
            stack.append((depth + 1, child))

    return found


def copy_text_from_control(control: Any) -> str:
    old_clipboard = get_clipboard_text()
    rect = None
    selected = False
    try:
        set_clipboard_text("")
        rect = get_control_rect(control)
        if rect:
            click_control_rect(rect)
        try:
            control.SetFocus()
        except Exception:
            pass
        send_ctrl_key("A")
        selected = True
        send_ctrl_key("C")
        copied = normalize_text(get_clipboard_text() or "")
        if clear_control_selection(control, rect):
            selected = False
        return copied
    except Exception:
        return ""
    finally:
        if selected:
            try:
                clear_control_selection(control, rect)
            except Exception:
                pass
        if old_clipboard is not None:
            try:
                set_clipboard_text(old_clipboard)
            except Exception:
                pass


def get_control_rect(control: Any) -> Any:
    return getattr(control, "BoundingRectangle", None)


def clear_control_selection(control: Any, fallback_rect: Any = None) -> bool:
    rect = get_control_rect(control) or fallback_rect
    if not rect:
        return False
    click_control_rect(rect)
    return True


def click_control_rect(rect: Any) -> None:
    click_point(int((rect.left + rect.right) / 2), int((rect.top + rect.bottom) / 2))


def parse_copied_messages(text: str) -> list[MessageRecord]:
    lines = [line.strip() for line in text.replace("\r", "\n").split("\n")]
    lines = [line for line in lines if line]
    messages: list[MessageRecord] = []
    current_header: str | None = None
    current_body: list[str] = []

    for line in lines:
        if is_message_header(line):
            if current_header:
                messages.append(build_message(current_header, current_body))
            current_header = line
            current_body = []
            continue
        if current_header:
            current_body.append(line)

    if current_header:
        messages.append(build_message(current_header, current_body))
    return messages


@dataclass(frozen=True)
class AccessibleHeader:
    sender: str
    timestamp: str
    raw: str
    extra_body: str = ""


def parse_accessible_messages(texts: list[str]) -> list[MessageRecord]:
    lines = expand_accessible_lines(texts)
    lines = [line for line in lines if line and not should_skip_accessible_parse_line(line)]

    messages: list[MessageRecord] = []
    current_header: AccessibleHeader | None = None
    current_body: list[str] = []
    current_status = ""

    for line in lines:
        header = parse_accessible_header(line)
        if header:
            if current_header and is_same_accessible_header(current_header, header):
                if header.extra_body:
                    add_body_line(current_body, header.extra_body)
                continue
            if current_header:
                append_accessible_message(messages, current_header, current_body, current_status)
            current_header = header
            current_body = []
            current_status = ""
            if header.extra_body:
                add_body_line(current_body, header.extra_body)
            continue

        if not current_header:
            continue
        if is_accessible_metadata_line(line, current_header):
            continue
        if is_status_line(line):
            current_status = READ_STATUS if READ_STATUS in line else UNREAD_STATUS
            continue
        add_body_line(current_body, line)

    if current_header:
        append_accessible_message(messages, current_header, current_body, current_status)
    return dedupe_messages(messages)


def parse_accessible_header(line: str) -> AccessibleHeader | None:
    match = re.search(TIMESTAMP_PATTERN, line)
    if not match:
        return None

    before = line[: match.start()].strip()
    timestamp = match.group(0)
    after = line[match.end() :].strip()

    if before:
        sender = before
        extra_body = after
    elif after and not is_status_line(after):
        sender, extra_body = split_sender_and_inline_body(after)
    else:
        return None

    if not sender or is_timestamp_only(sender):
        return None
    return AccessibleHeader(sender=sender, timestamp=timestamp, raw=line, extra_body=extra_body)


def expand_accessible_lines(texts: list[str]) -> list[str]:
    lines: list[str] = []
    for text in texts:
        line = normalize_accessible_line(text)
        if not line:
            continue
        parts = split_merged_accessible_line(line)
        if parts:
            lines.extend(parts)
        else:
            lines.append(line)
    return lines


def split_merged_accessible_line(line: str) -> list[str]:
    matches = list(re.finditer(TIMESTAMP_PATTERN, line))
    if len(matches) < 2:
        return []

    starts: list[int] = []
    for match in matches:
        starts.append(accessible_header_start(line, match.start()))
    if sorted(set(starts)) != starts:
        return []

    parts: list[str] = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(line)
        segment = line[start:end].strip()
        if not segment:
            continue
        status = trailing_status(segment)
        if status:
            segment = segment[: -len(status)].strip()
        if segment:
            parts.append(segment)
        if status:
            parts.append(status)
    return parts


def accessible_header_start(line: str, timestamp_start: int) -> int:
    index = timestamp_start
    while index > 0 and line[index - 1].isspace():
        index -= 1
    while index > 0 and not line[index - 1].isspace():
        index -= 1
    return index


def trailing_status(value: str) -> str:
    for status in (READ_STATUS, UNREAD_STATUS):
        if value == status:
            return status
        if value.endswith(" " + status):
            return status
    return ""


def split_sender_and_inline_body(value: str) -> tuple[str, str]:
    parts = value.split(maxsplit=1)
    if len(parts) == 2 and looks_like_sender(parts[0]):
        return parts[0], parts[1]
    return value, ""


def looks_like_sender(value: str) -> bool:
    return value.lower().startswith("tb") or ":" in value or "\uff1a" in value


def should_skip_accessible_parse_line(line: str) -> bool:
    if should_skip_accessible_text(line):
        return True
    return False


def is_same_accessible_header(left: AccessibleHeader, right: AccessibleHeader) -> bool:
    return left.sender == right.sender and left.timestamp == right.timestamp


def is_accessible_metadata_line(line: str, header: AccessibleHeader) -> bool:
    if line == header.sender or line == header.timestamp:
        return True
    compact = line.replace(" ", "")
    sender_time = f"{header.sender}{header.timestamp}".replace(" ", "")
    time_sender = f"{header.timestamp}{header.sender}".replace(" ", "")
    if compact in {sender_time, time_sender}:
        return True
    return is_timestamp_only(line)


def is_timestamp_only(line: str) -> bool:
    return bool(re.fullmatch(TIMESTAMP_PATTERN, line))


def is_status_line(line: str) -> bool:
    return READ_STATUS in line or UNREAD_STATUS in line


def add_body_line(body: list[str], line: str) -> None:
    line = normalize_accessible_line(line)
    if not line or should_skip_accessible_parse_line(line):
        return
    if body and body[-1] == line:
        return
    body.append(line)


def append_accessible_message(
    messages: list[MessageRecord],
    header: AccessibleHeader,
    body: list[str],
    status: str,
) -> None:
    message_text = normalize_text("\n".join(body))
    if not message_text and not status:
        return
    messages.append(
        MessageRecord(
            sender=header.sender,
            timestamp=header.timestamp,
            text=message_text,
            raw="\n".join([header.raw] + body + ([status] if status else [])),
            direction="outbound" if status else "inbound",
            status=status,
        )
    )


def dedupe_messages(messages: list[MessageRecord]) -> list[MessageRecord]:
    seen: set[tuple[str, str, str, str]] = set()
    result: list[MessageRecord] = []
    for message in messages:
        key = (message.sender, message.timestamp, message.text, message.status)
        if key in seen:
            continue
        seen.add(key)
        result.append(message)
    return result


def is_message_header(line: str) -> bool:
    return bool(re.search(r"\d{4}-\d{1,2}-\d{1,2}\s+\d{1,2}:\d{2}:\d{2}", line))


def build_message(header: str, body: list[str]) -> MessageRecord:
    match = re.search(r"(\d{4}-\d{1,2}-\d{1,2}\s+\d{1,2}:\d{2}:\d{2})", header)
    timestamp = match.group(1) if match else ""
    sender = header.replace(timestamp, "").strip() if timestamp else header
    status = ""
    content_lines: list[str] = []
    for line in body:
        if line in {READ_STATUS, UNREAD_STATUS}:
            status = line
            continue
        content_lines.append(line)

    message_text = normalize_text("\n".join(content_lines))
    direction = "outbound" if status else "inbound"
    return MessageRecord(
        sender=sender,
        timestamp=timestamp,
        text=message_text,
        raw="\n".join([header] + body),
        direction=direction,
        status=status,
    )


def normalize_text(value: str) -> str:
    lines = [line.strip() for line in value.replace("\r", "\n").split("\n")]
    lines = [line for line in lines if line]
    return "\n".join(lines)


def send_ctrl_key(letter: str) -> None:
    import win32api
    import win32con

    win32api.keybd_event(win32con.VK_CONTROL, 0, 0, 0)
    win32api.keybd_event(ord(letter.upper()), 0, 0, 0)
    win32api.keybd_event(ord(letter.upper()), 0, win32con.KEYEVENTF_KEYUP, 0)
    win32api.keybd_event(win32con.VK_CONTROL, 0, win32con.KEYEVENTF_KEYUP, 0)
    time.sleep(0.15)


def click_point(x: int, y: int) -> None:
    import win32api
    import win32con

    old = win32api.GetCursorPos()
    win32api.SetCursorPos((x, y))
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.03)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    win32api.SetCursorPos(old)
    time.sleep(0.08)


def get_clipboard_text() -> str | None:
    try:
        import win32clipboard
        import win32con
    except ImportError:
        return None
    try:
        win32clipboard.OpenClipboard()
        if not win32clipboard.IsClipboardFormatAvailable(win32con.CF_UNICODETEXT):
            return ""
        return str(win32clipboard.GetClipboardData(win32con.CF_UNICODETEXT))
    except Exception:
        return None
    finally:
        try:
            win32clipboard.CloseClipboard()
        except Exception:
            pass


def set_clipboard_text(text: str) -> None:
    import win32clipboard
    import win32con

    win32clipboard.OpenClipboard()
    try:
        win32clipboard.EmptyClipboard()
        win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT, text)
    finally:
        win32clipboard.CloseClipboard()


def _read_value_pattern(control: Any) -> str:
    try:
        pattern = control.GetValuePattern()
        if not pattern:
            return ""
        for attr in ("Value", "CurrentValue"):
            value = normalize_text(str(getattr(pattern, attr, "") or ""))
            if value:
                return value
    except Exception:
        return ""
    return ""


def _read_text_pattern(control: Any) -> str:
    try:
        pattern = control.GetTextPattern()
        if not pattern:
            return ""
        document_range = getattr(pattern, "DocumentRange", None)
        if not document_range:
            return ""
        get_text = getattr(document_range, "GetText", None)
        if not get_text:
            return ""
        return normalize_text(str(get_text(-1) or ""))
    except Exception:
        return ""


def _read_legacy_pattern(control: Any) -> list[str]:
    values: list[str] = []
    try:
        pattern = control.GetLegacyIAccessiblePattern()
        if not pattern:
            return values
        for attr in ("Name", "Value", "Description", "Help"):
            value = normalize_text(str(getattr(pattern, attr, "") or ""))
            if value:
                values.append(value)
    except Exception:
        return values
    return values


def _dedupe(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        result.append(value)
    return result
