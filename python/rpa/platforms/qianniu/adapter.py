from __future__ import annotations

import hashlib
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Protocol

from .detector import QianniuDetector
from .qianniu_logging import get_logger
from .reader import MessageRecord, MessageReadResult, QianniuReader
from .sender import QianniuSender
from .sessions import QianniuSessionReader, SessionItem, session_titles_match
from .uia import uia_guard

logger = get_logger(__name__)

PLATFORM_QIANNIU = "qianniu"
DEFAULT_ACCOUNT_ID = "local_qianniu"
OBSERVER_POLL_INTERVAL_SEC = 1.5
OBSERVER_AFTER_WORK_SLEEP_SEC = 0.3
OBSERVER_ERROR_SLEEP_SEC = 3.0
OBSERVER_MESSAGE_LIMIT = 30


@dataclass(frozen=True)
class EnsureSessionResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""
    selected_title: str = ""


class EventSink(Protocol):
    def append(self, event: dict[str, Any]) -> int:
        ...


def clean(value: Any) -> str:
    return "" if value is None else str(value).strip()


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


def _elapsed_ms(started_at: float) -> float:
    return (time.perf_counter() - started_at) * 1000.0


def _conversation_key(account_id: str, display_name: str) -> str:
    safe_name = clean(display_name) or "current"
    return f"qianniu:{account_id}:{safe_name}"


def _display_name_from_key(value: str) -> str:
    raw = clean(value)
    if ":" in raw:
        return raw.rsplit(":", 1)[-1].strip()
    if raw.startswith("qianniu_"):
        return raw[len("qianniu_") :].strip()
    return raw


def _requested_display_name(display_name: Any, conversation_key: Any) -> str:
    display = clean(display_name)
    key = clean(conversation_key)
    if display and display != "current":
        if display == key or display.startswith("qianniu:") or display.startswith("qianniu_"):
            return _display_name_from_key(display)
        return display
    return _display_name_from_key(key)


