from __future__ import annotations

import time
from dataclasses import dataclass

from .click_strategy import (
    ClickResult,
    click_send_button_detailed,
    click_session_item_detailed,
    click_session_item_foreground_detailed,
)
from .detector import WechatDetector
from .uia_scoring import find_input_candidates, find_send_button_candidates
from .wechat_logging import get_logger


logger = get_logger(__name__)


@dataclass(frozen=True)
class DraftResult:
    prepared: bool
    method: str
    strict_background_write_success: bool = False
    strict_background_write_method: str = ""
    strict_background_supported: bool = False
    strict_background_reason: str = ""


@dataclass(frozen=True)
class SendResult:
    sent: bool
    method: str
    foreground: bool = False


@dataclass(frozen=True)
class StrictBackgroundInputProbe:
    supported: bool
    reason: str
    input_hwnds: list[int]
    render_hosts: list[str]
    value_pattern_readable: bool
    value_before: str


class WechatMessageSender:
    def __init__(self, detector: WechatDetector | None = None) -> None:
        self._detector = detector or WechatDetector()

    def prepare_reply_draft(
        self,
        *,
        text: str,
        display_name: str = "",
        allow_foreground_fallback: bool = False,
    ) -> DraftResult:
        if not text:
            raise RuntimeError("empty_text")
        logger.info(
            "prepare_reply_draft start display_name=%s allow_foreground_fallback=%s text_len=%s",
            display_name or "current",
            allow_foreground_fallback,
            len(text),
        )

        from rpa.platforms.wechat.uia import (
            clear_chat_input_via_shortcuts,
            find_chat_input,
            focus_chat_input,
            paste_text_via_clipboard,
        )

        win = self._detector.find_main_window_control()
        if win is None:
            raise RuntimeError("wechat_window_not_found")

        hwnd = int(getattr(win, "NativeWindowHandle", 0) or 0)
        if display_name and not self._ensure_target_session_selected(
            win,
            display_name=display_name,
            allow_foreground_fallback=allow_foreground_fallback,
        ):
            raise RuntimeError("target_session_not_verified")

        input_ctrl = find_chat_input(win)
        if input_ctrl is None:
            input_candidates = find_input_candidates(win)
            input_ctrl = input_candidates[0].control if input_candidates and input_candidates[0].score >= 60 else None
        if input_ctrl is None:
            raise RuntimeError("chat_input_not_found")

        logger.info("chat input control=%s", _describe_control(input_ctrl))
        logger.info("chat input parent_chain=%s", _describe_parent_chain(input_ctrl))
        if _try_set_value(input_ctrl, text):
            logger.info("prepare_reply_draft done method=value_pattern")
            return DraftResult(prepared=True, method="value_pattern")

        if not allow_foreground_fallback:
            logger.warning("prepare_reply_draft background write failed and foreground fallback disabled")
            raise RuntimeError("chat_input_background_write_failed")

        logger.warning("prepare_reply_draft using foreground clipboard fallback")
        if not focus_chat_input(input_ctrl):
            raise RuntimeError("chat_input_focus_failed")
        clear_chat_input_via_shortcuts()
        paste_text_via_clipboard(text)
        logger.info("prepare_reply_draft done method=clipboard_paste")
        return DraftResult(prepared=True, method="clipboard_paste")

    def _ensure_target_session_selected(
        self,
        win: object,
        *,
        display_name: str,
        allow_foreground_fallback: bool,
        settle_ms: int = 120,
        verify_timeout_ms: int = 900,
    ) -> bool:
        if not display_name:
            return True

        expected = self._detector.extract_session_title(display_name)
        if not expected:
            return True

        current_selected = self._detector.get_selected_session_title(win)
        current_chat = self._detector.get_current_chat_name(win)
        if self._detector._titles_match(expected, current_selected) or self._detector._titles_match(expected, current_chat):
            logger.info(
                "ensure_target_session_selected fast_path display_name=%s selected=%s current=%s",
                expected,
                current_selected,
                current_chat,
            )
            return True

        session_list = self._detector.get_session_list(win)
        logger.info(
            "ensure_target_session_selected session_list_scanned display_name=%s found=%s",
            expected,
            bool(session_list),
        )

        session_ctrl = self._detector.find_session_control_in_list(session_list, expected, exact=True)
        if session_ctrl is None:
            session_ctrl = self._detector.find_session_control_in_list(session_list, expected, exact=False)
        if session_ctrl is None:
            session_ctrl = self._detector.find_session_control(win, expected, exact=False)
        if session_ctrl is None:
            logger.warning("ensure_target_session_selected target session not found display_name=%s", expected)
            return False

        verified = False
        clicked = self._click_and_verify_session(
            session_ctrl,
            win,
            expected=expected,
            allow_foreground=False,
            settle_ms=settle_ms,
            verify_timeout_ms=verify_timeout_ms,
        )
        if clicked.ok:
            verified = True

        if not verified and allow_foreground_fallback:
            logger.warning(
                "ensure_target_session_selected background click not verified display_name=%s method=%s; retrying foreground",
                expected,
                clicked.method,
            )
            clicked = self._click_and_verify_session(
                session_ctrl,
                win,
                expected=expected,
                allow_foreground=True,
                settle_ms=max(settle_ms, 180),
                verify_timeout_ms=max(verify_timeout_ms, 1200),
            )
            verified = clicked.ok

        logger.info(
            "ensure_target_session_selected verified display_name=%s ok=%s method=%s foreground=%s",
            expected,
            verified,
            clicked.method,
            clicked.foreground,
        )
        return verified

    def _click_and_verify_session(
        self,
        session_ctrl: object,
        win: object,
        *,
        expected: str,
        allow_foreground: bool,
        settle_ms: int,
        verify_timeout_ms: int,
    ) -> ClickResult:
        fallback_hwnd = int(getattr(win, "NativeWindowHandle", 0) or 0)
        clicked = (
            click_session_item_foreground_detailed(session_ctrl)
            if allow_foreground
            else click_session_item_detailed(session_ctrl, fallback_hwnd=fallback_hwnd, allow_foreground=False)
        )
        if not clicked.ok:
            logger.warning(
                "ensure_target_session_selected click failed display_name=%s allow_foreground=%s detail=%s",
                expected,
                allow_foreground,
                clicked.detail,
            )
            return clicked

        if settle_ms > 0:
            time.sleep(settle_ms / 1000.0)

        verified = self._detector.verify_session_switch(
            expected,
            win,
            timeout_ms=verify_timeout_ms,
        )
        if verified:
            return clicked

        selected = self._detector.get_selected_session_title(win)
        current = self._detector.get_current_chat_name(win)
        logger.warning(
            "ensure_target_session_selected click not verified display_name=%s method=%s foreground=%s selected=%s current=%s",
            expected,
            clicked.method,
            clicked.foreground,
            selected,
            current,
        )
        return ClickResult(ok=False, method=clicked.method, foreground=clicked.foreground, detail="session_switch_not_verified")

    def send_prepared_message(self, *, allow_foreground_fallback: bool = False) -> SendResult:
        from rpa.platforms.wechat.uia import find_send_button

        win = self._detector.find_main_window_control()
        send_button = find_send_button(win) if win is not None else None
        if send_button is None and win is not None:
            send_candidates = find_send_button_candidates(win)
            send_button = send_candidates[0].control if send_candidates and send_candidates[0].score >= 60 else None
        if send_button is None:
            raise RuntimeError("send_button_click_failed")
        fallback_hwnd = int(getattr(win, "NativeWindowHandle", 0) or 0) if win is not None else 0
        logger.info(
            "send_prepared_message start allow_foreground_fallback=%s fallback_hwnd=%s",
            allow_foreground_fallback,
            fallback_hwnd,
        )
        clicked = click_send_button_detailed(
            send_button,
            fallback_hwnd=fallback_hwnd,
            allow_foreground=allow_foreground_fallback,
        )
        if not clicked.ok:
            logger.warning("send_prepared_message click failed detail=%s", clicked.detail)
            raise RuntimeError("send_button_click_failed")
        logger.info(
            "send_prepared_message done method=%s foreground=%s",
            clicked.method,
            clicked.foreground,
        )
        return SendResult(sent=True, method=clicked.method, foreground=clicked.foreground)


