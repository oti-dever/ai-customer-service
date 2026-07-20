from __future__ import annotations

from dataclasses import dataclass
from typing import Any
import threading
import time

from PIL import ImageGrab

from .config import AppConfig, load_config
from .detector import QianniuDetector
from .qianniu_logging import get_listen_flow_logger, get_logger
from .reader import collect_control_texts
from .uia import control_from_hwnd, is_control_available, safe_prop, safe_rect, safe_rect_tuple, walk_controls

logger = get_logger(__name__)
listen_flow_logger = get_listen_flow_logger(__name__)


@dataclass(frozen=True)
class SessionItem:
    title: str
    control: Any
    rect: str
    rect_tuple: tuple[int, int, int, int] | None
    automation_id: str
    class_name: str
    control_type: str
    raw_texts: list[str]
    unread: bool = False
    unread_score: float = 0.0
    selected: bool = False


class QianniuSessionReader:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QianniuDetector(self.config)
        self.last_chat_root: Any | None = None
        self.last_session_root: Any | None = None
        self.last_session_root_source: str = ""
        self._cache_thread_id: int | None = None
        self._preferred_window_hwnd: int = 0

    def set_window_hwnd(self, hwnd: int) -> None:
        normalized = max(0, int(hwnd or 0))
        if normalized != self._preferred_window_hwnd:
            self.invalidate_cache()
        self._preferred_window_hwnd = normalized

    def preferred_window_hwnd(self) -> int:
        return self._preferred_window_hwnd

    def read_visible_sessions(self, limit: int = 50, detect_unread: bool = False) -> list[SessionItem]:
        total_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_reader_start limit=%s detect_unread=%s",
            limit,
            detect_unread,
        )
        stage_started_at = time.perf_counter()
        reused_chat_root = self._is_cached_chat_root_available()
        listen_flow_logger.info(
            "listen_flow session_reader_cache_state cache=chat_root available=%s elapsed_ms=%.1f",
            reused_chat_root,
            (time.perf_counter() - stage_started_at) * 1000.0,
        )
        handle = None
        chat_root = self.last_chat_root if reused_chat_root else None
        chat_root_source = "cache" if reused_chat_root else ""
        window_hwnd = self._preferred_window_hwnd
        if chat_root is None and window_hwnd:
            listen_flow_logger.info(
                "listen_flow session_reader_stage_start stage=find_chat_from_window hwnd=0x%X limit=%s detect_unread=%s",
                window_hwnd,
                limit,
                detect_unread,
            )
            chat_root = self._find_chat_root_from_window_hwnd(window_hwnd)
            chat_root_source = "window_hwnd" if chat_root else "window_hwnd_not_found"
            listen_flow_logger.info(
                "listen_flow session_reader_stage_done stage=find_chat_from_window elapsed_ms=%.1f hwnd=0x%X found_chat=%s",
                (time.perf_counter() - stage_started_at) * 1000.0,
                window_hwnd,
                bool(chat_root),
            )
        if chat_root is None and not window_hwnd:
            stage_started_at = time.perf_counter()
            listen_flow_logger.info(
                "listen_flow session_reader_stage_start stage=find_current_chat limit=%s detect_unread=%s",
                limit,
                detect_unread,
            )
            handle = self.detector.find_current_chat()
            chat_root = handle.chat_root if handle else None
            chat_root_source = "find_current_chat" if chat_root else ""
        find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow session_reader_stage_done stage=find_chat_root elapsed_ms=%.1f reused_chat_root=%s found_chat=%s source=%s",
            find_chat_ms,
            reused_chat_root,
            bool(chat_root),
            chat_root_source,
        )
        if not chat_root:
            self.last_chat_root = None
            self.last_session_root = None
            self.last_session_root_source = ""
            self._cache_thread_id = None
            logger.info(
                "qianniu session_scan_timing stage=total ms=%.1f find_chat_ms=%.1f found_chat=False reused_chat_root=%s limit=%s detect_unread=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                reused_chat_root,
                limit,
                detect_unread,
            )
            return []
        self.last_chat_root = chat_root
        self._cache_thread_id = threading.get_ident()

        stage_started_at = time.perf_counter()
        reused_session_root = self._is_cached_session_root_available()
        listen_flow_logger.info(
            "listen_flow session_reader_cache_state cache=session_root available=%s elapsed_ms=%.1f",
            reused_session_root,
            (time.perf_counter() - stage_started_at) * 1000.0,
        )
        root = self.last_session_root if reused_session_root else None
        root_source = self.last_session_root_source if reused_session_root else ""
        if root is None:
            listen_flow_logger.info(
                "listen_flow session_reader_stage_start stage=find_session_root limit=%s detect_unread=%s",
                limit,
                detect_unread,
            )
            root, root_source = find_session_root_from_chat(self.detector, chat_root)
        find_root_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow session_reader_stage_done stage=find_session_root elapsed_ms=%.1f reused_session_root=%s found_root=%s root_source=%s",
            find_root_ms,
            reused_session_root,
            bool(root),
            root_source,
        )
        if not root:
            self.last_session_root = None
            self.last_session_root_source = ""
            logger.info(
                "qianniu session_scan_timing stage=total ms=%.1f find_chat_ms=%.1f find_root_ms=%.1f found_root=False reused_chat_root=%s reused_session_root=%s limit=%s detect_unread=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                find_root_ms,
                reused_chat_root,
                reused_session_root,
                limit,
                detect_unread,
            )
            return []
        self.last_session_root = root
        self.last_session_root_source = root_source
        self._cache_thread_id = threading.get_ident()

        stage_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_reader_stage_start stage=extract_session_items limit=%s detect_unread=%s root_source=%s",
            limit,
            detect_unread,
            root_source,
        )
        items = extract_session_items(root, limit=limit)
        extract_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow session_reader_stage_done stage=extract_session_items elapsed_ms=%.1f session_count=%s",
            extract_ms,
            len(items),
        )
        visual_unread_ms = 0.0
        if detect_unread:
            stage_started_at = time.perf_counter()
            listen_flow_logger.info(
                "listen_flow session_reader_stage_start stage=visual_unread session_count=%s",
                len(items),
            )
            items = [with_visual_unread(item) for item in items]
            visual_unread_ms = (time.perf_counter() - stage_started_at) * 1000.0
            listen_flow_logger.info(
                "listen_flow session_reader_stage_done stage=visual_unread elapsed_ms=%.1f unread_count=%s",
                visual_unread_ms,
                sum(1 for item in items if item.unread),
            )
        unread_count = sum(1 for item in items if item.unread)
        listen_flow_logger.info(
            "listen_flow session_reader_done elapsed_ms=%.1f root_source=%s session_count=%s unread_count=%s",
            (time.perf_counter() - total_started_at) * 1000.0,
            root_source,
            len(items),
            unread_count,
        )
        logger.info(
            "qianniu session_scan_timing stage=total ms=%.1f find_chat_ms=%.1f find_root_ms=%.1f extract_ms=%.1f visual_unread_ms=%.1f root_source=%s reused_chat_root=%s reused_session_root=%s limit=%s detect_unread=%s session_count=%s unread_count=%s",
            (time.perf_counter() - total_started_at) * 1000.0,
            find_chat_ms,
            find_root_ms,
            extract_ms,
            visual_unread_ms,
            root_source,
            reused_chat_root,
            reused_session_root,
            limit,
            detect_unread,
            len(items),
            unread_count,
        )
        return items

    def current_chat_root(self) -> Any | None:
        return self.last_chat_root if self._is_cached_chat_root_available() else None

    def preferred_window_root(self, *, update_cache: bool = False) -> Any | None:
        hwnd = self._preferred_window_hwnd
        if not hwnd:
            listen_flow_logger.info(
                "listen_flow session_reader_stage_done stage=find_preferred_window_root hwnd=0x0 found_root=False reason=no_preferred_window"
            )
            return None

        started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_reader_stage_start stage=find_preferred_window_root hwnd=0x%X update_cache=%s",
            hwnd,
            update_cache,
        )
        root = control_from_hwnd(hwnd)
        if root is not None and update_cache:
            self._update_chat_root_cache(root)
        listen_flow_logger.info(
            "listen_flow session_reader_stage_done stage=find_preferred_window_root hwnd=0x%X found_root=%s update_cache=%s elapsed_ms=%.1f",
            hwnd,
            bool(root),
            update_cache,
            (time.perf_counter() - started_at) * 1000.0,
        )
        return root

    def chat_root_from_preferred_window(self, *, update_cache: bool = True) -> Any | None:
        hwnd = self._preferred_window_hwnd
        if not hwnd:
            listen_flow_logger.info(
                "listen_flow session_reader_stage_done stage=find_chat_from_preferred_window hwnd=0x0 found_chat=False reason=no_preferred_window"
            )
            return None

        started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_reader_stage_start stage=find_chat_from_preferred_window hwnd=0x%X update_cache=%s",
            hwnd,
            update_cache,
        )
        chat_root = self._find_chat_root_from_window_hwnd(hwnd)
        if chat_root is not None and update_cache:
            self._update_chat_root_cache(chat_root)
        listen_flow_logger.info(
            "listen_flow session_reader_stage_done stage=find_chat_from_preferred_window hwnd=0x%X found_chat=%s update_cache=%s elapsed_ms=%.1f",
            hwnd,
            bool(chat_root),
            update_cache,
            (time.perf_counter() - started_at) * 1000.0,
        )
        return chat_root

    def invalidate_cache(self) -> None:
        self.last_chat_root = None
        self.last_session_root = None
        self.last_session_root_source = ""
        self._cache_thread_id = None

    def _find_chat_root_from_window_hwnd(self, hwnd: int) -> Any | None:
        started_at = time.perf_counter()
        root = control_from_hwnd(hwnd)
        if root is None:
            listen_flow_logger.info(
                "listen_flow session_reader_window_root_unavailable hwnd=0x%X elapsed_ms=%.1f",
                hwnd,
                (time.perf_counter() - started_at) * 1000.0,
            )
            return None
        chat_root = find_chat_root_from_window(self.detector, root)
        listen_flow_logger.info(
            "listen_flow session_reader_window_chat_root_done hwnd=0x%X found_chat=%s elapsed_ms=%.1f",
            hwnd,
            bool(chat_root),
            (time.perf_counter() - started_at) * 1000.0,
        )
        return chat_root

    def _update_chat_root_cache(self, chat_root: Any) -> None:
        current_thread_id = threading.get_ident()
        if self._cache_thread_id != current_thread_id:
            self.last_session_root = None
            self.last_session_root_source = ""
        self.last_chat_root = chat_root
        self._cache_thread_id = current_thread_id

    def _is_cache_thread_current(self) -> bool:
        return self._cache_thread_id == threading.get_ident()

    def _is_cached_chat_root_available(self) -> bool:
        return self._is_cache_thread_current() and is_control_available(self.last_chat_root)

    def _is_cached_session_root_available(self) -> bool:
        return self._is_cache_thread_current() and is_control_available(self.last_session_root)

    def find_session(self, title: str, detect_unread: bool = False) -> SessionItem | None:
        for item in self.read_visible_sessions(limit=100, detect_unread=detect_unread):
            if session_titles_match(item.title, title):
                return item
        return None

    def selected_session(self, *, fresh: bool = False) -> SessionItem | None:
        if fresh:
            self.invalidate_cache()
        for item in self.read_visible_sessions(limit=100, detect_unread=False):
            if item.selected:
                return item
        return None

    def select_session(self, item: SessionItem) -> tuple[bool, str]:
        started_at = time.perf_counter()
        if click_session_rect(item):
            logger.info(
                "qianniu select_session_timing display_name=%s method=rect_click ok=True ms=%.1f",
                item.title,
                (time.perf_counter() - started_at) * 1000.0,
            )
            return True, "rect_click"
        if click_session_control(item.control):
            logger.info(
                "qianniu select_session_timing display_name=%s method=uia ok=True ms=%.1f",
                item.title,
                (time.perf_counter() - started_at) * 1000.0,
            )
            return True, "uia"
        logger.info(
            "qianniu select_session_timing display_name=%s method=failed ok=False ms=%.1f",
            item.title,
            (time.perf_counter() - started_at) * 1000.0,
        )
        return False, "failed"

    def select_first_unread(self) -> tuple[SessionItem | None, bool, str]:
        sessions = self.read_visible_sessions(limit=100, detect_unread=True)
        for item in sessions:
            if item.unread:
                ok, method = self.select_session(item)
                return item, ok, method
        return None, False, "no_unread"


