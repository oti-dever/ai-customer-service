from __future__ import annotations

from dataclasses import dataclass
import threading
import time
from typing import Any, Callable

from .config import AppConfig, load_config
from .uia import (
    control_from_hwnd,
    enum_all_top_level_windows,
    find_process_ids,
    safe_prop,
    safe_rect_tuple,
    trim,
    walk_controls,
)


@dataclass(frozen=True)
class WindowCandidate:
    hwnd: int
    pid: int
    class_name: str
    title: str
    score: int
    reason: str


@dataclass(frozen=True)
class ControlCandidate:
    control: Any
    depth: int
    score: int
    reason: str


@dataclass(frozen=True)
class ChatHandle:
    window: WindowCandidate
    window_control: Any
    chat_root: Any


@dataclass(frozen=True)
class CachedWindow:
    hwnd: int
    pid: int
    class_name: str
    title: str
    cached_at: float


_WINDOW_CACHE_LOCK = threading.Lock()
_WINDOW_CACHE: dict[str, CachedWindow] = {}
_WINDOW_CACHE_TTL_SEC = 120.0


class QianniuDetector:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.q = self.config.qianniu

    def find_process_ids(self) -> list[int]:
        return find_process_ids(self.q.process_name)

    def find_window_candidates(self, log: Callable[[str], None] | None = None) -> list[WindowCandidate]:
        process_ids = set(self.find_process_ids())
        if log:
            log(f"process ids: {sorted(process_ids) or 'not found'}")

        windows = enum_all_top_level_windows()
        if log:
            log(f"top-level windows enumerated: {len(windows)}")

        candidates: list[WindowCandidate] = []
        for item in windows:
            if item.pid not in process_ids:
                continue

            score, reason = self.score_window_candidate(item.class_name)
            if log:
                log(
                    f"window pid-match hwnd=0x{item.hwnd:X} class={item.class_name} "
                    f"base_score={score} title={trim(item.title)}"
                )

            control = control_from_hwnd(item.hwnd)
            if control:
                root_score, root_reason = self._score_chat_root_candidate(control, 0)
                if root_score:
                    score += root_score
                    reason = _join_reasons(reason, f"root:{root_reason}")
                if log:
                    log(
                        f"  uia-root ok hwnd=0x{item.hwnd:X} "
                        f"root_score={root_score} root_reason={root_reason or '-'} "
                        f"{self.describe_control(control)}"
                    )
                if self._is_definitive_chat_root(control, root_score):
                    candidate = WindowCandidate(
                        hwnd=item.hwnd,
                        pid=item.pid,
                        class_name=item.class_name,
                        title=item.title,
                        score=score,
                        reason=_join_reasons(reason, "early-chat-root"),
                    )
                    if log:
                        log(f"  definitive chat root found; stop window scan hwnd=0x{item.hwnd:X}")
                    return [candidate]
            elif log:
                log(f"  uia-root unavailable hwnd=0x{item.hwnd:X}")

            if score <= 0:
                continue
            candidates.append(
                WindowCandidate(
                    hwnd=item.hwnd,
                    pid=item.pid,
                    class_name=item.class_name,
                    title=item.title,
                    score=score,
                    reason=reason,
                )
            )
        return sorted(candidates, key=lambda x: x.score, reverse=True)

    def find_best_window(self, log: Callable[[str], None] | None = None) -> WindowCandidate | None:
        candidates = self.find_window_candidates(log=log)
        return candidates[0] if candidates else None

    def find_current_chat(self, log: Callable[[str], None] | None = None) -> ChatHandle | None:
        cached_window = self._cached_window()
        if cached_window:
            handle = self._chat_handle_from_window(cached_window)
            if handle:
                if log:
                    log(f"cached window hit hwnd=0x{cached_window.hwnd:X}")
                return handle
            self._clear_cached_window(cached_window.hwnd)

        window = self.find_best_window(log=log)
        if not window:
            return None
        handle = self._chat_handle_from_window(window)
        if handle:
            self._remember_window(window)
        return handle

    def _chat_handle_from_window(self, window: WindowCandidate | CachedWindow) -> ChatHandle | None:
        window_control = self.get_window_control(window.hwnd)
        if not window_control:
            return None
        root_score, _root_reason = self._score_chat_root_candidate(window_control, 0)
        if self._is_definitive_chat_root(window_control, root_score):
            chat_root = window_control
        else:
            roots = self.find_chat_root_candidates(window_control)
            if not roots:
                return None
            chat_root = roots[0].control
        candidate = WindowCandidate(
            hwnd=window.hwnd,
            pid=window.pid,
            class_name=window.class_name,
            title=window.title,
            score=getattr(window, "score", 0),
            reason=getattr(window, "reason", "cached-window"),
        )
        return ChatHandle(window=candidate, window_control=window_control, chat_root=chat_root)

    def _cached_window(self) -> CachedWindow | None:
        with _WINDOW_CACHE_LOCK:
            cached = _WINDOW_CACHE.get(self.q.process_name)
        if not cached:
            return None
        if time.monotonic() - cached.cached_at > _WINDOW_CACHE_TTL_SEC:
            self._clear_cached_window(cached.hwnd)
            return None
        return cached

    def _remember_window(self, window: WindowCandidate) -> None:
        with _WINDOW_CACHE_LOCK:
            _WINDOW_CACHE[self.q.process_name] = CachedWindow(
                hwnd=window.hwnd,
                pid=window.pid,
                class_name=window.class_name,
                title=window.title,
                cached_at=time.monotonic(),
            )

    def _clear_cached_window(self, hwnd: int | None = None) -> None:
        with _WINDOW_CACHE_LOCK:
            cached = _WINDOW_CACHE.get(self.q.process_name)
            if cached and (hwnd is None or cached.hwnd == hwnd):
                _WINDOW_CACHE.pop(self.q.process_name, None)

    def get_window_control(self, hwnd: int) -> Any | None:
        return control_from_hwnd(hwnd)

    def find_chat_root_candidates(self, window_control: Any | None) -> list[ControlCandidate]:
        return self._find_scored_controls(window_control, self._score_chat_root_candidate)

    def find_message_area_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_message_area_candidate(control, depth, root_rect),
        )

    def find_input_area_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_input_area_candidate(control, depth, root_rect),
        )

    def find_send_button_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_send_button_candidate(control, depth, root_rect),
        )

    def find_message_display(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.message_display_suffix)

    def find_message_web(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.message_web_suffix)

    def find_input_field(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(
            chat_root,
            self.q.input_suffix,
            prune_automation_id_keywords=[
                "ChatListWidget",
                "msgDisplayWidget",
                "ChatExtendView",
                "MobileStatusView",
            ],
        )

    def find_send_button(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.send_button_suffix)

    def find_chat_list(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.chat_list_suffix)

    def find_chat_list_view(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.chat_list_view_suffix)

    def find_chat_list_items_root(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.chat_list_items_suffix)

    def find_reception_normal_list(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.reception_normal_list_suffix)

    def find_chat_search_input(self, chat_root: Any | None) -> Any | None:
        return self.find_by_automation_id_suffix(chat_root, self.q.chat_search_input_suffix)

    def find_by_automation_id_suffix(
        self,
        root: Any | None,
        suffix: str,
        prune_automation_id_keywords: list[str] | None = None,
    ) -> Any | None:
        if not root or not suffix:
            return None
        if not prune_automation_id_keywords:
            for _, control in walk_controls(root, max_depth=self.q.max_tree_depth, max_nodes=self.q.max_tree_nodes):
                aid = safe_prop(control, "AutomationId")
                if aid == suffix or aid.endswith(suffix):
                    return control
            return None

        queue: list[tuple[int, Any]] = [(0, root)]
        visited = 0
        while queue and visited < self.q.max_tree_nodes:
            depth, control = queue.pop(0)
            visited += 1
            aid = safe_prop(control, "AutomationId")
            if aid == suffix or aid.endswith(suffix):
                return control
            if depth >= self.q.max_tree_depth:
                continue
            if any(keyword in aid for keyword in prune_automation_id_keywords):
                continue
            try:
                children = control.GetChildren()
            except Exception:
                continue
            for child in children:
                queue.append((depth + 1, child))
        return None

    def score_window_candidate(self, class_name: str, title: str = "") -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        if class_name in self.q.window_class_hints:
            score += 60
            reasons.append("class-hint")
        if "Popup" in class_name or "SaveBits" in class_name or "Tool" in class_name:
            score -= 20
            reasons.append("overlay-penalty")
        if class_name == "ChatView":
            score += 140
            reasons.append("chatview-class")
        return score, ",".join(reasons)

    def describe_control(self, control: Any) -> str:
        return (
            f"type={safe_prop(control, 'ControlTypeName') or safe_prop(control, 'LocalizedControlType') or '-'} "
            f"class={safe_prop(control, 'ClassName') or '-'} "
            f"aid={safe_prop(control, 'AutomationId') or '-'} "
            f"name={trim(safe_prop(control, 'Name'))}"
        )

    def _find_scored_controls(
        self,
        root: Any | None,
        scorer: Callable[[Any, int], tuple[int, str]],
    ) -> list[ControlCandidate]:
        if not root:
            return []
        candidates: list[ControlCandidate] = []
        for depth, control in walk_controls(root, max_depth=self.q.max_tree_depth, max_nodes=self.q.max_tree_nodes):
            score, reason = scorer(control, depth)
            if score > 0:
                candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
        return sorted(candidates, key=lambda x: x.score, reverse=True)

    def _score_chat_root_candidate(self, control: Any, depth: int) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        class_name = safe_prop(control, "ClassName")
        aid = safe_prop(control, "AutomationId")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        child_count = _child_count(control)

        if class_name == "ChatView":
            score += 140
            reasons.append("chatview")
        if aid == self.q.chat_root_automation_id:
            score += 100
            reasons.append("chat-root-aid")
        if aid in self.q.chat_container_automation_ids:
            score += 80
            reasons.append("chat-container-aid")
        if "MobileStatusView" in class_name or "MobileStatusView" in aid:
            score -= 80
            reasons.append("mobile-status-penalty")
        if "Window" in control_type or "Pane" in control_type or "Group" in control_type:
            score += 10
            reasons.append("container-type")
        if rect and rect[2] - rect[0] > 400 and rect[3] - rect[1] > 300:
            score += 10
            reasons.append("large-rect")
        if child_count >= 3:
            score += 10
            reasons.append("has-children")
        if depth > 6:
            score -= 5
            reasons.append("deep-penalty")
        return score, ",".join(reasons)

    def _is_definitive_chat_root(self, control: Any, score: int) -> bool:
        return (
            score >= 240
            and safe_prop(control, "ClassName") == "ChatView"
            and safe_prop(control, "AutomationId") == self.q.chat_root_automation_id
        )

    def _score_message_area_candidate(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        aid = safe_prop(control, "AutomationId")
        class_name = safe_prop(control, "ClassName")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        child_count = _child_count(control)

        if "ChatContentView" in aid or class_name == "ChatContentView":
            score += 140
            reasons.append("chat-content")
        if "ChatDisplayWidget" in aid:
            score += 90
            reasons.append("chat-display")
        if "ChatListWidget" in aid:
            score += 60
            reasons.append("chat-list")
        if "SubChatView" in aid:
            score += 20
            reasons.append("sub-chat")
        if "MobileStatusView" in aid or class_name == "MobileStatusView":
            score -= 80
            reasons.append("mobile-status-penalty")
        if "List" in control_type or "DataGrid" in control_type:
            score += 50
            reasons.append("list-type")
        if rect and root_rect:
            root_w = root_rect[2] - root_rect[0]
            root_h = root_rect[3] - root_rect[1]
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            if width > root_w * 0.45 and height > root_h * 0.25:
                score += 20
                reasons.append("large-area")
            if rect[1] < root_rect[1] + root_h * 0.75:
                score += 10
                reasons.append("upper-middle")
        if child_count >= 3:
            score += min(child_count, 30)
            reasons.append(f"children={child_count}")
        if depth <= 1:
            score -= 40
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _score_input_area_candidate(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        aid = safe_prop(control, "AutomationId")
        class_name = safe_prop(control, "ClassName")
        name = safe_prop(control, "Name")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        text = f"{aid} {class_name} {name} {control_type}".lower()

        if "chatextendview" in text or "tabwidget" in text:
            score += 80
            reasons.append("chat-extend")
        if "chatoperationview" in text:
            score += 40
            reasons.append("chat-operation")
        if "input" in text or "edit" in text or "textedit" in text:
            score += 70
            reasons.append("input-keyword")
        if "Edit" in control_type or "Document" in control_type:
            score += 50
            reasons.append("editable-type")
        if rect and root_rect:
            root_h = root_rect[3] - root_rect[1]
            if rect[1] > root_rect[1] + root_h * 0.50:
                score += 30
                reasons.append("lower-area")
            if rect[3] <= root_rect[3] + 10:
                score += 10
                reasons.append("inside-root-bottom")
        if depth <= 1:
            score -= 40
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _score_send_button_candidate(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        aid = safe_prop(control, "AutomationId")
        class_name = safe_prop(control, "ClassName")
        name = safe_prop(control, "Name")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        rect = safe_rect_tuple(control)
        text = f"{aid} {class_name} {name} {control_type}".lower()

        send_names = {"\u53d1\u9001", "\u53d1\u9001(S)", "\u53d1 \u9001"}
        if name in send_names or name.startswith("\u53d1\u9001"):
            score += 140
            reasons.append("send-name")
        if "send" in text:
            score += 80
            reasons.append("send-keyword")
        if "Button" in control_type or "button" in text:
            score += 40
            reasons.append("button-type")
        if rect and root_rect:
            root_h = root_rect[3] - root_rect[1]
            root_w = root_rect[2] - root_rect[0]
            if rect[1] > root_rect[1] + root_h * 0.50:
                score += 20
                reasons.append("lower-area")
            if rect[0] > root_rect[0] + root_w * 0.55:
                score += 20
                reasons.append("right-area")
        if depth <= 1:
            score -= 40
            reasons.append("root-penalty")
        return score, ",".join(reasons)


def _child_count(control: Any) -> int:
    try:
        return len(control.GetChildren())
    except Exception:
        return 0


def _join_reasons(left: str, right: str) -> str:
    return ",".join(value for value in (left, right) if value)


def main() -> int:
    detector = QianniuDetector()
    windows = detector.find_window_candidates()
    print(f"process={detector.q.process_name} pids={detector.find_process_ids() or 'not found'}")
    if not windows:
        print("no window candidates; bring Qianniu chat to foreground and run tests/watch_foreground.py")
        return 1
    for item in windows[:10]:
        print(
            f"hwnd=0x{item.hwnd:X} pid={item.pid} class={item.class_name} "
            f"score={item.score} reason={item.reason} title={trim(item.title)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
