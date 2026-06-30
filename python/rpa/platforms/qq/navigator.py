from __future__ import annotations

import time
from dataclasses import dataclass
import ctypes
from ctypes import wintypes
import re

from .config import AppConfig, load_config
from .reader import (
    QQConversationItem,
    QQReader,
    clean_text,
    collect_conversation_items,
    find_conversation_list_root,
    parse_rect,
    _conversation_item_controls,
)
from .qq_logging import get_logger
from .uia import safe_prop, safe_rect_tuple, uia_guard, walk_controls

logger = get_logger(__name__)

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
SW_RESTORE = 9
HWND_TOP = 0
HWND_TOPMOST = -1
HWND_NOTOPMOST = -2
SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
SWP_SHOWWINDOW = 0x0040


@dataclass(frozen=True)
class ForegroundActivationResult:
    ok: bool
    target_hwnd: int
    foreground_hwnd: int = 0
    detail: str = ""
    elapsed_ms: float = 0.0


@dataclass(frozen=True)
class SwitchConversationResult:
    ok: bool
    stage: str
    target: str
    before_title: str = ""
    after_title: str = ""
    matched_title: str = ""
    matched_rect: str = ""
    method: str = ""
    detail: str = ""


class QQNavigator:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.reader = QQReader(self.config)

    def switch_to_conversation(
        self,
        title: str,
        *,
        wait_seconds: float = 0.6,
        activate: bool = True,
        use_uia: bool = True,
        uia_only: bool = False,
    ) -> SwitchConversationResult:
        target = clean_text(title)
        if not target:
            return SwitchConversationResult(ok=False, stage="validate", target=title, detail="empty_title")

        before = self.reader.read_conversations(limit=120)
        if not before.ok:
            return SwitchConversationResult(
                ok=False,
                stage="scan_before",
                target=target,
                before_title=before.current_title,
                detail=before.detail,
            )

        match = find_conversation_match(before.conversations, target)
        if match is None:
            return SwitchConversationResult(
                ok=False,
                stage="match",
                target=target,
                before_title=before.current_title,
                detail="conversation_not_visible",
            )

        uia_failure_detail = ""
        if use_uia:
            uia_result = self._switch_with_uia_pattern(target, before.current_title, match, wait_seconds)
            if uia_result.ok or uia_only:
                return uia_result
            uia_failure_detail = f"uia_failed:{uia_result.stage}:{uia_result.detail or '-'}"

        handle = self.reader.detector.find_current_chat()
        if handle is None:
            return SwitchConversationResult(
                ok=False,
                stage="find_window",
                target=target,
                before_title=before.current_title,
                matched_title=match.title,
                matched_rect=match.rect,
                detail=_join_detail(uia_failure_detail, "qq_window_not_found"),
            )

        window = handle.window.window
        if not rect_inside(parse_rect(match.rect), window.rect):
            return SwitchConversationResult(
                ok=False,
                stage="bounds",
                target=target,
                before_title=before.current_title,
                matched_title=match.title,
                matched_rect=match.rect,
                detail=_join_detail(uia_failure_detail, f"conversation_rect_outside_window:{window.rect}"),
            )

        method = "foreground_click" if activate else "direct_click"
        if activate:
            if not bring_window_to_foreground(window.hwnd):
                return SwitchConversationResult(
                    ok=False,
                    stage="foreground",
                    target=target,
                    before_title=before.current_title,
                    matched_title=match.title,
                    matched_rect=match.rect,
                    method=method,
                    detail=_join_detail(uia_failure_detail, "set_foreground_failed"),
                )
            if not is_foreground_window(window.hwnd):
                return SwitchConversationResult(
                    ok=False,
                    stage="foreground",
                    target=target,
                    before_title=before.current_title,
                    matched_title=match.title,
                    matched_rect=match.rect,
                    method=method,
                    detail=_join_detail(uia_failure_detail, f"foreground_hwnd=0x{get_foreground_window():X}"),
                )

        if not click_conversation_item(match):
            return SwitchConversationResult(
                ok=False,
                stage="click",
                target=target,
                before_title=before.current_title,
                matched_title=match.title,
                matched_rect=match.rect,
                method=method,
                detail=_join_detail(uia_failure_detail, "click_failed"),
            )

        return self._verify_after_switch(
            target,
            before.current_title,
            match,
            wait_seconds,
            method,
            action_detail=uia_failure_detail,
        )

    def switch_to_conversation_item(
        self,
        item: QQConversationItem,
        *,
        current_title: str = "",
        wait_seconds: float = 0.6,
        activate: bool = True,
        use_uia: bool = True,
        uia_only: bool = False,
    ) -> SwitchConversationResult:
        target = clean_text(item.title)
        if not target:
            return SwitchConversationResult(ok=False, stage="validate", target=item.title, detail="empty_title")

        started_at = time.perf_counter()
        uia_failure_detail = ""
        if use_uia:
            uia_result = self._switch_with_uia_pattern(
                target,
                current_title,
                item,
                wait_seconds,
                use_prescanned_item=True,
            )
            logger.info(
                "qq switch_prescanned timing target=%s method=%s ok=%s stage=%s elapsed_ms=%.1f detail=%s",
                target,
                uia_result.method or "-",
                uia_result.ok,
                uia_result.stage,
                _elapsed_ms(started_at),
                uia_result.detail or "-",
            )
            if uia_result.ok or uia_only:
                return uia_result
            uia_failure_detail = f"uia_failed:{uia_result.stage}:{uia_result.detail or '-'}"

        handle = self.reader.detector.find_current_chat()
        if handle is None:
            return SwitchConversationResult(
                ok=False,
                stage="find_window",
                target=target,
                before_title=current_title,
                matched_title=item.title,
                matched_rect=item.rect,
                detail=_join_detail(uia_failure_detail, "qq_window_not_found"),
            )

        window = handle.window.window
        if not rect_inside(parse_rect(item.rect), window.rect):
            return SwitchConversationResult(
                ok=False,
                stage="bounds",
                target=target,
                before_title=current_title,
                matched_title=item.title,
                matched_rect=item.rect,
                detail=_join_detail(uia_failure_detail, f"conversation_rect_outside_window:{window.rect}"),
            )

        method = "foreground_click" if activate else "direct_click"
        if activate:
            if not bring_window_to_foreground(window.hwnd):
                return SwitchConversationResult(
                    ok=False,
                    stage="foreground",
                    target=target,
                    before_title=current_title,
                    matched_title=item.title,
                    matched_rect=item.rect,
                    method=method,
                    detail=_join_detail(uia_failure_detail, "set_foreground_failed"),
                )
            if not is_foreground_window(window.hwnd):
                return SwitchConversationResult(
                    ok=False,
                    stage="foreground",
                    target=target,
                    before_title=current_title,
                    matched_title=item.title,
                    matched_rect=item.rect,
                    method=method,
                    detail=_join_detail(uia_failure_detail, f"foreground_hwnd=0x{get_foreground_window():X}"),
                )

        if not click_conversation_item(item):
            return SwitchConversationResult(
                ok=False,
                stage="click",
                target=target,
                before_title=current_title,
                matched_title=item.title,
                matched_rect=item.rect,
                method=method,
                detail=_join_detail(uia_failure_detail, "click_failed"),
            )

        result = self._verify_after_switch(
            target,
            current_title,
            item,
            wait_seconds,
            method,
            action_detail=uia_failure_detail,
        )
        logger.info(
            "qq switch_prescanned timing target=%s method=%s ok=%s stage=%s elapsed_ms=%.1f detail=%s",
            target,
            result.method or "-",
            result.ok,
            result.stage,
            _elapsed_ms(started_at),
            result.detail or "-",
        )
        return result

    def _switch_with_uia_pattern(
        self,
        target: str,
        before_title: str,
        match: QQConversationItem,
        wait_seconds: float,
        *,
        use_prescanned_item: bool = False,
    ) -> SwitchConversationResult:
        found = (
            find_conversation_control_for_item(self.reader, match)
            if use_prescanned_item
            else find_conversation_control_for_title(self.reader, target)
        )
        if found is None:
            return SwitchConversationResult(
                ok=False,
                stage="uia_match",
                target=target,
                before_title=before_title,
                matched_title=match.title,
                matched_rect=match.rect,
                method="uia_pattern",
                detail="conversation_control_not_found",
            )
        matched_item, control = found
        ok, detail = try_activate_conversation_control(control)
        if not ok:
            return SwitchConversationResult(
                ok=False,
                stage="uia_pattern",
                target=target,
                before_title=before_title,
                matched_title=matched_item.title,
                matched_rect=matched_item.rect,
                method="uia_pattern",
                detail=detail,
            )

        return self._verify_after_switch(
            target,
            before_title,
            matched_item,
            wait_seconds,
            "uia_pattern",
            action_detail=detail,
        )

    def _verify_after_switch(
        self,
        target: str,
        before_title: str,
        matched_item: QQConversationItem,
        wait_seconds: float,
        method: str,
        *,
        action_detail: str = "",
    ) -> SwitchConversationResult:
        started_at = time.perf_counter()
        time.sleep(max(0.1, min(wait_seconds, 3.0)))

        selected, selected_detail = verify_conversation_selected(self.reader, matched_item)
        if selected:
            logger.info(
                "qq switch verify timing target=%s method=%s ok=True verify=selection elapsed_ms=%.1f detail=%s",
                target,
                method,
                _elapsed_ms(started_at),
                selected_detail or "-",
            )
            return SwitchConversationResult(
                ok=True,
                stage="switched",
                target=target,
                before_title=before_title,
                matched_title=matched_item.title,
                matched_rect=matched_item.rect,
                method=method,
                detail=_join_detail(action_detail, "verify=selection"),
            )

        header_title = read_fast_header_title(
            self.reader,
            expected_titles=[target, matched_item.title],
            diagnose=True,
        )
        if titles_match(header_title, target) or titles_match(header_title, matched_item.title):
            logger.info(
                "qq switch verify timing target=%s method=%s ok=True verify=header elapsed_ms=%.1f header_title=%s detail=%s",
                target,
                method,
                _elapsed_ms(started_at),
                header_title,
                selected_detail or "-",
            )
            return SwitchConversationResult(
                ok=True,
                stage="switched",
                target=target,
                before_title=before_title,
                after_title=header_title,
                matched_title=matched_item.title,
                matched_rect=matched_item.rect,
                method=method,
                detail=_join_detail(action_detail, "verify=header"),
            )

        after = self.reader.read_conversations(limit=120)
        after_title = after.current_title
        switched = titles_match(after_title, target) or titles_match(after_title, matched_item.title)
        logger.info(
            "qq switch verify timing target=%s method=%s ok=%s verify=title_scan elapsed_ms=%.1f header_title=%s after_title=%s detail=%s",
            target,
            method,
            switched,
            _elapsed_ms(started_at),
            header_title or "-",
            after_title or "-",
            selected_detail or "-",
        )
        return SwitchConversationResult(
            ok=switched,
            stage="switched" if switched else "verify",
            target=target,
            before_title=before_title,
            after_title=after_title,
            matched_title=matched_item.title,
            matched_rect=matched_item.rect,
            method=method,
            detail=_join_detail(action_detail, "" if switched else "after_title_mismatch"),
        )