def find_chat_root_from_window(detector: QianniuDetector, root: Any) -> Any | None:
    best: tuple[int, str, int, Any] | None = None
    for depth, control in walk_controls(root, max_depth=detector.q.max_tree_depth, max_nodes=detector.q.max_tree_nodes):
        try:
            score, reason = detector._score_chat_root_candidate(control, depth)
            if detector._is_definitive_chat_root(control, score):
                listen_flow_logger.info(
                    "listen_flow session_reader_chat_root_candidate source=window definitive=True score=%s reason=%s depth=%s",
                    score,
                    reason,
                    depth,
                )
                return control
            if score > 0 and (best is None or score > best[0]):
                best = (score, reason, depth, control)
        except Exception:
            continue

    if best is not None:
        score, reason, depth, control = best
        listen_flow_logger.info(
            "listen_flow session_reader_chat_root_candidate source=window definitive=False score=%s reason=%s depth=%s",
            score,
            reason,
            depth,
        )
        return control

    listen_flow_logger.info("listen_flow session_reader_chat_root_candidate source=window fallback=window_root")
    return root


def find_session_root_from_chat(detector: QianniuDetector, chat_root: Any) -> tuple[Any | None, str]:
    chat_list_items_fallback: tuple[int, Any] | None = None
    reception_view_fallback: tuple[int, Any] | None = None
    chat_list_view_fallback: tuple[int, Any] | None = None
    nodes = walk_controls(chat_root, max_depth=detector.q.max_tree_depth, max_nodes=detector.q.max_tree_nodes)
    for depth, control in nodes:
        aid = safe_prop(control, "AutomationId")
        if aid == detector.q.reception_normal_list_suffix or aid.endswith(detector.q.reception_normal_list_suffix):
            listen_flow_logger.info(
                "listen_flow session_reader_session_root_candidate source=reception_normal_list depth=%s",
                depth,
            )
            return control, "reception_normal_list"
        if chat_list_items_fallback is None and _looks_like_chat_list_items_root(detector, aid):
            chat_list_items_fallback = (depth, control)
        if reception_view_fallback is None and _looks_like_reception_list_view(control, aid):
            reception_view_fallback = (depth, control)
        if chat_list_view_fallback is None and _looks_like_chat_list_view(control, aid):
            chat_list_view_fallback = (depth, control)

    if reception_view_fallback is not None:
        depth, control = reception_view_fallback
        listen_flow_logger.info(
            "listen_flow session_reader_session_root_candidate source=reception_list_view_fallback depth=%s",
            depth,
        )
        return control, "reception_list_view_fallback"
    if chat_list_items_fallback is not None:
        depth, control = chat_list_items_fallback
        listen_flow_logger.info(
            "listen_flow session_reader_session_root_candidate source=chat_list_items depth=%s",
            depth,
        )
        return control, "chat_list_items"
    if chat_list_view_fallback is not None:
        depth, control = chat_list_view_fallback
        listen_flow_logger.info(
            "listen_flow session_reader_session_root_candidate source=chat_list_view_fallback depth=%s",
            depth,
        )
        return control, "chat_list_view_fallback"
    _log_session_root_not_found(detector, chat_root, nodes)
    return None, ""


