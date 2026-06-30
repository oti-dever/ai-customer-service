from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any

from .config import AppConfig, load_config
from .detector import QQDetector
from .navigator import bring_window_to_foreground, get_foreground_window, is_foreground_window, rect_inside
from .qq_logging import get_logger
from .uia import WindowInfo
from .uia import safe_rect_tuple, uia_guard


logger = get_logger(__name__)


@dataclass(frozen=True)
class DraftResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""


@dataclass(frozen=True)
class SendResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""
    draft_method: str = ""
    verified: bool = False


class QQSender:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QQDetector(self.config)

    def prepare_reply_draft(
        self,
        text: str,
        chat_root: Any | None = None,
        input_field: Any | None = None,
        *,
        activate: bool = True,
        use_uia: bool = False,
        uia_only: bool = False,
    ) -> DraftResult:
        started_at = time.perf_counter()
        if not text.strip():
            return DraftResult(ok=False, stage="validate", detail="empty_text")
        with uia_guard("qq_prepare_reply_draft"):
            window: WindowInfo | None = None
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return DraftResult(ok=False, stage="find_chat", detail="qq_window_not_found")
                chat_root = handle.chat_root
                window = handle.window.window
            if input_field is None:
                candidates = self.detector.find_input_area_candidates(chat_root)
                input_field = candidates[0].control if candidates else None
            if input_field is None:
                return DraftResult(ok=False, stage="find_input", detail="input_field_not_found")
            result = self._input_text(
                input_field,
                text,
                window=window,
                activate=activate,
                use_uia=use_uia,
                uia_only=uia_only,
            )
            logger.info(
                "qq prepare_reply_draft ok=%s stage=%s method=%s elapsed_ms=%.1f",
                result.ok,
                result.stage,
                result.method,
                (time.perf_counter() - started_at) * 1000.0,
            )
            return result

    def send_text(
        self,
        text: str,
        chat_root: Any | None = None,
        *,
        activate: bool = True,
        verify: bool = False,
        verify_wait_seconds: float = 0.8,
    ) -> SendResult:
        started_at = time.perf_counter()
        if not text.strip():
            return SendResult(ok=False, stage="validate", detail="empty_text")
        with uia_guard("qq_send_text"):
            window: WindowInfo | None = None
            if chat_root is None:
                handle = self.detector.find_current_chat()
                if not handle:
                    return SendResult(ok=False, stage="find_chat", detail="qq_window_not_found")
                chat_root = handle.chat_root
                window = handle.window.window
            else:
                handle = self.detector.find_current_chat()
                window = handle.window.window if handle else None

            input_candidates = self.detector.find_input_area_candidates(chat_root)
            input_field = input_candidates[0].control if input_candidates else None
            if input_field is None:
                return SendResult(ok=False, stage="find_input", detail="input_field_not_found")

            draft = self._input_text(
                input_field,
                text,
                window=window,
                activate=activate,
                use_uia=False,
                uia_only=False,
            )
            if not draft.ok:
                return SendResult(
                    ok=False,
                    stage=f"draft:{draft.stage}",
                    method=draft.method,
                    detail=draft.detail,
                    draft_method=draft.method,
                )

            button_candidates = self.detector.find_send_button_candidates(chat_root)
            send_button = button_candidates[0].control if button_candidates else None
            if send_button is None:
                return SendResult(
                    ok=False,
                    stage="find_send_button",
                    method="foreground_button_click" if activate else "direct_button_click",
                    detail="send_button_not_found",
                    draft_method=draft.method,
                )
            if window is None:
                handle = self.detector.find_current_chat()
                window = handle.window.window if handle else None
            if window is None:
                return SendResult(
                    ok=False,
                    stage="find_window",
                    method="foreground_button_click" if activate else "direct_button_click",
                    detail="qq_window_not_found",
                    draft_method=draft.method,
                )
            button_rect = safe_rect_tuple(send_button)
            if not rect_inside(button_rect, window.rect):
                return SendResult(
                    ok=False,
                    stage="bounds",
                    method="foreground_button_click" if activate else "direct_button_click",
                    detail=f"send_button_rect_outside_window:{window.rect}",
                    draft_method=draft.method,
                )
            if activate:
                if not bring_window_to_foreground(window.hwnd):
                    return SendResult(
                        ok=False,
                        stage="foreground",
                        method="foreground_button_click",
                        detail="set_foreground_failed",
                        draft_method=draft.method,
                    )
                if not is_foreground_window(window.hwnd):
                    return SendResult(
                        ok=False,
                        stage="foreground",
                        method="foreground_button_click",
                        detail=f"foreground_hwnd=0x{get_foreground_window():X}",
                        draft_method=draft.method,
                    )

            if not _click_control_center(send_button):
                return SendResult(
                    ok=False,
                    stage="click_send",
                    method="foreground_button_click" if activate else "direct_button_click",
                    detail="send_button_click_failed",
                    draft_method=draft.method,
                )

        method = "foreground_button_click" if activate else "direct_button_click"
        verified = False
        if verify:
            time.sleep(max(0.2, min(float(verify_wait_seconds), 3.0)))
            verified = _verify_visible_outgoing_text(text)
        if verify and not verified:
            logger.info(
                "qq send_text ok=False stage=verify method=%s draft_method=%s elapsed_ms=%.1f",
                method,
                draft.method,
                (time.perf_counter() - started_at) * 1000.0,
            )
            return SendResult(
                ok=False,
                stage="verify",
                method=method,
                detail="sent_message_not_observed",
                draft_method=draft.method,
                verified=False,
            )
        logger.info(
            "qq send_text ok=True stage=sent method=%s draft_method=%s verified=%s elapsed_ms=%.1f",
            method,
            draft.method,
            verified,
            (time.perf_counter() - started_at) * 1000.0,
        )
        return SendResult(
            ok=True,
            stage="sent",
            method=method,
            draft_method=draft.method,
            verified=verified,
        )

    def _input_text(
        self,
        input_field: Any,
        text: str,
        *,
        window: WindowInfo | None,
        activate: bool,
        use_uia: bool,
        uia_only: bool,
    ) -> DraftResult:
        uia_failure_detail = ""
        if use_uia:
            if _try_set_value(input_field, text):
                return DraftResult(ok=True, stage="prepared", method="value_pattern")
            uia_failure_detail = "value_pattern_failed"
            if uia_only:
                return DraftResult(ok=False, stage="input", method="value_pattern", detail=uia_failure_detail)

        if window is None:
            handle = self.detector.find_current_chat()
            window = handle.window.window if handle else None
        if window is None:
            return DraftResult(
                ok=False,
                stage="find_window",
                method=_paste_method(activate),
                detail=_join_detail(uia_failure_detail, "qq_window_not_found"),
            )

        input_rect = safe_rect_tuple(input_field)
        if not rect_inside(input_rect, window.rect):
            return DraftResult(
                ok=False,
                stage="bounds",
                method=_paste_method(activate),
                detail=_join_detail(uia_failure_detail, f"input_rect_outside_window:{window.rect}"),
            )

        if activate:
            if not bring_window_to_foreground(window.hwnd):
                return DraftResult(
                    ok=False,
                    stage="foreground",
                    method=_paste_method(activate),
                    detail=_join_detail(uia_failure_detail, "set_foreground_failed"),
                )
            if not is_foreground_window(window.hwnd):
                return DraftResult(
                    ok=False,
                    stage="foreground",
                    method=_paste_method(activate),
                    detail=_join_detail(uia_failure_detail, f"foreground_hwnd=0x{get_foreground_window():X}"),
                )

        if _paste_text(input_field, text):
            return DraftResult(ok=True, stage="prepared", method=_paste_method(activate), detail=uia_failure_detail)
        return DraftResult(
            ok=False,
            stage="input",
            method=_paste_method(activate),
            detail=_join_detail(uia_failure_detail, "clipboard_paste_failed"),
        )


