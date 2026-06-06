from __future__ import annotations

import re
import time
from dataclasses import dataclass
from typing import Any

from .detector import WechatDetector
from .role_judgement import MessageRoleJudgement, judge_message_role
from .uia_scoring import (
    find_message_list_candidates,
    normalize_text,
    safe_prop,
    safe_rect_repr,
    safe_rect_tuple,
    walk_controls,
)
from .wechat_logging import get_logger


logger = get_logger(__name__)


@dataclass(frozen=True)
class VisibleMessageBatch:
    display_name: str
    samples: list[Any]
    context: ChatContext | None = None


@dataclass(frozen=True)
class ChatContext:
    platform: str = "wechat"
    chat_title: str = ""
    user_id: str = ""
    is_group: bool = False
    member_count: int | None = None
    session_id: str = ""


@dataclass(frozen=True)
class ContextCandidate:
    control: Any
    depth: int
    score: int
    reason: str
    name: str


class WechatVisibleMessageReader:
    def __init__(self, detector: WechatDetector | None = None) -> None:
        self._detector = detector or WechatDetector()

    def read_visible_messages(
        self,
        *,
        display_name: str = "",
        limit: int = 30,
        tail_only: bool = False,
    ) -> VisibleMessageBatch:
        from rpa.platforms.wechat.uia import WechatUiaMessageSample, collect_chat_message_samples

        started_at = time.perf_counter()
        stage_started_at = time.perf_counter()
        win = self._detector.find_main_window_control()
        logger.info(
            "read_visible_messages_timing stage=find_main_window_control ms=%.1f found=%s",
            (time.perf_counter() - stage_started_at) * 1000.0,
            bool(win),
        )
        if win is None:
            raise RuntimeError("wechat_window_not_found")

        resolved_name = (display_name or self._detector.current_chat_name(win) or "current").strip()
        window_hwnd = int(getattr(win, "NativeWindowHandle", 0) or 0)
        collect_limit = max(limit, 40) if tail_only else limit
        logger.info(
            "read_visible_messages start display_name=%s limit=%s tail_only=%s",
            resolved_name,
            limit,
            tail_only,
        )
        stage_started_at = time.perf_counter()
        message_list = _find_message_list_root(win)
        logger.info(
            "read_visible_messages_timing stage=find_message_list_root ms=%.1f found=%s",
            (time.perf_counter() - stage_started_at) * 1000.0,
            bool(message_list),
        )
        stage_started_at = time.perf_counter()
        samples = collect_chat_message_samples(win, max_items=collect_limit)
        logger.info(
            "read_visible_messages_timing stage=collect_chat_message_samples ms=%.1f samples=%s",
            (time.perf_counter() - stage_started_at) * 1000.0,
            len(samples),
        )
        if not samples:
            stage_started_at = time.perf_counter()
            samples = _collect_messages_by_scored_list(
                win,
                limit=collect_limit,
                sample_type=WechatUiaMessageSample,
                window_hwnd=window_hwnd,
                message_list=message_list,
            )
            logger.info(
                "read_visible_messages_timing stage=collect_messages_by_scored_list ms=%.1f samples=%s",
                (time.perf_counter() - stage_started_at) * 1000.0,
                len(samples),
            )
        else:
            stage_started_at = time.perf_counter()
            samples = _attach_roles_to_samples(samples, message_list=message_list, window_hwnd=window_hwnd)
            logger.info(
                "read_visible_messages_timing stage=attach_roles ms=%.1f samples=%s",
                (time.perf_counter() - stage_started_at) * 1000.0,
                len(samples),
            )
        if tail_only and len(samples) > limit:
            samples = samples[-limit:]
        stage_started_at = time.perf_counter()
        context = self.get_chat_context(win)
        logger.info(
            "read_visible_messages_timing stage=get_chat_context ms=%.1f chat_title=%s",
            (time.perf_counter() - stage_started_at) * 1000.0,
            context.chat_title,
        )
        if not context.chat_title and resolved_name:
            context = ChatContext(
                platform=context.platform,
                chat_title=resolved_name,
                user_id=resolved_name,
                is_group=context.is_group,
                member_count=context.member_count,
                session_id=context.session_id or self.build_session_id(resolved_name, context.is_group),
            )
        logger.info(
            "read_visible_messages done display_name=%s samples=%s chat_title=%s session_id=%s",
            resolved_name,
            len(samples),
            context.chat_title,
            context.session_id,
        )
        logger.info(
            "read_visible_messages_timing stage=total ms=%.1f display_name=%s samples=%s",
            (time.perf_counter() - started_at) * 1000.0,
            resolved_name,
            len(samples),
        )
        return VisibleMessageBatch(display_name=resolved_name, samples=samples, context=context)

    def get_chat_context(self, window_control: Any | None = None) -> ChatContext:
        window_control = window_control or self._detector.get_window_control()
        chat_title = self._resolve_chat_title(window_control)
        member_count = self._extract_member_count(window_control)
        is_group = self._is_group_chat(chat_title, member_count)
        user_id = chat_title.strip()
        session_id = self.build_session_id(chat_title, is_group)
        return ChatContext(
            platform="wechat",
            chat_title=chat_title.strip(),
            user_id=user_id,
            is_group=is_group,
            member_count=member_count,
            session_id=session_id,
        )

    def build_session_id(self, chat_title: str, is_group: bool) -> str:
        token = re.sub(r"\s+", "_", chat_title.strip()) or "current"
        return f"wechat_{token}_group" if is_group else f"wechat_{token}"

    def build_openai_request(self, messages: list[Any], context: ChatContext | None = None) -> dict[str, Any]:
        context = context or self.get_chat_context()
        payload_messages: list[dict[str, str]] = []
        for message in messages:
            role = _extract_message_role(message)
            content = _extract_message_content(message)
            if not content:
                continue
            payload_messages.append({"role": role, "content": content})

        return {
            "messages": payload_messages,
            "user": context.user_id,
            "session_id": context.session_id,
            "extra_body": {
                "platform": context.platform,
                "context": {
                    "chat_title": context.chat_title,
                    "user_id": context.user_id,
                    "is_group": context.is_group,
                    "member_count": context.member_count,
                },
            },
        }

    def find_chat_title_candidates(self, window_control: Any | None = None) -> list[ContextCandidate]:
        window_control = window_control or self._detector.get_window_control()
        if not window_control:
            return []

        message_list = self._detector._find_message_list(window_control)
        message_rect = _safe_rect(message_list) if message_list else None
        session_list = self._detector.get_session_list(window_control)
        session_rect = _safe_rect(session_list) if session_list else None
        window_name = _safe_prop(window_control, "Name").strip()

        candidates: list[ContextCandidate] = []
        for depth, control in walk_controls(window_control, max_depth=12, max_nodes=2500):
            score, reason = self._score_chat_title_candidate(
                control=control,
                message_rect=message_rect,
                session_rect=session_rect,
                window_name=window_name,
            )
            if score > 0:
                candidates.append(
                    ContextCandidate(
                        control=control,
                        depth=depth,
                        score=score,
                        reason=reason,
                        name=_safe_prop(control, "Name").strip(),
                    )
                )

        return sorted(candidates, key=lambda item: item.score, reverse=True)

    def _resolve_chat_title(self, window_control: Any | None) -> str:
        if not window_control:
            return ""

        selected_session_title = self._detector.get_selected_session_title(window_control).strip()
        if selected_session_title and not self._is_generic_context_name(selected_session_title, _safe_prop(window_control, "Name")):
            return selected_session_title

        direct = self._detector.get_current_chat_name(window_control).strip()
        if direct and not self._is_generic_context_name(direct, _safe_prop(window_control, "Name")):
            return direct

        candidates = self.find_chat_title_candidates(window_control)
        if candidates:
            return candidates[0].name

        return _safe_prop(window_control, "Name").strip()

    def _score_chat_title_candidate(
        self,
        control: Any,
        message_rect: tuple[int, int, int, int] | None,
        session_rect: tuple[int, int, int, int] | None,
        window_name: str,
    ) -> tuple[int, str]:
        name = _safe_prop(control, "Name").strip()
        if not name or "\n" in name:
            return 0, ""
        if self._is_generic_context_name(name, window_name):
            return 0, ""

        rect = _safe_rect(control)
        if rect is None:
            return 0, ""

        automation_id = _safe_prop(control, "AutomationId")
        control_type = _safe_prop(control, "ControlTypeName") or _safe_prop(control, "LocalizedControlType")
        class_name = _safe_prop(control, "ClassName")

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

    def _extract_member_count(self, window_control: Any | None) -> int | None:
        if not window_control:
            return None

        for _, control in walk_controls(window_control, max_depth=8, max_nodes=1200):
            automation_id = _safe_prop(control, "AutomationId")
            name = _safe_prop(control, "Name")
            if "count" not in automation_id.lower() and "成员" not in name and "人数" not in name:
                continue
            match = re.search(r"(\d+)", name)
            if match:
                return int(match.group(1))
        return None

    def _is_group_chat(self, chat_title: str, member_count: int | None) -> bool:
        if member_count and member_count > 1:
            return True
        return bool(chat_title and "群" in chat_title)

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
        stripped = name.strip()
        if stripped in generic_names:
            return True
        return bool(window_name and stripped == window_name.strip())


