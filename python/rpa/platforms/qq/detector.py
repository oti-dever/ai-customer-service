from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any, Callable

from .config import AppConfig, load_config
from .uia import (
    WindowInfo,
    child_count,
    control_from_hwnd,
    find_process_windows,
    safe_prop,
    safe_rect_tuple,
    summarize_control,
    trim,
    uia_guard,
    walk_controls,
)


@dataclass(frozen=True)
class WindowCandidate:
    window: WindowInfo
    score: int
    reason: str
    root_control: Any | None = None


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


class QQDetector:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.q = self.config.qq

    def find_process_windows(self) -> list[WindowInfo]:
        return find_process_windows(self.q.process_name, visible_only=True)

    def find_window_candidates(self, log: Callable[[str], None] | None = None) -> list[WindowCandidate]:
        with uia_guard("qq_find_window"):
            windows = self.find_process_windows()
            if log:
                log(f"process={self.q.process_name} visible_windows={len(windows)}")

            candidates: list[WindowCandidate] = []
            for window in windows:
                score, reason = self._score_window(window)
                root = control_from_hwnd(window.hwnd)
                if root is not None:
                    root_score, root_reason = self._score_root(root)
                    score += root_score
                    reason = _join_reasons(reason, f"root:{root_reason}")
                    if log:
                        log(
                            f"window hwnd=0x{window.hwnd:X} pid={window.pid} class={window.class_name} "
                            f"area={window.area} score={score} reason={reason} title={trim(window.title)} "
                            f"{self.describe_control(root)}"
                        )
                elif log:
                    log(
                        f"window hwnd=0x{window.hwnd:X} pid={window.pid} class={window.class_name} "
                        f"area={window.area} score={score} reason={reason} title={trim(window.title)} uia_root=missing"
                    )

                if score > 0:
                    candidates.append(WindowCandidate(window=window, score=score, reason=reason, root_control=root))
            return sorted(candidates, key=lambda item: item.score, reverse=True)

    def find_best_window(self, log: Callable[[str], None] | None = None) -> WindowCandidate | None:
        candidates = self.find_window_candidates(log=log)
        return candidates[0] if candidates else None

    def find_current_chat(self, log: Callable[[str], None] | None = None) -> ChatHandle | None:
        candidate = self.find_best_window(log=log)
        if candidate is None:
            return None
        window_control = candidate.root_control or control_from_hwnd(candidate.window.hwnd)
        if window_control is None:
            return None
        # Use the top-level QQ window as the PoC scan root. QQ exposes the
        # message list as its own named WindowControl ("消息列表"), and scoring it
        # as the chat root hides the input area from later searches.
        chat_root = window_control
        return ChatHandle(window=candidate, window_control=window_control, chat_root=chat_root)

    def find_chat_root_candidates(self, window_control: Any | None) -> list[ControlCandidate]:
        return self._find_scored_controls(window_control, self._score_chat_root)

    def find_message_area_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_message_area(control, depth, root_rect),
        )

    def find_input_area_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        message_candidates = self.find_message_area_candidates(chat_root)
        message_rect = safe_rect_tuple(message_candidates[0].control) if message_candidates else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_input_area(control, depth, root_rect, message_rect),
        )

    def find_send_button_candidates(self, chat_root: Any | None) -> list[ControlCandidate]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        message_candidates = self.find_message_area_candidates(chat_root)
        message_rect = safe_rect_tuple(message_candidates[0].control) if message_candidates else None
        input_candidates = self.find_input_area_candidates(chat_root)
        input_rect = safe_rect_tuple(input_candidates[0].control) if input_candidates else None
        return self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_send_button(control, depth, root_rect, message_rect, input_rect),
        )

    def find_title_candidates(self, chat_root: Any | None, limit: int = 10) -> list[tuple[int, str, ControlCandidate]]:
        root_rect = safe_rect_tuple(chat_root) if chat_root else None
        message_candidates = self.find_message_area_candidates(chat_root)
        message_rect = safe_rect_tuple(message_candidates[0].control) if message_candidates else None
        scored: list[tuple[int, str, ControlCandidate]] = []
        seen: set[str] = set()
        for item in self._find_scored_controls(
            chat_root,
            lambda control, depth: self._score_title(control, depth, root_rect, message_rect),
        ):
            name = trim(safe_prop(item.control, "Name"), 80)
            if not name or name in seen:
                continue
            seen.add(name)
            scored.append((item.score, name, item))
            if len(scored) >= limit:
                break
        return scored

    def describe_control(self, control: Any) -> str:
        info = summarize_control(0, control)
        return (
            f"type={info.control_type or '-'} class={info.class_name or '-'} "
            f"aid={info.automation_id or '-'} name={trim(info.name)} rect={info.rect}"
        )

    def _find_scored_controls(
        self,
        root: Any | None,
        scorer: Callable[[Any, int], tuple[int, str]],
    ) -> list[ControlCandidate]:
        if root is None:
            return []
        candidates: list[ControlCandidate] = []
        for depth, control in walk_controls(root, max_depth=self.q.max_tree_depth, max_nodes=self.q.max_tree_nodes):
            score, reason = scorer(control, depth)
            if score > 0:
                candidates.append(ControlCandidate(control=control, depth=depth, score=score, reason=reason))
        return sorted(candidates, key=lambda item: item.score, reverse=True)

    def _score_window(self, window: WindowInfo) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        if window.visible:
            score += 20
            reasons.append("visible")
        if window.area >= self.q.min_window_area:
            score += 50
            reasons.append("large-window")
        if window.title.strip():
            score += 20
            reasons.append("has-title")
        if any(hint and hint in window.class_name for hint in self.q.window_class_hints):
            score += 40
            reasons.append("class-hint")
        if any(block and block in window.title for block in self.q.title_blocklist):
            score -= 60
            reasons.append("title-blocked")
        if window.area < 40_000:
            score -= 50
            reasons.append("tiny-window")
        return score, ",".join(reasons)

    def _score_root(self, control: Any) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        rect = safe_rect_tuple(control)
        if rect:
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            if width > 500 and height > 350:
                score += 30
                reasons.append("large-root")
        children = child_count(control)
        if children >= 4:
            score += 20
            reasons.append(f"children={children}")
        if trim(safe_prop(control, "Name")):
            score += 10
            reasons.append("root-name")
        return score, ",".join(reasons)

    def _score_chat_root(self, control: Any, depth: int) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        rect = safe_rect_tuple(control)
        text = _control_text(control)
        children = child_count(control)
        if any(value in text for value in {"chat", "message", "msg", "aio", "conversation", "session"}):
            score += 45
            reasons.append("chat-keyword")
        if any(value in text for value in {"聊天", "消息", "会话"}):
            score += 45
            reasons.append("chat-cn-keyword")
        if "Window" in text or "Pane" in text or "Group" in text:
            score += 10
            reasons.append("container-type")
        if rect:
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            if width > 450 and height > 300:
                score += 25
                reasons.append("large-rect")
        if children >= 4:
            score += min(children, 30)
            reasons.append(f"children={children}")
        if depth == 0:
            score += 10
            reasons.append("root")
        if depth > 8:
            score -= 15
            reasons.append("deep-penalty")
        return score, ",".join(reasons)

    def _score_message_area(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        rect = safe_rect_tuple(control)
        text = _control_text(control)
        name = trim(safe_prop(control, "Name"))
        aid = safe_prop(control, "AutomationId")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        children = child_count(control)
        if name == "消息列表":
            score += 180
            reasons.append("qq-message-list-name")
        if aid == "ml-root":
            score += 160
            reasons.append("qq-message-list-root")
        if any(value in text for value in {"message", "msg", "chat", "conversation", "list", "document"}):
            score += 45
            reasons.append("message-keyword")
        if any(value in text for value in {"消息", "聊天", "会话", "列表"}):
            score += 45
            reasons.append("message-cn-keyword")
        if "List" in text or "DataItem" in text or "Document" in text:
            score += 30
            reasons.append("content-type")
        if control_type == "ToolBarControl":
            score -= 80
            reasons.append("toolbar-penalty")
        if name in {"会话", "语音消息", "聊天记录"}:
            score -= 70
            reasons.append("tool-name-penalty")
        if rect and root_rect:
            root_w = max(1, root_rect[2] - root_rect[0])
            root_h = max(1, root_rect[3] - root_rect[1])
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            if width > root_w * 0.35 and height > root_h * 0.25:
                score += 35
                reasons.append("large-area")
            if rect[1] < root_rect[1] + root_h * 0.78:
                score += 15
                reasons.append("not-bottom-only")
        if children >= 3:
            score += min(children, 35)
            reasons.append(f"children={children}")
        if depth <= 1:
            score -= 35
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _score_input_area(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
        message_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        rect = safe_rect_tuple(control)
        text = _control_text(control)
        name = trim(safe_prop(control, "Name"))
        aid = safe_prop(control, "AutomationId")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        children = child_count(control)
        if name == "搜索":
            score -= 180
            reasons.append("search-penalty")
        if "func-bar" in aid:
            score -= 90
            reasons.append("toolbar-aid-penalty")
        if name in {"发送", "语音输入文字", "聊天记录", "表情", "截图", "文件", "图片", "红包", "更多"}:
            score -= 80
            reasons.append("button-name-penalty")
        if any(value in text for value in {"input", "edit", "textarea", "textbox", "rich", "compose"}):
            score += 70
            reasons.append("input-keyword")
        if any(value in text for value in {"输入", "编辑", "回复"}):
            score += 70
            reasons.append("input-cn-keyword")
        if "Edit" in text or "Document" in text:
            score += 45
            reasons.append("editable-type")
        if control_type == "ButtonControl":
            score -= 45
            reasons.append("button-penalty")
        if control_type == "ToolBarControl":
            score -= 80
            reasons.append("toolbar-penalty")
        if rect and message_rect:
            msg_left, _msg_top, msg_right, msg_bottom = message_rect
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            gap = rect[1] - msg_bottom
            if rect[0] >= msg_left - 8 and rect[2] <= msg_right + 8 and rect[1] >= msg_bottom - 8:
                score += 130
                reasons.append("below-message-list")
            if width > (msg_right - msg_left) * 0.55 and height >= 45:
                score += 55
                reasons.append("large-compose-area")
            if 24 <= gap <= 75 and 60 <= height <= 160:
                score += 130
                reasons.append("compose-band")
            if gap < 18:
                score -= 70
                reasons.append("starts-at-toolbar")
            if height > 175:
                score -= 80
                reasons.append("too-tall-compose")
            if rect[3] > msg_bottom + 175:
                score -= 45
                reasons.append("includes-send-row")
            if rect[3] <= msg_bottom + 45:
                score -= 80
                reasons.append("message-toolbar-penalty")
            if rect[2] <= msg_left:
                score -= 120
                reasons.append("left-pane-penalty")
        if rect and root_rect:
            root_h = max(1, root_rect[3] - root_rect[1])
            if rect[1] > root_rect[1] + root_h * 0.48:
                score += 35
                reasons.append("lower-area")
            if rect[3] <= root_rect[3] + 20:
                score += 10
                reasons.append("inside-bottom")
        if not name and control_type in {"GroupControl", "PaneControl"} and children >= 1:
            score += 20
            reasons.append("anonymous-container")
        if depth <= 1:
            score -= 35
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _score_send_button(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
        message_rect: tuple[int, int, int, int] | None,
        input_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        score = 0
        reasons: list[str] = []
        rect = safe_rect_tuple(control)
        if not rect:
            return 0, ""
        text = _control_text(control)
        name = trim(safe_prop(control, "Name"))
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        if control_type != "ButtonControl":
            score -= 40
            reasons.append("not-button")
        else:
            score += 35
            reasons.append("button")
        if name in {"发送", "Send"}:
            score += 220
            reasons.append("send-name")
        if any(value in text.lower() for value in {"send", "submit"}):
            score += 90
            reasons.append("send-keyword")
        if any(value in text for value in {"发送", "鍙戦€?"}):
            score += 120
            reasons.append("send-cn-keyword")
        blocked_names = {"搜索", "表情", "截图", "文件", "图片", "红包", "更多", "语音消息", "聊天记录"}
        if name in blocked_names:
            score -= 120
            reasons.append("blocked-tool-name")
        if input_rect:
            input_left, input_top, input_right, input_bottom = input_rect
            width = rect[2] - rect[0]
            height = rect[3] - rect[1]
            if rect[1] >= input_top - 20 and rect[3] <= input_bottom + 70:
                score += 70
                reasons.append("near-compose")
            if rect[0] >= input_left + (input_right - input_left) * 0.65:
                score += 65
                reasons.append("compose-right")
            if 36 <= width <= 120 and 24 <= height <= 50:
                score += 45
                reasons.append("button-size")
        elif message_rect:
            msg_left, _msg_top, msg_right, msg_bottom = message_rect
            if rect[1] >= msg_bottom:
                score += 55
                reasons.append("below-message")
            if rect[0] >= msg_left + (msg_right - msg_left) * 0.65:
                score += 45
                reasons.append("message-right")
        if root_rect:
            root_h = max(1, root_rect[3] - root_rect[1])
            root_w = max(1, root_rect[2] - root_rect[0])
            if rect[1] > root_rect[1] + root_h * 0.58:
                score += 30
                reasons.append("lower-area")
            if rect[0] > root_rect[0] + root_w * 0.58:
                score += 25
                reasons.append("right-area")
        if depth <= 1:
            score -= 35
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _score_title(
        self,
        control: Any,
        depth: int,
        root_rect: tuple[int, int, int, int] | None,
        message_rect: tuple[int, int, int, int] | None,
    ) -> tuple[int, str]:
        name = trim(safe_prop(control, "Name"), 100)
        if not self._looks_like_title(name):
            return 0, ""
        rect = safe_rect_tuple(control)
        if not rect:
            return 0, ""
        score = 0
        reasons: list[str] = []
        text = _control_text(control)
        if "Text" in text or "Button" in text or "Name" in text:
            score += 20
            reasons.append("title-like-type")
        if any(value in text for value in {"title", "header", "name", "contact", "profile"}):
            score += 30
            reasons.append("header-keyword")
        if message_rect:
            header_top = message_rect[1] - 90
            header_bottom = message_rect[1] - 4
            if rect[1] < header_top or rect[3] > header_bottom:
                return 0, ""
            if rect[3] <= message_rect[1] + 40:
                score += 45
                reasons.append("above-message-area")
            if rect[0] >= message_rect[0] - 80 and rect[2] <= message_rect[2] + 200:
                score += 30
                reasons.append("aligned-message-area")
            if rect[0] >= message_rect[0] - 8 and rect[0] <= message_rect[0] + 260:
                score += 55
                reasons.append("chat-title-x")
            if rect[0] > message_rect[2] - 260:
                score -= 45
                reasons.append("right-toolbar-penalty")
        elif root_rect:
            root_h = max(1, root_rect[3] - root_rect[1])
            if rect[1] < root_rect[1] + root_h * 0.25:
                score += 40
                reasons.append("top-area")
        if depth <= 1:
            score -= 20
            reasons.append("root-penalty")
        return score, ",".join(reasons)

    def _looks_like_title(self, value: str) -> bool:
        if not value or value in self.q.generic_text_blocklist:
            return False
        if len(value) > 80:
            return False
        if "\n" in value:
            return False
        if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", value):
            return False
        if re.fullmatch(r"[\d\s:：/\-.年月日]+", value):
            return False
        blocked_keywords = [
            "切换",
            "经典模式",
            "天气",
            "头像",
            "在线状态",
            "个性签名",
            "点击",
            "窗口控制",
            "发起群聊",
            "聊天记录",
            "语音消息",
        ]
        if any(keyword in value for keyword in blocked_keywords):
            return False
        lowered = value.lower()
        if lowered in {"qq", "send", "search", "close", "minimize", "maximize"}:
            return False
        return True


def _control_text(control: Any) -> str:
    return " ".join(
        [
            safe_prop(control, "AutomationId"),
            safe_prop(control, "ClassName"),
            safe_prop(control, "ControlTypeName"),
            safe_prop(control, "LocalizedControlType"),
            safe_prop(control, "Name"),
        ]
    )


def _join_reasons(left: str, right: str) -> str:
    return ",".join(value for value in (left, right) if value)