def _try_win32_text_message(input_ctrl: object, text: str, *, fallback_hwnd: int = 0) -> bool:
    try:
        import win32con
        import win32gui
    except Exception as exc:
        logger.warning("win32 text input unavailable: %s", exc)
        return False

    handles = _native_window_handle_chain(input_ctrl)
    if not handles:
        logger.warning("win32 text input: input control has no native hwnd; trying render host hwnds")
        return _try_render_host_text_message(win32gui, win32con, fallback_hwnd, input_ctrl, text)

    for hwnd in handles:
        if not _is_window(hwnd):
            continue
        if _send_wm_settext(win32gui, win32con, hwnd, text):
            logger.info("win32 text input succeeded by WM_SETTEXT hwnd=%s", hwnd)
            return True
        if _send_em_replacesel(win32gui, win32con, hwnd, text):
            logger.info("win32 text input succeeded by EM_REPLACESEL hwnd=%s", hwnd)
            return True
        if _send_wm_char_sequence(win32gui, win32con, hwnd, text):
            logger.info("win32 text input succeeded by WM_CHAR hwnd=%s", hwnd)
            return True

    logger.warning("win32 text input failed for %s hwnd candidates", len(handles))
    return _try_render_host_text_message(win32gui, win32con, fallback_hwnd, input_ctrl, text)