def find_conversation_match(items: list[QQConversationItem], target: str) -> QQConversationItem | None:
    normalized = clean_text(target)
    exact = [item for item in items if clean_text(item.title) == normalized]
    if exact:
        return exact[0]
    contains = [item for item in items if normalized in clean_text(item.title) or clean_text(item.title) in normalized]
    return contains[0] if contains else None


def titles_match(left: str, right: str) -> bool:
    left_text = clean_text(left)
    right_text = clean_text(right)
    if not left_text or not right_text:
        return False
    normalized_left = normalize_chat_title_for_match(left_text)
    normalized_right = normalize_chat_title_for_match(right_text)
    if not normalized_left or not normalized_right:
        return left_text == right_text
    return (
        left_text == right_text
        or normalized_left == normalized_right
        or normalized_left in normalized_right
        or normalized_right in normalized_left
    )


def normalize_chat_title_for_match(value: str) -> str:
    text = clean_text(value)
    text = re.sub(r"\s*[\(（]\d{1,5}[\)）]\s*$", "", text)
    return clean_text(text)


def _join_detail(*parts: str) -> str:
    return " | ".join(part for part in parts if part)


def find_conversation_control_for_title(
    reader: QQReader,
    target: str,
) -> tuple[QQConversationItem, object] | None:
    with uia_guard("qq_find_conversation_control"):
        handle = reader.detector.find_current_chat()
        if handle is None:
            return None
        conversation_root = find_conversation_list_root(handle.chat_root)
        if conversation_root is None:
            return None
        controls = _conversation_item_controls(conversation_root)
        items = collect_conversation_items(conversation_root, current_title="")
        match = find_conversation_match(items, target)
        if match is None:
            return None
        for item, control in zip(items, controls):
            if item.index == match.index and clean_text(item.title) == clean_text(match.title):
                return item, control
        return None


