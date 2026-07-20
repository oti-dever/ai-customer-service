from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .config import AppConfig, load_config
from .detector import QianniuDetector
from .qianniu_logging import get_listen_flow_logger, get_logger
from .uia import is_control_available

logger = get_logger(__name__)
listen_flow_logger = get_listen_flow_logger(__name__)


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
        allow_global_find: bool = True,
    ) -> SendResult:
        total_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow sender_send_text_start text_len=%s dry_run=%s chat_root_available=%s input_field_available=%s allow_global_find=%s",
            len(text),
            dry_run,
            bool(chat_root),
            bool(input_field),
            allow_global_find,
        )
        if not text.strip():
            listen_flow_logger.info(
                "listen_flow sender_send_text_done ok=False stage=validate detail=empty_text elapsed_ms=%.1f",
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return SendResult(ok=False, stage="validate", detail="empty text")

        reused_chat_root = chat_root is not None
        find_chat_ms = 0.0
        if chat_root is None:
            if not allow_global_find:
                listen_flow_logger.info(
                    "listen_flow sender_find_chat_done ok=False method=global_find_skipped elapsed_ms=0.0"
                )
                return SendResult(ok=False, stage="find_chat", detail="chat root not provided")
            stage_started_at = time.perf_counter()
            listen_flow_logger.info("listen_flow sender_find_chat_start method=find_current_chat")
            handle = self.detector.find_current_chat()
            find_chat_ms = (time.perf_counter() - stage_started_at) * 1000.0
            listen_flow_logger.info(
                "listen_flow sender_find_chat_done method=find_current_chat found_chat=%s elapsed_ms=%.1f",
                bool(handle),
                find_chat_ms,
            )
            if not handle:
                logger.info(
                    "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f ok=False stage=find_chat dry_run=%s",
                    (time.perf_counter() - total_started_at) * 1000.0,
                    find_chat_ms,
                    dry_run,
                )
                listen_flow_logger.info(
                    "listen_flow sender_send_text_done ok=False stage=find_chat detail=chat_window_not_found elapsed_ms=%.1f",
                    (time.perf_counter() - total_started_at) * 1000.0,
                )
                return SendResult(ok=False, stage="find_chat", detail="chat window not found")
            chat_root = handle.chat_root

        stage_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow sender_resolve_input_start reused_chat_root=%s input_field_provided=%s",
            reused_chat_root,
            bool(input_field),
        )
        input_field = self._resolve_input_field(chat_root, input_field)
        if not input_field and reused_chat_root:
            if allow_global_find:
                fallback_started_at = time.perf_counter()
                listen_flow_logger.info("listen_flow sender_find_chat_start method=fallback_find_current_chat")
                handle = self.detector.find_current_chat()
                fallback_ms = (time.perf_counter() - fallback_started_at) * 1000.0
                find_chat_ms += fallback_ms
                listen_flow_logger.info(
                    "listen_flow sender_find_chat_done method=fallback_find_current_chat found_chat=%s elapsed_ms=%.1f",
                    bool(handle),
                    fallback_ms,
                )
                if handle:
                    input_field = self._resolve_input_field(handle.chat_root)
            else:
                listen_flow_logger.info(
                    "listen_flow sender_find_chat_done method=fallback_find_current_chat skipped=True reason=chat_root_supplied"
                )
        resolve_input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow sender_resolve_input_done found_input=%s reused_chat_root=%s elapsed_ms=%.1f",
            bool(input_field),
            reused_chat_root,
            resolve_input_ms,
        )
        if not input_field:
            logger.info(
                "qianniu sender_timing action=send_text total_ms=%.1f find_chat_ms=%.1f resolve_input_ms=%.1f ok=False stage=find_input dry_run=%s reused_chat_root=%s",
                (time.perf_counter() - total_started_at) * 1000.0,
                find_chat_ms,
                resolve_input_ms,
                dry_run,
                reused_chat_root,
            )
            listen_flow_logger.info(
                "listen_flow sender_send_text_done ok=False stage=find_input detail=input_field_not_found elapsed_ms=%.1f",
                (time.perf_counter() - total_started_at) * 1000.0,
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
            listen_flow_logger.info(
                "listen_flow sender_send_text_done ok=True stage=dry_run elapsed_ms=%.1f",
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return SendResult(ok=True, stage="dry_run", method="none", detail="dry-run, not sent")

        stage_started_at = time.perf_counter()
        listen_flow_logger.info("listen_flow sender_input_start method=auto")
        input_result = self._input_text(input_field, text)
        input_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow sender_input_done ok=%s stage=%s method=%s elapsed_ms=%.1f",
            input_result.ok,
            input_result.stage,
            input_result.method,
            input_ms,
        )
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
            listen_flow_logger.info(
                "listen_flow sender_send_text_done ok=False stage=%s detail=%s elapsed_ms=%.1f",
                input_result.stage,
                input_result.detail,
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return input_result

        time.sleep(0.15)
        stage_started_at = time.perf_counter()
        listen_flow_logger.info("listen_flow sender_enter_start method=enter_key")
        enter_result = self._send_enter(input_field)
        enter_ms = (time.perf_counter() - stage_started_at) * 1000.0
        listen_flow_logger.info(
            "listen_flow sender_enter_done ok=%s stage=%s method=%s elapsed_ms=%.1f",
            enter_result.ok,
            enter_result.stage,
            enter_result.method,
            enter_ms,
        )
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
            listen_flow_logger.info(
                "listen_flow sender_send_text_done ok=False stage=%s detail=%s elapsed_ms=%.1f",
                enter_result.stage,
                enter_result.detail,
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return enter_result

        listen_flow_logger.info(
            "listen_flow sender_send_text_done ok=True stage=sent method=%s+%s elapsed_ms=%.1f",
            input_result.method,
            enter_result.method,
            (time.perf_counter() - total_started_at) * 1000.0,
        )
        return SendResult(ok=True, stage="sent", method=f"{input_result.method}+{enter_result.method}")

    def send_media(
        self,
        file_path: str,
        content_type: str,
        dry_run: bool = False,
        chat_root: Any | None = None,
        input_field: Any | None = None,
    ) -> SendResult:
        source = Path(file_path)
        if not source.is_file():
            return SendResult(ok=False, stage="validate", detail="file_not_found")
        if content_type not in {"image", "video", "file"}:
            return SendResult(ok=False, stage="validate", detail="unsupported_content_type")

        if chat_root is None:
            handle = self.detector.find_current_chat()
            if not handle:
                return SendResult(ok=False, stage="find_chat", detail="chat window not found")
            chat_root = handle.chat_root

        input_field = self._resolve_input_field(chat_root, input_field)
        if not input_field:
            return SendResult(ok=False, stage="find_input", detail="input field not found")
        if dry_run:
            return SendResult(ok=True, stage="dry_run", method="none", detail="dry-run, not sent")

        try:
            input_field.SetFocus()
        except Exception:
            pass

        from rpa.platforms.wechat.media_clipboard import (
            press_paste_shortcut,
            set_clipboard_file_paths,
        )

        if not set_clipboard_file_paths([source]):
            return SendResult(ok=False, stage="clipboard", detail="clipboard_file_write_failed")
        if not press_paste_shortcut():
            return SendResult(ok=False, stage="paste", detail="clipboard_file_paste_failed")

        time.sleep(0.9 if source.stat().st_size < 20 * 1024 * 1024 else 1.8)
        enter_result = self._send_enter(input_field)
        if not enter_result.ok:
            return enter_result
        return SendResult(ok=True, stage="sent", method=f"clipboard_file+{enter_result.method}")

    def resolve_input_field(self, chat_root: Any, input_field: Any | None = None) -> Any | None:
        return self._resolve_input_field(chat_root, input_field)

    def _resolve_input_field(self, chat_root: Any, input_field: Any | None = None) -> Any | None:
        total_started_at = time.perf_counter()
        self._ensure_cache_thread()
        had_cached_input = self.cached_input_field is not None
        if input_field is not None:
            self.cached_input_field = input_field
            self._cache_thread_id = threading.get_ident()
            logger.info(
                "qianniu resolve_input_timing method=provided chat_root_available=%s input_field_provided=True cache_checked=False cache_available=False cache_check_ms=0.0 find_input_ms=0.0 total_ms=%.1f found_input=True",
                bool(chat_root),
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return input_field

        cache_check_started_at = time.perf_counter()
        cached_available = False
        if had_cached_input:
            cached_available = is_control_available(self.cached_input_field)
        cache_check_ms = (time.perf_counter() - cache_check_started_at) * 1000.0
        if cached_available:
            logger.info(
                "qianniu resolve_input_timing method=cache chat_root_available=%s input_field_provided=False cache_checked=True cache_available=True cache_check_ms=%.1f find_input_ms=0.0 total_ms=%.1f found_input=True",
                bool(chat_root),
                cache_check_ms,
                (time.perf_counter() - total_started_at) * 1000.0,
            )
            return self.cached_input_field

        find_started_at = time.perf_counter()
        input_field = self.detector.find_input_field(chat_root)
        find_input_ms = (time.perf_counter() - find_started_at) * 1000.0
        if input_field:
            self.cached_input_field = input_field
            self._cache_thread_id = threading.get_ident()
        logger.info(
            "qianniu resolve_input_timing method=find_input_field chat_root_available=%s input_field_provided=False cache_checked=%s cache_available=%s cache_check_ms=%.1f find_input_ms=%.1f total_ms=%.1f found_input=%s",
            bool(chat_root),
            had_cached_input,
            cached_available,
            cache_check_ms,
            find_input_ms,
            (time.perf_counter() - total_started_at) * 1000.0,
            bool(input_field),
        )
        return input_field

    def _ensure_cache_thread(self) -> None:
        current_thread_id = threading.get_ident()
        if self._cache_thread_id in {None, current_thread_id}:
            return
        self.invalidate_cache()

    def invalidate_cache(self) -> None:
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