def _try_set_value(input_field: Any, text: str) -> bool:
    try:
        pattern = input_field.GetValuePattern()
        if not pattern:
            return False
        pattern.SetValue(text)
        return True
    except Exception:
        return False


def _paste_text(input_field: Any, text: str) -> bool:
    try:
        import win32api
        import win32con
        import win32clipboard
    except ImportError:
        return False

    old_clipboard = _get_clipboard_text()
    try:
        _set_clipboard_text(text)
        _click_control_center(input_field)
        try:
            input_field.SetFocus()
        except Exception:
            pass
        time.sleep(0.08)
        win32api.keybd_event(win32con.VK_CONTROL, 0, 0, 0)
        win32api.keybd_event(ord("V"), 0, 0, 0)
        win32api.keybd_event(ord("V"), 0, win32con.KEYEVENTF_KEYUP, 0)
        win32api.keybd_event(win32con.VK_CONTROL, 0, win32con.KEYEVENTF_KEYUP, 0)
        time.sleep(0.12)
        return True
    except Exception:
        return False
    finally:
        if old_clipboard is not None:
            try:
                _set_clipboard_text(old_clipboard)
            except Exception:
                pass


def _click_control_center(control: Any) -> bool:
    try:
        import win32api
        import win32con
    except ImportError:
        return False

    rect = safe_rect_tuple(control)
    if not rect:
        return False
    left, top, right, bottom = rect
    if right <= left or bottom <= top:
        return False
    x = left + (right - left) // 2
    y = top + (bottom - top) // 2
    try:
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, x, y, 0, 0)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, x, y, 0, 0)
        time.sleep(0.05)
        return True
    except Exception:
        return False


def _get_clipboard_text() -> str | None:
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


def _set_clipboard_text(text: str) -> None:
    import win32clipboard
    import win32con

    win32clipboard.OpenClipboard()
    try:
        win32clipboard.EmptyClipboard()
        win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT, text)
    finally:
        win32clipboard.CloseClipboard()


def _paste_method(activate: bool) -> str:
    return "foreground_clipboard_paste" if activate else "direct_clipboard_paste"


def _join_detail(*parts: str) -> str:
    return " | ".join(part for part in parts if part)


def _verify_visible_outgoing_text(text: str) -> bool:
    try:
        from .messages import read_visible_messages
    except Exception:
        return False
    expected = _normalize_message_text(text)
    if not expected:
        return False
    try:
        result = read_visible_messages(limit=12, include_media=False)
    except Exception:
        logger.exception("qq send_text verification read failed")
        return False
    if not result.ok:
        return False
    for message in reversed(result.messages):
        if message.content_type != "text":
            continue
        if message.direction not in {"outgoing", "outbound", "out"}:
            continue
        if _normalize_message_text(message.text) == expected:
            return True
    return False


def _normalize_message_text(value: str) -> str:
    return " ".join(str(value or "").split())