def _looks_like_chat_list_items_root(detector: QianniuDetector, aid: str) -> bool:
    if not aid:
        return False
    if aid == detector.q.chat_list_items_suffix or aid.endswith(detector.q.chat_list_items_suffix):
        return True
    return "ChatListWidget.ChatListView.centralwidget.list_widget" in aid


def _looks_like_reception_list_view(control: Any, aid: str) -> bool:
    class_name = safe_prop(control, "ClassName")
    return class_name == "ReceptionListView" or aid.endswith(".ReceptionListView") or ".ReceptionListView." in aid


def _looks_like_chat_list_view(control: Any, aid: str) -> bool:
    class_name = safe_prop(control, "ClassName")
    return class_name == "ChatListView" or aid.endswith(".ChatListWidget.ChatListView")


def _log_session_root_not_found(detector: QianniuDetector, chat_root: Any, nodes: list[tuple[int, Any]]) -> None:
    aid_fragments = [
        detector.q.reception_normal_list_suffix,
        detector.q.chat_list_items_suffix,
        "ReceptionListView",
        "normalList",
        "ChatListWidget",
        "ChatListView",
        "list_widget",
        "SubChatView",
    ]
    aid_fragment_keys = [fragment.lower() for fragment in aid_fragments if fragment]
    aid_candidates: list[dict[str, Any]] = []
    list_candidates: list[dict[str, Any]] = []
    aid_candidate_count = 0
    list_candidate_count = 0

    for depth, control in nodes:
        aid = safe_prop(control, "AutomationId")
        class_name = safe_prop(control, "ClassName")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        haystack = f"{aid}|{class_name}|{control_type}".lower()
        is_aid_candidate = any(fragment in haystack for fragment in aid_fragment_keys)
        is_list_candidate = (
            "list" in control_type.lower()
            or "tree" in control_type.lower()
            or "datagrid" in control_type.lower()
            or "list" in class_name.lower()
            or "tree" in class_name.lower()
        )
        if is_aid_candidate:
            aid_candidate_count += 1
            if len(aid_candidates) < 12:
                aid_candidates.append(_control_log_sample(depth, control))
        if is_list_candidate:
            list_candidate_count += 1
            if len(list_candidates) < 12:
                list_candidates.append(_control_log_sample(depth, control))

    listen_flow_logger.info(
        "listen_flow session_reader_session_root_not_found node_count=%s max_nodes=%s scan_limited=%s chat_root=%s aid_candidate_count=%s aid_candidates=%s list_candidate_count=%s list_candidates=%s",
        len(nodes),
        detector.q.max_tree_nodes,
        len(nodes) >= detector.q.max_tree_nodes,
        _control_log_sample(0, chat_root),
        aid_candidate_count,
        aid_candidates,
        list_candidate_count,
        list_candidates,
    )