def _probe_strict_background_input(input_ctrl: object, *, root_hwnd: int = 0) -> StrictBackgroundInputProbe:
    input_hwnds = _native_window_handle_chain(input_ctrl)
    nearby = _nearby_win32_child_windows(root_hwnd, input_ctrl)
    render_hosts = [
        f"{item.get('class_name')}:{item.get('hwnd')}"
        for item in nearby
        if item.get("class_name") in {"MMUIRenderSubWindowHW", "Chrome_WidgetWin_0", "Intermediate D3D Window"}
    ]
    value_before = _safe_input_value(input_ctrl)
    value_pattern_readable = _has_value_pattern(input_ctrl)

    if input_hwnds:
        return StrictBackgroundInputProbe(
            supported=True,
            reason="native_input_hwnd_available",
            input_hwnds=input_hwnds,
            render_hosts=render_hosts,
            value_pattern_readable=value_pattern_readable,
            value_before=value_before,
        )

    if render_hosts:
        return StrictBackgroundInputProbe(
            supported=False,
            reason="render_host_only_no_native_input_hwnd",
            input_hwnds=[],
            render_hosts=render_hosts,
            value_pattern_readable=value_pattern_readable,
            value_before=value_before,
        )

    return StrictBackgroundInputProbe(
        supported=False,
        reason="no_native_input_hwnd_or_render_host",
        input_hwnds=[],
        render_hosts=[],
        value_pattern_readable=value_pattern_readable,
        value_before=value_before,
    )


def _try_render_host_text_message(win32gui: object, win32con: object, root_hwnd: int, input_ctrl: object, text: str) -> bool:
    candidates = _nearby_win32_child_windows(root_hwnd, input_ctrl)
    if not candidates:
        logger.warning("render host text input skipped: no nearby win32 child candidates")
        return False

    for item in candidates:
        hwnd = int(item["hwnd"])
        class_name = str(item["class_name"])
        if class_name not in {"MMUIRenderSubWindowHW", "Chrome_WidgetWin_0", "Intermediate D3D Window"}:
            continue
        logger.warning(
            "render host candidate skipped hwnd=%s class=%s value_before=%s",
            hwnd,
            class_name,
            _safe_input_value_repr(input_ctrl),
        )

    logger.warning("render host text input skipped candidates=%s", _format_win32_child_candidates(candidates))
    return False


def _send_button_visible_after_input(input_ctrl: object) -> bool:
    root = input_ctrl
    try:
        current = input_ctrl
        for _ in range(16):
            if not current:
                break
            root = current
            current = current.GetParentControl()
    except Exception:
        pass
    try:
        from rpa.platforms.wechat.uia import find_send_button
        from rpa.platforms.wechat.uia_scoring import find_send_button_candidates

        send_button = find_send_button(root)
        if send_button is not None:
            return True
        candidates = find_send_button_candidates(root)
        return bool(candidates and candidates[0].score >= 60)
    except Exception as exc:
        logger.warning("render host text input verify failed: %s", exc)
        return False


