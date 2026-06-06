from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Any

from .config import AppConfig, load_config
from .detector import QianniuDetector
from .qianniu_logging import get_logger
from .uia import is_control_available

logger = get_logger(__name__)


@dataclass(frozen=True)
class SendResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""


class QianniuSender:
    def __init__(self, config: AppConfig | None = None) -> None:
        self.config = config or load_config()
        self.detector = QianniuDetector(self.config)
        self.cached_input_field: Any | None = None
        self._cache_thread_id: int | None = None

    def prepare_reply_draft(
        self,
        text: str,
        chat_root: Any | None = None,
        input_field: Any | None = None,
    ) -> SendResult:
        total_started_at = time.perf_counter()
        if not text.strip():
            return SendResult(ok=False, stage="validate", detail="empty text")

        reused_chat_root = chat_root is not None
        find_chat_ms = 0.0
        if chat_root is None:
            stage_started_at = time.perf_counter()
            handle = self.detector.find_current_chat()
            find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if not handle:
                logger.info(
                    "qianniu sender_timing action=prepare_reply_draft total_ms=%.1f find_chat_ms=%.1f ok=False stage=find_chat",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                )
                return SendResult(ok=False, stage="find_chat", detail="chat window not found")
            chat_root = handle.chat_root

        stage_started_at = time.perf_counter()
        input_field = self._resolve_input_field(chat_root, input_field)
        if not input_field and reused_chat_root:
            fallback_started_at = time.perf_counter()
            handle = self.detector.find_current_chat()
            find_chat_ms += (time.perf_counter() - fallback_started_at) * 1000.0
            if handle:
                input_field = self._resolve_input_field(handle.chat_root)
        resolve_input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if not input_field:
            logger.info(
                "qianniu sender_timing action=prepare_reply_draft total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f ok=False stage=find_input reused_chat_root=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                resolve_input_ms,
                reused_chat_root,
            )
            return SendResult(ok=False, stage="find_input", detail="input field not found")

        stage_started_at = time.perf_counter()
        input_result = self._input_text(input_field, text)
        input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        logger.info(
            "qianniu sender_timing action=prepare_reply_draft total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f input_ms=%.1f ok=%s method=%s stage=%s reused_chat_root=%s",
            (time.perf_counter() - total_started_at) * 1000.0,
            find_chat_ms,
            resolve_input_ms,
            input_ms,
            input_result.ok,
            input_result.method,
            input_result.stage,
            reused_chat_root,
        )
        if not input_result.ok:
            return input_result
        return SendResult(ok=True, stage="prepared", method=input_result.method)

    def send_text(
        self,
        text: str,
        dry_run: bool = True,
        chat_root: Any | None = None,
        input_field: Any | None = None,
    ) -> SendResult:
        total_started_at = time.perf_counter()
        if not text.strip():
            return SendResult(ok=False, stage="validate", detail="empty text")

        reused_chat_root = chat_root is not None
        find_chat_ms = 0.0
        if chat_root is None:
            stage_started_at = time.perf_counter()
            handle = self.detector.find_current_chat()
            find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
            if not handle:
                logger.info(
                    "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f ok=False stage=find_chat dry_run=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    dry_run,
                )
                return SendResult(ok=False, stage="find_chat", detail="chat window not found")
            chat_root = handle.chat_root

        stage_started_at = time.perf_counter()
        input_field = self._resolve_input_field(chat_root, input_field)
        if not input_field and reused_chat_root:
            fallback_started_at = time.perf_counter()
            handle = self.detector.find_current_chat()
            find_chat_ms += (time.perf_counter() - fallback_started_at) * 1000.0
            if handle:
                input_field = self._resolve_input_field(handle.chat_root)
        resolve_input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if not input_field:
            logger.info(
                "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f ok=False stage=find_input dry_run=%s reused_chat_root=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                resolve_input_ms,
                dry_run,
                reused_chat_root,
            )
            return SendResult(ok=False, stage="find_input", detail="input field not found")

        if dry_run:
            logger.info(
                "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f ok=True stage=dry_run dry_run=True reused_chat_root=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                resolve_input_ms,
                reused_chat_root,
            )
            return SendResult(ok=True, stage="dry_run", method="none", detail="dry-run, not sent")

        stage_started_at = time.perf_counter()
        input_result = self._input_text(input_field, text)
        input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        if not input_result.ok:
            logger.info(
                "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f input_ms=%.1f ok=False stage=%s method=%s dry_run=%s reused_chat_root=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                resolve_input_ms,
                input_ms,
                input_result.stage,
                input_result.method,
                dry_run,
                reused_chat_root,
            )
            return input_result

        time.sleep(0.15)
        stage_started_at = time.perf_counter()
        enter_result = self._send_enter(input_field)
        enter_ms = (time.perf_counter() - stage_started_at) * 1000.0
        logger.info(
            "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f input_ms=%.1f enter_ms=%.1f ok=%s stage=%s input_method=%s enter_method=%s dry_run=%s reused_chat_root=%s",
            (time.perf_counter() - total_started_at) * 1000.0,
            find_chat_ms,
            resolve_input_ms,
            input_ms,
            enter_ms,
            enter_result.ok,
            enter_result.stage,
            input_result.method,
            enter_result.method,
            dry_run,
            reused_chat_root,
        )
        if not enter_result.ok:
            return enter_result

        return SendResult(ok=True, stage="sent", method=f"{input_result.method}+{enter_result.method}")

    def _resolve_input_field(self, chat_root: Any, input_field: Any | None = None) -> Any | None:
        self._ensure_cache_thread()
        if input_field is not None:
            self.cached_input_field = input_field
            self._cache_thread_id = threading.get_ident()
            return input_field
        if self.cached_input_field is not None and is_control_available(self.cached_input_field):
            return self.cached_input_field
        input_field = self.detector.find_input_field(chat_root)
        if input_field:
            self.cached_input_field = input_field
            self._cache_thread_id = threading.get_ident()
        return input_field

    def _ensure_cache_thread(self) -> None:
        current_thread_id = threading.get_ident()
        if self._cache_thread_id in {None, current_thread_id}:
            return
        self.cached_input_field = None
        self._cache_thread_id = None

    def _input_text(self, input_field: Any, text: str) -> SendResult:
        started_at = time.perf_counter()
        if self._try_set_value(input_field, text):
            logger.info(
                "qianniu input_text_timing method=value_pattern ok=True ms=%.1f",
                (time.perf_counter() - started_at) * 1000.0,
            )
            return SendResult(ok=True, stage="input", method="value_pattern")

        fallback_started_at = time.perf_counter()
        if self._paste_text(input_field, text):
            logger.info(
                "qianniu input_text_timing method=clipboard_paste ok=True ms=%.1f fallback_ms=%.1f",
                (time.perf_counter() - started_at) * 1000.0,
                (time.perf_counter() - fallback_started_at) * 1000.0,
            )
            return SendResult(ok=True, stage="input", method="clipboard_paste")

        logger.info(
            "qianniu input_text_timing method=all ok=False ms=%.1f",
            (time.perf_counter() - started_at) * 1000.0,
        )
        return SendResult(ok=False, stage="input", detail="all input strategies failed")

    def _try_set_value(self, input_field: Any, text: str) -> bool:
        try:
            pattern = input_field.GetValuePattern()
            if not pattern:
                return False
            pattern.SetValue(text)
            return True
        except Exception as exc:
            logger.debug("value pattern input failed: %s", exc)
            return False

    def _paste_text(self, input_field: Any, text: str) -> bool:
        try:
            import win32api
            import win32con
        except ImportError:
            return False

        old_clipboard = _get_clipboard_text()
        try:
            _set_clipboard_text(text)
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
        except Exception as exc:
            logger.debug("clipboard paste failed: %s", exc)
            return False
        finally:
            if old_clipboard is not None:
                try:
                    _set_clipboard_text(old_clipboard)
                except Exception:
                    pass

    def _send_enter(self, input_field: Any) -> SendResult:
        try:
            input_field.SetFocus()
        except Exception:
            pass
        if _press_enter():
            return SendResult(ok=True, stage="send_enter", method="enter_key")
        return SendResult(ok=False, stage="send_enter", detail="enter key failed")


def _press_enter() -> bool:
    try:
        import win32api
        import win32con

        win32api.keybd_event(win32con.VK_RETURN, 0, 0, 0)
        time.sleep(0.04)
        win32api.keybd_event(win32con.VK_RETURN, 0, win32con.KEYEVENTF_KEYUP, 0)
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