def find_conversation_control_for_item(
    reader: QQReader,
    target: QQConversationItem,
) -> tuple[QQConversationItem, object] | None:
    with uia_guard("qq_find_conversation_control_for_item"):
        handle = reader.detector.find_current_chat()
        if handle is None:
            return None
        conversation_root = find_conversation_list_root(handle.chat_root)
        if conversation_root is None:
            return None
        controls = _conversation_item_controls(conversation_root)
        index = int(target.index or 0)
        if 1 <= index <= len(controls):
            control = controls[index - 1]
            if _conversation_control_matches_item(control, target):
                return target, control

        target_rect = parse_rect(target.rect)
        if target_rect is not None:
            for control in controls:
                if rects_close(safe_rect_tuple(control), target_rect, margin=10):
                    return target, control

        items = collect_conversation_items(conversation_root, current_title="")
        match = find_conversation_match(items, target.title)
        if match is None:
            return None
        for item, control in zip(items, controls):
            if item.index == match.index and clean_text(item.title) == clean_text(match.title):
                return item, control
        return None


def _conversation_control_matches_item(control: object, target: QQConversationItem) -> bool:
    target_rect = parse_rect(target.rect)
    control_rect = safe_rect_tuple(control)
    if target_rect is not None and rects_close(control_rect, target_rect, margin=10):
        return True
    texts = collect_conversation_items_from_single_control(control)
    return any(titles_match(text, target.title) for text in texts)