def _safe_input_value(input_ctrl: object) -> str:
    try:
        get_pattern = getattr(input_ctrl, "GetValuePattern", None)
        if not get_pattern:
            return ""
        pattern = get_pattern()
        if not pattern:
            return ""
        get_value = getattr(pattern, "Value", None)
        if get_value is not None:
            return str(get_value)
        current_value = getattr(pattern, "CurrentValue", None)
        if current_value is not None:
            return str(current_value)
        get_value_fn = getattr(pattern, "GetValue", None)
        if get_value_fn:
            return str(get_value_fn())
    except Exception as exc:
        logger.warning("chat input value read failed: %s", exc)
    return ""


def _has_value_pattern(input_ctrl: object) -> bool:
    try:
        get_pattern = getattr(input_ctrl, "GetValuePattern", None)
        if not get_pattern:
            return False
        return bool(get_pattern())
    except Exception:
        return False


def _safe_input_value_repr(input_ctrl: object) -> str:
    value = _safe_input_value(input_ctrl)
    return f"len={len(value)} value={value[:80]!r}"


def _send_wm_settext(win32gui: object, win32con: object, hwnd: int, text: str) -> bool:
    try:
        result = win32gui.SendMessage(hwnd, win32con.WM_SETTEXT, 0, text)
        time.sleep(0.12)
        return bool(result) and _control_text_matches(hwnd, text)
    except Exception as exc:
        logger.warning("WM_SETTEXT failed hwnd=%s error=%s", hwnd, exc)
        return False


def _send_em_replacesel(win32gui: object, win32con: object, hwnd: int, text: str) -> bool:
    try:
        win32gui.SendMessage(hwnd, win32con.EM_SETSEL, 0, -1)
        result = win32gui.SendMessage(hwnd, win32con.EM_REPLACESEL, True, text)
        time.sleep(0.12)
        return bool(result) and _control_text_matches(hwnd, text)
    except Exception as exc:
        logger.warning("EM_REPLACESEL failed hwnd=%s error=%s", hwnd, exc)
        return False


def _send_wm_char_sequence(win32gui: object, win32con: object, hwnd: int, text: str) -> bool:
    if not text:
        return False
    try:
        win32gui.SendMessage(hwnd, win32con.WM_SETTEXT, 0, "")
        for ch in text:
            win32gui.SendMessage(hwnd, win32con.WM_CHAR, ord(ch), 0)
        time.sleep(0.12)
        return _control_text_matches(hwnd, text)
    except Exception as exc:
        logger.warning("WM_CHAR sequence failed hwnd=%s error=%s", hwnd, exc)
        return False


def _control_text_matches(hwnd: int, expected: str) -> bool:
    try:
        import win32gui

        actual = win32gui.GetWindowText(hwnd)
        if actual == expected:
            return True
        if actual:
            logger.warning("win32 text verify mismatch hwnd=%s actual_len=%s expected_len=%s", hwnd, len(actual), len(expected))
        return False
    except Exception as exc:
        logger.warning("win32 text verify failed hwnd=%s error=%s", hwnd, exc)
        return False


def _is_window(hwnd: int) -> bool:
    try:
        import win32gui

        return bool(hwnd and win32gui.IsWindow(hwnd))
    except Exception:
        return bool(hwnd)


def _native_window_handle_chain(control: object, max_depth: int = 10) -> list[int]:
    handles: list[int] = []
    current = control
    for _ in range(max_depth):
        if not current:
            break
        hwnd = getattr(current, "NativeWindowHandle", 0) or 0
        if hwnd and int(hwnd) not in handles:
            handles.append(int(hwnd))
        try:
            current = current.GetParentControl()
        except Exception:
            break
    return handles


def _describe_nearby_win32_children(root_hwnd: int, control: object, limit: int = 12) -> str:
    target_rect = _safe_rect_tuple(control)
    if target_rect is None:
        return "target_rect_unavailable"
    matches = _nearby_win32_child_windows(root_hwnd, control)
    if not matches:
        return f"none target_rect={target_rect}"
    parts = [
        f"target_rect={target_rect}",
        f"count={len(matches)}",
    ]
    for index, item in enumerate(matches[:limit]):
        parts.append(
            f"[{index}] class={item['class_name']} text={item['text']!r} rect={item['rect']} "
            f"overlap={item['overlap']} distance={item['distance']}"
        )
    return " | ".join(parts)


