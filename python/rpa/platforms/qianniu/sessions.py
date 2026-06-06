from __future__ import annotations

from dataclasses import dataclass
from typing import Any
import threading
import time

from PIL import ImageGrab

from .config import AppConfig, load_config
from .detector import QianniuDetector
from .qianniu_logging import get_logger
from .reader import collect_control_texts
from .uia import is_control_available, safe_prop, safe_rect, safe_rect_tuple, walk_controls

logger = get_logger(__name__)


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


class QianniuSessionReader:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QianniuDetector(self.config)
        self.last_chat_root: Any | None = None
        self.last_session_root: Any | None = None
        self.last_session_root_source: str = ""
        self._cache_thread_id: int | None = None

    def read_visible_sessions(self, limit: int = 50, detect_unread: bool = False) -> list[SessionItem]:
        total_started_at = time.perf_counter()
        stage_started_at = time.perf_counter()
        reused_chat_root = self._is_cached_chat_root_available()
        handle = None
        chat_root = self.last_chat_root if reused_chat_root else None
        if chat_root is None:
            handle = self.detector.find_current_chat()
            chat_root = handle.chat_root if handle else None
        find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
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
        root = self.last_session_root if reused_session_root else None
        root_source = self.last_session_root_source if reused_session_root else ""
        if root is None:
            root = self.detector.find_reception_normal_list(chat_root)
            root_source = "reception_normal_list"
            if not root:
                root = self.detector.find_chat_list_items_root(chat_root)
                root_source = "chat_list_items"
        find_root_ms = (time.perf_counter() - stage_started_at) * 1000.0
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
        items = extract_session_items(root, limit=limit)
        extract_ms = (time.perf_counter() - stage_started_at) * 1000.0
        visual_unread_ms = 0.0
        if detect_unread:
            stage_started_at = time.perf_counter()
            items = [with_visual_unread(item) for item in items]
            visual_unread_ms = (time.perf_counter() - stage_started_at) * 1000.0
        unread_count = sum(1 for item in items if item.unread)
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

    def _is_cache_thread_current(self) -> bool:
        return self._cache_thread_id == threading.get_ident()

    def _is_cached_chat_root_available(self) -> bool:
        return self._is_cache_thread_current() and is_control_available(self.last_chat_root)

    def _is_cached_session_root_available(self) -> bool:
        return self._is_cache_thread_current() and is_control_available(self.last_session_root)

    def find_session(self, title: str, detect_unread: bool = False) -> SessionItem | None:
        for item in self.read_visible_sessions(limit=100, detect_unread=detect_unread):
            if item.title == title:
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
