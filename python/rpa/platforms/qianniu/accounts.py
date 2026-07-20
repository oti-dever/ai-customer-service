from __future__ import annotations

import hashlib
import re
import threading
import time
import warnings
from collections import deque
from dataclasses import dataclass
from typing import Any, Callable

from .config import AppConfig, load_config
from .detector import QianniuDetector, WindowCandidate
from .qianniu_logging import get_logger
from .reader import collect_control_texts
from .uia import control_from_hwnd, enum_all_top_level_windows, safe_prop, safe_rect, safe_rect_tuple

logger = get_logger(__name__)

DEFAULT_ACCOUNT_ID = "local_qianniu"
AccountSwitchCallback = Callable[[], None]
ProgressLogger = Callable[[str], None]
ACCOUNT_UNREAD_VISUAL_SCORE_THRESHOLD = 0.10
ACCOUNT_UNREAD_MIN_RED_PIXELS = 8

BLOCKED_TAB_TEXTS = {
    "",
    "+",
    "x",
    "X",
    "\u00d7",
    "\u5ba2\u670d",
    "\u8bbe\u7f6e",
    "\u5c55\u5f00",
    "\u64cd\u4f5c\u6307\u5357",
    "\u5173\u95ed",
    "\u6700\u5c0f\u5316",
    "\u6700\u5927\u5316",
    "\u8fd8\u539f",
    "\u6d88\u606f",
    "\u5de5\u4f5c\u53f0",
    "\u8fdb\u5e97",
    "\u5de5\u5355",
    "\u6253\u5355\u5de5\u5177",
    "\u8db3\u8ff9",
    "\u63a8\u8350",
    "\u9080\u8bf7\u5173\u6ce8",
    "\u9080\u8bf7\u5165\u4f1a",
    "\u9080\u8bf7\u5165\u7fa4",
    "\u53d1\u4f18\u60e0\u5238",
    "\u6dfb\u52a0\u5907\u6ce8",
}

BLOCKED_TEXT_FRAGMENTS = {
    "UIWindow.",
    "web_chat-packer",
    "http://",
    "https://",
}


@dataclass(frozen=True)
class AccountUnreadHint:
    has_unread_hint: bool
    unread_badge_text: str = ""
    unread_elapsed_text: str = ""
    unread_hint_rect: tuple[int, int, int, int] | None = None
    unread_score: float = 0.0


@dataclass(frozen=True)
class AccountTab:
    account_id: str
    display_name: str
    selected: bool
    rect: str
    rect_tuple: tuple[int, int, int, int] | None
    control: Any | None
    source: str
    confidence: float
    raw_texts: list[str]
    automation_id: str = ""
    class_name: str = ""
    control_type: str = ""
    depth: int = 0
    index: int = 0
    visual_blue_ratio: float = 0.0
    has_unread_hint: bool = False
    unread_badge_text: str = ""
    unread_elapsed_text: str = ""
    unread_hint_rect: tuple[int, int, int, int] | None = None
    unread_score: float = 0.0