def _control_log_sample(depth: int, control: Any) -> dict[str, Any]:
    return {
        "depth": depth,
        "automation_id": _trim_log_value(safe_prop(control, "AutomationId"), 180),
        "name": _trim_log_value(safe_prop(control, "Name"), 80),
        "class_name": _trim_log_value(safe_prop(control, "ClassName"), 80),
        "control_type": _trim_log_value(
            safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
            80,
        ),
        "rect": safe_rect(control),
    }


def _trim_log_value(value: Any, max_len: int) -> str:
    text = "" if value is None else str(value)
    text = text.replace("\r", " ").replace("\n", " ").strip()
    return text if len(text) <= max_len else text[: max_len - 3] + "..."


def extract_session_items(root: Any, limit: int) -> list[SessionItem]:
    tree_items = [
        control
        for _, control in walk_controls(root, max_depth=5, max_nodes=1500)
        if is_tree_item(control)
    ]
    source_controls = tree_items if tree_items else [control for _, control in walk_controls(root, max_depth=5, max_nodes=1500)]

    items: list[SessionItem] = []
    seen_titles: set[str] = set()
    for control in source_controls:
        texts = collect_texts_for_session(control)
        titles = [text for text in texts if looks_like_session_title(text)]
        if not titles:
            continue
        title = titles[0]
        if title in seen_titles:
            continue
        seen_titles.add(title)
        items.append(
            SessionItem(
                title=title,
                control=control,
                rect=safe_rect(control),
                rect_tuple=safe_rect_tuple(control),
                automation_id=safe_prop(control, "AutomationId"),
                class_name=safe_prop(control, "ClassName"),
                control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
                raw_texts=texts,
                selected=is_session_selected(control),
            )
        )
        if len(items) >= limit:
            break

    return items