def collect_conversation_items_from_single_control(control: object) -> list[str]:
    texts: list[str] = []
    for _depth, item in walk_controls(control, max_depth=8, max_nodes=400):
        control_type = safe_prop(item, "ControlTypeName") or safe_prop(item, "LocalizedControlType")
        if control_type != "TextControl":
            continue
        text = clean_text(safe_prop(item, "Name"))
        if text:
            texts.append(text)
    return texts


def verify_conversation_selected(reader: QQReader, item: QQConversationItem) -> tuple[bool, str]:
    found = find_conversation_control_for_item(reader, item)
    if found is None:
        return False, "control_not_found"
    _matched_item, control = found
    selected, detail = _read_selection_state(control)
    if selected:
        return True, detail
    return False, detail or "selection_unknown"


def _read_selection_state(control: object) -> tuple[bool, str]:
    try:
        pattern = control.GetSelectionItemPattern()  # type: ignore[attr-defined]
    except Exception as exc:
        pattern = None
        selection_detail = f"selection:{exc.__class__.__name__}"
    else:
        selection_detail = "selection:missing"
    if pattern:
        for attr in ("IsSelected", "CurrentIsSelected"):
            try:
                value = getattr(pattern, attr)
            except Exception:
                continue
            if callable(value):
                try:
                    value = value()
                except Exception:
                    continue
            if value is not None:
                return bool(value), f"selection:{attr}={bool(value)}"

    try:
        legacy = control.GetLegacyIAccessiblePattern()  # type: ignore[attr-defined]
    except Exception as exc:
        return False, _join_detail(selection_detail, f"legacy:{exc.__class__.__name__}")
    if not legacy:
        return False, _join_detail(selection_detail, "legacy:missing")
    for attr in ("State", "CurrentState"):
        try:
            state = int(getattr(legacy, attr, 0) or 0)
        except Exception:
            continue
        if state:
            selected = bool(state & 0x2)
            return selected, _join_detail(selection_detail, f"legacy:{attr}=0x{state:X}")
    return False, _join_detail(selection_detail, "legacy:state_missing")