def _find_message_list_root(win: Any) -> Any | None:
    candidates = find_message_list_candidates(win)
    return candidates[0].control if candidates and candidates[0].score >= 60 else None


def _safe_prop(control: Any, name: str) -> str:
    try:
        value = getattr(control, name, "")
        return "" if value is None else str(value)
    except Exception:
        return ""


def _safe_rect(control: Any) -> tuple[int, int, int, int] | None:
    try:
        rect = control.BoundingRectangle
        return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)
    except Exception:
        return None


def _extract_message_role(message: Any) -> str:
    if isinstance(message, dict):
        role = message.get("role", "user")
        return str(role) if role else "user"
    role = getattr(message, "role", "")
    if role:
        return str(role)
    return "user"


def _extract_message_content(message: Any) -> str:
    if isinstance(message, dict):
        return normalize_text(str(message.get("content", "")))
    for attr in ("content", "text", "message"):
        value = getattr(message, attr, "")
        if value:
            return normalize_text(str(value))
    if isinstance(message, tuple) and len(message) >= 2:
        return normalize_text(str(message[1]))
    return ""


def _collect_messages_by_scored_list(
    win: Any,
    *,
    limit: int,
    sample_type: Any,
    window_hwnd: int,
    message_list: Any | None,
) -> list[Any]:
    message_root = message_list
    if message_root is None:
        return []

    found: list[Any] = []
    for index, control in enumerate(
        item for _depth, item in walk_controls(message_root, max_depth=5, max_nodes=2500) if _is_message_control(item)
    ):
        if len(found) >= limit:
            break
        text = _extract_text(control)
        if not text:
            continue
        rect = safe_rect_tuple(control)
        left, top, right, bottom = rect if rect is not None else (0, 0, 0, 0)
        role = judge_message_role(control, message_list=message_root, window_hwnd=window_hwnd)
        sample = sample_type(
            kind=_message_kind(control),
            name=text,
            class_name=safe_prop(control, "ClassName"),
            automation_id=safe_prop(control, "AutomationId") or f"scored_message_{index}",
            control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
            rect=safe_rect_repr(control),
            left=float(left),
            top=float(top),
            right=float(right),
            bottom=float(bottom),
        )
        found.append(_attach_role(sample, role))
    found.sort(key=lambda item: (item.top, item.left, item.right, item.kind))
    return found