def with_visual_unread(item: SessionItem, threshold: float = 0.006) -> SessionItem:
    started_at = time.perf_counter()
    score = visual_unread_score(item)
    elapsed_ms = (time.perf_counter() - started_at) * 1000.0
    logger.debug(
        "qianniu visual_unread_timing display_name=%s ms=%.1f score=%.5f threshold=%.5f unread=%s",
        item.title,
        elapsed_ms,
        score,
        threshold,
        score >= threshold,
    )
    return SessionItem(
        title=item.title,
        control=item.control,
        rect=item.rect,
        rect_tuple=item.rect_tuple,
        automation_id=item.automation_id,
        class_name=item.class_name,
        control_type=item.control_type,
        raw_texts=item.raw_texts,
        unread=score >= threshold,
        unread_score=score,
        selected=item.selected,
    )


def visual_unread_score(item: SessionItem) -> float:
    if not item.rect_tuple:
        return 0.0

    left, top, right, bottom = item.rect_tuple
    width = max(1, right - left)
    height = max(1, bottom - top)
    probe_rect = (
        int(left + width * 0.38),
        top,
        right,
        int(top + height * 0.55),
    )
    image = ImageGrab.grab(bbox=probe_rect).convert("RGB")
    red_pixels = count_red_pixels(image)
    total = max(1, image.width * image.height)
    return red_pixels / total