def read_fast_header_title(
    reader: QQReader,
    expected_titles: list[str] | None = None,
    *,
    diagnose: bool = False,
) -> str:
    with uia_guard("qq_read_fast_header_title"):
        handle = reader.detector.find_current_chat()
        if handle is None:
            if diagnose:
                logger.info("qq fast_header_title diagnose result=missing_chat")
            return ""
        root = handle.chat_root
        root_rect = safe_rect_tuple(root)
        if root_rect is None:
            if diagnose:
                logger.info("qq fast_header_title diagnose result=missing_root_rect")
            return ""
        left, top, right, bottom = root_rect
        width = max(1, right - left)
        header_left = left + int(width * 0.32)
        header_top = top
        header_bottom = min(bottom, top + 95)
        expected = [clean_text(item) for item in (expected_titles or []) if clean_text(item)]
        candidates: list[tuple[int, int, int, str]] = []
        diagnostics: list[tuple[int, str, str, str, str]] = []
        for _depth, control in walk_controls(root, max_depth=6, max_nodes=900):
            rect = safe_rect_tuple(control)
            if rect is None:
                continue
            text = clean_text(safe_prop(control, "Name"))
            if not text:
                continue
            control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
            rect_text = _format_rect(rect)
            if rect[0] < header_left or rect[1] < header_top or rect[3] > header_bottom:
                if diagnose and _is_near_header_rect(rect, root_rect):
                    diagnostics.append((0, "reject:range", control_type, rect_text, text))
                continue
            if not _looks_like_header_title(text):
                if diagnose:
                    diagnostics.append((0, "reject:text", control_type, rect_text, text))
                continue
            expected_match = any(titles_match(text, item) for item in expected)
            if "Text" not in control_type and not expected_match:
                if diagnose:
                    diagnostics.append((0, "reject:type", control_type, rect_text, text))
                continue
            score = _score_header_title_candidate(text, rect, control_type, root_rect, expected)
            candidates.append((score, -rect[1], -rect[0], text))
            if diagnose:
                diagnostics.append((score, "candidate", control_type, rect_text, text))
        if not candidates:
            if diagnose:
                _log_fast_header_title_diagnostics(
                    expected=expected,
                    root_rect=root_rect,
                    header_rect=(header_left, header_top, right, header_bottom),
                    selected="",
                    diagnostics=diagnostics,
                )
            return ""
        selected = max(candidates)[3]
        if diagnose:
            _log_fast_header_title_diagnostics(
                expected=expected,
                root_rect=root_rect,
                header_rect=(header_left, header_top, right, header_bottom),
                selected=selected,
                diagnostics=diagnostics,
            )
        return selected


