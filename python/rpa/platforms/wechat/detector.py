from __future__ import annotations

import random
import re
import time
from dataclasses import asdict, dataclass
from typing import Any

from .config import WechatAutomationConfig, load_wechat_config
from .uia_scoring import (
    find_input_candidates,
    find_message_list_candidates,
    find_send_button_candidates,
    find_session_list_candidates,
    normalize_text,
    safe_prop,
    safe_rect_repr,
    safe_rect_tuple,
    walk_controls,
)
from .wechat_logging import get_logger

logger = get_logger(__name__)

@dataclass(frozen=True)
class WechatWindowSnapshot:
    hwnd: int
    title: str
    class_name: str


@dataclass(frozen=True)
class UnreadSession:
    name: str
    control: Any
    unread_count: int | None = None


@dataclass(frozen=True)
class SessionScanResult:
    sessions: list[UnreadSession]
    source: str
    scanned_items: int


@dataclass(frozen=True)
class ChatTitleCandidate:
    control: Any
    depth: int
    score: int
    reason: str
    name: str


class WechatDetector:
    def __init__(self, config: WechatAutomationConfig | None = None) -> None:
        self.config = config or load_wechat_config()

    def find_main_window_control(self) -> Any | None:
        from rpa.platforms.wechat.uia import find_wechat_main_window_uia

        win = self._find_main_window_by_win32() or find_wechat_main_window_uia()
        logger.info("find_main_window_control found=%s", bool(win))
        return win

    def find_main_window(self) -> WechatWindowSnapshot | None:
        return self.get_window_snapshot(self.find_main_window_control())

    def get_window_control(self, window_control: Any | None = None) -> Any | None:
        return window_control or self.find_main_window_control()

    def get_window_snapshot(self, window_control: Any | None = None) -> WechatWindowSnapshot | None:
        win = self.get_window_control(window_control)
        if win is None:
            return None
        return WechatWindowSnapshot(
            hwnd=int(getattr(win, "NativeWindowHandle", 0) or 0),
            title=str(getattr(win, "Name", "") or ""),
            class_name=str(getattr(win, "ClassName", "") or ""),
        )

    def probe(self) -> dict[str, Any]:
        from rpa.platforms.wechat.uia import probe_wechat_uia_capabilities

        probe = probe_wechat_uia_capabilities()
        data = asdict(probe)
        data["healthy"] = bool(probe.available)
        logger.info(
            "probe healthy=%s reason=%s main_window=%s session_list=%s message_list=%s send_button=%s",
            data["healthy"],
            data.get("reason", ""),
            data.get("main_window_found"),
            data.get("session_list_found"),
            data.get("message_list_found"),
            data.get("send_button_found"),
        )
        return data

    def get_session_list(self, window_control: Any | None = None) -> Any | None:
        from rpa.platforms.wechat.uia import find_session_list_control

        win = self.get_window_control(window_control)
        if win is None:
            return None
        direct = find_session_list_control(win)
        if direct is not None:
            logger.info("get_session_list direct match=True")
            return direct
        candidates = find_session_list_candidates(win)
        selected = candidates[0].control if candidates and candidates[0].score >= 60 else None
        logger.info("get_session_list direct match=False candidates=%s selected=%s", len(candidates), bool(selected))
        return selected

    def collect_visible_sessions(self, limit: int = 60) -> list[Any]:
        from rpa.platforms.wechat.uia import (
            WechatUiaSessionSample,
            collect_visible_session_samples,
        )

        win = self.find_main_window_control()
        if win is None:
            raise RuntimeError("wechat_window_not_found")

        samples = collect_visible_session_samples(win, max_items=limit)
        if samples:
            logger.info("collect_visible_sessions direct samples=%s", len(samples))
            return samples

        list_root = self.get_session_list(win)
        if list_root is None:
            return []

        found: list[WechatUiaSessionSample] = []
        seen_names: set[str] = set()
        for _depth, control in walk_controls(list_root, max_depth=8, max_nodes=1500):
            if len(found) >= limit:
                break
            class_name = safe_prop(control, "ClassName")
            automation_id = safe_prop(control, "AutomationId")
            if class_name != "mmui::ChatSessionCell" and not automation_id.startswith("session_item_"):
                continue
            name = extract_session_title(_extract_session_display_name(safe_prop(control, "Name"), automation_id))
            if not name or self.is_blacklisted(name) or name in seen_names:
                continue
            rect = safe_rect_tuple(control)
            left, top, right, bottom = rect if rect is not None else (0, 0, 0, 0)
            found.append(
                WechatUiaSessionSample(
                    name=name,
                    class_name=class_name,
                    automation_id=automation_id,
                    rect=safe_rect_repr(control),
                    left=float(left),
                    top=float(top),
                    right=float(right),
                    bottom=float(bottom),
                )
            )
            seen_names.add(name)
        found.sort(key=lambda item: (item.top, item.left, item.right))
        logger.info("collect_visible_sessions fallback samples=%s", len(found))
        return found

    def scan_unread_sessions(self, session_list: Any | None = None) -> list[UnreadSession]:
        return self.scan_unread_sessions_detailed(session_list).sessions

    def scan_unread_sessions_detailed(self, session_list: Any | None = None) -> SessionScanResult:
        unread_sessions: list[UnreadSession] = []

        if session_list:
            scanned = 0
            for _, item in walk_controls(session_list, max_depth=8, max_nodes=1000):
                scanned += 1
                name = safe_prop(item, "Name")
                if self._is_unread_session_name(name):
                    unread_sessions.append(UnreadSession(name=name, control=item, unread_count=self._extract_unread_count(name)))
            logger.info("scan_unread_sessions source=session-list-tree scanned=%s unread=%s", scanned, len(unread_sessions))
            return SessionScanResult(unread_sessions, "session-list-tree", scanned)

        window_control = self.get_window_control()
        if not window_control:
            return SessionScanResult([], "none", 0)

        scanned = 0
        for _, control in walk_controls(window_control, max_depth=14, max_nodes=2000):
            scanned += 1
            name = safe_prop(control, "Name")
            if self._is_unread_session_name(name):
                unread_sessions.append(UnreadSession(name=name, control=control, unread_count=self._extract_unread_count(name)))

        logger.info("scan_unread_sessions source=whole-tree scanned=%s unread=%s", scanned, len(unread_sessions))
        return SessionScanResult(unread_sessions, "whole-tree", scanned)

    def select_session_to_click(self, sessions: list[UnreadSession]) -> UnreadSession | None:
        if not sessions:
            return None
        return random.choice(sessions[:3])

    def click_session(self, session: UnreadSession, fallback_hwnd: int | None = None) -> bool:
        return self.click_session_detailed(session, fallback_hwnd=fallback_hwnd).ok

    def click_session_detailed(
        self,
        session: UnreadSession,
        fallback_hwnd: int | None = None,
        allow_foreground: bool = True,
    ):
        from .click_strategy import click_session_item_detailed

        return click_session_item_detailed(
            session.control,
            fallback_hwnd=fallback_hwnd or 0,
            allow_foreground=allow_foreground,
        )

    def extract_session_title(self, session_name: str) -> str:
        for line in str(session_name or "").splitlines():
            value = normalize_text(line)
            if value:
                return value
        return normalize_text(session_name)

    def get_selected_session_title(self, window_control: Any | None = None) -> str:
        win = self.get_window_control(window_control)
        if not win:
            return ""

        session_list = self.get_session_list(win)
        if not session_list:
            return ""

        for _, control in walk_controls(session_list, max_depth=8, max_nodes=1500):
            try:
                pattern = control.GetSelectionItemPattern()
                if not pattern or not pattern.IsSelected:
                    continue
            except Exception:
                continue

            name = safe_prop(control, "Name").strip()
            if name:
                return self.extract_session_title(name)

        return ""

    def get_current_chat_name(self, window_control: Any | None = None) -> str:
        win = self.get_window_control(window_control)
        if not win:
            return ""

        selected_title = self.get_selected_session_title(win)
        if selected_title and not self._is_generic_context_name(selected_title, safe_prop(win, "Name")):
            return selected_title

        candidates = self.find_chat_title_candidates(win)
        if candidates:
            return candidates[0].name

        fallback = normalize_text(safe_prop(win, "Name"))
        if fallback and not self._is_generic_context_name(fallback, fallback):
            return fallback
        return ""

    def current_chat_name(self, window_control: Any | None = None) -> str:
        return self.get_current_chat_name(window_control)

    def find_chat_title_candidates(self, window_control: Any | None = None) -> list[ChatTitleCandidate]:
        win = self.get_window_control(window_control)
        if not win:
            return []

        message_list = self._find_message_list(win)
        message_rect = safe_rect_tuple(message_list) if message_list else None
        session_list = self.get_session_list(win)
        session_rect = safe_rect_tuple(session_list) if session_list else None
        window_name = normalize_text(safe_prop(win, "Name"))

        candidates: list[ChatTitleCandidate] = []
        for depth, control in walk_controls(win, max_depth=12, max_nodes=2500):
            score, reason = self._score_chat_title_candidate(
                control=control,
                message_rect=message_rect,
                session_rect=session_rect,
                window_name=window_name,
            )
            if score <= 0:
                continue
            candidates.append(
                ChatTitleCandidate(
                    control=control,
                    depth=depth,
                    score=score,
                    reason=reason,
                    name=normalize_text(safe_prop(control, "Name")),
                )
            )

        return sorted(candidates, key=lambda item: item.score, reverse=True)

    def find_session_control(self, window_control: Any | None, contact_name: str, *, exact: bool = False) -> Any | None:
        win = self.get_window_control(window_control)
        if win is None:
            return None
        from .uia_scoring import find_session_candidate

        return find_session_candidate(win, normalize_text(contact_name), exact=exact)

    def find_session_control_in_list(self, session_list: Any | None, contact_name: str, *, exact: bool = False) -> Any | None:
        if session_list is None:
            return None
        target = normalize_text(contact_name)
        if not target:
            return None

        scored: list[tuple[tuple[int, int, int, int], Any]] = []
        for _depth, control in walk_controls(session_list, max_depth=8, max_nodes=1600):
            name = normalize_text(safe_prop(control, "Name"))
            automation_id = normalize_text(safe_prop(control, "AutomationId"))
            class_name = safe_prop(control, "ClassName")
            display_name = _extract_session_display_name(name, automation_id)
            expected_aid = "session_item_" + target
            if exact:
                matched = display_name == target or automation_id == expected_aid
            else:
                matched = target in display_name or display_name in target or expected_aid in automation_id or target in automation_id
            if not matched:
                continue
            rect = safe_rect_tuple(control)
            top = rect[1] if rect is not None else 0
            exact_name = 0 if display_name == target else 1
            exact_aid = 0 if automation_id == expected_aid else 1
            class_rank = 0 if class_name == "mmui::ChatSessionCell" else 1
            scored.append(((class_rank, exact_aid, exact_name, top), control))

        if not scored:
            return None
        scored.sort(key=lambda item: item[0])
        return scored[0][1]

    def is_chat_open(self, chat_title: str, window_control: Any | None = None, session_list: Any | None = None) -> bool:
        if not chat_title:
            return False

        win = self.get_window_control(window_control)
        if not win:
            return False

        session_list = session_list or self.get_session_list(win)
        session_right = _safe_rect_right(session_list) if session_list else 0

        for _, control in walk_controls(win, max_depth=12, max_nodes=2000):
            name = safe_prop(control, "Name").strip()
            if name != chat_title:
                continue
            left = _safe_rect_left(control)
            if session_right and left <= session_right:
                continue
            return True

        return False

    def is_session_selected(self, session: UnreadSession) -> bool:
        try:
            pattern = session.control.GetSelectionItemPattern()
            return bool(pattern and pattern.IsSelected)
        except Exception:
            return False

    def describe_control(self, control: Any) -> str:
        return (
            f"type={safe_prop(control, 'ControlTypeName') or safe_prop(control, 'LocalizedControlType')}, "
            f"class={safe_prop(control, 'ClassName')}, "
            f"automationId={safe_prop(control, 'AutomationId')}, "
            f"name={safe_prop(control, 'Name')[:80]}, "
            f"rect={safe_rect_repr(control)}"
        )

    def verify_session_switch(
        self,
        target_title: str,
        window_control: Any | None = None,
        *,
        session_list: Any | None = None,
        timeout_ms: int = 1200,
        interval_ms: int = 120,
    ) -> bool:
        expected = self.extract_session_title(target_title)
        if not expected:
            return False

        win = self.get_window_control(window_control)
        if not win:
            return False

        deadline = time.time() + max(0, timeout_ms) / 1000.0
        sleep_sec = max(0.05, interval_ms / 1000.0)
        attempts = 0
        started_at = time.perf_counter()
        while True:
            attempts += 1
            probe_started_at = time.perf_counter()
            selected = self.get_selected_session_title(win)
            selected_ms = (time.perf_counter() - probe_started_at) * 1000.0
            probe_started_at = time.perf_counter()
            current = self.get_current_chat_name(win)
            current_ms = (time.perf_counter() - probe_started_at) * 1000.0
            matched = self._titles_match(expected, selected) or self._titles_match(expected, current)
            if matched:
                logger.info(
                    "verify_session_switch matched expected=%s attempts=%s total_ms=%.1f selected_ms=%.1f current_ms=%.1f",
                    expected,
                    attempts,
                    (time.perf_counter() - started_at) * 1000.0,
                    selected_ms,
                    current_ms,
                )
                return True
            probe_started_at = time.perf_counter()
            chat_open = self.is_chat_open(expected, win, session_list)
            chat_open_ms = (time.perf_counter() - probe_started_at) * 1000.0
            if chat_open:
                logger.info(
                    "verify_session_switch matched_by_open expected=%s attempts=%s total_ms=%.1f selected_ms=%.1f current_ms=%.1f chat_open_ms=%.1f",
                    expected,
                    attempts,
                    (time.perf_counter() - started_at) * 1000.0,
                    selected_ms,
                    current_ms,
                    chat_open_ms,
                )
                return True
            if time.time() >= deadline:
                break
            time.sleep(sleep_sec)
        logger.info(
            "verify_session_switch timeout expected=%s attempts=%s total_ms=%.1f",
            expected,
            attempts,
            (time.perf_counter() - started_at) * 1000.0,
        )
        return False

    def diagnose_uia(self, window_control: Any | None = None, *, candidate_limit: int = 5) -> dict[str, Any]:
        win = self.get_window_control(window_control)
        probe = self.probe()
        snapshot = self.get_window_snapshot(win)
        session_list = self.get_session_list(win) if win else None
        message_list = self._find_message_list(win) if win else None
        chat_input = self._find_chat_input(win) if win else None
        send_button = self._find_send_button(win) if win else None

        data: dict[str, Any] = {
            "probe": probe,
            "window_snapshot": asdict(snapshot) if snapshot else None,
            "window_control": self.describe_control(win) if win else "",
            "session_list": self.describe_control(session_list) if session_list else "",
            "message_list": self.describe_control(message_list) if message_list else "",
            "chat_input": self.describe_control(chat_input) if chat_input else "",
            "send_button": self.describe_control(send_button) if send_button else "",
            "selected_session_title": self.get_selected_session_title(win) if win else "",
            "current_chat_name": self.get_current_chat_name(win) if win else "",
            "chat_open_selected": self.is_chat_open(self.get_selected_session_title(win), win, session_list) if win else False,
            "chat_open_current": self.is_chat_open(self.get_current_chat_name(win), win, session_list) if win else False,
            "session_candidates": self._candidate_report(find_session_list_candidates(win)[:candidate_limit]) if win else [],
            "chat_title_candidates": self._chat_title_candidate_report(self.find_chat_title_candidates(win)[:candidate_limit]) if win else [],
            "message_candidates": self._candidate_report(find_message_list_candidates(win)[:candidate_limit]) if win else [],
            "input_candidates": self._candidate_report(find_input_candidates(win)[:candidate_limit]) if win else [],
            "send_button_candidates": self._candidate_report(find_send_button_candidates(win)[:candidate_limit]) if win else [],
        }
        return data

    def has_new_message(self, click: bool = False) -> bool:
        sessions = self.scan_unread_sessions()
        if not sessions:
            return False
        if click:
            selected = self.select_session_to_click(sessions)
            if selected:
                return self.click_session(selected)
        return True

    def _is_unread_session_name(self, name: str) -> bool:
        normalized = normalize_text(name)
        if not normalized or self.is_blacklisted(normalized):
            return False
        if any(marker in normalized for marker in self.config.unread_markers):
            return True
        return any(re.search(pattern, normalized, flags=re.IGNORECASE) for pattern in self.config.unread_patterns)

    def _extract_unread_count(self, name: str) -> int | None:
        normalized = normalize_text(name)
        for pattern in (r"\[(\d+)\s*条\]", r"\[(\d+)\s*條\]"):
            match = re.search(pattern, normalized)
            if match:
                return max(1, int(match.group(1)))
        return 1 if self._is_unread_session_name(normalized) else None

    def is_blacklisted(self, session_name: str) -> bool:
        normalized = normalize_text(session_name)
        return any(normalized.startswith(prefix) for prefix in self.config.blacklist)

    def _find_message_list(self, window_control: Any | None = None) -> Any | None:
        from rpa.platforms.wechat.uia import find_chat_message_list_control

        win = self.get_window_control(window_control)
        if not win:
            return None
        return find_chat_message_list_control(win)

    def _find_chat_input(self, window_control: Any | None = None) -> Any | None:
        from rpa.platforms.wechat.uia import find_chat_input

        win = self.get_window_control(window_control)
        if not win:
            return None
        return find_chat_input(win)

    def _find_send_button(self, window_control: Any | None = None) -> Any | None:
        from rpa.platforms.wechat.uia import find_send_button

        win = self.get_window_control(window_control)
        if not win:
            return None
        return find_send_button(win)

    def _candidate_report(self, candidates: list[Any]) -> list[dict[str, Any]]:
        report: list[dict[str, Any]] = []
        for candidate in candidates:
            control = getattr(candidate, "control", None)
            report.append(
                {
                    "depth": int(getattr(candidate, "depth", 0) or 0),
                    "score": int(getattr(candidate, "score", 0) or 0),
                    "reason": normalize_text(getattr(candidate, "reason", "")),
                    "control": self.describe_control(control) if control is not None else "",
                }
            )
        return report

    def _chat_title_candidate_report(self, candidates: list[ChatTitleCandidate]) -> list[dict[str, Any]]:
        report: list[dict[str, Any]] = []
        for candidate in candidates:
            report.append(
                {
                    "depth": candidate.depth,
                    "score": candidate.score,
                    "reason": candidate.reason,
                    "name": candidate.name,
                    "control": self.describe_control(candidate.control) if candidate.control is not None else "",
                }
            )
        return report

    def _titles_match(self, left: str, right: str) -> bool:
        a = normalize_text(left)
        b = normalize_text(right)
        return bool(a and b and (a == b or a in b or b in a))

    def _find_main_window_by_win32(self) -> Any | None:
        try:
            import psutil
            import pywintypes
            import win32gui
            import win32process
        except Exception:
            return None

        pids: set[int] = set()
        configured_names = list(getattr(self.config, "process_names", []) or [])
        if self.config.process_name:
            configured_names.append(self.config.process_name)
        target_processes = {normalize_text(name).lower() for name in configured_names if normalize_text(name)}
        for proc in psutil.process_iter(["name", "pid"]):
            try:
                name = normalize_text(proc.info.get("name") or "").lower()
                if name in target_processes:
                    pids.add(int(proc.info["pid"]))
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        if not pids:
            return None

        matches: list[int] = []

        def enum_callback(hwnd: int, _param: Any) -> bool:
            try:
                if not win32gui.IsWindowVisible(hwnd):
                    return True
                _, pid = win32process.GetWindowThreadProcessId(hwnd)
                if pid not in pids:
                    return True
                class_name = win32gui.GetClassName(hwnd)
                title = win32gui.GetWindowText(hwnd)
            except pywintypes.error:
                return True
            if not self._matches_main_window(class_name, title):
                return True
            matches.append(int(hwnd))
            return False

        try:
            win32gui.EnumWindows(enum_callback, None)
        except Exception:
            return None
        if not matches:
            return None

        try:
            import uiautomation as auto

            return auto.ControlFromHandle(matches[0])
        except Exception:
            return None

    def _matches_main_window(self, class_name: str, title: str) -> bool:
        class_name = normalize_text(class_name)
        title = normalize_text(title)
        class_hit = not self.config.window_classes or class_name in self.config.window_classes
        title_hit = not self.config.window_title_keywords or any(keyword in title for keyword in self.config.window_title_keywords)
        return class_hit and title_hit

    def _score_chat_title_candidate(
        self,
        control: Any,
        message_rect: tuple[int, int, int, int] | None,
        session_rect: tuple[int, int, int, int] | None,
        window_name: str,
    ) -> tuple[int, str]:
        name = normalize_text(safe_prop(control, "Name"))
        if not name or "\n" in name:
            return 0, ""
        if self._is_generic_context_name(name, window_name):
            return 0, ""

        rect = safe_rect_tuple(control)
        if rect is None:
            return 0, ""

        automation_id = safe_prop(control, "AutomationId")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        class_name = safe_prop(control, "ClassName")

        score = 0
        reasons: list[str] = []

        if "current_chat_name_label" in automation_id:
            score += 120
            reasons.append("current-chat-name-id")
        elif "current_chat" in automation_id:
            score += 80
            reasons.append("current-chat-id")

        if "Text" in control_type or "文本" in control_type:
            score += 20
            reasons.append("text-type")
        if "Button" in control_type or "按钮" in control_type:
            score -= 20
            reasons.append("button-penalty")

        if message_rect:
            if rect[3] <= message_rect[1] + 8:
                score += 35
                reasons.append("above-message-list")
            if rect[2] >= message_rect[0] - 80 and rect[0] <= message_rect[2]:
                score += 20
                reasons.append("right-pane-x")
            if rect[1] < message_rect[1] and rect[3] > 0:
                distance = message_rect[1] - rect[3]
                if 0 <= distance <= 220:
                    score += max(0, 40 - distance // 8)
                    reasons.append("near-message-header")

        if session_rect and rect[0] <= session_rect[2]:
            score -= 50
            reasons.append("session-pane-penalty")

        if 1 <= len(name) <= 40:
            score += 15
            reasons.append("title-length")
        elif len(name) > 80:
            score -= 30
            reasons.append("long-name-penalty")

        if "Chat" in class_name or "chat" in automation_id.lower():
            score += 10
            reasons.append("chat-class-or-id")

        return score, ",".join(reasons)

    def _is_generic_context_name(self, name: str, window_name: str = "") -> bool:
        generic_names = {
            "",
            "微信",
            "消息",
            "会话",
            "通讯录",
            "收藏",
            "聊天信息",
            "搜索",
            "更多",
            "最小化",
            "最大化",
            "关闭",
            "置顶",
            "取消置顶",
            "消息免打扰",
            "取消消息免打扰",
            "静音",
            "取消静音",
            "语音聊天",
            "视频聊天",
            "添加",
            "发送文件",
            "快捷操作",
        }
        stripped = normalize_text(name)
        if stripped in generic_names:
            return True
        return bool(window_name and stripped == normalize_text(window_name))


def extract_session_title(session_name: str) -> str:
    for line in str(session_name or "").splitlines():
        value = normalize_text(line)
        if value:
            return value
    return normalize_text(session_name)


def _extract_session_display_name(raw_name: str, automation_id: str) -> str:
    automation_id = normalize_text(automation_id)
    if automation_id.startswith("session_item_"):
        return normalize_text(automation_id[len("session_item_") :])
    return extract_session_title(raw_name)


def _safe_rect_left(control: Any) -> int:
    try:
        return int(control.BoundingRectangle.left)
    except Exception:
        return 0


def _safe_rect_right(control: Any) -> int:
    try:
        return int(control.BoundingRectangle.right)
    except Exception:
        return 0