def count_red_pixels(image: object) -> int:
    get_pixels = getattr(image, "get_flattened_data", None) or getattr(image, "getdata")
    count = 0
    for r, g, b in get_pixels():
        if r >= 180 and g <= 110 and b <= 110 and r >= g * 1.45 and r >= b * 1.45:
            count += 1
    return count


def click_session_control(control: Any) -> bool:
    for method_name in ("GetSelectionItemPattern", "GetInvokePattern"):
        try:
            method = getattr(control, method_name, None)
            if not method:
                continue
            pattern = method()
            if not pattern:
                continue
            if method_name == "GetSelectionItemPattern":
                pattern.Select()
            else:
                pattern.Invoke()
            time.sleep(0.2)
            return True
        except Exception:
            continue
    return False


def session_titles_match(left: str, right: str) -> bool:
    return normalize_session_title(left) == normalize_session_title(right)


def normalize_session_title(value: str) -> str:
    return " ".join(str(value or "").strip().split()).lower()


def is_session_selected(control: Any) -> bool:
    try:
        getter = getattr(control, "GetSelectionItemPattern", None)
        if callable(getter):
            pattern = getter()
            if pattern:
                selected = getattr(pattern, "IsSelected", None)
                if callable(selected):
                    selected = selected()
                if selected is not None:
                    return bool(selected)
    except Exception:
        pass
    try:
        value = safe_prop(control, "IsSelected")
        if isinstance(value, str):
            return value.strip().lower() in {"1", "true", "yes", "selected"}
        return bool(value)
    except Exception:
        return False


def click_session_rect(item: SessionItem) -> bool:
    if not item.rect_tuple:
        return False
    left, top, right, bottom = item.rect_tuple
    width = max(1, right - left)
    x = int(left + min(width * 0.35, 90))
    y = int((top + bottom) / 2)
    try:
        import win32api
        import win32con

        old = win32api.GetCursorPos()
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.04)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        win32api.SetCursorPos(old)
        time.sleep(0.25)
        return True
    except Exception:
        return False


def is_tree_item(control: Any) -> bool:
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    return "TreeItem" in control_type


def collect_texts_for_session(control: Any) -> list[str]:
    texts: list[str] = []
    seen: set[str] = set()
    for _, child in walk_controls(control, max_depth=3, max_nodes=120):
        for text in collect_control_texts(child):
            text = text.strip()
            if not text or text in seen:
                continue
            seen.add(text)
            texts.append(text)
    return texts


def looks_like_session_title(text: str) -> bool:
    if not text:
        return False
    if text in {"\u5df2\u8bfb", "\u672a\u8bfb", "\u641c\u7d22\u6846", "\u5237\u65b0"}:
        return False
    if len(text) > 80:
        return False
    if text.startswith("UIWindow."):
        return False
    return True