def _nearby_win32_child_windows(root_hwnd: int, control: object) -> list[dict[str, object]]:
    if not root_hwnd:
        return []
    try:
        import win32gui
    except Exception:
        return []

    target_rect = _safe_rect_tuple(control)
    if target_rect is None:
        return []

    matches: list[dict[str, object]] = []

    def callback(hwnd: int, _extra: object) -> None:
        try:
            rect = tuple(int(v) for v in win32gui.GetWindowRect(hwnd))
            class_name = win32gui.GetClassName(hwnd)
            text = win32gui.GetWindowText(hwnd)
        except Exception:
            return
        overlap = _rect_overlap_area(target_rect, rect)
        distance = _rect_center_distance(target_rect, rect)
        if overlap > 0 or distance <= 180:
            matches.append(
                {
                    "hwnd": int(hwnd),
                    "overlap": overlap,
                    "distance": distance,
                    "class_name": class_name,
                    "text": text[:40],
                    "rect": rect,
                }
            )

    try:
        win32gui.EnumChildWindows(root_hwnd, callback, None)
    except Exception:
        return []

    matches.sort(key=lambda item: (-int(item["overlap"]), int(item["distance"])))
    return matches


def _format_win32_child_candidates(candidates: list[dict[str, object]], limit: int = 8) -> str:
    parts: list[str] = []
    for index, item in enumerate(candidates[:limit]):
        parts.append(
            f"[{index}] hwnd={item.get('hwnd')} class={item.get('class_name')} "
            f"overlap={item.get('overlap')} distance={item.get('distance')}"
        )
    return " | ".join(parts)


def _describe_control(control: object) -> str:
    return (
        f"type={_safe_prop(control, 'ControlTypeName') or _safe_prop(control, 'LocalizedControlType')}, "
        f"class={_safe_prop(control, 'ClassName')}, "
        f"automationId={_safe_prop(control, 'AutomationId')}, "
        f"name={_safe_prop(control, 'Name')[:80]}, "
        f"hwnd={_safe_hwnd(control)}"
    )


def _describe_parent_chain(control: object, max_depth: int = 10) -> str:
    parts: list[str] = []
    current = control
    for depth in range(max_depth):
        if not current:
            break
        parts.append(f"[{depth}] {_describe_control(current)}")
        try:
            current = current.GetParentControl()
        except Exception as exc:
            parts.append(f"[{depth + 1}] parent_error={exc}")
            break
    return " -> ".join(parts)


def _safe_prop(control: object, name: str) -> str:
    try:
        value = getattr(control, name, "")
        return "" if value is None else str(value)
    except Exception:
        return ""


def _safe_hwnd(control: object) -> int:
    try:
        return int(getattr(control, "NativeWindowHandle", 0) or 0)
    except Exception:
        return 0


def _safe_rect_tuple(control: object) -> tuple[int, int, int, int] | None:
    try:
        rect = control.BoundingRectangle
        return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)
    except Exception:
        return None


def _rect_overlap_area(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> int:
    left = max(a[0], b[0])
    top = max(a[1], b[1])
    right = min(a[2], b[2])
    bottom = min(a[3], b[3])
    if right <= left or bottom <= top:
        return 0
    return (right - left) * (bottom - top)


def _rect_center_distance(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> int:
    ax = (a[0] + a[2]) // 2
    ay = (a[1] + a[3]) // 2
    bx = (b[0] + b[2]) // 2
    by = (b[1] + b[3]) // 2
    return int(((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5)


def _try_set_value(input_ctrl: object, text: str) -> bool:
    try:
        get_pattern = getattr(input_ctrl, "GetValuePattern", None)
        if not get_pattern:
            return False
        pattern = get_pattern()
        if not pattern:
            return False
        pattern.SetValue(text)
        time.sleep(0.12)
        return True
    except Exception as exc:
        logger.warning("chat input SetValue failed: %s", exc)
        return False