def _log_fast_header_title_diagnostics(
    *,
    expected: list[str],
    root_rect: tuple[int, int, int, int],
    header_rect: tuple[int, int, int, int],
    selected: str,
    diagnostics: list[tuple[int, str, str, str, str]],
) -> None:
    ranked = sorted(diagnostics, key=lambda item: (item[1] != "candidate", -item[0], item[3], item[4]))[:14]
    detail = "; ".join(
        f"{reason}|score={score}|type={control_type or '-'}|rect={rect}|text={clean_text(text)[:80]}"
        for score, reason, control_type, rect, text in ranked
    )
    logger.info(
        "qq fast_header_title diagnose expected=%s selected=%s root_rect=%s header_rect=%s items=%s detail=%s",
        "|".join(expected) or "-",
        selected or "-",
        _format_rect(root_rect),
        _format_rect(header_rect),
        len(diagnostics),
        detail or "-",
    )


def _is_near_header_rect(
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
) -> bool:
    left, top, right, bottom = root_rect
    width = max(1, right - left)
    return rect[0] >= left + int(width * 0.25) and rect[1] >= top and rect[1] <= min(bottom, top + 150)


def _format_rect(rect: tuple[int, int, int, int] | None) -> str:
    if rect is None:
        return "-"
    return f"({rect[0]},{rect[1]},{rect[2]},{rect[3]})"


def _score_header_title_candidate(
    text: str,
    rect: tuple[int, int, int, int],
    control_type: str,
    root_rect: tuple[int, int, int, int],
    expected_titles: list[str],
) -> int:
    left, top, right, _bottom = root_rect
    width = max(1, right - left)
    candidate_width = max(0, rect[2] - rect[0])
    candidate_height = max(0, rect[3] - rect[1])
    score = 0
    if any(titles_match(text, item) for item in expected_titles):
        score += 1000
    if "Text" in control_type:
        score += 140
    elif "Button" in control_type:
        score += 20
    if 10 <= candidate_height <= 42:
        score += 80
    if 20 <= candidate_width <= width * 0.55:
        score += 40
    if top + 8 <= rect[1] <= top + 72:
        score += 35
    if left + width * 0.45 <= rect[0] <= left + width * 0.72:
        score += 45
    if len(text) <= 40:
        score += 10
    return score


def _looks_like_header_title(value: str) -> bool:
    text = clean_text(value)
    if not text or len(text) > 100:
        return False
    if text in {"QQ", "搜索", "关闭", "最小化", "最大化", "更多", "发起群聊", "窗口控制区域"}:
        return False
    if "窗口控制区域" in text:
        return False
    if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text):
        return False
    if re.fullmatch(r"[\d\s:/\\.\-()（）]+", text):
        return False
    return True


def try_activate_conversation_control(control: object) -> tuple[bool, str]:
    attempts: list[str] = []
    for method_name, caller in (
        ("SelectionItemPattern.Select", _try_selection_item_select),
        ("InvokePattern.Invoke", _try_invoke),
        ("LegacyIAccessible.DoDefaultAction", _try_legacy_default_action),
    ):
        ok, detail = caller(control)
        attempts.append(f"{method_name}:{detail}")
        if ok:
            return True, method_name
    return False, ";".join(attempts)


def _try_selection_item_select(control: object) -> tuple[bool, str]:
    try:
        pattern = control.GetSelectionItemPattern()  # type: ignore[attr-defined]
    except Exception as exc:
        return False, exc.__class__.__name__
    if not pattern:
        return False, "missing"
    try:
        pattern.Select()
        return True, "ok"
    except Exception as exc:
        return False, exc.__class__.__name__


def _try_invoke(control: object) -> tuple[bool, str]:
    try:
        pattern = control.GetInvokePattern()  # type: ignore[attr-defined]
    except Exception as exc:
        return False, exc.__class__.__name__
    if not pattern:
        return False, "missing"
    try:
        pattern.Invoke()
        return True, "ok"
    except Exception as exc:
        return False, exc.__class__.__name__