class QianniuSidecarAdapter:
    def __init__(self, store: EventSink) -> None:
        self._store = store
        self._connected = False
        self._account_id = DEFAULT_ACCOUNT_ID
        self._detector = QianniuDetector()
        self._sessions = QianniuSessionReader(self._detector.config)
        self._reader = QianniuReader(self._detector.config)
        self._sender = QianniuSender(self._detector.config)
        self._observer_stop = threading.Event()
        self._observer_thread: threading.Thread | None = None
        self._observer_lock = threading.Lock()

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        started_at = time.perf_counter()
        request_id = clean(payload.get("request_id"))
        command = clean(payload.get("command"))
        self._account_id = clean(payload.get("account_id")) or self._account_id
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}

        handlers = {
            "connect": self._connect,
            "disconnect": self._disconnect,
            "health_check": self._health_check,
            "fetch_visible_conversations": self._fetch_visible_conversations,
            "fetch_visible_messages": self._fetch_visible_messages,
            "scan_unread_and_fetch": self._scan_unread_and_fetch,
            "prepare_reply_draft": self._prepare_reply_draft,
            "send_message": self._send_message,
        }
        handler = handlers.get(command)
        if handler is None:
            logger.warning("qianniu command unsupported request_id=%s command=%s", request_id, command)
            return payload_status(
                "error",
                request_id,
                error=f"unsupported_command:{command}",
                result={},
            )
        try:
            logger.info(
                "qianniu command start request_id=%s command=%s account_id=%s params=%s",
                request_id,
                command,
                self._account_id,
                list(params.keys()),
            )
            with uia_guard(command or "qianniu"):
                result = handler(params, request_id)
            logger.info(
                "qianniu command done request_id=%s command=%s status=success elapsed_ms=%.1f keys=%s",
                request_id,
                command,
                _elapsed_ms(started_at),
                list(result.keys()) if isinstance(result, dict) else [],
            )
            return payload_status("success", request_id, result=result)
        except Exception as exc:
            logger.exception(
                "qianniu command failed request_id=%s command=%s elapsed_ms=%.1f",
                request_id,
                command,
                _elapsed_ms(started_at),
            )
            self._store.append(
                self._health_event(
                    healthy=False,
                    status="error",
                    message=str(exc),
                    metadata={"stage": command or "qianniu", "detail": str(exc)},
                )
            )
            return payload_status("error", request_id, error=str(exc), result={})

    def health(self) -> dict[str, Any]:
        result = self._probe()
        return {
            "status": "success",
            "platform": PLATFORM_QIANNIU,
            "account_id": self._account_id,
            "connected": self._connected,
            "health": result,
        }

    def _connect(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        self._connected = True
        health = self._probe()
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata=health,
            )
        )
        if params.get("emit_initial_snapshot", False):
            self._fetch_visible_conversations({"limit": params.get("limit", 30), "detect_unread": True}, request_id)
            self._fetch_visible_messages({"limit": params.get("message_limit", 30)}, request_id)
        self._start_observer()
        return {"connected": True, "health": health}

    def _disconnect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        self._connected = False
        self._stop_observer()
        self._store.append(
            self._health_event(
                healthy=False,
                status="offline",
                message="adapter disconnected",
                metadata={"stage": "disconnect"},
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
                name="qianniu-observer",
                daemon=True,
            )
            self._observer_thread.start()
        logger.info("qianniu observer started poll_interval=%.1fs", OBSERVER_POLL_INTERVAL_SEC)

    def _stop_observer(self) -> None:
        with self._observer_lock:
            thread = self._observer_thread
            self._observer_stop.set()
        if thread and thread.is_alive():
            thread.join(timeout=2.0)
        with self._observer_lock:
            if self._observer_thread is thread:
                self._observer_thread = None
        logger.info("qianniu observer stopped")

    def _observer_loop(self) -> None:
        while not self._observer_stop.is_set():
            if not self._connected:
                self._observer_stop.wait(OBSERVER_POLL_INTERVAL_SEC)
                continue
            try:
                tick_started_at = time.perf_counter()
                request_id = f"observer-{int(time.time() * 1000)}"
                uia_guard_started_at = time.perf_counter()
                with uia_guard("qianniu_observer"):
                    uia_guard_wait_ms = _elapsed_ms(uia_guard_started_at)
                    scan_started_at = time.perf_counter()
                    result = self._scan_unread_and_fetch(
                        {"message_limit": OBSERVER_MESSAGE_LIMIT},
                        request_id,
                    )
                    scan_ms = _elapsed_ms(scan_started_at)
                had_work = bool(result.get("message_count") or result.get("conversation_count") or result.get("processed_count"))
                logger.info(
                    "qianniu observer timing request_id=%s total_ms=%.1f uia_guard_wait_ms=%.1f scan_ms=%.1f had_work=%s unread=%s conversations=%s messages=%s processed=%s",
                    request_id,
                    _elapsed_ms(tick_started_at),
                    uia_guard_wait_ms,
                    scan_ms,
                    had_work,
                    result.get("unread_count"),
                    result.get("conversation_count"),
                    result.get("message_count"),
                    result.get("processed_count"),
                )
                self._observer_stop.wait(OBSERVER_AFTER_WORK_SLEEP_SEC if had_work else OBSERVER_POLL_INTERVAL_SEC)
            except Exception as exc:
                logger.exception("qianniu observer tick failed: %s", exc)
                self._store.append(
                    self._health_event(
                        healthy=False,
                        status="degraded",
                        message=str(exc),
                        metadata={"stage": "qianniu_observer", "detail": str(exc)},
                    )
                )
                self._observer_stop.wait(OBSERVER_ERROR_SLEEP_SEC)

    def _health_check(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        health = self._probe()
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata=health,
            )
        )
        return health

    def _fetch_visible_conversations(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 60) or 60), 100))
        detect_unread = bool(params.get("detect_unread", True))
        samples = self._sessions.read_visible_sessions(limit=limit, detect_unread=detect_unread)
        conversations: list[dict[str, Any]] = []
        for item in samples:
            if not clean(item.title):
                continue
            event = self._conversation_event(item)
            self._store.append(event)
            conversations.append(event)
        logger.info(
            "qianniu fetch_visible_conversations timing request_id=%s total_ms=%.1f limit=%s detect_unread=%s samples=%s emitted=%s",
            _request_id,
            _elapsed_ms(started_at),
            limit,
            detect_unread,
            len(samples),
            len(conversations),
        )
        return {"count": len(conversations), "conversations": conversations}

    def _fetch_visible_messages(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 30) or 30), 100))
        requested_name = _display_name_from_key(clean(params.get("conversation_key")))
        display_name = clean(params.get("display_name")) or requested_name or "current"
        read_started_at = time.perf_counter()
        chat_root = self._current_session_chat_root()
        try:
            result, messages = self._reader.read_visible_messages_debug(limit=limit, chat_root=chat_root)
        except TypeError:
            result, messages = self._reader.read_visible_messages_debug(limit=limit)
        read_ms = _elapsed_ms(read_started_at)
        emit_started_at = time.perf_counter()
        emitted: list[dict[str, Any]] = []
        for item in messages:
            event = self._message_event(display_name, item)
            self._store.append(event)
            emitted.append(event)
        emit_ms = _elapsed_ms(emit_started_at)
        logger.info(
            "qianniu fetch_visible_messages timing request_id=%s total_ms=%.1f read_ms=%.1f emit_ms=%.1f display_name=%s read_source=%s read_ok=%s messages=%s emitted=%s",
            _request_id,
            _elapsed_ms(started_at),
            read_ms,
            emit_ms,
            display_name,
            result.source,
            result.ok,
            len(messages),
            len(emitted),
        )
        return {
            "count": len(emitted),
            "messages": emitted,
            "display_name": display_name,
            "read_result": {
                "ok": result.ok,
                "source": result.source,
                "detail": result.detail,
                "text_count": len(result.texts),
            },
        }

    def _scan_unread_and_fetch(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        total_started_at = time.perf_counter()
        limit = max(1, min(int(params.get("message_limit", params.get("limit", 30)) or 30), 100))
        sessions_started_at = time.perf_counter()
        sessions = self._sessions.read_visible_sessions(limit=100, detect_unread=True)
        sessions_ms = _elapsed_ms(sessions_started_at)
        unread_items = [item for item in sessions if item.unread]
        logger.info(
            "qianniu scan_unread_timing request_id=%s stage=read_visible_sessions ms=%.1f session_count=%s unread_count=%s limit=%s",
            _request_id,
            sessions_ms,
            len(sessions),
            len(unread_items),
            limit,
        )
        if not unread_items:
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f session_count=%s unread_count=0 processed=0 messages=0",
                _request_id,
                _elapsed_ms(total_started_at),
                len(sessions),
            )
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "processed": [],
            }

        target = unread_items[0]
        select_started_at = time.perf_counter()
        if hasattr(self._sessions, "select_session"):
            switched, method = self._sessions.select_session(target)
        else:
            selected, switched, method = self._sessions.select_first_unread()
            if selected is not None:
                target = selected
        select_ms = _elapsed_ms(select_started_at)
        if not switched:
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f session_count=%s unread_count=%s selected=%s select_ms=%.1f switch_method=%s switched=False",
                _request_id,
                _elapsed_ms(total_started_at),
                len(sessions),
                len(unread_items),
                target.title,
                select_ms,
                method,
            )
            return {
                "unread_count": len(unread_items),
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 1,
                "processed": [{"display_name": target.title, "error": method or "session_switch_failed"}],
            }

        emit_started_at = time.perf_counter()
        conversation_event = self._conversation_event(target)
        self._store.append(conversation_event)
        conversation_emit_ms = _elapsed_ms(emit_started_at)
        read_started_at = time.perf_counter()
        chat_root = self._current_session_chat_root()
        try:
            read_result, messages = self._reader.read_visible_messages_debug(limit=limit, chat_root=chat_root)
        except TypeError:
            read_result, messages = self._reader.read_visible_messages_debug(limit=limit)
        read_ms = _elapsed_ms(read_started_at)
        messages_emit_started_at = time.perf_counter()
        message_events = [self._message_event(target.title, item) for item in messages]
        filtered_message_events = self._filter_unread_message_events(message_events)
        emitted_messages: list[dict[str, Any]] = []
        for event in filtered_message_events:
            self._store.append(event)
            emitted_messages.append(event)
        messages_emit_ms = _elapsed_ms(messages_emit_started_at)
        logger.info(
            "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f sessions_ms=%.1f select_ms=%.1f conversation_emit_ms=%.1f read_ms=%.1f message_emit_ms=%.1f session_count=%s unread_count=%s selected=%s switch_method=%s read_source=%s read_ok=%s parsed_messages=%s messages=%s filtered=%s",
            _request_id,
            _elapsed_ms(total_started_at),
            sessions_ms,
            select_ms,
            conversation_emit_ms,
            read_ms,
            messages_emit_ms,
            len(sessions),
            len(unread_items),
            target.title,
            method,
            read_result.source,
            read_result.ok,
            len(message_events),
            len(emitted_messages),
            len(message_events) - len(emitted_messages),
        )
        return {
            "unread_count": len(unread_items),
            "conversation_count": 1,
            "message_count": len(emitted_messages),
            "processed_count": 1,
            "processed": [
                {
                    "display_name": target.title,
                    "switch_method": method,
                    "messages": len(emitted_messages),
                    "parsed_messages": len(message_events),
                    "filtered_messages": len(message_events) - len(emitted_messages),
                    "read_source": read_result.source,
                    "read_ok": read_result.ok,
                }
            ],
        }

    def _prepare_reply_draft(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        text = clean(params.get("text"))
        if not text:
            raise RuntimeError("empty_text")
        display_name = _requested_display_name(params.get("display_name"), params.get("conversation_key")) or "current"
        chat_root = self._current_session_chat_root()
        try:
            result = self._sender.prepare_reply_draft(text, chat_root=chat_root)
        except TypeError:
            result = self._sender.prepare_reply_draft(text)
        if not result.ok:
            raise RuntimeError(result.detail or result.stage)
        task_id = clean(params.get("task_id"))
        self._store.append(
            self._task_result_event(
                "draft_prepared",
                display_name,
                task_id=task_id,
                status="success",
                metadata={"method": result.method},
            )
        )
        return {
            "prepared": True,
            "task_id": task_id,
            "method": result.method,
        }

    def _ensure_target_session_selected(self, display_name: str, conversation_key: str = "") -> EnsureSessionResult:
        started_at = time.perf_counter()
        target = _requested_display_name(display_name, conversation_key)
        if not target or target == "current":
            return EnsureSessionResult(ok=True, stage="skipped", method="no_explicit_target")

        selected = self._selected_session(fresh=False)
        if selected and session_titles_match(selected.title, target):
            logger.info(
                "qianniu ensure_target_session timing target=%s stage=current_selected method=selected_scan selected=%s total_ms=%.1f",
                target,
                selected.title,
                _elapsed_ms(started_at),
            )
            return EnsureSessionResult(
                ok=True,
                stage="current_selected",
                method="selected_scan",
                selected_title=selected.title,
            )

        last_detail = clean(getattr(selected, "title", "")) if selected else ""
        for attempt in range(1, 4):
            scan_started_at = time.perf_counter()
            target_item = self._find_target_session_fresh(target)
            scan_ms = _elapsed_ms(scan_started_at)
            if target_item is None:
                logger.info(
                    "qianniu ensure_target_session timing target=%s attempt=%s stage=find_target ok=False scan_ms=%.1f selected_before=%s total_ms=%.1f",
                    target,
                    attempt,
                    scan_ms,
                    last_detail,
                    _elapsed_ms(started_at),
                )
                return EnsureSessionResult(
                    ok=False,
                    stage="find_target",
                    method="fresh_scan",
                    detail="target_session_not_found",
                    selected_title=last_detail,
                )

            select_started_at = time.perf_counter()
            switched, switch_method = self._select_session(target_item)
            select_ms = _elapsed_ms(select_started_at)
            if not switched:
                logger.info(
                    "qianniu ensure_target_session timing target=%s attempt=%s stage=select_target ok=False method=%s scan_ms=%.1f select_ms=%.1f total_ms=%.1f",
                    target,
                    attempt,
                    switch_method,
                    scan_ms,
                    select_ms,
                    _elapsed_ms(started_at),
                )
                last_detail = switch_method or "session_switch_failed"
                continue

            verify_started_at = time.perf_counter()
            verified = self._selected_session(fresh=True)
            verify_ms = _elapsed_ms(verify_started_at)
            if verified and session_titles_match(verified.title, target):
                logger.info(
                    "qianniu ensure_target_session timing target=%s attempt=%s stage=verified ok=True method=%s scan_ms=%.1f select_ms=%.1f verify_ms=%.1f selected=%s total_ms=%.1f",
                    target,
                    attempt,
                    switch_method,
                    scan_ms,
                    select_ms,
                    verify_ms,
                    verified.title,
                    _elapsed_ms(started_at),
                )
                return EnsureSessionResult(
                    ok=True,
                    stage="verified",
                    method=f"fresh_scan+{switch_method}+verify_selected",
                    selected_title=verified.title,
                )

            last_detail = clean(getattr(verified, "title", "")) if verified else "selected_session_unavailable"
            logger.info(
                "qianniu ensure_target_session timing target=%s attempt=%s stage=verify_target ok=False method=%s scan_ms=%.1f select_ms=%.1f verify_ms=%.1f selected_after=%s total_ms=%.1f",
                target,
                attempt,
                switch_method,
                scan_ms,
                select_ms,
                verify_ms,
                last_detail,
                _elapsed_ms(started_at),
            )

        return EnsureSessionResult(
            ok=False,
            stage="verify_target",
            method="fresh_scan+select+verify_selected",
            detail="target_session_not_verified",
            selected_title=last_detail,
        )

    def _selected_session(self, *, fresh: bool) -> SessionItem | None:
        getter = getattr(self._sessions, "selected_session", None)
        if callable(getter):
            try:
                return getter(fresh=fresh)
            except TypeError:
                return getter()
        return None

    def _find_target_session_fresh(self, target: str) -> SessionItem | None:
        invalidator = getattr(self._sessions, "invalidate_cache", None)
        if callable(invalidator):
            invalidator()
        sessions = self._sessions.read_visible_sessions(limit=100, detect_unread=False)
        for item in sessions:
            if session_titles_match(item.title, target):
                return item
        return None

    def _select_session(self, item: SessionItem) -> tuple[bool, str]:
        selector = getattr(self._sessions, "select_session", None)
        if not callable(selector):
            return False, "select_session_unavailable"
        return selector(item)

    def _send_message(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        total_started_at = time.perf_counter()
        token = clean(params.get("confirm_token"))
        if token != "manual_confirmed_by_agent":
            raise RuntimeError("send_message_requires_manual_confirm_token")
        content_type = clean(params.get("content_type")) or "text"
        text = clean(params.get("text"))
        file_path = clean(params.get("file_path"))
        if content_type == "text" and not text:
            raise RuntimeError("empty_text")
        if content_type in {"image", "video", "file"} and not file_path:
            raise RuntimeError("file_path_required")
        if content_type not in {"text", "image", "video", "file"}:
            raise RuntimeError("unsupported_content_type")
        display_name = _requested_display_name(params.get("display_name"), params.get("conversation_key")) or "current"
        task_id = clean(params.get("task_id"))
        client_message_id = clean(params.get("client_message_id")) or task_id or request_id
        ensure_result = self._ensure_target_session_selected(display_name, clean(params.get("conversation_key")))
        if not ensure_result.ok:
            raise RuntimeError(ensure_result.detail or ensure_result.stage or "target_session_not_verified")
        send_started_at = time.perf_counter()
        chat_root = self._current_session_chat_root()
        if content_type == "text":
            try:
                result = self._sender.send_text(text, dry_run=False, chat_root=chat_root)
            except TypeError:
                result = self._sender.send_text(text, dry_run=False)
        else:
            try:
                result = self._sender.send_media(
                    file_path,
                    content_type,
                    dry_run=False,
                    chat_root=chat_root,
                )
            except TypeError:
                result = self._sender.send_media(file_path, content_type, dry_run=False)
        send_ms = _elapsed_ms(send_started_at)
        if not result.ok:
            logger.info(
                "qianniu send_message timing request_id=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f status=failed stage=%s detail=%s",
                request_id,
                client_message_id,
                _elapsed_ms(total_started_at),
                send_ms,
                result.stage,
                result.detail,
            )
            raise RuntimeError(result.detail or result.stage)
        emit_started_at = time.perf_counter()
        self._store.append(
            self._task_result_event(
                "send_result_observed",
                display_name,
                task_id=task_id,
                client_message_id=client_message_id,
                status="sent",
                metadata={
                    "method": result.method,
                    "client_message_id": client_message_id,
                    "target_session_method": ensure_result.method,
                    "target_session_stage": ensure_result.stage,
                    "target_session_selected": ensure_result.selected_title,
                    "content_type": content_type,
                    "file_path": file_path,
                    "file_name": clean(params.get("file_name")),
                },
            )
        )
        self._store.append(
            self._event(
                "message_sent",
                _conversation_key(self._account_id, display_name),
                {
                    "status": "sent",
                    "send_method": result.method,
                    "client_message_id": client_message_id,
                    "target_session_method": ensure_result.method,
                    "content_type": content_type,
                    "content": text,
                    "file_path": file_path,
                    "file_name": clean(params.get("file_name")),
                },
            )
        )
        emit_ms = _elapsed_ms(emit_started_at)
        logger.info(
            "qianniu send_message timing request_id=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f emit_ms=%.1f status=sent method=%s display_name=%s",
            request_id,
            client_message_id,
            _elapsed_ms(total_started_at),
            send_ms,
            emit_ms,
            result.method,
            display_name,
        )
        return {
            "sent": True,
            "task_id": task_id,
            "client_message_id": client_message_id,
            "method": result.method,
            "target_session_method": ensure_result.method,
            "target_session_stage": ensure_result.stage,
            "content_type": content_type,
        }

    def _current_session_chat_root(self) -> Any | None:
        getter = getattr(self._sessions, "current_chat_root", None)
        if callable(getter):
            return getter()
        return getattr(self._sessions, "last_chat_root", None)

    def _probe(self) -> dict[str, Any]:
        process_ids = self._detector.find_process_ids()
        window = self._detector.find_best_window()
        handle = self._detector.find_current_chat()
        chat_root = handle.chat_root if handle else None
        has_message_display = bool(self._detector.find_message_display(chat_root)) if chat_root else False
        has_message_web = bool(self._detector.find_message_web(chat_root)) if chat_root else False
        has_input_field = bool(self._detector.find_input_field(chat_root)) if chat_root else False
        has_send_button = bool(self._detector.find_send_button(chat_root)) if chat_root else False
        healthy = bool(handle and (has_message_display or has_message_web) and has_input_field)
        reason = "ok" if healthy else "未找到可用千牛聊天窗口"
        return {
            "healthy": healthy,
            "reason": reason,
            "process_ids": process_ids,
            "window_title": clean(getattr(window, "title", "")),
            "window_class_name": clean(getattr(window, "class_name", "")),
            "has_chat_root": bool(chat_root),
            "has_message_display": has_message_display,
            "has_message_web": has_message_web,
            "has_input_field": has_input_field,
            "has_send_button": has_send_button,
        }

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
            "metadata": metadata or {},
        }
        return self._event("account_health_changed", "", payload)

    def _conversation_event(self, item: SessionItem) -> dict[str, Any]:
        payload = {
            "display_name": clean(item.title),
            "source_type": "ui_observed",
            "confidence": 70,
            "verification_status": "unverified",
            "metadata": {
                "observation_method": "uia",
                "automation_id": clean(item.automation_id),
                "class_name": clean(item.class_name),
                "control_type": clean(item.control_type),
                "rect": clean(item.rect),
                "raw_texts": list(item.raw_texts),
                "unread": bool(item.unread),
                "unread_score": float(item.unread_score),
            },
        }
        return self._event("conversation_observed", _conversation_key(self._account_id, item.title), payload)

    def _message_event(self, display_name: str, item: MessageRecord) -> dict[str, Any]:
        direction = clean(item.direction) or "unknown"
        sender_role = "customer" if direction == "inbound" else "agent" if direction == "outbound" else "unknown"
        raw_id = "|".join(
            [
                PLATFORM_QIANNIU,
                self._account_id,
                display_name,
                clean(item.timestamp),
                clean(item.sender),
                direction,
                clean(item.text),
            ]
        )
        payload = {
            "platform_msg_id": "qianniu_" + _sha1(raw_id)[:24],
            "display_name": display_name,
            "direction": direction,
            "sender_role": sender_role,
            "sender_name": clean(item.sender),
            "content_type": "text",
            "content": clean(item.text),
            "source_type": "ui_observed",
            "confidence": 70,
            "verification_status": "unverified",
            "metadata": {
                "observation_method": "uia",
                "timestamp": clean(item.timestamp),
                "status": clean(item.status),
                "raw": clean(item.raw),
            },
        }
        return self._event("message_observed", _conversation_key(self._account_id, display_name), payload)

    def _filter_unread_message_events(self, events: list[dict[str, Any]]) -> list[dict[str, Any]]:
        if not events:
            return []
        filterer = getattr(self._store, "filter_observed_message_events", None)
        if not callable(filterer):
            return events
        try:
            filtered = filterer(events, bootstrap_limit=100, incremental_limit=10)
        except Exception:
            logger.exception("qianniu unread message filter failed; emitting parsed messages")
            return events
        return list(filtered)

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
            "platform": PLATFORM_QIANNIU,
            "account_id": self._account_id,
            "occurred_at": occurred_at,
            "payload": payload,
            "seq": None,
            "cursor": "",
        }
        if conversation_key:
            event["conversation_key"] = conversation_key
        return event