class QianniuAccountReader:
    def __init__(
        self,
        config: AppConfig | None = None,
        *,
        detector: QianniuDetector | None = None,
        on_switched: AccountSwitchCallback | None = None,
        wait_seconds: float = 0.7,
        max_depth: int = 10,
        max_nodes: int = 3000,
        min_confidence: float = 0.35,
        candidate_limit: int = 20,
        allow_synthetic_single_account: bool = True,
        activate_before_scan: bool = False,
    ) -> None:
        self.config = config or load_config()
        self.detector = detector or QianniuDetector(self.config)
        self.on_switched = on_switched
        self.wait_seconds = max(0.1, float(wait_seconds))
        self.max_depth = max(1, int(max_depth))
        self.max_nodes = max(1, int(max_nodes))
        self.min_confidence = max(0.0, float(min_confidence))
        self.candidate_limit = max(1, int(candidate_limit))
        self.allow_synthetic_single_account = allow_synthetic_single_account
        self.activate_before_scan = bool(activate_before_scan)
        self._accounts_cache: list[AccountTab] | None = None
        self._cache_hwnd: int = 0
        self._cache_thread_id: int | None = None

    def list_accounts(self, *, fresh: bool = False) -> list[AccountTab]:
        if not fresh and self._is_cache_current() and self._accounts_cache is not None:
            logger.info(
                "qianniu account_probe_timing cache_hit=True accounts=%s hwnd=0x%X",
                len(self._accounts_cache),
                self._cache_hwnd,
            )
            return list(self._accounts_cache)

        total_started_at = time.perf_counter()
        stage_started_at = time.perf_counter()
        window = self._selected_window()
        selected_window_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if window is None:
            self.invalidate_cache()
            logger.info(
                "qianniu account_probe_timing cache_hit=False accounts=0 hwnd=0x0 selected_window_ms=%.1f total_ms=%.1f reason=no_window",
                selected_window_ms,
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return []

        stage_started_at = time.perf_counter()
        root = control_from_hwnd(window.hwnd)
        control_from_hwnd_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if root is None:
            self.invalidate_cache()
            logger.info(
                "qianniu account_probe_timing cache_hit=False accounts=0 hwnd=0x%X selected_window_ms=%.1f control_from_hwnd_ms=%.1f total_ms=%.1f reason=no_root",
                int(window.hwnd),
                selected_window_ms,
                control_from_hwnd_ms,
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return []

        activated = False
        activate_ms = 0.0
        if self.activate_before_scan:
            stage_started_at = time.perf_counter()
            activated = activate_window(window.hwnd)
            activate_ms = (time.perf_counter() - stage_started_at) * 1000.0
        stage_started_at = time.perf_counter()
        top_image = grab_top_image(root)
        top_image_ms = (time.perf_counter() - stage_started_at) * 1000.0
        stage_started_at = time.perf_counter()
        accounts = detect_account_candidates(
            root,
            top_image=top_image,
            max_depth=self.max_depth,
            max_nodes=self.max_nodes,
            min_confidence=self.min_confidence,
            limit=self.candidate_limit,
        )
        detect_ms = (time.perf_counter() - stage_started_at) * 1000.0
        synthetic_ms = 0.0
        synthetic_used = False
        if not accounts and self.allow_synthetic_single_account:
            stage_started_at = time.perf_counter()
            has_tab_container = has_account_tab_container(
                root,
                max_depth=self.max_depth,
                max_nodes=self.max_nodes,
            )
            synthetic_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if not has_tab_container:
                accounts = [synthetic_single_account()]
                synthetic_used = True

        self._accounts_cache = list(accounts)
        self._cache_hwnd = int(window.hwnd)
        self._cache_thread_id = threading.get_ident()
        logger.info(
            "qianniu account_probe_timing cache_hit=False accounts=%s hwnd=0x%X activated=%s top_image=%s synthetic_used=%s selected_window_ms=%.1f control_from_hwnd_ms=%.1f activate_ms=%.1f top_image_ms=%.1f detect_ms=%.1f synthetic_ms=%.1f total_ms=%.1f",
            len(accounts),
            self._cache_hwnd,
            activated,
            bool(top_image),
            synthetic_used,
            selected_window_ms,
            control_from_hwnd_ms,
            activate_ms,
            top_image_ms,
            detect_ms,
            synthetic_ms,
            (time.perf_counter() - total_started_at) * 1000.0,
        )
        return list(accounts)

    def selected_account(self, *, fresh: bool = False) -> AccountTab | None:
        return selected_account(self.list_accounts(fresh=fresh))

    def cached_window_hwnd(self) -> int:
        return self._cache_hwnd if self._is_cache_current() else 0

    def switch_account(self, account_id: str) -> tuple[bool, str]:
        started_at = time.perf_counter()
        query = normalize_candidate_text(account_id)
        if not query:
            return False, "empty_account_id"

        accounts = self.list_accounts(fresh=False)
        target, match_mode, error = resolve_account(accounts, query)
        if target is None:
            return False, error or "account_not_found"
        if target.source == "synthetic_single_account" or target.selected:
            return True, "already_selected"
        if not target.rect_tuple:
            return False, "missing_rect"

        hwnd = self._cache_hwnd
        clicked, click_method = click_account_tab(target, hwnd=hwnd, activate=True)
        if not clicked:
            return False, click_method

        time.sleep(self.wait_seconds)
        self.invalidate_cache()
        fresh_accounts = self.list_accounts(fresh=True)
        active = selected_account(fresh_accounts)
        verified = bool(active and active.account_id == target.account_id)
        elapsed_ms = (time.perf_counter() - started_at) * 1000.0
        logger.info(
            "qianniu switch_account_timing account_id=%s display_name=%s match_mode=%s method=%s verified=%s active=%s ms=%.1f",
            target.account_id,
            target.display_name,
            match_mode,
            click_method,
            verified,
            active.account_id if active else "",
            elapsed_ms,
        )
        if not verified:
            return False, f"switch_not_verified:{active.account_id if active else 'none'}"
        if self.on_switched:
            self.on_switched()
        return True, click_method

    def invalidate_cache(self) -> None:
        self._accounts_cache = None
        self._cache_hwnd = 0
        self._cache_thread_id = None

    def _selected_window(self) -> WindowCandidate | None:
        windows = find_window_candidates_win32_only(self.detector)
        return windows[0] if windows else None

    def _is_cache_current(self) -> bool:
        return self._cache_thread_id == threading.get_ident()


def find_window_candidates_win32_only(detector: QianniuDetector) -> list[WindowCandidate]:
    process_ids = set(detector.find_process_ids())
    windows = enum_all_top_level_windows()
    matches: list[tuple[WindowCandidate, int]] = []
    for item in windows:
        if process_ids and item.pid not in process_ids:
            continue
        if not item.visible:
            continue
        score, reason = detector.score_window_candidate(item.class_name, item.title)
        if score <= 0:
            score = 1
            reason = reason or "pid-match"
        matches.append(
            (
                WindowCandidate(
                    hwnd=item.hwnd,
                    pid=item.pid,
                    class_name=item.class_name,
                    title=item.title,
                    score=score,
                    reason=reason,
                ),
                window_area(item.rect),
            )
        )
    matches.sort(key=lambda pair: (pair[0].score, pair[1]), reverse=True)
    return [item for item, _area in matches]


def window_area(rect: tuple[int, int, int, int] | None) -> int:
    if not rect:
        return 0
    left, top, right, bottom = rect
    return max(0, int(right) - int(left)) * max(0, int(bottom) - int(top))


def detect_account_tabs(
    root: Any,
    *,
    top_image: Any | None,
    max_depth: int,
    max_nodes: int,
    min_confidence: float,
    limit: int,
) -> list[AccountTab]:
    return detect_account_candidates(
        root,
        top_image=top_image,
        max_depth=max_depth,
        max_nodes=max_nodes,
        min_confidence=min_confidence,
        limit=limit,
    )


def detect_account_candidates(
    root: Any,
    *,
    top_image: Any | None,
    max_depth: int,
    max_nodes: int,
    min_confidence: float,
    limit: int,
    progress: ProgressLogger | None = None,
) -> list[AccountTab]:
    log = progress or noop_progress
    root_rect = safe_rect_tuple(root)
    if not root_rect:
        return []

    candidates: list[AccountTab] = []
    for index, (depth, control) in enumerate(
        iter_controls(root, max_depth=max_depth, max_nodes=max_nodes, progress=log, label="account candidate scan")
    ):
        rect_tuple = safe_rect_tuple(control)
        if not rect_tuple or not is_top_tab_rect(rect_tuple, root_rect):
            continue
        texts = collect_texts_for_candidate(control)
        display_name = choose_account_display_name(texts)
        if not display_name:
            continue
        visual_blue_ratio = account_rect_blue_ratio(top_image, rect_tuple, root_rect)
        score = score_account_candidate(control, rect_tuple, root_rect, display_name, depth, visual_blue_ratio)
        if score < min_confidence:
            continue
        unread_hint = detect_account_unread_hint(control, top_image, rect_tuple, root_rect, display_name)
        candidates.append(
            AccountTab(
                account_id=stable_account_id(display_name),
                display_name=display_name,
                selected=is_selected_control(control) or visual_blue_ratio >= 0.12,
                rect=safe_rect(control),
                rect_tuple=rect_tuple,
                control=control,
                source="uia",
                confidence=round(score, 3),
                raw_texts=texts,
                automation_id=safe_prop(control, "AutomationId"),
                class_name=safe_prop(control, "ClassName"),
                control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
                depth=depth,
                index=index,
                visual_blue_ratio=round(visual_blue_ratio, 4),
                has_unread_hint=unread_hint.has_unread_hint,
                unread_badge_text=unread_hint.unread_badge_text,
                unread_elapsed_text=unread_hint.unread_elapsed_text,
                unread_hint_rect=unread_hint.unread_hint_rect,
                unread_score=round(unread_hint.unread_score, 4),
            )
        )
    return dedupe_account_tabs(candidates)[:limit]


def has_account_tab_container(root: Any, *, max_depth: int, max_nodes: int) -> bool:
    for _depth, control in iter_controls(root, max_depth=max_depth, max_nodes=max_nodes):
        text = control_descriptor(control)
        if "mutilaccounttabview" in text or "multipleaccounttabview" in text:
            return True
    return False


def score_account_candidate(
    control: Any,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
    display_name: str,
    depth: int,
    visual_blue_ratio: float,
) -> float:
    left, top, right, bottom = rect
    root_left, root_top, root_right, _root_bottom = root_rect
    root_width = max(1, root_right - root_left)
    width = max(1, right - left)
    height = max(1, bottom - top)
    score = 0.0

    if root_top - 4 <= top <= root_top + 54:
        score += 0.18
    if 35 <= width <= min(320, root_width * 0.45):
        score += 0.16
    if 18 <= height <= 46:
        score += 0.14
    if left <= root_left + root_width * 0.55:
        score += 0.12
    if visual_blue_ratio >= 0.12:
        score += 0.18

    control_text = control_descriptor(control)
    if "tab" in control_text:
        score += 0.18
    if "button" in control_text:
        score += 0.08
    if ":" in display_name or "\uff1a" in display_name:
        score += 0.08
    if re.search(r"[\u4e00-\u9fffA-Za-z0-9]", display_name):
        score += 0.08
    if depth > 8:
        score -= 0.08
    return max(0.0, min(score, 1.0))


def detect_account_unread_hint(
    control: Any,
    top_image: Any | None,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
    display_name: str,
) -> AccountUnreadHint:
    badge_text, elapsed_text = extract_unread_hint_texts(control, display_name)
    visual_rect, visual_score = account_rect_red_hint(top_image, rect, root_rect)
    has_hint = bool(badge_text or elapsed_text or visual_score >= ACCOUNT_UNREAD_VISUAL_SCORE_THRESHOLD)
    return AccountUnreadHint(
        has_unread_hint=has_hint,
        unread_badge_text=badge_text,
        unread_elapsed_text=elapsed_text,
        unread_hint_rect=visual_rect,
        unread_score=max(visual_score, 0.35 if badge_text or elapsed_text else 0.0),
    )


def extract_unread_hint_texts(control: Any, display_name: str) -> tuple[str, str]:
    display_key = normalize_account_display_name(display_name)
    badge_text = ""
    elapsed_text = ""
    seen: set[str] = set()
    for _depth, item in iter_controls(control, max_depth=2, max_nodes=80):
        for value in collect_texts_for_candidate(item):
            text = normalize_candidate_text(value)
            if not text or text in seen or normalize_account_display_name(text) == display_key:
                continue
            seen.add(text)
            if re.fullmatch(r"\d{1,3}", text):
                badge_text = badge_text or text
                continue
            if re.fullmatch(r"\d{1,4}\s*(秒|s|S)", text):
                elapsed_text = elapsed_text or re.sub(r"\s+", "", text)
                continue
            if re.fullmatch(r"\d{1,3}\s+\d{1,4}\s*(秒|s|S)", text):
                parts = re.split(r"\s+", text.strip(), maxsplit=1)
                badge_text = badge_text or parts[0]
                elapsed_text = elapsed_text or re.sub(r"\s+", "", parts[1])
    return badge_text, elapsed_text


def collect_texts_for_candidate(control: Any) -> list[str]:
    values: list[str] = []
    for value in collect_control_texts(control):
        normalized = normalize_candidate_text(value)
        if normalized and normalized not in values:
            values.append(normalized)
    for attr in ("Name", "Value", "LegacyIAccessibleName", "HelpText"):
        normalized = normalize_candidate_text(safe_prop(control, attr))
        if normalized and normalized not in values:
            values.append(normalized)
    return values


def choose_account_display_name(texts: list[str]) -> str:
    for text in texts:
        candidate = normalize_account_display_name(text)
        if looks_like_account_tab_text(candidate):
            return candidate
    return ""


def normalize_candidate_text(value: Any) -> str:
    text = str(value or "").replace("\r", " ").replace("\n", " ")
    return re.sub(r"\s+", " ", text).strip()


def normalize_account_display_name(value: Any) -> str:
    text = normalize_candidate_text(value)
    text = text.strip(" \t\r\n")
    text = re.sub(r"\s*[\u00d7xX]\s*$", "", text).strip()
    return re.sub(r"\s+", " ", text).strip()[:80]


def looks_like_account_tab_text(value: str) -> bool:
    text = normalize_account_display_name(value)
    if not text or text in BLOCKED_TAB_TEXTS:
        return False
    if any(fragment in text for fragment in BLOCKED_TEXT_FRAGMENTS):
        return False
    if len(text) < 2 or len(text) > 80:
        return False
    if text.isdigit():
        return False
    if re.fullmatch(r"[\W_]+", text, flags=re.UNICODE):
        return False
    if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text):
        return False
    if text in {"true", "false", "None"}:
        return False
    return True


def stable_account_id(display_name: str) -> str:
    normalized = normalize_account_display_name(display_name).lower()
    digest = hashlib.sha1(normalized.encode("utf-8", errors="ignore")).hexdigest()[:12]
    return f"qnacct_{digest}"


def synthetic_single_account() -> AccountTab:
    return AccountTab(
        account_id=DEFAULT_ACCOUNT_ID,
        display_name=DEFAULT_ACCOUNT_ID,
        selected=True,
        rect="-",
        rect_tuple=None,
        control=None,
        source="synthetic_single_account",
        confidence=0.3,
        raw_texts=[],
    )


def is_top_tab_rect(rect: tuple[int, int, int, int], root_rect: tuple[int, int, int, int]) -> bool:
    left, top, right, bottom = rect
    root_left, root_top, root_right, _root_bottom = root_rect
    if right <= left or bottom <= top:
        return False
    root_width = max(1, root_right - root_left)
    height = max(1, bottom - top)
    width = max(1, right - left)
    top_limit = root_top + 64
    if top < root_top - 12 or top > top_limit:
        return False
    if left < root_left - 4 or right > root_right + 4:
        return False
    if left > root_left + root_width * 0.72:
        return False
    if width < 24 or width > min(420, root_width):
        return False
    if height < 12 or height > 70:
        return False
    return True


def is_selected_control(control: Any) -> bool:
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
    value = safe_prop(control, "IsSelected").strip().lower()
    return value in {"1", "true", "yes", "selected"}


def selected_account(candidates: list[AccountTab]) -> AccountTab | None:
    selected = [item for item in candidates if item.selected]
    if selected:
        return sorted(selected, key=lambda item: (-item.confidence, rect_sort_key(item.rect_tuple)))[0]
    return candidates[0] if candidates else None


def resolve_account(accounts: list[AccountTab], query: str) -> tuple[AccountTab | None, str, str]:
    normalized_query = normalize_candidate_text(query)
    if not normalized_query:
        return None, "", "empty_account_id"
    query_display = normalize_account_display_name(normalized_query)
    query_key = normalized_query.casefold()
    query_display_key = query_display.casefold()

    for account in accounts:
        if account.account_id.casefold() == query_key:
            return account, "account_id_exact", ""

    exact_display_matches = [
        account
        for account in accounts
        if normalize_account_display_name(account.display_name).casefold() == query_display_key
    ]
    if len(exact_display_matches) == 1:
        return exact_display_matches[0], "display_name_exact", ""
    if len(exact_display_matches) > 1:
        return None, "", ambiguous_account_error(normalized_query, exact_display_matches)

    contains_matches = [account for account in accounts if account_query_matches(account, query_key, query_display_key)]
    if len(contains_matches) == 1:
        return contains_matches[0], "contains", ""
    if len(contains_matches) > 1:
        return None, "", ambiguous_account_error(normalized_query, contains_matches)

    available = ", ".join(account.display_name for account in accounts[:10])
    return None, "", f"account_not_found query={normalized_query} available=[{available}]"


def account_query_matches(account: AccountTab, query_key: str, query_display_key: str) -> bool:
    if query_key and query_key in account.account_id.casefold():
        return True
    for value in [account.display_name, *account.raw_texts]:
        normalized = normalize_account_display_name(value).casefold()
        if query_display_key and query_display_key in normalized:
            return True
    return False


def ambiguous_account_error(query: str, accounts: list[AccountTab]) -> str:
    names = ", ".join(account.display_name for account in accounts[:10])
    return f"account_ambiguous query={query} matches=[{names}]"


def dedupe_account_tabs(candidates: list[AccountTab]) -> list[AccountTab]:
    ordered = sorted(
        candidates,
        key=lambda item: (
            -account_candidate_priority(item),
            -item.selected,
            rect_sort_key(item.rect_tuple),
            -item.confidence,
        ),
    )
    result: list[AccountTab] = []
    for item in ordered:
        if any(item.account_id == existing.account_id or rects_overlap(item.rect_tuple, existing.rect_tuple) for existing in result):
            continue
        result.append(item)
    return sorted(result, key=lambda item: rect_sort_key(item.rect_tuple))


def account_candidate_priority(item: AccountTab) -> int:
    control_text = f"{item.control_type} {item.class_name} {item.automation_id}".lower()
    if "tabitem" in control_text:
        return 30
    if "tabbar" in control_text or "tabcontrol" in control_text or "tab" in control_text:
        return 20
    return 10


def rect_sort_key(rect: tuple[int, int, int, int] | None) -> tuple[int, int, int]:
    if rect is None:
        return (10**9, 10**9, 10**9)
    left, top, right, bottom = rect
    return (top // 8, left, right - left + bottom - top)


def rects_overlap(left: tuple[int, int, int, int] | None, right: tuple[int, int, int, int] | None) -> bool:
    if left is None or right is None:
        return False
    overlap_width = min(left[2], right[2]) - max(left[0], right[0])
    overlap_height = min(left[3], right[3]) - max(left[1], right[1])
    return overlap_width > 0 and overlap_height > 0


def account_rect_blue_ratio(
    top_image: Any | None,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
) -> float:
    if top_image is None:
        return 0.0
    try:
        left, top, right, bottom = rect
        root_left, root_top, _root_right, _root_bottom = root_rect
        local_box = (
            max(0, left - root_left),
            max(0, top - root_top),
            max(0, right - root_left),
            max(0, bottom - root_top),
        )
        if local_box[2] <= local_box[0] or local_box[3] <= local_box[1]:
            return 0.0
        image = top_image.crop(local_box).convert("RGB")
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            pixels = list(image.getdata())
        if not pixels:
            return 0.0
        blue = 0
        for r, g, b in pixels:
            if b >= 150 and g >= 80 and r <= 110 and b >= r * 1.5 and b >= g * 1.05:
                blue += 1
        return blue / max(1, len(pixels))
    except Exception:
        return 0.0


def account_rect_red_hint(
    top_image: Any | None,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
) -> tuple[tuple[int, int, int, int] | None, float]:
    if top_image is None:
        return None, 0.0
    try:
        left, top, right, bottom = rect
        root_left, root_top, _root_right, _root_bottom = root_rect
        width = max(1, right - left)
        # The shop icon on the left can be orange/red in every tab; skip it.
        skip_left = min(width - 1, max(28, int(width * 0.16)))
        local_box = (
            max(0, left - root_left + skip_left),
            max(0, top - root_top),
            max(0, right - root_left),
            max(0, bottom - root_top),
        )
        if local_box[2] <= local_box[0] or local_box[3] <= local_box[1]:
            return None, 0.0
        image = top_image.crop(local_box).convert("RGB")
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            pixels = list(image.getdata())
        if not pixels:
            return None, 0.0
        image_width, image_height = image.size
        red_points: list[tuple[int, int]] = []
        for index, (r, g, b) in enumerate(pixels):
            if is_unread_red_pixel(r, g, b):
                red_points.append((index % image_width, index // image_width))
        if len(red_points) < ACCOUNT_UNREAD_MIN_RED_PIXELS:
            return None, 0.0
        component = best_unread_red_component(red_points, image_width, image_height)
        if component is None:
            return None, 0.0
        min_x, min_y, max_x, max_y, pixel_count = component
        score = min(1.0, pixel_count / 80.0)
        hint_rect = (
            root_left + local_box[0] + min_x,
            root_top + local_box[1] + min_y,
            root_left + local_box[0] + max_x + 1,
            root_top + local_box[1] + max_y + 1,
        )
        return hint_rect, score
    except Exception:
        return None, 0.0


def best_unread_red_component(
    red_points: list[tuple[int, int]],
    image_width: int,
    image_height: int,
) -> tuple[int, int, int, int, int] | None:
    red_set = set(red_points)
    visited: set[tuple[int, int]] = set()
    best: tuple[int, int, int, int, int] | None = None
    for start in red_points:
        if start in visited:
            continue
        queue: deque[tuple[int, int]] = deque([start])
        visited.add(start)
        xs: list[int] = []
        ys: list[int] = []
        while queue:
            x, y = queue.popleft()
            xs.append(x)
            ys.append(y)
            for ny in range(max(0, y - 1), min(image_height - 1, y + 1) + 1):
                for nx in range(max(0, x - 1), min(image_width - 1, x + 1) + 1):
                    point = (nx, ny)
                    if point in visited or point not in red_set:
                        continue
                    visited.add(point)
                    queue.append(point)
        pixel_count = len(xs)
        if pixel_count < ACCOUNT_UNREAD_MIN_RED_PIXELS:
            continue
        min_x, max_x = min(xs), max(xs)
        min_y, max_y = min(ys), max(ys)
        width = max_x - min_x + 1
        height = max_y - min_y + 1
        if width < 3 or height < 3:
            continue
        if width > 44 or height > 28:
            continue
        if best is None or pixel_count > best[4]:
            best = (min_x, min_y, max_x, max_y, pixel_count)
    return best


def is_unread_red_pixel(r: int, g: int, b: int) -> bool:
    return r >= 170 and g <= 130 and b <= 130 and r >= g * 1.35 and r >= b * 1.35


def grab_top_image(root: Any) -> Any | None:
    rect = safe_rect_tuple(root)
    if not rect:
        return None
    try:
        from PIL import ImageGrab

        left, top, right, bottom = rect
        top_bottom = min(bottom, top + max(56, min(100, int((bottom - top) * 0.12))))
        return ImageGrab.grab(bbox=(left, top, right, top_bottom)).convert("RGB")
    except Exception:
        return None


def click_account_tab(account: AccountTab, *, hwnd: int, activate: bool) -> tuple[bool, str]:
    if not account.rect_tuple:
        return False, "missing_rect"
    if activate and hwnd:
        activate_window(hwnd)
    left, top, right, bottom = account.rect_tuple
    x = int((left + right) / 2)
    y = int((top + bottom) / 2)
    try:
        import win32api
        import win32con
    except ImportError:
        return False, "pywin32_unavailable"

    old = win32api.GetCursorPos()
    win32api.SetCursorPos((x, y))
    time.sleep(0.04)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.04)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    win32api.SetCursorPos(old)
    return True, "rect_click"


def activate_window(hwnd: int) -> bool:
    try:
        import win32con
        import win32gui

        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetForegroundWindow(hwnd)
        time.sleep(0.08)
        return True
    except Exception:
        return False


def iter_controls(
    root: Any,
    *,
    max_depth: int,
    max_nodes: int,
    progress: ProgressLogger | None = None,
    label: str = "UIA scan",
) -> list[tuple[int, Any]]:
    return list(
        iter_controls_progressive(
            root,
            max_depth=max_depth,
            max_nodes=max_nodes,
            progress=progress,
            label=label,
        )
    )


def iter_controls_progressive(
    root: Any,
    *,
    max_depth: int,
    max_nodes: int,
    progress: ProgressLogger | None = None,
    label: str = "UIA scan",
) -> Any:
    log = progress or noop_progress
    queue: list[tuple[int, Any]] = [(0, root)]
    visited = 0
    returned = 0
    last_log_at = 0
    try:
        while queue and visited < max_nodes:
            depth, control = queue.pop(0)
            visited += 1
            returned += 1
            if visited - last_log_at >= 500:
                last_log_at = visited
                log(f"{label}: visited={visited} queued={len(queue)} depth={depth}")
            yield depth, control
            if depth >= max_depth:
                continue
            try:
                children = control.GetChildren()
            except Exception:
                continue
            for child in children:
                queue.append((depth + 1, child))
    finally:
        log(f"{label}: complete visited={visited} returned={returned}")


def noop_progress(_message: str) -> None:
    return None


def control_descriptor(control: Any) -> str:
    return " ".join(
        [
            safe_prop(control, "AutomationId"),
            safe_prop(control, "ClassName"),
            safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
            safe_prop(control, "FrameworkId"),
        ]
    ).lower()