def _try_legacy_default_action(control: object) -> tuple[bool, str]:
    try:
        pattern = control.GetLegacyIAccessiblePattern()  # type: ignore[attr-defined]
    except Exception as exc:
        return False, exc.__class__.__name__
    if not pattern:
        return False, "missing"
    action = getattr(pattern, "DoDefaultAction", None)
    if action is None:
        return False, "missing"
    try:
        action()
        return True, "ok"
    except Exception as exc:
        return False, exc.__class__.__name__


def click_conversation_item(item: QQConversationItem) -> bool:
    rect = parse_rect(item.rect)
    if rect is None:
        return False
    left, top, right, bottom = rect
    if right <= left or bottom <= top:
        return False
    x = min(right - 24, left + 86)
    y = top + (bottom - top) // 2
    return click_point(x, y)


def bring_window_to_foreground(hwnd: int) -> bool:
    return activate_window_for_foreground_rpa(hwnd).ok


def activate_window_for_foreground_rpa(hwnd: int) -> ForegroundActivationResult:
    started_at = time.perf_counter()
    target = int(hwnd or 0)
    if not target:
        result = ForegroundActivationResult(
            ok=False,
            target_hwnd=0,
            detail="empty_hwnd",
            elapsed_ms=_elapsed_ms(started_at),
        )
        _log_foreground_activation_result(result)
        return result

    attempts: list[str] = []
    try:
        _restore_and_raise_window(target, attempts)
        if is_foreground_window(target):
            return _foreground_result(target, attempts, started_at)

        _attach_thread_and_foreground(target, attempts)
        if is_foreground_window(target):
            return _foreground_result(target, attempts, started_at)

        _topmost_foreground_attempt(target, attempts)
        if is_foreground_window(target):
            return _foreground_result(target, attempts, started_at)

        # One final direct foreground request after the z-order nudge gives
        # Windows a chance to accept activation without adding a mouse click.
        _call_bool("set_foreground_final", attempts, user32.SetForegroundWindow, wintypes.HWND(target))
        time.sleep(0.18)
        return _foreground_result(target, attempts, started_at)
    except Exception as exc:
        attempts.append(f"exception:{exc.__class__.__name__}")
        return _foreground_result(target, attempts, started_at)


def _restore_and_raise_window(hwnd: int, attempts: list[str]) -> None:
    _call_bool("show_restore", attempts, user32.ShowWindow, wintypes.HWND(hwnd), SW_RESTORE)
    time.sleep(0.05)
    _call_bool("bring_top", attempts, user32.BringWindowToTop, wintypes.HWND(hwnd))
    _call_bool(
        "set_window_pos_top",
        attempts,
        user32.SetWindowPos,
        wintypes.HWND(hwnd),
        wintypes.HWND(HWND_TOP),
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW,
    )
    _call_bool("set_active", attempts, user32.SetActiveWindow, wintypes.HWND(hwnd))
    _call_bool("set_foreground", attempts, user32.SetForegroundWindow, wintypes.HWND(hwnd))
    time.sleep(0.16)


def _attach_thread_and_foreground(hwnd: int, attempts: list[str]) -> None:
    foreground = get_foreground_window()
    current_thread = int(kernel32.GetCurrentThreadId() or 0)
    target_thread = _window_thread_id(hwnd)
    foreground_thread = _window_thread_id(foreground) if foreground else 0
    attached: list[int] = []
    try:
        for thread_id in (target_thread, foreground_thread):
            if thread_id and thread_id != current_thread and thread_id not in attached:
                if _call_bool("attach_thread", attempts, user32.AttachThreadInput, current_thread, thread_id, True):
                    attached.append(thread_id)
        _call_bool("bring_top_attached", attempts, user32.BringWindowToTop, wintypes.HWND(hwnd))
        _call_bool("set_active_attached", attempts, user32.SetActiveWindow, wintypes.HWND(hwnd))
        _call_bool("set_foreground_attached", attempts, user32.SetForegroundWindow, wintypes.HWND(hwnd))
        time.sleep(0.16)
    finally:
        for thread_id in reversed(attached):
            _call_bool("detach_thread", attempts, user32.AttachThreadInput, current_thread, thread_id, False)


