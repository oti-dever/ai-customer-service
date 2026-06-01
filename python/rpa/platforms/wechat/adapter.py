from __future__ import annotations

import hashlib
import threading
import time
from dataclasses import asdict, is_dataclass
from datetime import datetime, timezone
from typing import Any, Protocol

from .detector import WechatDetector
from .reader_v2 import WechatVisibleMessageReader
from .sender_v2 import WechatMessageSender
from .stability import DebugArtifactWriter, FailureTracker, uia_guard
from .wechat_logging import get_logger
logger = get_logger(__name__)

PLATFORM_WECHAT = "wechat_pc"
DEFAULT_ACCOUNT_ID = "local_wechat"
OBSERVER_POLL_INTERVAL_SEC = 0.8
OBSERVER_AFTER_WORK_SLEEP_SEC = 0.2
OBSERVER_ERROR_SLEEP_SEC = 2.0
OBSERVER_SESSION_LIMIT = 3
OBSERVER_MESSAGE_LIMIT = 20
OBSERVER_SETTLE_MS = 150


class EventSink(Protocol):
    def append(self, event: dict[str, Any]) -> int:
        ...


def clean(value: Any) -> str:
    return "" if value is None else str(value).strip()


def truthy(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in {"1", "true", "yes", "y", "on"}:
            return True
        if normalized in {"0", "false", "no", "n", "off"}:
            return False
    return bool(value)


def allow_foreground_fallback_from(params: dict[str, Any]) -> bool:
    if truthy(params.get("strict_background"), False):
        return False
    return truthy(params.get("allow_foreground_fallback"), True)


def payload_status(status: str, request_id: str = "", **extra: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "request_id": request_id,
        "status": status,
    }
    payload.update(extra)
    return payload


def _now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def _sha1(value: str) -> str:
    return hashlib.sha1(value.encode("utf-8", errors="ignore")).hexdigest()


def _conversation_key(account_id: str, display_name: str) -> str:
    safe_name = clean(display_name) or "current"
    return f"wechat:{account_id}:{safe_name}"


def _display_name_from_key(value: str) -> str:
    raw = clean(value)
    if ":" in raw:
        return raw.rsplit(":", 1)[-1].strip()
    if raw.startswith("wechat_"):
        return raw[len("wechat_") :].strip()
    return raw


class WechatSidecarAdapter:
    def __init__(self, store: EventSink) -> None:
        self._store = store
        self._connected = False
        self._account_id = DEFAULT_ACCOUNT_ID
        self._seen_message_ids: set[str] = set()
        self._seen_conversation_keys: set[str] = set()
        self._conversation_message_cursors: dict[str, str] = {}
        self._last_unread_counts: dict[str, int] = {}
        self._detector = WechatDetector()
        self._reader = WechatVisibleMessageReader(self._detector)
        self._sender = WechatMessageSender(self._detector)
        self._observer_stop = threading.Event()
        self._observer_thread: threading.Thread | None = None
        self._observer_lock = threading.Lock()
        self._state_lock = threading.Lock()
        self._last_active_conversation_key = ""
        self._last_active_chat_title = ""
        self._last_seen_message_fingerprint = ""
        config = self._detector.config
        self._failures = FailureTracker(
            max_consecutive_failures=int(getattr(config, "max_consecutive_failures", 8) or 8),
            cooldown_seconds=float(getattr(config, "failure_cooldown_seconds", 30.0) or 30.0),
        )
        self._debug_writer = DebugArtifactWriter.from_config(config)

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        request_id = clean(payload.get("request_id"))
        command = clean(payload.get("command") or payload.get("command_type"))
        self._account_id = clean(payload.get("account_id")) or self._account_id
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}

        handlers = {
            "connect": self._connect,
            "disconnect": self._disconnect,
            "health_check": self._health_check,
            "diagnose_wechat_uia": self._diagnose_wechat_uia,
            "fetch_visible_conversations": self._fetch_visible_conversations,
            "fetch_visible_messages": self._fetch_visible_messages,
            "scan_unread_and_fetch": self._scan_unread_and_fetch,
            "prepare_reply_draft": self._prepare_reply_draft,
            "send_message": self._send_message,
        }
        handler = handlers.get(command)
        if handler is None:
            logger.warning("command unsupported request_id=%s command=%s", request_id, command)
            return payload_status(
                "error",
                request_id,
                error=f"unsupported_command:{command}",
                result={},
            )
        try:
            logger.info(
                "command start request_id=%s command=%s account_id=%s params=%s",
                request_id,
                command,
                self._account_id,
                list(params.keys()),
            )
            with uia_guard(command or "wechat"):
                self._ensure_not_cooling(command or "wechat")
                result = handler(params, request_id)
            self._failures.record(True)
            logger.info(
                "command done request_id=%s command=%s status=success keys=%s",
                request_id,
                command,
                list(result.keys()) if isinstance(result, dict) else [],
            )
            return payload_status("success", request_id, result=result)
        except Exception as exc:
            self._failures.record(False, command)
            logger.exception("command failed request_id=%s command=%s", request_id, command)
            event = self._health_event(
                healthy=False,
                status="error",
                message=str(exc),
                metadata=self._failure_metadata(command or "wechat", str(exc)),
            )
            self._store.append(event)
            return payload_status("error", request_id, error=str(exc), result={})

    def health(self) -> dict[str, Any]:
        with uia_guard("health"):
            result = self._probe()
        logger.info("health request account_id=%s healthy=%s reason=%s", self._account_id, result.get("healthy"), result.get("reason"))
        return {
            "status": "success",
            "platform": PLATFORM_WECHAT,
            "account_id": self._account_id,
            "connected": self._connected,
            "health": result,
        }

    def _connect(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        self._connected = True
        health = self._probe()
        logger.info(
            "connect request_id=%s healthy=%s reason=%s emit_initial_snapshot=%s",
            request_id,
            bool(health.get("healthy")),
            clean(health.get("reason")),
            bool(params.get("emit_initial_snapshot", False)),
        )
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata={**health, **self._failure_state_payload()},
            )
        )
        if params.get("emit_initial_snapshot", False):
            self._fetch_visible_conversations({"limit": params.get("limit", 30)}, request_id)
            self._fetch_visible_messages({"limit": params.get("message_limit", 30)}, request_id)
        self._start_observer()
        return {"connected": True, "health": health}

    def _disconnect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        self._connected = False
        self._stop_observer()
        logger.info("disconnect request_id=%s", _request_id)
        self._store.append(
            self._health_event(
                healthy=False,
                status="offline",
                message="adapter disconnected",
                metadata=self._failure_metadata("disconnect", "adapter disconnected"),
            )
        )
        return {"connected": False}

    def _start_observer(self) -> None:
        with self._observer_lock:
            if self._observer_thread and self._observer_thread.is_alive():
                return
            self._observer_stop.clear()
            self._observer_thread = threading.Thread(
                target=self._observer_loop,
                name="wechat-observer",
                daemon=True,
            )
            self._observer_thread.start()
        logger.info(
            "wechat observer started poll_interval=%.1fs session_limit=%s message_limit=%s settle_ms=%s",
            OBSERVER_POLL_INTERVAL_SEC,
            OBSERVER_SESSION_LIMIT,
            OBSERVER_MESSAGE_LIMIT,
            OBSERVER_SETTLE_MS,
        )

    def _stop_observer(self) -> None:
        with self._observer_lock:
            thread = self._observer_thread
            self._observer_stop.set()
        if thread and thread.is_alive():
            thread.join(timeout=2.0)
        with self._observer_lock:
            if self._observer_thread is thread:
                self._observer_thread = None
        logger.info("wechat observer stopped")

    def _observer_loop(self) -> None:
        while not self._observer_stop.is_set():
            if not self._connected:
                self._observer_stop.wait(OBSERVER_POLL_INTERVAL_SEC)
                continue

            try:
                had_work = False
                with uia_guard("wechat_observer"):
                    self._ensure_not_cooling("wechat_observer")
                    had_work = self._observer_tick()
                self._failures.record(True)
                self._observer_stop.wait(OBSERVER_AFTER_WORK_SLEEP_SEC if had_work else OBSERVER_POLL_INTERVAL_SEC)
            except Exception as exc:
                self._failures.record(False, "wechat_observer")
                logger.exception("wechat observer tick failed: %s", exc)
                self._store.append(
                    self._health_event(
                        healthy=False,
                        status="degraded",
                        message=str(exc),
                        metadata=self._failure_metadata("wechat_observer", str(exc)),
                    )
                )
                self._observer_stop.wait(OBSERVER_ERROR_SLEEP_SEC)

    def _observer_tick(self) -> bool:
        started_at = time.perf_counter()
        win = self._detector.find_main_window_control()
        if win is None:
            logger.info("wechat_observer_timing stage=find_main_window_control ms=%.1f found=False", (time.perf_counter() - started_at) * 1000.0)
            return False

        stage_started_at = time.perf_counter()
        session_list = self._detector.get_session_list(win)
        session_list_ms = (time.perf_counter() - stage_started_at) * 1000.0
        stage_started_at = time.perf_counter()
        scan = self._detector.scan_unread_sessions_detailed(session_list)
        scan_ms = (time.perf_counter() - stage_started_at) * 1000.0
        unread_count = len(scan.sessions)
        logger.info(
            "wechat_observer_timing stage=light_scan total_ms=%.1f session_list_ms=%.1f scan_ms=%.1f source=%s scanned=%s unread=%s",
            (time.perf_counter() - started_at) * 1000.0,
            session_list_ms,
            scan_ms,
            scan.source,
            scan.scanned_items,
            unread_count,
        )
        if unread_count <= 0:
            return False

        request_id = f"observer-{int(time.time() * 1000)}"
        result = self._scan_unread_and_fetch(
            {
                "session_limit": OBSERVER_SESSION_LIMIT,
                "message_limit": OBSERVER_MESSAGE_LIMIT,
                "allow_foreground": False,
                "settle_ms": OBSERVER_SETTLE_MS,
            },
            request_id,
        )
        logger.info(
            "wechat_observer processed request_id=%s unread=%s processed=%s messages=%s",
            request_id,
            result.get("unread_count"),
            result.get("processed_count"),
            result.get("message_count"),
        )
        return bool(result.get("message_count") or result.get("conversation_count") or result.get("processed_count"))

    def _health_check(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        health = self._probe()
        logger.info("health_check request_id=%s healthy=%s reason=%s", _request_id, bool(health.get("healthy")), clean(health.get("reason")))
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata={**health, **self._failure_state_payload()},
            )
        )
        return health

    def _diagnose_wechat_uia(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        candidate_limit = max(1, min(int(params.get("candidate_limit", 5) or 5), 20))
        logger.info("diagnose_wechat_uia request_id=%s candidate_limit=%s", _request_id, candidate_limit)
        return self._detector.diagnose_uia(candidate_limit=candidate_limit)

    def _fetch_visible_conversations(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        limit = max(1, min(int(params.get("limit", 60) or 60), 100))
        logger.info("fetch_visible_conversations request_id=%s limit=%s", _request_id, limit)
        samples = self._detector.collect_visible_sessions(limit)
        conversations: list[dict[str, Any]] = []
        for item in samples:
            name = clean(getattr(item, "name", ""))
            if not name:
                continue
            event = self._conversation_event(name, item)
            self._store.append(event)
            conversations.append(event)
        return {"count": len(conversations), "conversations": conversations}

    def _fetch_visible_messages(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        limit = max(1, min(int(params.get("limit", 30) or 30), 100))
        requested_name = _display_name_from_key(clean(params.get("conversation_key")))
        display_name = clean(params.get("display_name")) or requested_name
        logger.info(
            "fetch_visible_messages request_id=%s display_name=%s limit=%s",
            _request_id,
            display_name or "current",
            limit,
        )

        key = _conversation_key(self._account_id, display_name)
        tail_only = key in self._seen_conversation_keys
        unread_count = self._last_unread_counts.get(key)
        read_limit = self._message_window_limit_for_unread(display_name, unread_count, limit)
        batch = self._reader.read_visible_messages(display_name=display_name, limit=read_limit, tail_only=tail_only)
        display_name = batch.display_name
        context = getattr(batch, "context", None)
        self._update_active_chat_state(display_name, context=context, samples=batch.samples)
        messages: list[dict[str, Any]] = []
        for item in batch.samples:
            content = clean(getattr(item, "name", ""))
            if not content:
                continue
            event = self._message_event(display_name, item, context=context)
            platform_msg_id = clean(event.get("payload", {}).get("platform_msg_id"))
            if platform_msg_id and platform_msg_id in self._seen_message_ids:
                continue
            if platform_msg_id:
                self._seen_message_ids.add(platform_msg_id)
                self._conversation_message_cursors[key] = platform_msg_id
            self._store.append(event)
            messages.append(event)
        self._seen_conversation_keys.add(key)
        return {
            "count": len(messages),
            "messages": messages,
            "display_name": display_name,
            "context": _chat_context_payload(context),
        }

    def _scan_unread_and_fetch(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()

        def log_timing(stage: str, stage_started_at: float, **extra: Any) -> None:
            details = " ".join(f"{key}={value}" for key, value in extra.items())
            logger.info(
                "scan_unread_timing request_id=%s stage=%s ms=%.1f%s%s",
                _request_id,
                stage,
                (time.perf_counter() - stage_started_at) * 1000.0,
                " " if details else "",
                details,
            )

        message_limit = max(1, min(int(params.get("message_limit", params.get("limit", 30)) or 30), 100))
        session_limit = max(1, min(int(params.get("session_limit", 3) or 3), 20))
        allow_foreground = bool(params.get("allow_foreground", True))
        settle_ms = max(0, min(int(params.get("settle_ms", 500) or 500), 3000))
        logger.info(
            "scan_unread_and_fetch request_id=%s session_limit=%s message_limit=%s allow_foreground=%s settle_ms=%s",
            _request_id,
            session_limit,
            message_limit,
            allow_foreground,
            settle_ms,
        )

        stage_started_at = time.perf_counter()
        win = self._detector.find_main_window_control()
        log_timing("find_main_window_control", stage_started_at, found=bool(win))
        if win is None:
            raise RuntimeError("wechat_window_not_found")

        hwnd = int(getattr(win, "NativeWindowHandle", 0) or 0)
        stage_started_at = time.perf_counter()
        session_list = self._detector.get_session_list(win)
        log_timing("get_session_list", stage_started_at, found=bool(session_list))
        stage_started_at = time.perf_counter()
        scan = self._detector.scan_unread_sessions_detailed(session_list)
        self._last_unread_counts = {
            _conversation_key(self._account_id, self._detector.extract_session_title(item.name)): int(getattr(item, "unread_count", 1) or 1)
            for item in scan.sessions
            if self._detector.extract_session_title(item.name)
        }
        log_timing(
            "scan_unread_sessions",
            stage_started_at,
            source=scan.source,
            scanned=scan.scanned_items,
            unread=len(scan.sessions),
        )
        selected_sessions = scan.sessions[:session_limit]
        conversations: list[dict[str, Any]] = []
        messages: list[dict[str, Any]] = []
        processed: list[dict[str, Any]] = []

        for unread in selected_sessions:
            session_started_at = time.perf_counter()
            display_name = self._detector.extract_session_title(unread.name)
            if not display_name:
                continue

            clicked_method = ""
            clicked_foreground = False
            switched = False
            same_active_fast_path = self._should_skip_session_switch(display_name)
            if same_active_fast_path:
                selected_title = self._detector.extract_session_title(self._detector.get_selected_session_title(win))
                if selected_title == display_name:
                    logger.info(
                        "scan_unread fast_path display_name=%s reason=same_active_conversation selected_title=%s",
                        display_name,
                        selected_title,
                    )
                    switched = True
                else:
                    same_active_fast_path = False

            if not switched:
                stage_started_at = time.perf_counter()
                clicked = self._detector.click_session_detailed(
                    unread,
                    fallback_hwnd=hwnd,
                    allow_foreground=allow_foreground,
                )
                clicked_method = clicked.method
                clicked_foreground = clicked.foreground
                log_timing(
                    "click_session",
                    stage_started_at,
                    display_name=display_name,
                    ok=clicked.ok,
                    method=clicked.method,
                    foreground=clicked.foreground,
                )
                logger.info(
                    "scan_unread click display_name=%s ok=%s method=%s foreground=%s detail=%s",
                    display_name,
                    clicked.ok,
                    clicked.method,
                    clicked.foreground,
                    clicked.detail,
                )
                if not clicked.ok:
                    self._capture_failure_artifact(
                        "scan_unread_click",
                        window=win,
                        bubble_control=unread.control,
                        stage="click_session",
                        detail=clicked.detail,
                        window_title=clean(getattr(win, "Name", "")),
                        chat_title=display_name,
                    )
                    processed.append(
                        {
                            "display_name": display_name,
                            "clicked": False,
                            "method": clicked.method,
                            "error": clicked.detail,
                        }
                    )
                    continue

                if settle_ms:
                    stage_started_at = time.perf_counter()
                    time.sleep(settle_ms / 1000.0)
                    log_timing("settle_sleep", stage_started_at, display_name=display_name, configured_ms=settle_ms)

                stage_started_at = time.perf_counter()
                if not self._detector.verify_session_switch(
                    display_name,
                    win,
                    session_list=session_list,
                    timeout_ms=max(600, settle_ms + 600),
                ):
                    log_timing("verify_session_switch", stage_started_at, display_name=display_name, ok=False)
                    logger.warning("scan_unread verify failed display_name=%s", display_name)
                    self._capture_failure_artifact(
                        "scan_unread_verify",
                        window=win,
                        bubble_control=unread.control,
                        stage="verify_session_switch",
                        detail="session_switch_not_verified",
                        window_title=clean(getattr(win, "Name", "")),
                        chat_title=display_name,
                    )
                    processed.append(
                        {
                            "display_name": display_name,
                            "clicked": True,
                            "verified": False,
                            "method": clicked.method,
                            "error": "session_switch_not_verified",
                        }
                    )
                    continue
                log_timing("verify_session_switch", stage_started_at, display_name=display_name, ok=True)
                switched = True

            key = _conversation_key(self._account_id, display_name)
            unread_count = self._last_unread_counts.get(key)
            read_limit = self._message_window_limit_for_unread(display_name, unread_count, message_limit)
            tail_only = key in self._seen_conversation_keys
            stage_started_at = time.perf_counter()
            batch = self._reader.read_visible_messages(
                display_name=display_name,
                limit=read_limit,
                tail_only=tail_only,
            )
            log_timing(
                "read_visible_messages",
                stage_started_at,
                display_name=display_name,
                samples=len(batch.samples),
            )
            context = getattr(batch, "context", None)
            logger.info(
                "scan_unread read messages display_name=%s sample_count=%s chat_title=%s",
                display_name,
                len(batch.samples),
                getattr(context, "chat_title", ""),
            )
            event = self._conversation_event(display_name, unread, context=context)
            self._store.append(event)
            conversations.append(event)
            self._update_active_chat_state(display_name, context=context, samples=batch.samples)

            count_before = len(messages)
            for item in batch.samples:
                content = clean(getattr(item, "name", ""))
                if not content:
                    continue
                msg_event = self._message_event(display_name, item, context=context)
                platform_msg_id = clean(msg_event.get("payload", {}).get("platform_msg_id"))
                if platform_msg_id and platform_msg_id in self._seen_message_ids:
                    continue
                if platform_msg_id:
                    self._seen_message_ids.add(platform_msg_id)
                    self._conversation_message_cursors[_conversation_key(self._account_id, display_name)] = platform_msg_id
                self._store.append(msg_event)
                messages.append(msg_event)
            self._seen_conversation_keys.add(key)

            processed.append(
                {
                "display_name": display_name,
                "clicked": bool(switched or same_active_fast_path),
                "verified": bool(switched or same_active_fast_path),
                "method": clicked_method or "same_active_fast_path",
                "messages": len(messages) - count_before,
                "context": _chat_context_payload(context),
                }
            )
            log_timing("session_total", session_started_at, display_name=display_name)

        result = {
            "scan_source": scan.source,
            "scanned_items": scan.scanned_items,
            "unread_count": len(scan.sessions),
            "processed_count": len(processed),
            "conversation_count": len(conversations),
            "message_count": len(messages),
            "processed": processed,
        }
        log_timing(
            "total",
            started_at,
            unread=len(scan.sessions),
            processed=len(processed),
            conversations=len(conversations),
            messages=len(messages),
        )
        return result

    def _message_window_limit_for_unread(self, display_name: str, unread_count: int | None, default_limit: int) -> int:
        key = _conversation_key(self._account_id, display_name)
        if key not in self._seen_conversation_keys:
            return default_limit
        count = int(unread_count or 0)
        if count <= 0:
            return default_limit
        return max(1, min(default_limit, count + 2))

    def _is_same_active_conversation(self, display_name: str) -> bool:
        expected_key = clean(_conversation_key(self._account_id, display_name))
        with self._state_lock:
            return bool(expected_key and expected_key == self._last_active_conversation_key)

    def _update_active_chat_state(self, display_name: str, *, context: Any | None = None, samples: list[Any] | None = None) -> None:
        key = _conversation_key(self._account_id, display_name)
        fingerprint = ""
        if samples:
            parts: list[str] = []
            for sample in samples[:5]:
                parts.append(clean(getattr(sample, "platform_msg_id", "")) or clean(getattr(sample, "name", "")) or clean(getattr(sample, "rect", "")))
            fingerprint = "|".join(parts)
        with self._state_lock:
            self._last_active_conversation_key = key
            self._last_active_chat_title = clean(getattr(context, "chat_title", "")) or clean(display_name)
            if fingerprint:
                self._last_seen_message_fingerprint = fingerprint

    def _should_skip_session_switch(self, display_name: str) -> bool:
        with self._state_lock:
            return bool(
                self._last_active_conversation_key
                and self._last_active_chat_title
                and _conversation_key(self._account_id, display_name) == self._last_active_conversation_key
            )

    def _prepare_reply_draft(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        text = clean(params.get("text"))
        if not text:
            raise RuntimeError("empty_text")
        display_name = clean(params.get("display_name")) or _display_name_from_key(clean(params.get("conversation_key")))
        allow_foreground_fallback = allow_foreground_fallback_from(params)
        logger.info(
            "prepare_reply_draft request_id=%s display_name=%s allow_foreground_fallback=%s strict_background=%s text_len=%s",
            _request_id,
            display_name or "current",
            allow_foreground_fallback,
            truthy(params.get("strict_background"), False),
            len(text),
        )

        draft = self._sender.prepare_reply_draft(
            text=text,
            display_name=display_name,
            allow_foreground_fallback=allow_foreground_fallback,
        )

        task_id = clean(params.get("task_id"))
        event = self._task_result_event(
            "draft_prepared",
            display_name or "current",
            task_id=task_id,
            status="success",
            metadata={
                "method": draft.method,
                "strict_background_write_success": draft.strict_background_write_success,
                "strict_background_write_method": draft.strict_background_write_method,
                "strict_background_supported": draft.strict_background_supported,
                "strict_background_reason": draft.strict_background_reason,
            },
        )
        self._store.append(event)
        return {
            "prepared": draft.prepared,
            "task_id": task_id,
            "method": draft.method,
            "strict_background_write_success": draft.strict_background_write_success,
            "strict_background_write_method": draft.strict_background_write_method,
            "strict_background_supported": draft.strict_background_supported,
            "strict_background_reason": draft.strict_background_reason,
        }

    def _send_message(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        token = clean(params.get("confirm_token"))
        if token != "manual_confirmed_by_agent":
            raise RuntimeError("send_message_requires_manual_confirm_token")
        text = clean(params.get("text"))
        client_message_id = clean(params.get("client_message_id")) or clean(params.get("task_id")) or request_id
        allow_foreground_fallback = allow_foreground_fallback_from(params)
        result = self._prepare_reply_draft(params, request_id)
        draft_method = clean(result.get("draft_method") or result.get("method"))
        send_result = self._sender.send_prepared_message(
            allow_foreground_fallback=allow_foreground_fallback,
        )
        display_name = clean(params.get("display_name")) or _display_name_from_key(clean(params.get("conversation_key"))) or "current"
        task_id = clean(params.get("task_id"))
        self._store.append(
            self._task_result_event(
                "send_result_observed",
                display_name,
                task_id=task_id,
                client_message_id=client_message_id,
                status="sent",
                metadata={
                    "method": send_result.method,
                    "draft_method": draft_method,
                    "foreground": send_result.foreground,
                    "allow_foreground_fallback": allow_foreground_fallback,
                    "strict_background_write_success": bool(result.get("strict_background_write_success")),
                    "strict_background_write_method": clean(result.get("strict_background_write_method")),
                    "strict_background_supported": bool(result.get("strict_background_supported")),
                    "strict_background_reason": clean(result.get("strict_background_reason")),
                    "client_message_id": client_message_id,
                },
            )
        )
        self._store.append(
            self._event(
                "message_sent",
                _conversation_key(self._account_id, display_name),
                {
                    "status": "sent" if send_result.sent else "failed",
                    "foreground": send_result.foreground,
                    "send_method": send_result.method,
                    "draft_method": draft_method,
                    "strict_background_write_success": bool(result.get("strict_background_write_success")),
                    "strict_background_write_method": clean(result.get("strict_background_write_method")),
                    "strict_background_supported": bool(result.get("strict_background_supported")),
                    "strict_background_reason": clean(result.get("strict_background_reason")),
                    "client_message_id": client_message_id,
                    "content": text,
                    "task_id": task_id,
                },
            )
        )
        result["draft_method"] = draft_method
        result["send_method"] = send_result.method
        result["foreground"] = send_result.foreground
        result["sent"] = send_result.sent
        self._update_active_chat_state(display_name, context=None, samples=None)
        logger.info(
            "send_message request_id=%s display_name=%s draft_method=%s send_method=%s foreground=%s sent=%s strict_background_write_success=%s strict_background_write_method=%s strict_background_supported=%s strict_background_reason=%s",
            request_id,
            display_name,
            result["draft_method"],
            result["send_method"],
            result["foreground"],
            result["sent"],
            result.get("strict_background_write_success"),
            result.get("strict_background_write_method", ""),
            result.get("strict_background_supported"),
            result.get("strict_background_reason", ""),
        )
        return result

    def _probe(self) -> dict[str, Any]:
        return self._detector.probe()

    def _health_event(
        self,
        *,
        healthy: bool,
        status: str,
        message: str = "",
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        payload = {
            "status": status,
            "healthy": healthy,
            "message": message,
            "capabilities": {
                "text_message_read": True,
                "send_text": True,
                "fill_draft": True,
                "background_send": False,
                "requires_foreground_window": True,
                "requires_ocr": False,
            },
            "metadata": {
                **(metadata or {}),
                **self._failure_state_payload(),
            },
        }
        return self._event("account_health_changed", "", payload)

    def _conversation_event(self, display_name: str, sample: Any, *, context: Any | None = None) -> dict[str, Any]:
        key = _conversation_key(self._account_id, display_name)
        context_payload = _chat_context_payload(context)
        payload = {
            "display_name": display_name,
            "source_type": "ui_observed",
            "confidence": 75,
            "verification_status": "unverified",
            "session_id": clean(context_payload.get("session_id")) if context_payload else "",
            "is_group": bool(context_payload.get("is_group")) if context_payload else False,
            "member_count": context_payload.get("member_count") if context_payload else None,
            "metadata": {
                "observation_method": "uia",
                "automation_id": clean(getattr(sample, "automation_id", "")),
                "class_name": clean(getattr(sample, "class_name", "")),
                "rect": clean(getattr(sample, "rect", "")),
                "chat_context": context_payload,
            },
        }
        return self._event("conversation_observed", key, payload)

    def _message_event(self, display_name: str, sample: Any, *, context: Any | None = None) -> dict[str, Any]:
        key = _conversation_key(self._account_id, display_name)
        content = clean(getattr(sample, "name", ""))
        direction = self._infer_direction(sample)
        direction_method = clean(getattr(sample, "direction_method", "")) or "bubble_position"
        confidence = _event_confidence(getattr(sample, "role_confidence", None), default=65)
        context_payload = _chat_context_payload(context)
        raw_id = "|".join(
            [
                PLATFORM_WECHAT,
                self._account_id,
                key,
                direction,
                content,
                clean(getattr(sample, "rect", "")),
            ]
        )
        platform_msg_id = "wechat_" + _sha1(raw_id)[:24]
        payload = {
            "platform_msg_id": platform_msg_id,
            "direction": "inbound" if direction == "in" else "outbound",
            "sender_role": "customer" if direction == "in" else "agent",
            "sender_name": display_name if direction == "in" else "",
            "content_type": "text",
            "content": content,
            "source_type": "ui_observed",
            "confidence": confidence,
            "verification_status": "unverified",
            "metadata": {
                "observation_method": "uia",
                "direction_method": direction_method,
                "kind": clean(getattr(sample, "kind", "")),
                "class_name": clean(getattr(sample, "class_name", "")),
                "automation_id": clean(getattr(sample, "automation_id", "")),
                "rect": clean(getattr(sample, "rect", "")),
                "role_confidence": getattr(sample, "role_confidence", 0.0),
                "left_variance": getattr(sample, "left_variance", 0.0),
                "right_variance": getattr(sample, "right_variance", 0.0),
                "chat_context": context_payload,
            },
        }
        return self._event("message_observed", key, payload)

    def _task_result_event(
        self,
        event_type: str,
        display_name: str,
        *,
        task_id: str,
        client_message_id: str = "",
        status: str,
        metadata: dict[str, Any],
    ) -> dict[str, Any]:
        payload = {
            "status": status,
            "error_message": "",
            "verification_status": "auto_verified" if status in {"success", "sent"} else "unverified",
            "metadata": metadata,
        }
        event = self._event(event_type, _conversation_key(self._account_id, display_name), payload)
        if task_id:
            event["task_id"] = task_id
        if client_message_id:
            event["client_message_id"] = client_message_id
        return event

    def _event(self, event_type: str, conversation_key: str, payload: dict[str, Any]) -> dict[str, Any]:
        occurred_at = _now_iso()
        raw_id = f"{event_type}|{conversation_key}|{occurred_at}|{payload}"
        event = {
            "event_id": "evt_" + _sha1(raw_id)[:24],
            "event_type": event_type,
            "platform": PLATFORM_WECHAT,
            "account_id": self._account_id,
            "occurred_at": occurred_at,
            "payload": payload,
            "seq": None,
            "cursor": "",
        }
        if conversation_key:
            event["conversation_key"] = conversation_key
        return event

    def _ensure_not_cooling(self, stage: str) -> None:
        if not self._failures.should_cooldown():
            return
        sleep_seconds = min(self._failures.next_sleep(0.5, 6), self._failures.cooldown_seconds)
        time.sleep(max(0.0, sleep_seconds))

    def _failure_state_payload(self) -> dict[str, Any]:
        return {
            "failure_count": self._failures.failure_count,
            "same_stage_count": self._failures.same_stage_count,
            "last_stage": self._failures.last_stage,
            "cooldown_seconds": self._failures.cooldown_seconds,
            "should_cooldown": self._failures.should_cooldown(),
        }

    def _failure_metadata(self, stage: str, detail: str = "") -> dict[str, Any]:
        return {
            **self._failure_state_payload(),
            "stage": stage,
            "detail": detail,
        }

    def _capture_failure_artifact(
        self,
        label: str,
        *,
        window: Any | None,
        bubble_control: Any | None,
        stage: str,
        detail: str,
        window_title: str = "",
        chat_title: str = "",
    ) -> None:
        if window is None:
            return
        hwnd = int(getattr(window, "NativeWindowHandle", 0) or 0)
        if not hwnd:
            return
        self._debug_writer.save_snapshot(
            label,
            stage=stage,
            detail=detail,
            hwnd=hwnd,
            bubble_control=bubble_control,
            window_title=window_title,
            chat_title=chat_title,
        )

    def _infer_direction(self, sample: Any) -> str:
        explicit = clean(getattr(sample, "direction", ""))
        if explicit in {"in", "out"}:
            return explicit
        sender_role = clean(getattr(sample, "sender_role", ""))
        if sender_role == "agent":
            return "out"
        if sender_role == "customer":
            return "in"
        try:
            left = float(getattr(sample, "left", 0.0) or 0.0)
            right = float(getattr(sample, "right", 0.0) or 0.0)
        except (TypeError, ValueError):
            return "in"
        center = (left + right) / 2.0
        return "out" if center > 900.0 else "in"


def _event_confidence(value: Any, *, default: int) -> int:
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        return default
    if numeric <= 1.0:
        numeric *= 100.0
    return max(1, min(99, int(round(numeric))))


def _chat_context_payload(context: Any | None) -> dict[str, Any]:
    if context is None:
        return {}
    if is_dataclass(context):
        raw = asdict(context)
    elif isinstance(context, dict):
        raw = dict(context)
    else:
        raw = {
            "platform": getattr(context, "platform", ""),
            "chat_title": getattr(context, "chat_title", ""),
            "user_id": getattr(context, "user_id", ""),
            "is_group": getattr(context, "is_group", False),
            "member_count": getattr(context, "member_count", None),
            "session_id": getattr(context, "session_id", ""),
        }
    return {
        "platform": clean(raw.get("platform")),
        "chat_title": clean(raw.get("chat_title")),
        "user_id": clean(raw.get("user_id")),
        "is_group": bool(raw.get("is_group")),
        "member_count": raw.get("member_count"),
        "session_id": clean(raw.get("session_id")),
    }