def _attach_roles_to_samples(samples: list[Any], *, message_list: Any | None, window_hwnd: int = 0) -> list[Any]:
    return [
        _attach_role(item, judge_message_role(item, message_list=message_list, window_hwnd=window_hwnd))
        for item in samples
    ]


def _attach_role(sample: Any, role: MessageRoleJudgement) -> Any:
    try:
        object.__setattr__(sample, "direction", role.direction)
        object.__setattr__(sample, "sender_role", role.role)
        object.__setattr__(sample, "direction_method", role.method)
        object.__setattr__(sample, "role_confidence", role.confidence)
        object.__setattr__(sample, "left_variance", role.left_variance)
        object.__setattr__(sample, "right_variance", role.right_variance)
    except Exception:
        pass
    return sample


def _is_message_control(control: Any) -> bool:
    automation_id = safe_prop(control, "AutomationId")
    class_name = safe_prop(control, "ClassName")
    control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
    if "ChatItemView" in class_name and "ChatTextItemView" not in class_name:
        return False
    if "chat_bubble_item_view" in automation_id:
        return True
    if "ChatTextItemView" in class_name or "ChatBubbleReferItemView" in class_name:
        return True
    return "ListItem" in control_type and ("chat" in class_name.lower() or "chat" in automation_id.lower())


def _message_kind(control: Any) -> str:
    class_name = safe_prop(control, "ClassName")
    name = normalize_text(safe_prop(control, "Name"))
    if name in {"[图片]", "[Image]"}:
        return "image"
    if name in {"[动画表情]", "[表情]", "[Emoji]"}:
        return "emoji"
    if "ChatBubbleReferItemView" in class_name:
        return "bubble_ref"
    return "text"


def _extract_text(control: Any) -> str:
    values: list[str] = []
    for attr in ("Name", "Value", "LegacyIAccessibleName", "HelpText"):
        value = normalize_text(safe_prop(control, attr))
        if value:
            values.append(value)
    if not values:
        for depth, child in walk_controls(control, max_depth=2, max_nodes=80):
            if depth == 0:
                continue
            value = normalize_text(safe_prop(child, "Name"))
            if value:
                values.append(value)

    seen: set[str] = set()
    lines: list[str] = []
    for value in values:
        for line in str(value).replace("\r", "\n").split("\n"):
            line = normalize_text(line)
            if line and line not in seen:
                seen.add(line)
                lines.append(line)
    return "\n".join(lines)