def _topmost_foreground_attempt(hwnd: int, attempts: list[str]) -> None:
    _call_bool(
        "set_window_pos_topmost",
        attempts,
        user32.SetWindowPos,
        wintypes.HWND(hwnd),
        wintypes.HWND(HWND_TOPMOST),
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW,
    )
    time.sleep(0.04)
    _call_bool(
        "set_window_pos_notopmost",
        attempts,
        user32.SetWindowPos,
        wintypes.HWND(hwnd),
        wintypes.HWND(HWND_NOTOPMOST),
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW,
    )
    _call_bool("set_foreground_after_topmost", attempts, user32.SetForegroundWindow, wintypes.HWND(hwnd))
    time.sleep(0.18)


def _window_thread_id(hwnd: int) -> int:
    if not hwnd:
        return 0
    try:
        return int(user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), None) or 0)
    except Exception:
        return 0


def _call_bool(label: str, attempts: list[str], func: object, *args: object) -> bool:
    ctypes.set_last_error(0)
    try:
        result = bool(func(*args))  # type: ignore[misc]
    except Exception as exc:
        attempts.append(f"{label}:exception:{exc.__class__.__name__}")
        return False
    if result:
        attempts.append(f"{label}:ok")
        return True
    error = ctypes.get_last_error()
    attempts.append(f"{label}:failed:{error}")
    return False


def _foreground_result(hwnd: int, attempts: list[str], started_at: float) -> ForegroundActivationResult:
    foreground = get_foreground_window()
    result = ForegroundActivationResult(
        ok=bool(hwnd) and foreground == int(hwnd),
        target_hwnd=int(hwnd or 0),
        foreground_hwnd=foreground,
        detail=";".join(attempts),
        elapsed_ms=_elapsed_ms(started_at),
    )
    _log_foreground_activation_result(result)
    return result


def _log_foreground_activation_result(result: ForegroundActivationResult) -> None:
    logger.info(
        "qq foreground activation timing ok=%s target_hwnd=0x%X foreground_hwnd=0x%X elapsed_ms=%.1f detail=%s",
        result.ok,
        result.target_hwnd,
        result.foreground_hwnd,
        result.elapsed_ms,
        result.detail or "-",
    )


def _elapsed_ms(started_at: float) -> float:
    return (time.perf_counter() - started_at) * 1000.0


def get_foreground_window() -> int:
    try:
        return int(user32.GetForegroundWindow() or 0)
    except Exception:
        return 0


def is_foreground_window(hwnd: int) -> bool:
    return bool(hwnd) and get_foreground_window() == int(hwnd)


def rect_inside(
    inner: tuple[int, int, int, int] | None,
    outer: tuple[int, int, int, int],
    margin: int = 4,
) -> bool:
    if inner is None:
        return False
    return (
        inner[0] >= outer[0] - margin
        and inner[1] >= outer[1] - margin
        and inner[2] <= outer[2] + margin
        and inner[3] <= outer[3] + margin
    )


def rects_close(
    left: tuple[int, int, int, int] | None,
    right: tuple[int, int, int, int] | None,
    *,
    margin: int = 4,
) -> bool:
    if left is None or right is None:
        return False
    return all(abs(a - b) <= margin for a, b in zip(left, right))


def click_point(x: int, y: int) -> bool:
    try:
        import win32api
        import win32con
    except ImportError:
        return False
    try:
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, x, y, 0, 0)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, x, y, 0, 0)
        return True
    except Exception:
        return False
