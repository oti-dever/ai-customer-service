from __future__ import annotations

import hashlib
import threading
import time
from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Protocol

from .accounts import AccountTab, QianniuAccountReader
from .detector import QianniuDetector
from .qianniu_logging import get_listen_flow_logger, get_logger
from .reader import MessageRecord, MessageReadResult, QianniuReader
from .sender import QianniuSender
from .sessions import QianniuSessionReader, SessionItem, normalize_session_title, session_titles_match
from .uia import uia_guard

logger = get_logger(__name__)
listen_flow_logger = get_listen_flow_logger(__name__)

PLATFORM_QIANNIU = "qianniu"
DEFAULT_ACCOUNT_ID = "local_qianniu"
OBSERVER_POLL_INTERVAL_SEC = 1.5
OBSERVER_AFTER_WORK_SLEEP_SEC = 0.3
OBSERVER_ERROR_SLEEP_SEC = 3.0
OBSERVER_MESSAGE_LIMIT = 30
OBSERVER_UIA_LOCK_TIMEOUT_SEC = 0.8
ASYNC_HEALTH_PROBE_ENABLED = False
HEALTH_PROBE_TIMEOUT_SEC = 3.0
HEALTH_PROBE_CONNECT_DELAY_SEC = 3.0
HEALTH_PROBE_STOP_JOIN_SEC = 1.0
HEALTH_PROBE_UIA_LOCK_TIMEOUT_SEC = 0.8
PROBE_STAGE_WARN_MS = 800.0
SEND_FAST_CONTEXT_TTL_SEC = 30.0
CONVERSATION_WORKER_IDLE_WAIT_SEC = 0.5
CONVERSATION_WORKER_STOP_JOIN_SEC = 2.0
WORKER_AUTO_REPLY_CONTEXT_TTL_SEC = 180.0
CONVERSATION_TASK_SESSION_HINT_TTL_SEC = 30.0


@dataclass(frozen=True)
class EnsureSessionResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""
    selected_title: str = ""


@dataclass(frozen=True)
class EnsureAccountResult:
    ok: bool
    stage: str
    method: str = ""
    detail: str = ""
    account_id: str = ""
    display_name: str = ""
    switched: bool = False
    unread_account_count: int = 0


@dataclass(frozen=True)
class AccountScanTarget:
    account_id: str
    display_name: str = ""
    selected: bool = False
    has_top_unread_hint: bool = False
    unread_score: float = 0.0
    source: str = ""
    synthetic: bool = False


@dataclass(frozen=True)
class ActiveConversationContext:
    account_id: str
    account_display_name: str
    session_title: str
    conversation_key: str
    updated_at: float
    source: str = ""


@dataclass(frozen=True)
class WorkerAutoReplyContext:
    account_id: str
    account_display_name: str
    session_title: str
    conversation_key: str
    task_id: str
    dedupe_key: str
    updated_at: float
    source: str = ""
    chat_root: Any | None = None
    input_field: Any | None = None
    input_source: str = ""


@dataclass
class QianniuConversationTask:
    task_id: str
    dedupe_key: str
    account_id: str
    account_display_name: str
    session_title: str
    conversation_key: str
    source: str
    created_at: float
    message_limit: int = OBSERVER_MESSAGE_LIMIT
    attempt: int = 0
    last_unread_hint: dict[str, Any] | None = None
    session_hint: dict[str, Any] | None = None


class QianniuConversationTaskQueue:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._items: deque[QianniuConversationTask] = deque()
        self._queued_keys: set[str] = set()
        self._processing_keys: set[str] = set()

    def enqueue(self, task: QianniuConversationTask) -> tuple[bool, str]:
        key = clean(task.dedupe_key)
        if not key:
            return False, "missing_dedupe_key"
        with self._lock:
            if key in self._queued_keys:
                return False, "already_queued"
            if key in self._processing_keys:
                return False, "already_processing"
            self._items.append(task)
            self._queued_keys.add(key)
            return True, "queued"

    def dequeue(self) -> QianniuConversationTask | None:
        with self._lock:
            if not self._items:
                return None
            task = self._items.popleft()
            key = clean(task.dedupe_key)
            self._queued_keys.discard(key)
            self._processing_keys.add(key)
            task.attempt += 1
            return task

    def complete(self, task: QianniuConversationTask) -> None:
        with self._lock:
            self._processing_keys.discard(clean(task.dedupe_key))

    def fail(self, task: QianniuConversationTask, *, requeue: bool = False) -> None:
        key = clean(task.dedupe_key)
        with self._lock:
            self._processing_keys.discard(key)
            if requeue and key and key not in self._queued_keys:
                self._items.append(task)
                self._queued_keys.add(key)

    def clear(self) -> None:
        with self._lock:
            self._items.clear()
            self._queued_keys.clear()
            self._processing_keys.clear()

    def snapshot(self) -> dict[str, int]:
        with self._lock:
            return {
                "queued_count": len(self._items),
                "processing_count": len(self._processing_keys),
            }


class QianniuConversationWorker:
    def __init__(self, adapter: Any) -> None:
        self._adapter = adapter
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._processing = False

    def start(self) -> None:
        with self._lock:
            if self._thread and self._thread.is_alive():
                return
            self._stop.clear()
            self._wake.clear()
            self._processing = False
            self._thread = threading.Thread(
                target=self._loop,
                name="qianniu-conversation-worker",
                daemon=True,
            )
            self._thread.start()
        listen_flow_logger.info("listen_flow conversation_worker_started")

    def stop(self, *, timeout_sec: float = CONVERSATION_WORKER_STOP_JOIN_SEC) -> None:
        with self._lock:
            thread = self._thread
            self._stop.set()
            self._wake.set()
        if thread and thread.is_alive():
            thread.join(timeout=max(0.0, float(timeout_sec)))
        alive_after = bool(thread and thread.is_alive())
        with self._lock:
            if self._thread is thread and not alive_after:
                self._thread = None
            if not alive_after:
                self._processing = False
        listen_flow_logger.info(
            "listen_flow conversation_worker_stopped alive_after=%s timeout_sec=%.1f",
            alive_after,
            max(0.0, float(timeout_sec)),
        )

    def wake(self) -> None:
        self._wake.set()

    def is_running(self) -> bool:
        with self._lock:
            return bool(self._thread and self._thread.is_alive())

    def is_processing(self) -> bool:
        with self._lock:
            return bool(self._processing)

    def snapshot(self) -> dict[str, bool]:
        with self._lock:
            return {
                "running": bool(self._thread and self._thread.is_alive()),
                "processing": bool(self._processing),
            }

    def _set_processing(self, value: bool) -> None:
        with self._lock:
            self._processing = value

    def _loop(self) -> None:
        listen_flow_logger.info("listen_flow conversation_worker_loop_start")
        try:
            while not self._stop.is_set():
                self._wake.wait(CONVERSATION_WORKER_IDLE_WAIT_SEC)
                self._wake.clear()
                if self._stop.is_set():
                    break
                if not self._adapter._connected:
                    continue
                pending_context = self._adapter._worker_auto_reply_context_snapshot()
                if pending_context is not None:
                    listen_flow_logger.info(
                        "listen_flow conversation_worker_wait_auto_reply task_id=%s dedupe_key=%s queue_depth=%s",
                        pending_context.task_id,
                        pending_context.dedupe_key,
                        self._adapter._conversation_task_queue.snapshot().get("queued_count", 0),
                    )
                    continue

                snapshot = self._adapter._conversation_task_queue.snapshot()
                if int(snapshot.get("queued_count", 0) or 0) <= 0:
                    continue

                self._set_processing(True)
                try:
                    while not self._stop.is_set() and self._adapter._connected:
                        pending_context = self._adapter._worker_auto_reply_context_snapshot()
                        if pending_context is not None:
                            listen_flow_logger.info(
                                "listen_flow conversation_worker_pause_for_auto_reply task_id=%s dedupe_key=%s queue_depth=%s",
                                pending_context.task_id,
                                pending_context.dedupe_key,
                                self._adapter._conversation_task_queue.snapshot().get("queued_count", 0),
                            )
                            break
                        snapshot = self._adapter._conversation_task_queue.snapshot()
                        if int(snapshot.get("queued_count", 0) or 0) <= 0:
                            break
                        request_id = f"worker-{int(time.time() * 1000)}"
                        listen_flow_logger.info(
                            "listen_flow conversation_worker_task_start request_id=%s queue_depth=%s processing=%s",
                            request_id,
                            snapshot.get("queued_count", 0),
                            snapshot.get("processing_count", 0),
                        )
                        started_at = time.perf_counter()
                        try:
                            with uia_guard("qianniu_conversation_worker"):
                                if self._stop.is_set() or not self._adapter._connected:
                                    listen_flow_logger.info(
                                        "listen_flow conversation_worker_task_skipped request_id=%s reason=%s",
                                        request_id,
                                        "stopped_after_lock" if self._stop.is_set() else "disconnected_after_lock",
                                    )
                                    break
                                result = self._adapter._drain_one_conversation_task(
                                    {},
                                    request_id,
                                    limit=OBSERVER_MESSAGE_LIMIT,
                                )
                            listen_flow_logger.info(
                                "listen_flow conversation_worker_task_done request_id=%s elapsed_ms=%.1f status=%s conversations=%s messages=%s processed=%s",
                                request_id,
                                _elapsed_ms(started_at),
                                clean(result.get("task_status")),
                                result.get("conversation_count", 0),
                                result.get("message_count", 0),
                                result.get("processed_count", 0),
                            )
                        except Exception as exc:
                            logger.exception("qianniu conversation worker task failed request_id=%s", request_id)
                            listen_flow_logger.exception(
                                "listen_flow conversation_worker_task_failed request_id=%s elapsed_ms=%.1f error=%s",
                                request_id,
                                _elapsed_ms(started_at),
                                str(exc),
                            )
                            break
                finally:
                    self._set_processing(False)
        finally:
            self._set_processing(False)
            listen_flow_logger.info("listen_flow conversation_worker_loop_exit")


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


def _conversation_key_parts(value: str) -> tuple[str, str]:
    raw = clean(value)
    if raw.startswith("qianniu:"):
        parts = raw.split(":", 2)
        if len(parts) == 3:
            return parts[1].strip(), parts[2].strip()
        if len(parts) == 2:
            return "", parts[1].strip()
    return "", ""


def _display_name_from_key(value: str) -> str:
    raw = clean(value)
    _account_id, display_name = _conversation_key_parts(raw)
    if display_name:
        return display_name
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


def _session_unread_key(item: SessionItem) -> str:
    return normalize_session_title(clean(getattr(item, "title", "")))


def _account_scoped_session_unread_key(account_id: str, item: SessionItem) -> str:
    return f"{clean(account_id) or DEFAULT_ACCOUNT_ID}:{_session_unread_key(item)}"


def _queue_dedupe_key(account_id: str, session_title: str) -> str:
    normalized_title = normalize_session_title(clean(session_title)) or "current"
    return f"{clean(account_id) or DEFAULT_ACCOUNT_ID}:{normalized_title}"


def _queue_task_id(request_id: str, account_id: str, session_title: str, source: str = "") -> str:
    raw = "|".join(
        [
            clean(request_id),
            clean(account_id) or DEFAULT_ACCOUNT_ID,
            normalize_session_title(clean(session_title)) or "current",
            clean(source),
        ]
    )
    return "qnq_" + _sha1(raw)[:16]


def _queue_wait_ms_from_params(params: dict[str, Any]) -> float:
    raw_wait = params.get("queue_wait_ms")
    if raw_wait is not None:
        try:
            return max(0.0, float(raw_wait))
        except (TypeError, ValueError):
            return 0.0
    raw_created_at = params.get("task_created_at", params.get("created_at"))
    if raw_created_at is None:
        return 0.0
    try:
        created_at = float(raw_created_at)
    except (TypeError, ValueError):
        return 0.0
    # Future queue tasks may pass seconds or milliseconds. Treat large values as milliseconds.
    now = time.time()
    if created_at > 10_000_000_000:
        return max(0.0, (now * 1000.0) - created_at)
    return max(0.0, (now - created_at) * 1000.0)


def _session_item_hint(item: SessionItem) -> dict[str, Any]:
    rect_tuple = getattr(item, "rect_tuple", None)
    return {
        "title": clean(getattr(item, "title", "")),
        "rect": clean(getattr(item, "rect", "")),
        "rect_tuple": list(rect_tuple) if rect_tuple else None,
        "automation_id": clean(getattr(item, "automation_id", "")),
        "class_name": clean(getattr(item, "class_name", "")),
        "control_type": clean(getattr(item, "control_type", "")),
        "raw_texts": [clean(text) for text in list(getattr(item, "raw_texts", []) or [])[:12] if clean(text)],
        "unread": bool(getattr(item, "unread", False)),
        "unread_score": float(getattr(item, "unread_score", 0.0) or 0.0),
        "selected": bool(getattr(item, "selected", False)),
        "captured_at": time.time(),
    }


def _session_item_from_hint(hint: dict[str, Any] | None, fallback_title: str = "") -> SessionItem | None:
    if not isinstance(hint, dict):
        return None
    title = clean(hint.get("title")) or clean(fallback_title)
    if not title:
        return None
    rect_tuple = _hint_rect_tuple(hint.get("rect_tuple"))
    return SessionItem(
        title=title,
        control=None,
        rect=clean(hint.get("rect")) or ("-" if rect_tuple is None else f"({rect_tuple[0]},{rect_tuple[1]},{rect_tuple[2]},{rect_tuple[3]})"),
        rect_tuple=rect_tuple,
        automation_id=clean(hint.get("automation_id")),
        class_name=clean(hint.get("class_name")),
        control_type=clean(hint.get("control_type")),
        raw_texts=[clean(text) for text in list(hint.get("raw_texts") or []) if clean(text)] or [title],
        unread=bool(hint.get("unread", False)),
        unread_score=float(hint.get("unread_score", 0.0) or 0.0),
        selected=bool(hint.get("selected", False)),
    )


def _hint_rect_tuple(value: Any) -> tuple[int, int, int, int] | None:
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        return None
    try:
        left, top, right, bottom = (int(part) for part in value)
    except (TypeError, ValueError):
        return None
    if right <= left or bottom <= top:
        return None
    return left, top, right, bottom


class QianniuSidecarAdapter:
    def __init__(self, store: EventSink) -> None:
        self._store = store
        self._connected = False
        self._account_id = DEFAULT_ACCOUNT_ID
        self._account_display_name = ""
        self._detector = QianniuDetector()
        self._sessions = QianniuSessionReader(self._detector.config)
        self._reader = QianniuReader(self._detector.config)
        self._sender = QianniuSender(self._detector.config)
        self._accounts = QianniuAccountReader(
            self._detector.config,
            detector=self._detector,
            on_switched=self._invalidate_account_scoped_caches,
            activate_before_scan=True,
        )
        self._observer_stop = threading.Event()
        self._observer_thread: threading.Thread | None = None
        self._observer_lock = threading.Lock()
        self._health_lock = threading.Lock()
        self._health_probe_lock = threading.Lock()
        self._health_probe_thread: threading.Thread | None = None
        self._health_probe_stop: threading.Event | None = None
        self._cached_health: dict[str, Any] = {
            "healthy": False,
            "reason": "health_not_checked",
            "probe_status": "not_checked",
            "checked_at": "",
        }
        self._last_scan_started_at = 0.0
        self._last_scan_finished_at = 0.0
        self._last_scan_error = ""
        self._last_account_scan_summary: dict[str, Any] = {}
        self._handled_unread_session_keys: set[str] = set()
        self._conversation_task_queue = QianniuConversationTaskQueue()
        self._conversation_worker = QianniuConversationWorker(self)
        self._active_context_lock = threading.Lock()
        self._active_conversation_context: ActiveConversationContext | None = None
        self._worker_auto_reply_context: WorkerAutoReplyContext | None = None

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        started_at = time.perf_counter()
        request_id = clean(payload.get("request_id"))
        command = clean(payload.get("command"))
        payload_account_id = clean(payload.get("account_id"))
        self._account_id = payload_account_id or self._account_id
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}
        handler_params = dict(params)
        if payload_account_id:
            handler_params["_payload_account_id"] = payload_account_id

        handlers = {
            "connect": self._connect,
            "disconnect": self._disconnect,
            "health_check": self._health_check,
            "list_accounts": self._list_accounts,
            "fetch_visible_conversations": self._fetch_visible_conversations,
            "fetch_visible_messages": self._fetch_visible_messages,
            "scan_unread_and_fetch": self._scan_unread_and_fetch,
            "prepare_reply_draft": self._prepare_reply_draft,
            "send_message": self._send_message,
        }
        handler = handlers.get(command)
        if handler is None:
            logger.warning("qianniu command unsupported request_id=%s command=%s", request_id, command)
            listen_flow_logger.warning(
                "listen_flow adapter_command_unsupported request_id=%s command=%s account_id=%s",
                request_id,
                command,
                self._account_id,
            )
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
            listen_flow_logger.info(
                "listen_flow adapter_command_start request_id=%s command=%s account_id=%s params=%s connected=%s",
                request_id,
                command,
                self._account_id,
                sorted(params.keys()),
                self._connected,
            )
            if command in {"connect", "disconnect", "health_check"}:
                result = handler(handler_params, request_id)
            else:
                with uia_guard(command or "qianniu"):
                    result = handler(handler_params, request_id)
            logger.info(
                "qianniu command done request_id=%s command=%s status=success elapsed_ms=%.1f keys=%s",
                request_id,
                command,
                _elapsed_ms(started_at),
                list(result.keys()) if isinstance(result, dict) else [],
            )
            listen_flow_logger.info(
                "listen_flow adapter_command_done request_id=%s command=%s elapsed_ms=%.1f connected=%s result_keys=%s",
                request_id,
                command,
                _elapsed_ms(started_at),
                self._connected,
                list(result.keys()) if isinstance(result, dict) else [],
            )
            return payload_status("success", request_id, result=result)
        except Exception as exc:
            if command in {"scan_unread_and_fetch", "fetch_visible_conversations", "fetch_visible_messages"}:
                self._remember_scan_error(str(exc))
            logger.exception(
                "qianniu command failed request_id=%s command=%s elapsed_ms=%.1f",
                request_id,
                command,
                _elapsed_ms(started_at),
            )
            listen_flow_logger.exception(
                "listen_flow adapter_command_failed request_id=%s command=%s elapsed_ms=%.1f connected=%s",
                request_id,
                command,
                _elapsed_ms(started_at),
                self._connected,
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
        result = self._refresh_lightweight_health("health_endpoint")
        listen_flow_logger.info(
            "listen_flow adapter_health_snapshot connected=%s account_id=%s healthy=%s probe_status=%s reason=%s",
            self._connected,
            self._account_id,
            bool(result.get("healthy")),
            clean(result.get("probe_status")),
            clean(result.get("reason")),
        )
        return {
            "status": "success",
            "platform": PLATFORM_QIANNIU,
            "account_id": self._account_id,
            "connected": self._connected,
            "health": result,
        }

    def status_snapshot(self) -> dict[str, Any]:
        observer_thread = self._observer_thread
        observer_running = bool(observer_thread is not None and observer_thread.is_alive())
        health = self._refresh_lightweight_health("status_snapshot")
        worker = self._conversation_worker_snapshot()
        listen_flow_logger.info(
            "listen_flow adapter_status_snapshot connected=%s observer_running=%s worker_running=%s worker_processing=%s queue_depth=%s account_id=%s account_display_name=%s healthy=%s probe_status=%s reason=%s",
            self._connected,
            observer_running,
            worker.get("worker_running"),
            worker.get("worker_processing"),
            worker.get("queue_queued_count", 0),
            self._account_id,
            self._account_display_name,
            bool(health.get("healthy")),
            clean(health.get("probe_status")),
            clean(health.get("reason")),
        )
        return {
            "connected": self._connected,
            "observer_running": observer_running,
            "conversation_worker_running": bool(worker.get("worker_running")),
            "conversation_worker_processing": bool(worker.get("worker_processing")),
            "conversation_worker_auto_reply_pending": bool(worker.get("worker_auto_reply_pending")),
            "conversation_queue_queued_count": int(worker.get("queue_queued_count", 0) or 0),
            "conversation_queue_processing_count": int(worker.get("queue_processing_count", 0) or 0),
            "account_id": self._account_id,
            "account_display_name": self._account_display_name,
            "health": health,
            "healthy": bool(health.get("healthy")),
            "reason": clean(health.get("reason")),
        }

    def _connect(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow adapter_connect_start request_id=%s account_id=%s emit_initial_snapshot=%s connected_before=%s",
            request_id,
            self._account_id,
            bool(params.get("emit_initial_snapshot", False)),
            self._connected,
        )
        self._stop_health_probe(request_id, reason="connect_reset", join_timeout=0.2)
        self._connected = True
        self._handled_unread_session_keys.clear()
        self._conversation_task_queue.clear()
        self._clear_worker_auto_reply_context("connect_reset")
        self._start_conversation_worker()
        self._start_observer()
        health = self._refresh_lightweight_health("connect")
        listen_flow_logger.info(
            "listen_flow health_probe_schedule_skipped trigger=connect request_id=%s reason=disabled_for_listening",
            request_id,
        )
        if params.get("emit_initial_snapshot", False):
            self._schedule_initial_snapshot(params, request_id)
        listen_flow_logger.info(
            "listen_flow adapter_connect_return request_id=%s connected=%s health_async=%s cached_healthy=%s cached_probe_status=%s elapsed_ms=%.1f",
            request_id,
            self._connected,
            False,
            bool(health.get("healthy")),
            clean(health.get("probe_status")),
            _elapsed_ms(started_at),
        )
        return {"connected": True, "health": health, "health_async": False}

    def _disconnect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        listen_flow_logger.info(
            "listen_flow adapter_disconnect_start request_id=%s connected_before=%s",
            _request_id,
            self._connected,
        )
        self._connected = False
        self._clear_active_session_context("disconnect")
        self._clear_worker_auto_reply_context("disconnect")
        self._stop_health_probe(_request_id, reason="disconnect")
        self._stop_observer()
        self._stop_conversation_worker()
        self._handled_unread_session_keys.clear()
        self._conversation_task_queue.clear()
        health = {
            "healthy": False,
            "reason": "adapter disconnected",
            "probe_status": "offline",
            "checked_at": _now_iso(),
        }
        self._set_cached_health(health)
        self._store.append(
            self._health_event(
                healthy=False,
                status="offline",
                message="adapter disconnected",
                metadata={"stage": "disconnect"},
            )
        )
        listen_flow_logger.info(
            "listen_flow adapter_disconnect_done request_id=%s connected=%s",
            _request_id,
            self._connected,
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
        listen_flow_logger.info(
            "listen_flow observer_started poll_interval_sec=%.1f connected=%s account_id=%s",
            OBSERVER_POLL_INTERVAL_SEC,
            self._connected,
            self._account_id,
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
        logger.info("qianniu observer stopped")
        listen_flow_logger.info(
            "listen_flow observer_stopped connected=%s account_id=%s",
            self._connected,
            self._account_id,
        )

    def _start_conversation_worker(self) -> None:
        self._conversation_worker.start()

    def _stop_conversation_worker(self) -> None:
        self._conversation_worker.stop()

    def _wake_conversation_worker(self) -> None:
        if self._conversation_worker.is_running():
            self._conversation_worker.wake()

    def _conversation_worker_snapshot(self) -> dict[str, Any]:
        worker = self._conversation_worker.snapshot()
        queue = self._conversation_task_queue.snapshot()
        auto_reply_context = self._worker_auto_reply_context_snapshot()
        return {
            "worker_running": bool(worker.get("running")),
            "worker_processing": bool(worker.get("processing")),
            "queue_queued_count": int(queue.get("queued_count", 0) or 0),
            "queue_processing_count": int(queue.get("processing_count", 0) or 0),
            "worker_auto_reply_pending": auto_reply_context is not None,
            "worker_auto_reply_task_id": clean(getattr(auto_reply_context, "task_id", "")) if auto_reply_context else "",
            "worker_auto_reply_dedupe_key": clean(getattr(auto_reply_context, "dedupe_key", "")) if auto_reply_context else "",
        }

    def _conversation_worker_should_pause_scanner(self) -> tuple[bool, dict[str, Any]]:
        snapshot = self._conversation_worker_snapshot()
        if not bool(snapshot.get("worker_running")):
            return False, snapshot
        if bool(snapshot.get("worker_processing")):
            return True, snapshot
        if bool(snapshot.get("worker_auto_reply_pending")):
            return True, snapshot
        if int(snapshot.get("queue_queued_count", 0) or 0) > 0:
            return True, snapshot
        if int(snapshot.get("queue_processing_count", 0) or 0) > 0:
            return True, snapshot
        return False, snapshot

    def _stop_health_probe(self, request_id: str = "", *, reason: str, join_timeout: float = HEALTH_PROBE_STOP_JOIN_SEC) -> None:
        with self._health_probe_lock:
            thread = self._health_probe_thread
            stop_event = self._health_probe_stop
            if stop_event is not None:
                stop_event.set()
        if not thread:
            return

        alive_before = thread.is_alive()
        listen_flow_logger.info(
            "listen_flow health_probe_stop_start request_id=%s reason=%s alive_before=%s join_timeout_sec=%.1f",
            request_id,
            reason,
            alive_before,
            max(0.0, float(join_timeout)),
        )
        if alive_before:
            thread.join(timeout=max(0.0, float(join_timeout)))
        alive_after = thread.is_alive()
        with self._health_probe_lock:
            if self._health_probe_thread is thread and not alive_after:
                self._health_probe_thread = None
                self._health_probe_stop = None
        listen_flow_logger.info(
            "listen_flow health_probe_stop_done request_id=%s reason=%s alive_after=%s",
            request_id,
            reason,
            alive_after,
        )

    def _schedule_async_health_probe(self, trigger: str, request_id: str = "", *, delay_sec: float = 0.0) -> None:
        if not ASYNC_HEALTH_PROBE_ENABLED:
            listen_flow_logger.info(
                "listen_flow health_probe_schedule_skipped trigger=%s request_id=%s reason=disabled_for_listening",
                trigger,
                request_id,
            )
            return
        with self._health_probe_lock:
            if self._health_probe_thread and self._health_probe_thread.is_alive():
                logger.info(
                    "qianniu async health probe skip trigger=%s request_id=%s reason=already_running",
                    trigger,
                    request_id,
                )
                listen_flow_logger.info(
                    "listen_flow health_probe_schedule_skipped trigger=%s request_id=%s reason=already_running",
                    trigger,
                    request_id,
                )
                return
            stop_event = threading.Event()
            self._health_probe_stop = stop_event
            self._health_probe_thread = threading.Thread(
                target=self._run_async_health_probe,
                args=(trigger, request_id, max(0.0, float(delay_sec)), stop_event),
                name="qianniu-health-probe",
                daemon=True,
            )
            self._health_probe_thread.start()
        listen_flow_logger.info(
            "listen_flow health_probe_scheduled trigger=%s request_id=%s connected=%s account_id=%s delay_sec=%.1f",
            trigger,
            request_id,
            self._connected,
            self._account_id,
            max(0.0, float(delay_sec)),
        )

    def _run_async_health_probe(
        self,
        trigger: str,
        request_id: str,
        delay_sec: float = 0.0,
        stop_event: threading.Event | None = None,
    ) -> None:
        started_at = time.perf_counter()
        current_thread = threading.current_thread()
        stop_event = stop_event or threading.Event()
        try:
            if delay_sec > 0:
                listen_flow_logger.info(
                    "listen_flow health_probe_delay_start trigger=%s request_id=%s delay_sec=%.1f connected=%s",
                    trigger,
                    request_id,
                    delay_sec,
                    self._connected,
                )
                delay_deadline = time.perf_counter() + delay_sec
                while self._connected and not stop_event.is_set() and time.perf_counter() < delay_deadline:
                    stop_event.wait(min(0.2, max(0.0, delay_deadline - time.perf_counter())))
                if stop_event.is_set() or not self._connected:
                    listen_flow_logger.info(
                        "listen_flow health_probe_skipped trigger=%s request_id=%s reason=%s elapsed_ms=%.1f",
                        trigger,
                        request_id,
                        "cancelled_during_delay" if stop_event.is_set() else "disconnected_during_delay",
                        _elapsed_ms(started_at),
                    )
                    return
            if stop_event.is_set() or not self._connected:
                listen_flow_logger.info(
                    "listen_flow health_probe_skipped trigger=%s request_id=%s reason=%s elapsed_ms=%.1f",
                    trigger,
                    request_id,
                    "cancelled_before_start" if stop_event.is_set() else "disconnected_before_start",
                    _elapsed_ms(started_at),
                )
                return

            logger.info(
                "qianniu async health probe start trigger=%s request_id=%s",
                trigger,
                request_id,
            )
            listen_flow_logger.info(
                "listen_flow health_probe_start trigger=%s request_id=%s connected=%s account_id=%s",
                trigger,
                request_id,
                self._connected,
                self._account_id,
            )
            try:
                with uia_guard("qianniu_health_probe", timeout_sec=HEALTH_PROBE_UIA_LOCK_TIMEOUT_SEC):
                    if stop_event.is_set() or not self._connected:
                        listen_flow_logger.info(
                            "listen_flow health_probe_skipped trigger=%s request_id=%s reason=%s elapsed_ms=%.1f",
                            trigger,
                            request_id,
                            "cancelled_after_lock" if stop_event.is_set() else "disconnected_after_lock",
                            _elapsed_ms(started_at),
                        )
                        return
                    health = self._probe(timeout_sec=HEALTH_PROBE_TIMEOUT_SEC, reason=trigger)
            except TimeoutError:
                listen_flow_logger.info(
                    "listen_flow health_probe_skipped trigger=%s request_id=%s reason=uia_lock_timeout lock_timeout_sec=%.1f elapsed_ms=%.1f",
                    trigger,
                    request_id,
                    HEALTH_PROBE_UIA_LOCK_TIMEOUT_SEC,
                    _elapsed_ms(started_at),
                )
                return

            if stop_event.is_set() or not self._connected:
                listen_flow_logger.info(
                    "listen_flow health_probe_result_discarded trigger=%s request_id=%s reason=%s elapsed_ms=%.1f",
                    trigger,
                    request_id,
                    "cancelled_after_probe" if stop_event.is_set() else "disconnected_after_probe",
                    _elapsed_ms(started_at),
                )
                return

            self._set_cached_health(health)
            self._store.append(
                self._health_event(
                    healthy=bool(health.get("healthy")),
                    status="online" if health.get("healthy") else "degraded",
                    message=clean(health.get("reason")),
                    metadata=health,
                )
            )
            logger.info(
                "qianniu async health probe done trigger=%s request_id=%s healthy=%s status=%s elapsed_ms=%.1f",
                trigger,
                request_id,
                bool(health.get("healthy")),
                clean(health.get("probe_status")),
                _elapsed_ms(started_at),
            )
            listen_flow_logger.info(
                "listen_flow health_probe_done trigger=%s request_id=%s healthy=%s probe_status=%s timeout_stage=%s reason=%s elapsed_ms=%.1f",
                trigger,
                request_id,
                bool(health.get("healthy")),
                clean(health.get("probe_status")),
                clean(health.get("probe_timeout_stage")),
                clean(health.get("reason")),
                _elapsed_ms(started_at),
            )
            return
        except Exception as exc:
            if stop_event.is_set() or not self._connected:
                listen_flow_logger.info(
                    "listen_flow health_probe_exception_discarded trigger=%s request_id=%s reason=%s error=%s elapsed_ms=%.1f",
                    trigger,
                    request_id,
                    "cancelled" if stop_event.is_set() else "disconnected",
                    str(exc),
                    _elapsed_ms(started_at),
                )
                return
            logger.exception(
                "qianniu async health probe failed trigger=%s request_id=%s elapsed_ms=%.1f",
                trigger,
                request_id,
                _elapsed_ms(started_at),
            )
            listen_flow_logger.exception(
                "listen_flow health_probe_failed trigger=%s request_id=%s elapsed_ms=%.1f",
                trigger,
                request_id,
                _elapsed_ms(started_at),
            )
            health = {
                "healthy": False,
                "reason": str(exc),
                "probe_status": "error",
                "probe_error": str(exc),
                "checked_at": _now_iso(),
            }
            self._set_cached_health(health)
            self._store.append(
                self._health_event(
                    healthy=False,
                    status="degraded",
                    message=str(exc),
                    metadata=health,
                )
            )
        finally:
            with self._health_probe_lock:
                if self._health_probe_thread is current_thread:
                    self._health_probe_thread = None
                    self._health_probe_stop = None

    def _schedule_initial_snapshot(self, params: dict[str, Any], request_id: str) -> None:
        snapshot_params = dict(params)

        def run_snapshot() -> None:
            try:
                with uia_guard("qianniu_initial_snapshot"):
                    self._fetch_visible_conversations(
                        {"limit": snapshot_params.get("limit", 30), "detect_unread": True},
                        request_id,
                    )
                    self._fetch_visible_messages(
                        {"limit": snapshot_params.get("message_limit", 30)},
                        request_id,
                    )
                logger.info("qianniu initial snapshot done request_id=%s", request_id)
            except Exception:
                logger.exception("qianniu initial snapshot failed request_id=%s", request_id)

        threading.Thread(
            target=run_snapshot,
            name="qianniu-initial-snapshot",
            daemon=True,
        ).start()

    def _observer_loop(self) -> None:
        while not self._observer_stop.is_set():
            if not self._connected:
                self._observer_stop.wait(OBSERVER_POLL_INTERVAL_SEC)
                continue
            try:
                tick_started_at = time.perf_counter()
                request_id = f"observer-{int(time.time() * 1000)}"
                listen_flow_logger.info(
                    "listen_flow observer_tick_start request_id=%s connected=%s account_id=%s account_display_name=%s",
                    request_id,
                    self._connected,
                    self._account_id,
                    self._account_display_name,
                )
                pause_scan, worker_snapshot = self._conversation_worker_should_pause_scanner()
                if pause_scan:
                    self._wake_conversation_worker()
                    elapsed_ms = _elapsed_ms(tick_started_at)
                    logger.info(
                        "qianniu observer timing request_id=%s total_ms=%.1f uia_guard_wait_ms=0.0 scan_ms=0.0 had_work=True unread=0 conversations=0 messages=0 processed=0 queued=%s skipped=worker_busy",
                        request_id,
                        elapsed_ms,
                        worker_snapshot.get("queue_queued_count", 0),
                    )
                    listen_flow_logger.info(
                        "listen_flow observer_tick_skipped request_id=%s reason=conversation_worker_busy total_ms=%.1f worker_processing=%s worker_auto_reply_pending=%s queue_depth=%s queue_processing=%s",
                        request_id,
                        elapsed_ms,
                        worker_snapshot.get("worker_processing"),
                        worker_snapshot.get("worker_auto_reply_pending"),
                        worker_snapshot.get("queue_queued_count", 0),
                        worker_snapshot.get("queue_processing_count", 0),
                    )
                    self._observer_stop.wait(OBSERVER_AFTER_WORK_SLEEP_SEC)
                    continue
                uia_guard_started_at = time.perf_counter()
                try:
                    with uia_guard("qianniu_observer", timeout_sec=OBSERVER_UIA_LOCK_TIMEOUT_SEC):
                        uia_guard_wait_ms = _elapsed_ms(uia_guard_started_at)
                        listen_flow_logger.info(
                            "listen_flow observer_uia_guard_acquired request_id=%s wait_ms=%.1f account_id=%s",
                            request_id,
                            uia_guard_wait_ms,
                            self._account_id,
                        )
                        scan_started_at = time.perf_counter()
                        result = self._scan_unread_and_fetch(
                            {"message_limit": OBSERVER_MESSAGE_LIMIT},
                            request_id,
                        )
                        scan_ms = _elapsed_ms(scan_started_at)
                except TimeoutError:
                    wait_ms = _elapsed_ms(uia_guard_started_at)
                    detail = f"observer UIA lock timeout after {wait_ms:.1f}ms"
                    self._remember_scan_error(detail)
                    logger.warning(
                        "qianniu observer uia guard timeout request_id=%s wait_ms=%.1f timeout_sec=%.1f",
                        request_id,
                        wait_ms,
                        OBSERVER_UIA_LOCK_TIMEOUT_SEC,
                    )
                    listen_flow_logger.warning(
                        "listen_flow observer_uia_guard_timeout request_id=%s wait_ms=%.1f timeout_sec=%.1f account_id=%s total_ms=%.1f",
                        request_id,
                        wait_ms,
                        OBSERVER_UIA_LOCK_TIMEOUT_SEC,
                        self._account_id,
                        _elapsed_ms(tick_started_at),
                    )
                    self._observer_stop.wait(OBSERVER_ERROR_SLEEP_SEC)
                    continue
                had_work = bool(
                    result.get("message_count")
                    or result.get("conversation_count")
                    or result.get("processed_count")
                    or result.get("queued_count")
                    or result.get("queue_queued_count")
                )
                logger.info(
                    "qianniu observer timing request_id=%s total_ms=%.1f uia_guard_wait_ms=%.1f scan_ms=%.1f had_work=%s unread=%s queued=%s queue_depth=%s conversations=%s messages=%s processed=%s",
                    request_id,
                    _elapsed_ms(tick_started_at),
                    uia_guard_wait_ms,
                    scan_ms,
                    had_work,
                    result.get("unread_count"),
                    result.get("queued_count"),
                    result.get("queue_queued_count"),
                    result.get("conversation_count"),
                    result.get("message_count"),
                    result.get("processed_count"),
                )
                listen_flow_logger.info(
                    "listen_flow observer_tick_done request_id=%s total_ms=%.1f uia_guard_wait_ms=%.1f scan_ms=%.1f had_work=%s unread=%s queued=%s queue_depth=%s conversations=%s messages=%s processed=%s account_stage=%s account_display_name=%s",
                    request_id,
                    _elapsed_ms(tick_started_at),
                    uia_guard_wait_ms,
                    scan_ms,
                    had_work,
                    result.get("unread_count"),
                    result.get("queued_count"),
                    result.get("queue_queued_count"),
                    result.get("conversation_count"),
                    result.get("message_count"),
                    result.get("processed_count"),
                    clean(result.get("account_stage")),
                    clean(result.get("account_display_name")),
                )
                self._observer_stop.wait(OBSERVER_AFTER_WORK_SLEEP_SEC if had_work else OBSERVER_POLL_INTERVAL_SEC)
            except Exception as exc:
                logger.exception("qianniu observer tick failed: %s", exc)
                listen_flow_logger.exception(
                    "listen_flow observer_tick_failed request_id=%s account_id=%s elapsed_ms=%.1f",
                    locals().get("request_id", ""),
                    self._account_id,
                    _elapsed_ms(locals().get("tick_started_at", time.perf_counter())),
                )
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
        health = self._refresh_lightweight_health("health_check")
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata=health,
            )
        )
        listen_flow_logger.info(
            "listen_flow health_check_lightweight request_id=%s connected=%s healthy=%s probe_status=%s reason=%s",
            _request_id,
            self._connected,
            bool(health.get("healthy")),
            clean(health.get("probe_status")),
            clean(health.get("reason")),
        )
        return health

    def _list_accounts(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        fresh = bool(params.get("fresh", True))
        accounts = self._list_accounts_safely(fresh=fresh)
        active = self._selected_account_from(accounts)
        self._remember_active_account(active)
        return {
            "account_count": len(accounts),
            "accounts": [self._account_tab_payload(item) for item in accounts],
            "active_account_id": clean(getattr(active, "account_id", "")),
            "active_display_name": clean(getattr(active, "display_name", "")),
        }

    def _fetch_visible_conversations(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 60) or 60), 100))
        detect_unread = bool(params.get("detect_unread", True))
        account_result = self._ensure_requested_account_selected(params, fresh=False)
        if not account_result.ok:
            raise RuntimeError(account_result.detail or account_result.stage)
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
        result = {"count": len(conversations), "conversations": conversations}
        result.update(self._account_result_payload(account_result))
        return result

    def _fetch_visible_messages(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 30) or 30), 100))
        conversation_key = clean(params.get("conversation_key"))
        account_result = self._ensure_requested_account_selected(params, conversation_key=conversation_key, fresh=False)
        if not account_result.ok:
            raise RuntimeError(account_result.detail or account_result.stage)
        requested_name = _display_name_from_key(conversation_key)
        display_name = _requested_display_name(params.get("display_name"), conversation_key) or requested_name or "current"
        ensure_result = self._ensure_target_session_selected(display_name, conversation_key) if display_name != "current" else EnsureSessionResult(ok=True, stage="skipped", method="no_explicit_target")
        if not ensure_result.ok:
            raise RuntimeError(ensure_result.detail or ensure_result.stage or "target_session_not_verified")
        read_started_at = time.perf_counter()
        chat_root = self._current_session_chat_root()
        try:
            result, messages = self._reader.read_visible_messages_debug(limit=limit, chat_root=chat_root)
        except TypeError:
            result, messages = self._reader.read_visible_messages_debug(limit=limit)
        read_ms = _elapsed_ms(read_started_at)
        emit_started_at = time.perf_counter()
        raw_message_events = [
            self._message_event(display_name, item, sequence_index=sequence_index)
            for sequence_index, item in enumerate(messages)
        ]
        filtered_message_events = self._filter_unread_message_events(raw_message_events)
        emitted: list[dict[str, Any]] = []
        for event in filtered_message_events:
            self._store.append(event)
            emitted.append(event)
        emit_ms = _elapsed_ms(emit_started_at)
        logger.info(
            "qianniu fetch_visible_messages timing request_id=%s total_ms=%.1f read_ms=%.1f emit_ms=%.1f display_name=%s read_source=%s read_ok=%s messages=%s emitted=%s filtered=%s",
            _request_id,
            _elapsed_ms(started_at),
            read_ms,
            emit_ms,
            display_name,
            result.source,
            result.ok,
            len(messages),
            len(emitted),
            len(raw_message_events) - len(filtered_message_events),
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
            "target_session_method": ensure_result.method,
            "target_session_stage": ensure_result.stage,
            "target_session_selected": ensure_result.selected_title,
            **self._account_result_payload(account_result),
        }

    def _scan_unread_and_fetch(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        total_started_at = time.perf_counter()
        self._last_scan_started_at = time.time()
        limit = max(1, min(int(params.get("message_limit", params.get("limit", 30)) or 30), 100))
        listen_flow_logger.info(
            "listen_flow scan_unread_start request_id=%s account_id=%s account_display_name=%s message_limit=%s",
            _request_id,
            self._account_id,
            self._account_display_name,
            limit,
        )

        build_targets_started_at = time.perf_counter()
        targets, top_unread_count, account_count, real_account_count = self._build_account_scan_targets_for_unread(_request_id)
        logger.info(
            "qianniu scan_unread_timing request_id=%s stage=build_account_targets ms=%.1f account_scan_count=%s account_count=%s real_account_count=%s top_unread_account_count=%s",
            _request_id,
            _elapsed_ms(build_targets_started_at),
            len(targets),
            account_count,
            real_account_count,
            top_unread_count,
        )
        aggregate: dict[str, Any] = {
            "unread_count": 0,
            "conversation_count": 0,
            "message_count": 0,
            "processed_count": 0,
            "skipped_count": 0,
            "queued_count": 0,
            "processed": [],
            "account_scan_count": len(targets),
            "account_count": account_count,
            "real_account_count": real_account_count,
            "top_unread_account_count": top_unread_count,
            "account_scan_plan": [
                {
                    "account_id": target.account_id,
                    "display_name": target.display_name,
                    "selected": target.selected,
                    "has_top_unread_hint": target.has_top_unread_hint,
                    "unread_score": round(float(target.unread_score), 4),
                    "source": target.source,
                }
                for target in targets
            ],
        }
        primary_account_result: EnsureAccountResult | None = None

        for index, target in enumerate(targets):
            item_started_at = time.perf_counter()
            listen_flow_logger.info(
                "listen_flow account_scan_item_start request_id=%s item_index=%s account_id=%s account_display_name=%s selected=%s has_top_unread_hint=%s unread_score=%.4f",
                _request_id,
                index,
                target.account_id,
                target.display_name,
                target.selected,
                target.has_top_unread_hint,
                float(target.unread_score),
            )
            account_started_at = time.perf_counter()
            account_result = self._select_account_scan_target(
                target,
                request_id=_request_id,
                top_unread_count=top_unread_count,
            )
            account_select_ms = _elapsed_ms(account_started_at)
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=account_select item_index=%s ms=%.1f ok=%s account_id=%s account_display_name=%s switched=%s method=%s",
                _request_id,
                index,
                account_select_ms,
                account_result.ok,
                account_result.account_id,
                account_result.display_name,
                account_result.switched,
                account_result.method,
            )
            listen_flow_logger.info(
                "listen_flow scan_account_select_done request_id=%s item_index=%s ok=%s stage=%s method=%s account_id=%s account_display_name=%s switched=%s unread_account_count=%s elapsed_ms=%.1f",
                _request_id,
                index,
                account_result.ok,
                account_result.stage,
                account_result.method,
                account_result.account_id,
                account_result.display_name,
                account_result.switched,
                account_result.unread_account_count,
                account_select_ms,
            )
            if primary_account_result is None:
                primary_account_result = account_result
            if not account_result.ok:
                logger.info(
                    "qianniu scan_unread_timing request_id=%s stage=account_select ok=False detail=%s ms=%.1f",
                    _request_id,
                    account_result.detail,
                    _elapsed_ms(item_started_at),
                )
                aggregate["processed_count"] += 1
                aggregate["processed"].append(
                    {
                        "account_id": account_result.account_id,
                        "account_display_name": account_result.display_name,
                        "error": account_result.detail or account_result.stage,
                    }
                )
                listen_flow_logger.info(
                    "listen_flow account_scan_item_done request_id=%s item_index=%s ok=False account_id=%s account_display_name=%s elapsed_ms=%.1f",
                    _request_id,
                    index,
                    account_result.account_id,
                    account_result.display_name,
                    _elapsed_ms(item_started_at),
                )
                continue

            item_result = self._scan_current_account_unread_sessions(
                params,
                _request_id,
                limit=limit,
                account_result=account_result,
                scan_started_at=item_started_at,
                task_source="top_account_unread" if target.has_top_unread_hint else "current_session_unread",
            )
            for key in ("unread_count", "conversation_count", "message_count", "processed_count", "skipped_count"):
                aggregate[key] += int(item_result.get(key, 0) or 0)
            aggregate["queued_count"] += int(item_result.get("queued_count", 0) or 0)
            aggregate["processed"].extend(list(item_result.get("processed", []) or []))
            if int(item_result.get("processed_count", 0) or 0) > 0:
                primary_account_result = account_result
            item_elapsed_ms = _elapsed_ms(item_started_at)
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=account_scan_item_total item_index=%s ms=%.1f account_id=%s account_display_name=%s unread_count=%s queued=%s conversations=%s messages=%s processed=%s",
                _request_id,
                index,
                item_elapsed_ms,
                account_result.account_id,
                account_result.display_name,
                item_result.get("unread_count", 0),
                item_result.get("queued_count", 0),
                item_result.get("conversation_count", 0),
                item_result.get("message_count", 0),
                item_result.get("processed_count", 0),
            )
            listen_flow_logger.info(
                "listen_flow account_scan_item_done request_id=%s item_index=%s ok=True account_id=%s account_display_name=%s unread=%s conversations=%s messages=%s processed=%s skipped=%s elapsed_ms=%.1f",
                _request_id,
                index,
                account_result.account_id,
                account_result.display_name,
                item_result.get("unread_count", 0),
                item_result.get("conversation_count", 0),
                item_result.get("message_count", 0),
                item_result.get("processed_count", 0),
                item_result.get("skipped_count", 0),
                item_elapsed_ms,
            )

        worker_snapshot = self._conversation_worker_snapshot()
        if bool(worker_snapshot.get("worker_running")):
            self._wake_conversation_worker()
            listen_flow_logger.info(
                "listen_flow queue_task_consumer_deferred request_id=%s consumer=conversation_worker queue_depth=%s processing=%s",
                _request_id,
                worker_snapshot.get("queue_queued_count", 0),
                worker_snapshot.get("queue_processing_count", 0),
            )
        else:
            queue_process_result = self._drain_one_conversation_task(params, _request_id, limit=limit)
            for key in ("unread_count", "conversation_count", "message_count", "processed_count", "skipped_count"):
                aggregate[key] += int(queue_process_result.get(key, 0) or 0)
            aggregate["processed"].extend(list(queue_process_result.get("processed", []) or []))
            if queue_process_result.get("account_id"):
                primary_account_result = EnsureAccountResult(
                    ok=True,
                    stage=clean(queue_process_result.get("account_stage")) or "conversation_task_processed",
                    method=clean(queue_process_result.get("account_method")),
                    account_id=clean(queue_process_result.get("account_id")),
                    display_name=clean(queue_process_result.get("account_display_name")),
                    switched=bool(queue_process_result.get("account_switched")),
                    unread_account_count=top_unread_count,
                )

        if primary_account_result is None:
            primary_account_result = EnsureAccountResult(
                ok=True,
                stage="account_scan_plan_empty",
                method="none",
                account_id=self._account_id,
                display_name=self._account_display_name,
            )
        aggregate.update(self._account_result_payload(primary_account_result))
        aggregate.update(
            {
                f"queue_{key}": value
                for key, value in self._conversation_task_queue.snapshot().items()
            }
        )
        logger.info(
            "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f account_scan_count=%s unread_count=%s queued=%s processed=%s messages=%s queue_depth=%s",
            _request_id,
            _elapsed_ms(total_started_at),
            len(targets),
            aggregate["unread_count"],
            aggregate["queued_count"],
            aggregate["processed_count"],
            aggregate["message_count"],
            aggregate.get("queue_queued_count", 0),
        )
        listen_flow_logger.info(
            "listen_flow scan_unread_done request_id=%s total_ms=%.1f account_scan_count=%s unread=%s queued=%s queue_depth=%s conversations=%s messages=%s processed=%s skipped=%s primary_account_stage=%s primary_account_display_name=%s",
            _request_id,
            _elapsed_ms(total_started_at),
            len(targets),
            aggregate["unread_count"],
            aggregate["queued_count"],
            aggregate.get("queue_queued_count", 0),
            aggregate["conversation_count"],
            aggregate["message_count"],
            aggregate["processed_count"],
            aggregate["skipped_count"],
            primary_account_result.stage,
            primary_account_result.display_name,
        )
        self._remember_scan_success(aggregate, primary_account_result)
        return aggregate

    def _scan_current_account_unread_sessions(
        self,
        params: dict[str, Any],
        _request_id: str,
        *,
        limit: int,
        account_result: EnsureAccountResult,
        scan_started_at: float,
        task_source: str = "current_session_unread",
    ) -> dict[str, Any]:
        sessions_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_scan_start request_id=%s account_id=%s account_display_name=%s detect_unread=true limit=100",
            _request_id,
            self._account_id,
            self._account_display_name,
        )
        sessions = self._sessions.read_visible_sessions(limit=100, detect_unread=True)
        sessions_ms = _elapsed_ms(sessions_started_at)
        unread_items = [item for item in sessions if item.unread]
        unread_keys = {_account_scoped_session_unread_key(self._account_id, item) for item in unread_items}
        self._prune_handled_unread_session_keys(self._account_id, unread_keys)
        listen_flow_logger.info(
            "listen_flow session_scan_done request_id=%s account_id=%s account_display_name=%s elapsed_ms=%.1f session_count=%s unread_count=%s unread_titles=%s",
            _request_id,
            self._account_id,
            self._account_display_name,
            sessions_ms,
            len(sessions),
            len(unread_items),
            [clean(item.title) for item in unread_items[:5]],
        )
        logger.info(
            "qianniu scan_unread_timing request_id=%s stage=read_visible_sessions ms=%.1f session_count=%s unread_count=%s limit=%s",
            _request_id,
            sessions_ms,
            len(sessions),
            len(unread_items),
            limit,
        )
        if not unread_items:
            listen_flow_logger.info(
                "listen_flow session_unread_decision request_id=%s decision=%s account_id=%s account_display_name=%s session_count=%s unread_count=0 elapsed_ms=%.1f",
                _request_id,
                "no_sessions_found" if not sessions else "no_unread_session",
                self._account_id,
                self._account_display_name,
                len(sessions),
                _elapsed_ms(scan_started_at),
            )
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f session_count=%s unread_count=0 processed=0 messages=0",
                _request_id,
                _elapsed_ms(scan_started_at),
                len(sessions),
            )
            result = {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "processed": [],
            }
            result.update(self._account_result_payload(account_result))
            return result

        target = next(
            (item for item in unread_items if _account_scoped_session_unread_key(self._account_id, item) not in self._handled_unread_session_keys),
            None,
        )
        if target is None:
            listen_flow_logger.info(
                "listen_flow session_unread_decision request_id=%s decision=all_unread_sessions_already_handled account_id=%s account_display_name=%s session_count=%s unread_count=%s skipped_count=%s elapsed_ms=%.1f",
                _request_id,
                self._account_id,
                self._account_display_name,
                len(sessions),
                len(unread_items),
                len(unread_items),
                _elapsed_ms(scan_started_at),
            )
            logger.info(
                "qianniu scan_unread_timing request_id=%s stage=total ms=%.1f session_count=%s unread_count=%s skipped_handled=%s processed=0 messages=0",
                _request_id,
                _elapsed_ms(scan_started_at),
                len(sessions),
                len(unread_items),
                len(unread_items),
            )
            result = {
                "unread_count": len(unread_items),
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "skipped_count": len(unread_items),
                "processed": [],
            }
            result.update(self._account_result_payload(account_result))
            return result

        task_account_id = clean(self._account_id) or clean(account_result.account_id) or DEFAULT_ACCOUNT_ID
        task_session_title = clean(target.title)
        task_dedupe_key = _queue_dedupe_key(task_account_id, task_session_title)
        task_id = _queue_task_id(_request_id, task_account_id, task_session_title, task_source)
        task = QianniuConversationTask(
            task_id=task_id,
            dedupe_key=task_dedupe_key,
            account_id=task_account_id,
            account_display_name=self._account_display_name,
            session_title=task_session_title,
            conversation_key=_conversation_key(task_account_id, task_session_title),
            source=task_source,
            created_at=time.time(),
            message_limit=limit,
            last_unread_hint={
                "unread_score": float(getattr(target, "unread_score", 0.0) or 0.0),
                "selected_before": bool(getattr(target, "selected", False)),
                "rect": clean(getattr(target, "rect", "")),
                "automation_id": clean(getattr(target, "automation_id", "")),
            },
            session_hint=_session_item_hint(target),
        )
        queued, enqueue_reason = self._conversation_task_queue.enqueue(task)
        queue_snapshot = self._conversation_task_queue.snapshot()
        listen_flow_logger.info(
            "listen_flow queue_task_enqueue request_id=%s task_id=%s dedupe_key=%s queued=%s reason=%s queue_depth=%s processing=%s account_id=%s account_display_name=%s session_title=%s source=%s unread_score=%.5f",
            _request_id,
            task_id,
            task_dedupe_key,
            queued,
            enqueue_reason,
            queue_snapshot.get("queued_count", 0),
            queue_snapshot.get("processing_count", 0),
            task_account_id,
            self._account_display_name,
            task_session_title,
            task_source,
            float(getattr(target, "unread_score", 0.0) or 0.0),
        )
        logger.info(
            "qianniu scan_unread_timing request_id=%s task_id=%s dedupe_key=%s stage=enqueue ms=%.1f session_count=%s unread_count=%s queued=%s queue_depth=%s reason=%s",
            _request_id,
            task_id,
            task_dedupe_key,
            _elapsed_ms(scan_started_at),
            len(sessions),
            len(unread_items),
            queued,
            queue_snapshot.get("queued_count", 0),
            enqueue_reason,
        )
        result = {
            "unread_count": len(unread_items),
            "conversation_count": 0,
            "message_count": 0,
            "processed_count": 0,
            "skipped_count": 0 if queued else 1,
            "queued_count": 1 if queued else 0,
            "processed": [],
        }
        result.update(self._account_result_payload(account_result))
        return result

    def _drain_one_conversation_task(
        self,
        params: dict[str, Any],
        request_id: str,
        *,
        limit: int,
    ) -> dict[str, Any]:
        task = self._conversation_task_queue.dequeue()
        if task is None:
            queue_snapshot = self._conversation_task_queue.snapshot()
            listen_flow_logger.info(
                "listen_flow queue_task_drain_idle request_id=%s queue_depth=%s processing=%s",
                request_id,
                queue_snapshot.get("queued_count", 0),
                queue_snapshot.get("processing_count", 0),
            )
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "skipped_count": 0,
                "processed": [],
            }

        queue_wait_ms = max(0.0, (time.time() - task.created_at) * 1000.0)
        queue_snapshot = self._conversation_task_queue.snapshot()
        listen_flow_logger.info(
            "listen_flow queue_task_dequeue request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f queue_depth=%s processing=%s account_id=%s account_display_name=%s session_title=%s source=%s attempt=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            queue_snapshot.get("queued_count", 0),
            queue_snapshot.get("processing_count", 0),
            task.account_id,
            task.account_display_name,
            task.session_title,
            task.source,
            task.attempt,
        )
        try:
            effective_limit = max(1, min(int(task.message_limit or limit or OBSERVER_MESSAGE_LIMIT), 100))
            result = self._process_conversation_task(
                task,
                params,
                request_id,
                limit=effective_limit,
                queue_wait_ms=queue_wait_ms,
            )
        except Exception as exc:
            self._conversation_task_queue.fail(task)
            logger.exception(
                "qianniu queued conversation task raised request_id=%s task_id=%s dedupe_key=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
            )
            listen_flow_logger.exception(
                "listen_flow queue_task_processed request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=0.0 status=failed stage=exception detail=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                str(exc),
            )
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 1,
                "skipped_count": 0,
                "processed": [
                    {
                        "account_id": task.account_id,
                        "account_display_name": task.account_display_name,
                        "display_name": task.session_title,
                        "task_id": task.task_id,
                        "dedupe_key": task.dedupe_key,
                        "queue_wait_ms": queue_wait_ms,
                        "process_ms": 0.0,
                        "error": str(exc),
                    }
                ],
                "account_id": task.account_id,
                "account_display_name": task.account_display_name,
                "account_stage": "conversation_task_exception",
                "account_method": "",
            }

        if clean(result.get("task_status")) == "done":
            self._conversation_task_queue.complete(task)
        else:
            self._conversation_task_queue.fail(task)
        return result

    def _process_conversation_task(
        self,
        task: QianniuConversationTask,
        params: dict[str, Any],
        request_id: str,
        *,
        limit: int,
        queue_wait_ms: float,
    ) -> dict[str, Any]:
        process_started_at = time.perf_counter()
        account_params: dict[str, Any] = {}
        if clean(task.account_id) and clean(task.account_id) not in {DEFAULT_ACCOUNT_ID, PLATFORM_QIANNIU}:
            account_params["target_account_id"] = task.account_id
        elif clean(task.account_display_name):
            account_params["target_account_name"] = task.account_display_name

        if clean(task.account_id) and clean(task.account_id) == clean(self._account_id):
            account_result = EnsureAccountResult(
                ok=True,
                stage="queue_task_account_already_selected",
                method="memory_account_context",
                account_id=self._account_id,
                display_name=self._account_display_name,
            )
        else:
            account_result = self._ensure_requested_account_selected(
                account_params,
                conversation_key=task.conversation_key,
                fresh=False,
            )
        listen_flow_logger.info(
            "listen_flow queue_task_account_select_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f ok=%s stage=%s method=%s account_id=%s account_display_name=%s switched=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            account_result.ok,
            account_result.stage,
            account_result.method,
            account_result.account_id,
            account_result.display_name,
            account_result.switched,
        )
        if not account_result.ok:
            process_ms = _elapsed_ms(process_started_at)
            listen_flow_logger.info(
                "listen_flow queue_task_processed request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f status=failed stage=account_select account_id=%s account_display_name=%s session_title=%s source=%s detail=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                process_ms,
                task.account_id,
                task.account_display_name,
                task.session_title,
                task.source,
                account_result.detail or account_result.stage,
            )
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 1,
                "skipped_count": 0,
                "processed": [
                    {
                        "account_id": task.account_id,
                        "account_display_name": task.account_display_name,
                        "display_name": task.session_title,
                        "task_id": task.task_id,
                        "dedupe_key": task.dedupe_key,
                        "queue_wait_ms": queue_wait_ms,
                        "process_ms": process_ms,
                        "error": account_result.detail or account_result.stage,
                    }
                ],
                "task_status": "failed",
                **self._account_result_payload(account_result),
            }

        find_started_at = time.perf_counter()
        target, hint_reason, hint_age_ms = self._target_session_from_task_hint(task)
        find_source = "task_hint" if target is not None else "fresh_scan"
        if target is not None:
            find_ms = _elapsed_ms(find_started_at)
            listen_flow_logger.info(
                "listen_flow queue_task_session_hint_used request_id=%s task_id=%s dedupe_key=%s session_title=%s hint_age_ms=%.1f rect=%s automation_id=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target.title),
                hint_age_ms,
                clean(target.rect),
                clean(target.automation_id),
            )
        else:
            listen_flow_logger.info(
                "listen_flow queue_task_session_hint_miss request_id=%s task_id=%s dedupe_key=%s session_title=%s reason=%s hint_age_ms=%.1f",
                request_id,
                task.task_id,
                task.dedupe_key,
                task.session_title,
                hint_reason,
                hint_age_ms,
            )
            target = self._find_target_session_fresh(task.session_title)
            find_ms = _elapsed_ms(find_started_at)
        listen_flow_logger.info(
            "listen_flow queue_task_session_resolve_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f found=%s session_title=%s find_source=%s hint_reason=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            bool(target),
            task.session_title,
            find_source,
            hint_reason,
            find_ms,
        )
        if target is None:
            process_ms = _elapsed_ms(process_started_at)
            logger.info(
                "qianniu scan_unread_timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f stage=queued_find_target ok=False find_ms=%.1f find_source=%s hint_reason=%s selected=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                process_ms,
                find_ms,
                find_source,
                hint_reason,
                task.session_title,
            )
            listen_flow_logger.info(
                "listen_flow queue_task_processed request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f status=failed stage=find_target account_id=%s account_display_name=%s session_title=%s source=%s detail=target_session_not_found",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                process_ms,
                task.account_id,
                task.account_display_name,
                task.session_title,
                task.source,
            )
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 1,
                "skipped_count": 0,
                "processed": [
                    {
                        "account_id": task.account_id,
                        "account_display_name": task.account_display_name,
                        "display_name": task.session_title,
                        "task_id": task.task_id,
                        "dedupe_key": task.dedupe_key,
                        "queue_wait_ms": queue_wait_ms,
                        "process_ms": process_ms,
                        "error": "target_session_not_found",
                    }
                ],
                "task_status": "failed",
                **self._account_result_payload(account_result),
            }

        select_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow session_switch_start request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f account_id=%s account_display_name=%s target_title=%s unread_score=%.5f selected_before=%s rect=%s automation_id=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            float(getattr(target, "unread_score", 0.0) or 0.0),
            bool(getattr(target, "selected", False)),
            clean(getattr(target, "rect", "")),
            clean(getattr(target, "automation_id", "")),
        )
        switched, method = self._select_session(target)
        select_ms = _elapsed_ms(select_started_at)
        listen_flow_logger.info(
            "listen_flow session_switch_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f ok=%s method=%s account_id=%s account_display_name=%s target_title=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            bool(switched),
            clean(method),
            self._account_id,
            self._account_display_name,
            clean(target.title),
            select_ms,
        )
        if not switched and find_source == "task_hint":
            listen_flow_logger.info(
                "listen_flow queue_task_session_hint_fallback_start request_id=%s task_id=%s dedupe_key=%s reason=hint_select_failed method=%s session_title=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(method),
                task.session_title,
            )
            fallback_find_started_at = time.perf_counter()
            fallback_target = self._find_target_session_fresh(task.session_title)
            fallback_find_ms = _elapsed_ms(fallback_find_started_at)
            find_ms += fallback_find_ms
            find_source = "task_hint_fallback_fresh_scan"
            listen_flow_logger.info(
                "listen_flow queue_task_session_hint_fallback_done request_id=%s task_id=%s dedupe_key=%s found=%s session_title=%s fallback_find_ms=%.1f",
                request_id,
                task.task_id,
                task.dedupe_key,
                bool(fallback_target),
                task.session_title,
                fallback_find_ms,
            )
            if fallback_target is not None:
                target = fallback_target
                fallback_select_started_at = time.perf_counter()
                switched, method = self._select_session(target)
                fallback_select_ms = _elapsed_ms(fallback_select_started_at)
                select_ms += fallback_select_ms
                listen_flow_logger.info(
                    "listen_flow session_switch_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f ok=%s method=%s account_id=%s account_display_name=%s target_title=%s elapsed_ms=%.1f fallback=True",
                    request_id,
                    task.task_id,
                    task.dedupe_key,
                    queue_wait_ms,
                    bool(switched),
                    clean(method),
                    self._account_id,
                    self._account_display_name,
                    clean(target.title),
                    fallback_select_ms,
                )
            else:
                method = clean(method) or "hint_select_failed_fresh_target_not_found"
        if not switched:
            process_ms = _elapsed_ms(process_started_at)
            listen_flow_logger.info(
                "listen_flow queue_task_processed request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f status=failed stage=session_switch account_id=%s account_display_name=%s session_title=%s source=%s detail=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                process_ms,
                self._account_id,
                self._account_display_name,
                clean(target.title),
                task.source,
                clean(method) or "session_switch_failed",
            )
            logger.info(
                "qianniu scan_unread_timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f stage=queued_process ms=%.1f find_ms=%.1f find_source=%s selected=%s select_ms=%.1f switch_method=%s switched=False",
                request_id,
                task.task_id,
                task.dedupe_key,
                queue_wait_ms,
                process_ms,
                process_ms,
                find_ms,
                find_source,
                target.title,
                select_ms,
                method,
            )
            result = {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 1,
                "processed": [
                    {
                        "account_id": self._account_id,
                        "account_display_name": self._account_display_name,
                        "display_name": target.title,
                        "error": method or "session_switch_failed",
                        "task_id": task.task_id,
                        "dedupe_key": task.dedupe_key,
                        "queue_wait_ms": queue_wait_ms,
                        "process_ms": process_ms,
                    }
                ],
                "task_status": "failed",
            }
            result.update(self._account_result_payload(account_result))
            return result

        self._remember_active_session(target.title, source="queue_task_session_switch", conversation_key=task.conversation_key)
        self._handled_unread_session_keys.add(_account_scoped_session_unread_key(self._account_id, target))
        emit_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow conversation_emit_start request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
        )
        conversation_event = self._conversation_event(target)
        self._store.append(conversation_event)
        conversation_emit_ms = _elapsed_ms(emit_started_at)
        listen_flow_logger.info(
            "listen_flow conversation_emit_done request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s conversation_key=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            clean(conversation_event.get("conversation_key")),
            conversation_emit_ms,
        )
        read_started_at = time.perf_counter()
        chat_root_started_at = time.perf_counter()
        chat_root, chat_root_source = self._current_session_chat_root_with_source(prefer_window_root=True)
        chat_root_ms = _elapsed_ms(chat_root_started_at)
        message_web_root, message_web_source, message_web_ms = self._resolve_worker_message_web_root(
            chat_root,
            task=task,
            request_id=request_id,
            target_title=clean(target.title),
        )
        listen_flow_logger.info(
            "listen_flow message_read_start request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s limit=%s chat_root_available=%s chat_root_source=%s chat_root_ms=%.1f message_web_available=%s message_web_source=%s message_web_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            limit,
            bool(chat_root),
            chat_root_source,
            chat_root_ms,
            bool(message_web_root),
            message_web_source,
            message_web_ms,
        )
        reader_call_started_at = time.perf_counter()
        try:
            read_result, messages = self._reader.read_visible_messages_debug(
                limit=limit,
                chat_root=chat_root,
                message_web=message_web_root,
            )
        except TypeError:
            read_result, messages = self._reader.read_visible_messages_debug(limit=limit)
        reader_call_ms = _elapsed_ms(reader_call_started_at)
        read_ms = _elapsed_ms(read_started_at)
        listen_flow_logger.info(
            "listen_flow message_read_done request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s read_ok=%s source=%s detail=%s parsed_messages=%s chat_root_source=%s chat_root_ms=%.1f message_web_source=%s message_web_ms=%.1f reader_call_ms=%.1f elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            bool(read_result.ok),
            clean(read_result.source),
            clean(read_result.detail),
            len(messages),
            chat_root_source,
            chat_root_ms,
            message_web_source,
            message_web_ms,
            reader_call_ms,
            read_ms,
        )
        messages_emit_started_at = time.perf_counter()
        message_events = [
            self._message_event(target.title, item, sequence_index=sequence_index)
            for sequence_index, item in enumerate(messages)
        ]
        filtered_message_events = self._filter_unread_message_events(message_events)
        listen_flow_logger.info(
            "listen_flow message_emit_start request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s parsed_messages=%s filtered_messages=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            len(message_events),
            len(message_events) - len(filtered_message_events),
        )
        emitted_messages: list[dict[str, Any]] = []
        for event in filtered_message_events:
            self._store.append(event)
            emitted_messages.append(event)
        messages_emit_ms = _elapsed_ms(messages_emit_started_at)
        auto_reply_input_field = None
        auto_reply_input_source = ""
        auto_reply_input_ms = 0.0
        if emitted_messages:
            auto_reply_input_field, auto_reply_input_source, auto_reply_input_ms = self._resolve_worker_auto_reply_input_field(
                chat_root,
                task=task,
                request_id=request_id,
                target_title=clean(target.title),
            )
            self._remember_worker_auto_reply_context(
                task,
                clean(target.title),
                chat_root=chat_root,
                input_field=auto_reply_input_field,
                input_source=auto_reply_input_source,
            )
        process_ms = _elapsed_ms(process_started_at)
        listen_flow_logger.info(
            "listen_flow message_emit_done request_id=%s task_id=%s dedupe_key=%s account_id=%s account_display_name=%s target_title=%s emitted_messages=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            len(emitted_messages),
            messages_emit_ms,
        )
        listen_flow_logger.info(
            "listen_flow queue_task_processed request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f status=done account_id=%s account_display_name=%s session_title=%s source=%s messages=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            process_ms,
            self._account_id,
            self._account_display_name,
            clean(target.title),
            task.source,
            len(emitted_messages),
        )
        logger.info(
            "qianniu scan_unread_timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f stage=queued_process ms=%.1f find_ms=%.1f find_source=%s select_ms=%.1f conversation_emit_ms=%.1f read_ms=%.1f chat_root_ms=%.1f chat_root_source=%s message_web_ms=%.1f message_web_source=%s message_web_hit=%s reader_call_ms=%.1f message_emit_ms=%.1f input_cache_ms=%.1f input_cache_source=%s input_cache_hit=%s selected=%s switch_method=%s read_source=%s read_ok=%s parsed_messages=%s messages=%s filtered=%s",
            request_id,
            task.task_id,
            task.dedupe_key,
            queue_wait_ms,
            process_ms,
            process_ms,
            find_ms,
            find_source,
            select_ms,
            conversation_emit_ms,
            read_ms,
            chat_root_ms,
            chat_root_source,
            message_web_ms,
            message_web_source,
            bool(message_web_root),
            reader_call_ms,
            messages_emit_ms,
            auto_reply_input_ms,
            auto_reply_input_source,
            bool(auto_reply_input_field),
            target.title,
            method,
            read_result.source,
            read_result.ok,
            len(message_events),
            len(emitted_messages),
            len(message_events) - len(emitted_messages),
        )
        result = {
            "unread_count": 0,
            "conversation_count": 1,
            "message_count": len(emitted_messages),
            "processed_count": 1,
            "processed": [
                {
                    "account_id": self._account_id,
                    "account_display_name": self._account_display_name,
                    "display_name": target.title,
                    "switch_method": method,
                    "session_find_source": find_source,
                    "messages": len(emitted_messages),
                    "parsed_messages": len(message_events),
                    "filtered_messages": len(message_events) - len(emitted_messages),
                    "read_source": read_result.source,
                    "read_ok": read_result.ok,
                    "chat_root_source": chat_root_source,
                    "chat_root_ms": chat_root_ms,
                    "message_web_source": message_web_source,
                    "message_web_ms": message_web_ms,
                    "reader_call_ms": reader_call_ms,
                    "task_id": task.task_id,
                    "dedupe_key": task.dedupe_key,
                    "queue_wait_ms": queue_wait_ms,
                    "process_ms": process_ms,
                }
            ],
            "task_status": "done",
        }
        result.update(self._account_result_payload(account_result))
        return result

    def _prepare_reply_draft(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        text = clean(params.get("text"))
        if not text:
            raise RuntimeError("empty_text")
        conversation_key = clean(params.get("conversation_key"))
        account_result = self._ensure_requested_account_selected(params, conversation_key=conversation_key, fresh=False)
        if not account_result.ok:
            raise RuntimeError(account_result.detail or account_result.stage)
        display_name = _requested_display_name(params.get("display_name"), conversation_key) or "current"
        ensure_result = self._ensure_target_session_selected(display_name, conversation_key)
        if not ensure_result.ok:
            raise RuntimeError(ensure_result.detail or ensure_result.stage or "target_session_not_verified")
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
                metadata={
                    "method": result.method,
                    "target_account_method": account_result.method,
                    "target_account_stage": account_result.stage,
                    "target_account_id": account_result.account_id,
                    "target_account_display_name": account_result.display_name,
                    "target_session_method": ensure_result.method,
                    "target_session_stage": ensure_result.stage,
                    "target_session_selected": ensure_result.selected_title,
                },
            )
        )
        response = {
            "prepared": True,
            "task_id": task_id,
            "method": result.method,
            "target_session_method": ensure_result.method,
            "target_session_stage": ensure_result.stage,
        }
        response.update(self._account_result_payload(account_result))
        return response

    def _ensure_target_session_selected(
        self,
        display_name: str,
        conversation_key: str = "",
        *,
        verify_selected_after_click: bool = False,
        allow_click_assumed: bool = False,
    ) -> EnsureSessionResult:
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
            self._remember_active_session(selected.title, source="ensure_current_selected", conversation_key=conversation_key)
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

            selected_after = None
            selected_verify_ms = 0.0
            if verify_selected_after_click:
                selected_verify_started_at = time.perf_counter()
                selected_after = self._selected_session(fresh=True)
                selected_verify_ms = _elapsed_ms(selected_verify_started_at)
                selected_after_title = clean(getattr(selected_after, "title", "")) if selected_after else ""
                if selected_after and session_titles_match(selected_after.title, target):
                    logger.info(
                        "qianniu ensure_target_session timing target=%s attempt=%s stage=selected_verified ok=True method=%s scan_ms=%.1f select_ms=%.1f selected_verify_ms=%.1f selected=%s total_ms=%.1f",
                        target,
                        attempt,
                        switch_method,
                        scan_ms,
                        select_ms,
                        selected_verify_ms,
                        selected_after.title,
                        _elapsed_ms(started_at),
                    )
                    self._remember_active_session(
                        selected_after.title,
                        source="ensure_selected_verified",
                        conversation_key=conversation_key,
                    )
                    return EnsureSessionResult(
                        ok=True,
                        stage="selected_verified",
                        method=f"fresh_scan+{switch_method}+verify_selected",
                        selected_title=selected_after.title,
                    )
                last_detail = selected_after_title or last_detail

            header_started_at = time.perf_counter()
            header_names = self._current_chat_header_names()
            header_ms = _elapsed_ms(header_started_at)
            matched_header = next((name for name in header_names if session_titles_match(name, target)), "")
            if matched_header:
                logger.info(
                    "qianniu ensure_target_session timing target=%s attempt=%s stage=header_verified ok=True method=%s scan_ms=%.1f select_ms=%.1f header_ms=%.1f header=%s total_ms=%.1f",
                    target,
                    attempt,
                    switch_method,
                    scan_ms,
                    select_ms,
                    header_ms,
                    matched_header,
                    _elapsed_ms(started_at),
                    )
                self._remember_active_session(
                    matched_header,
                    source="ensure_header_verified",
                    conversation_key=conversation_key,
                )
                return EnsureSessionResult(
                    ok=True,
                    stage="header_verified",
                    method=f"fresh_scan+{switch_method}+verify_header",
                    selected_title=matched_header,
                )

            header_detail = "|".join(header_names[:5]) or "header_unavailable"
            selected_detail = clean(getattr(selected_after, "title", "")) if selected_after else ""
            last_detail = header_detail
            if allow_click_assumed:
                logger.info(
                    "qianniu ensure_target_session timing target=%s attempt=%s stage=click_assumed ok=True method=%s scan_ms=%.1f select_ms=%.1f selected_verify_ms=%.1f header_ms=%.1f selected_after=%s header_after=%s total_ms=%.1f",
                    target,
                    attempt,
                    switch_method,
                    scan_ms,
                    select_ms,
                    selected_verify_ms,
                    header_ms,
                    selected_detail or "selected_session_unavailable",
                    header_detail,
                    _elapsed_ms(started_at),
                )
                self._remember_active_session(
                    clean(target_item.title) or target,
                    source="ensure_click_assumed",
                    conversation_key=conversation_key,
                )
                return EnsureSessionResult(
                    ok=True,
                    stage="click_assumed",
                    method=f"fresh_scan+{switch_method}+assume_clicked",
                    selected_title=clean(target_item.title) or target,
                )
            logger.info(
                "qianniu ensure_target_session timing target=%s attempt=%s stage=verify_header ok=False method=%s scan_ms=%.1f select_ms=%.1f selected_verify_ms=%.1f header_ms=%.1f selected_after=%s header_after=%s total_ms=%.1f",
                target,
                attempt,
                switch_method,
                scan_ms,
                select_ms,
                selected_verify_ms,
                header_ms,
                selected_detail,
                header_detail,
                _elapsed_ms(started_at),
            )

        return EnsureSessionResult(
            ok=False,
            stage="verify_header",
            method="fresh_scan+select+verify_header",
            detail="target_session_header_not_verified",
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

    def _target_session_from_task_hint(self, task: QianniuConversationTask) -> tuple[SessionItem | None, str, float]:
        hint = task.session_hint
        if not isinstance(hint, dict):
            return None, "missing_hint", 0.0

        captured_at = 0.0
        try:
            captured_at = float(hint.get("captured_at") or task.created_at or 0.0)
        except (TypeError, ValueError):
            captured_at = float(task.created_at or 0.0)
        age_ms = max(0.0, (time.time() - captured_at) * 1000.0) if captured_at else 0.0
        if age_ms > CONVERSATION_TASK_SESSION_HINT_TTL_SEC * 1000.0:
            return None, "hint_expired", age_ms

        task_account_id = clean(task.account_id)
        current_account_id = clean(self._account_id)
        if (
            task_account_id
            and current_account_id
            and task_account_id not in {DEFAULT_ACCOUNT_ID, PLATFORM_QIANNIU}
            and current_account_id not in {DEFAULT_ACCOUNT_ID, PLATFORM_QIANNIU}
            and task_account_id != current_account_id
        ):
            return None, "account_mismatch", age_ms

        item = _session_item_from_hint(hint, task.session_title)
        if item is None:
            return None, "invalid_hint", age_ms
        if not session_titles_match(item.title, task.session_title):
            return None, "title_mismatch", age_ms
        if item.rect_tuple is None:
            return None, "missing_rect", age_ms
        return item, "ok", age_ms

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
        if callable(selector):
            return selector(item)
        fallback = getattr(self._sessions, "select_first_unread", None)
        if callable(fallback):
            selected, switched, method = fallback()
            if selected is not None and not session_titles_match(selected.title, item.title):
                return False, "select_first_unread_mismatch"
            return bool(switched), clean(method) or "select_first_unread"
        return False, "select_session_unavailable"

    def _current_chat_header_names(self) -> list[str]:
        getter = getattr(self._detector, "find_chat_header_names", None)
        if not callable(getter):
            return []
        chat_root = self._current_session_chat_root()
        try:
            names = getter(chat_root)
        except TypeError:
            names = getter()
        except Exception:
            logger.exception("qianniu current chat header lookup failed")
            return []
        if not isinstance(names, list):
            return []
        return [clean(name) for name in names if clean(name)]

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
        conversation_key = clean(params.get("conversation_key"))
        display_name = _requested_display_name(params.get("display_name"), conversation_key) or "current"
        task_id = clean(params.get("task_id"))
        client_message_id = clean(params.get("client_message_id")) or task_id or request_id
        queue_wait_ms = _queue_wait_ms_from_params(params)
        auto_reply_send = self._is_auto_reply_send_request(params)
        start_account_query, _start_account_source = self._requested_account_query(params, conversation_key=conversation_key)
        start_account_id = start_account_query if start_account_query.startswith("qnacct_") else self._account_id
        start_task_id = task_id or _queue_task_id(request_id, start_account_id, display_name, "send_message")
        start_dedupe_key = _queue_dedupe_key(start_account_id, display_name)
        listen_flow_logger.info(
            "listen_flow send_message_start request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f auto_reply_send=%s client_message_id=%s conversation_key=%s display_name=%s content_type=%s text_len=%s file_path=%s account_id=%s account_display_name=%s",
            request_id,
            start_task_id,
            start_dedupe_key,
            queue_wait_ms,
            auto_reply_send,
            client_message_id,
            conversation_key,
            display_name,
            content_type,
            len(text),
            file_path,
            self._account_id,
            self._account_display_name,
        )

        worker_direct_context = self._worker_auto_reply_context_snapshot() if auto_reply_send else None
        if worker_direct_context is not None and not self._worker_auto_reply_context_matches(
            worker_direct_context,
            display_name=display_name,
            conversation_key=conversation_key,
            params=params,
        ):
            listen_flow_logger.info(
                "listen_flow send_worker_direct_context_miss request_id=%s client_message_id=%s reason=target_mismatch context_task_id=%s context_dedupe_key=%s context_account_id=%s context_session=%s target_display_name=%s conversation_key=%s",
                request_id,
                client_message_id,
                worker_direct_context.task_id,
                worker_direct_context.dedupe_key,
                worker_direct_context.account_id,
                worker_direct_context.session_title,
                display_name,
                conversation_key,
            )
            worker_direct_context = None
        if worker_direct_context is not None:
            self._account_id = worker_direct_context.account_id or self._account_id
            self._account_display_name = worker_direct_context.account_display_name or self._account_display_name
            listen_flow_logger.info(
                "listen_flow send_worker_direct_context_hit request_id=%s client_message_id=%s context_task_id=%s context_dedupe_key=%s account_id=%s account_display_name=%s session=%s conversation_key=%s",
                request_id,
                client_message_id,
                worker_direct_context.task_id,
                worker_direct_context.dedupe_key,
                self._account_id,
                self._account_display_name,
                worker_direct_context.session_title,
                worker_direct_context.conversation_key,
            )
            account_result = EnsureAccountResult(
                ok=True,
                stage="worker_auto_reply_context",
                method="worker_direct_context",
                account_id=self._account_id,
                display_name=self._account_display_name,
            )
            ensure_result = EnsureSessionResult(
                ok=True,
                stage="worker_auto_reply_context",
                method="worker_direct_context",
                selected_title=worker_direct_context.session_title,
            )
            fast_context = None
        else:
            fast_context = self._send_fast_context(
                display_name,
                conversation_key,
                params,
                request_id=request_id,
                client_message_id=client_message_id,
            )
        send_direct_path = worker_direct_context is not None or fast_context is not None
        if worker_direct_context is not None:
            pass
        elif fast_context is not None:
            account_result, ensure_result = fast_context
        else:
            account_result = self._ensure_requested_account_selected(params, conversation_key=conversation_key, fresh=False)
            if not account_result.ok:
                process_ms = _elapsed_ms(total_started_at)
                failed_account_id = clean(account_result.account_id) or start_account_id
                failed_task_id = task_id or _queue_task_id(request_id, failed_account_id, display_name, "send_message")
                failed_dedupe_key = _queue_dedupe_key(failed_account_id, display_name)
                listen_flow_logger.info(
                    "listen_flow send_message_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s client_message_id=%s status=failed stage=%s detail=%s total_ms=%.1f",
                    request_id,
                    failed_task_id,
                    failed_dedupe_key,
                    queue_wait_ms,
                    process_ms,
                    False,
                    client_message_id,
                    account_result.stage,
                    account_result.detail,
                    process_ms,
                )
                logger.info(
                    "qianniu send_message timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f status=failed stage=%s detail=%s",
                    request_id,
                    failed_task_id,
                    failed_dedupe_key,
                    queue_wait_ms,
                    process_ms,
                    False,
                    client_message_id,
                    process_ms,
                    0.0,
                    account_result.stage,
                    account_result.detail,
                )
                raise RuntimeError(account_result.detail or account_result.stage)
            ensure_started_at = time.perf_counter()
            ensure_result = self._ensure_target_session_selected(
                display_name,
                conversation_key,
                verify_selected_after_click=True,
                allow_click_assumed=True,
            )
            listen_flow_logger.info(
                "listen_flow send_session_ensure_done request_id=%s client_message_id=%s ok=%s stage=%s method=%s selected=%s detail=%s elapsed_ms=%.1f",
                request_id,
                client_message_id,
                ensure_result.ok,
                ensure_result.stage,
                ensure_result.method,
                ensure_result.selected_title,
                ensure_result.detail,
                _elapsed_ms(ensure_started_at),
            )
        send_account_id = clean(account_result.account_id) or self._account_id
        send_task_id = task_id or _queue_task_id(
            request_id,
            send_account_id,
            display_name,
            "send_message",
        )
        send_dedupe_key = _queue_dedupe_key(send_account_id, display_name)
        listen_flow_logger.info(
            "listen_flow send_queue_context request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s account_id=%s account_display_name=%s display_name=%s target_account_stage=%s target_session_stage=%s",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            send_account_id,
            clean(account_result.display_name) or self._account_display_name,
            display_name,
            account_result.stage,
            ensure_result.stage,
        )
        listen_flow_logger.info(
            "listen_flow send_account_select_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s ok=%s stage=%s method=%s account_id=%s account_display_name=%s switched=%s",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            account_result.ok,
            account_result.stage,
            account_result.method,
            account_result.account_id,
            account_result.display_name,
            account_result.switched,
        )
        if not ensure_result.ok:
            process_ms = _elapsed_ms(total_started_at)
            listen_flow_logger.info(
                "listen_flow send_message_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s status=failed stage=%s detail=%s total_ms=%.1f",
                request_id,
                send_task_id,
                send_dedupe_key,
                queue_wait_ms,
                process_ms,
                send_direct_path,
                worker_direct_context is not None,
                auto_reply_send,
                client_message_id,
                ensure_result.stage,
                ensure_result.detail,
                process_ms,
            )
            logger.info(
                "qianniu send_message timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f status=failed stage=%s detail=%s",
                request_id,
                send_task_id,
                send_dedupe_key,
                queue_wait_ms,
                process_ms,
                send_direct_path,
                worker_direct_context is not None,
                auto_reply_send,
                client_message_id,
                process_ms,
                0.0,
                ensure_result.stage,
                ensure_result.detail,
            )
            raise RuntimeError(ensure_result.detail or ensure_result.stage or "target_session_not_verified")
        send_started_at = time.perf_counter()
        cached_input_field = None
        cached_input_source = ""
        if worker_direct_context is not None and content_type == "text":
            cached_input_field = getattr(worker_direct_context, "input_field", None)
            cached_input_source = clean(getattr(worker_direct_context, "input_source", ""))

        cached_chat_root = getattr(worker_direct_context, "chat_root", None) if worker_direct_context is not None else None
        if cached_chat_root is not None:
            chat_root = cached_chat_root
            chat_root_source = "worker_auto_reply_context"
        else:
            chat_root, chat_root_source = self._current_session_chat_root_with_source(
                prefer_window_root=send_direct_path
            )
        if worker_direct_context is not None:
            listen_flow_logger.info(
                "listen_flow send_worker_direct_input_cache_%s request_id=%s task_id=%s dedupe_key=%s client_message_id=%s chat_root_available=%s input_field_available=%s input_source=%s",
                "hit" if cached_input_field is not None else "miss",
                request_id,
                worker_direct_context.task_id,
                worker_direct_context.dedupe_key,
                client_message_id,
                cached_chat_root is not None,
                cached_input_field is not None,
                cached_input_source,
            )
        listen_flow_logger.info(
            "listen_flow send_action_start request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s content_type=%s chat_root_available=%s chat_root_source=%s input_field_available=%s input_source=%s target_session_stage=%s",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            content_type,
            bool(chat_root),
            chat_root_source,
            bool(cached_input_field),
            cached_input_source,
            ensure_result.stage,
        )
        if content_type == "text":
            try:
                send_text_kwargs: dict[str, Any] = {
                    "chat_root": chat_root,
                    "allow_global_find": chat_root is None,
                }
                if cached_input_field is not None:
                    send_text_kwargs["input_field"] = cached_input_field
                result = self._sender.send_text(
                    text,
                    dry_run=False,
                    **send_text_kwargs,
                )
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
        listen_flow_logger.info(
            "listen_flow send_action_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s ok=%s stage=%s method=%s detail=%s elapsed_ms=%.1f",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            result.ok,
            result.stage,
            result.method,
            result.detail,
            send_ms,
        )
        if not result.ok:
            process_ms = _elapsed_ms(total_started_at)
            listen_flow_logger.info(
                "listen_flow send_message_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s status=failed stage=%s detail=%s total_ms=%.1f",
                request_id,
                send_task_id,
                send_dedupe_key,
                queue_wait_ms,
                process_ms,
                send_direct_path,
                worker_direct_context is not None,
                auto_reply_send,
                client_message_id,
                result.stage,
                result.detail,
                process_ms,
            )
            logger.info(
                "qianniu send_message timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f status=failed stage=%s detail=%s",
                request_id,
                send_task_id,
                send_dedupe_key,
                queue_wait_ms,
                process_ms,
                send_direct_path,
                worker_direct_context is not None,
                auto_reply_send,
                client_message_id,
                process_ms,
                send_ms,
                result.stage,
                result.detail,
            )
            if worker_direct_context is not None:
                self._clear_worker_auto_reply_context("send_failed")
            raise RuntimeError(result.detail or result.stage)
        self._remember_active_session(display_name, source="send_message_sent", conversation_key=conversation_key)
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
                    "target_account_method": account_result.method,
                    "target_account_stage": account_result.stage,
                    "target_account_id": account_result.account_id,
                    "target_account_display_name": account_result.display_name,
                    "target_session_method": ensure_result.method,
                    "target_session_stage": ensure_result.stage,
                    "target_session_selected": ensure_result.selected_title,
                    "send_direct_path": send_direct_path,
                    "worker_direct_path": worker_direct_context is not None,
                    "send_source": clean(params.get("send_source")),
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
                    "target_account_method": account_result.method,
                    "target_account_stage": account_result.stage,
                    "target_session_method": ensure_result.method,
                    "send_direct_path": send_direct_path,
                    "worker_direct_path": worker_direct_context is not None,
                    "send_source": clean(params.get("send_source")),
                    "content_type": content_type,
                    "content": text,
                    "file_path": file_path,
                    "file_name": clean(params.get("file_name")),
                },
            )
        )
        emit_ms = _elapsed_ms(emit_started_at)
        process_ms = _elapsed_ms(total_started_at)
        listen_flow_logger.info(
            "listen_flow send_message_done request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s status=sent method=%s display_name=%s conversation_key=%s emit_ms=%.1f total_ms=%.1f",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            process_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            result.method,
            display_name,
            _conversation_key(self._account_id, display_name),
            emit_ms,
            process_ms,
        )
        logger.info(
            "qianniu send_message timing request_id=%s task_id=%s dedupe_key=%s queue_wait_ms=%.1f process_ms=%.1f send_direct_path=%s worker_direct_path=%s auto_reply_send=%s client_message_id=%s total_ms=%.1f send_text_ms=%.1f emit_ms=%.1f status=sent method=%s display_name=%s",
            request_id,
            send_task_id,
            send_dedupe_key,
            queue_wait_ms,
            process_ms,
            send_direct_path,
            worker_direct_context is not None,
            auto_reply_send,
            client_message_id,
            process_ms,
            send_ms,
            emit_ms,
            result.method,
            display_name,
        )
        if worker_direct_context is not None:
            self._clear_worker_auto_reply_context("send_sent")
        return {
            "sent": True,
            "task_id": task_id,
            "client_message_id": client_message_id,
            "method": result.method,
            "target_account_method": account_result.method,
            "target_account_stage": account_result.stage,
            "target_account_id": account_result.account_id,
            "target_account_display_name": account_result.display_name,
            "target_session_method": ensure_result.method,
            "target_session_stage": ensure_result.stage,
            "send_direct_path": send_direct_path,
            "worker_direct_path": worker_direct_context is not None,
            "content_type": content_type,
        }

    def _ensure_requested_account_selected(
        self,
        params: dict[str, Any],
        *,
        conversation_key: str = "",
        fresh: bool = False,
    ) -> EnsureAccountResult:
        query, source = self._requested_account_query(params, conversation_key=conversation_key)
        if not query or query in {"current", DEFAULT_ACCOUNT_ID, PLATFORM_QIANNIU}:
            active = self._sync_selected_account_from_tabs(fresh=fresh)
            return EnsureAccountResult(
                ok=True,
                stage="no_explicit_account",
                method="selected_account_scan" if active else "skipped",
                account_id=clean(getattr(active, "account_id", "")) or self._account_id,
                display_name=clean(getattr(active, "display_name", "")),
            )

        accounts = self._list_accounts_safely(fresh=fresh)
        real_accounts = [item for item in accounts if item.source != "synthetic_single_account"]
        if not real_accounts:
            # Legacy single-account mode may pass an external account_id even though Qianniu has no account tabs.
            return EnsureAccountResult(
                ok=True,
                stage="legacy_no_account_tabs",
                method=f"skip_{source}",
                account_id=self._account_id,
            )

        result = self._switch_account_query(query)
        if result.ok:
            return EnsureAccountResult(
                ok=True,
                stage="target_account_selected",
                method=f"{source}+{result.method}" if source else result.method,
                account_id=result.account_id,
                display_name=result.display_name,
                switched=result.switched,
            )
        return EnsureAccountResult(
            ok=False,
            stage="target_account_select",
            method=source,
            detail=result.detail or result.method or "account_switch_failed",
            account_id=result.account_id,
            display_name=result.display_name,
        )

    def _send_fast_context(
        self,
        display_name: str,
        conversation_key: str,
        params: dict[str, Any],
        *,
        request_id: str,
        client_message_id: str,
    ) -> tuple[EnsureAccountResult, EnsureSessionResult] | None:
        target = _requested_display_name(display_name, conversation_key)
        if not target or target == "current":
            listen_flow_logger.info(
                "listen_flow send_fast_context_miss reason=no_explicit_target conversation_key=%s display_name=%s",
                conversation_key,
                display_name,
            )
            return None

        context = self._active_session_context_snapshot()
        if context is None:
            listen_flow_logger.info(
                "listen_flow send_fast_context_miss reason=no_context conversation_key=%s display_name=%s target=%s",
                conversation_key,
                display_name,
                target,
            )
            return None

        age_sec = max(0.0, time.perf_counter() - context.updated_at)
        if age_sec > SEND_FAST_CONTEXT_TTL_SEC:
            listen_flow_logger.info(
                "listen_flow send_fast_context_miss reason=context_expired age_ms=%.1f ttl_ms=%.1f context_account_id=%s context_session=%s target=%s",
                age_sec * 1000.0,
                SEND_FAST_CONTEXT_TTL_SEC * 1000.0,
                context.account_id,
                context.session_title,
                target,
            )
            self._clear_active_session_context("expired")
            return None

        account_query, account_source = self._requested_account_query(params, conversation_key=conversation_key)
        if not self._account_query_matches_context(account_query, context):
            listen_flow_logger.info(
                "listen_flow send_fast_context_miss reason=account_mismatch query=%s source=%s context_account_id=%s context_account_display_name=%s target=%s context_session=%s age_ms=%.1f",
                account_query,
                account_source,
                context.account_id,
                context.account_display_name,
                target,
                context.session_title,
                age_sec * 1000.0,
            )
            return None

        if not self._session_query_matches_context(target, conversation_key, context):
            listen_flow_logger.info(
                "listen_flow send_fast_context_miss reason=session_mismatch target=%s conversation_key=%s context_conversation_key=%s context_session=%s context_account_id=%s age_ms=%.1f",
                target,
                conversation_key,
                context.conversation_key,
                context.session_title,
                context.account_id,
                age_sec * 1000.0,
            )
            return None

        self._account_id = context.account_id or self._account_id
        self._account_display_name = context.account_display_name or self._account_display_name
        listen_flow_logger.info(
            "listen_flow send_fast_context_hit account_id=%s account_display_name=%s session=%s conversation_key=%s source=%s age_ms=%.1f",
            self._account_id,
            self._account_display_name,
            context.session_title,
            context.conversation_key,
            context.source,
            age_sec * 1000.0,
        )
        listen_flow_logger.info(
            "listen_flow send_session_ensure_done request_id=%s client_message_id=%s ok=%s stage=%s method=%s selected=%s detail=%s elapsed_ms=%.1f",
            request_id,
            client_message_id,
            True,
            "active_context",
            "memory_context",
            context.session_title,
            "",
            0.0,
        )
        return (
            EnsureAccountResult(
                ok=True,
                stage="active_context",
                method=f"{account_source or 'memory'}+active_context",
                account_id=self._account_id,
                display_name=self._account_display_name,
            ),
            EnsureSessionResult(
                ok=True,
                stage="active_context",
                method="memory_context",
                selected_title=context.session_title,
            ),
        )

    def _build_account_scan_targets_for_unread(self, request_id: str) -> tuple[list[AccountScanTarget], int, int, int]:
        started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow account_scan_start request_id=%s current_account_id=%s current_display_name=%s activate_before_scan=%s",
            request_id,
            self._account_id,
            self._account_display_name,
            bool(getattr(self._accounts, "activate_before_scan", False)),
        )
        accounts = self._list_accounts_safely(fresh=True)
        if not accounts:
            listen_flow_logger.info(
                "listen_flow account_scan_done request_id=%s account_count=0 decision=legacy_current_account elapsed_ms=%.1f",
                request_id,
                _elapsed_ms(started_at),
            )
            target = AccountScanTarget(
                account_id=self._account_id,
                display_name=self._account_display_name,
                selected=True,
                source="legacy_current_account",
                synthetic=True,
            )
            listen_flow_logger.info(
                "listen_flow account_scan_plan request_id=%s plan_count=1 top_unread_account_count=0 accounts=[%s]",
                request_id,
                {"account_id": target.account_id, "display_name": target.display_name, "selected": True, "source": target.source},
            )
            return [target], 0, 0, 0

        active = self._selected_account_from(accounts)
        self._remember_active_account(active)
        real_accounts = [item for item in accounts if item.source != "synthetic_single_account"]
        unread_accounts = sorted(
            [item for item in real_accounts if item.has_unread_hint and not item.selected],
            key=lambda item: (-float(item.unread_score), self._account_rect_sort_key(item)),
        )
        account_details = [
            {
                "account_id": clean(item.account_id),
                "display_name": clean(item.display_name),
                "selected": bool(item.selected),
                "has_unread_hint": bool(item.has_unread_hint),
                "unread_score": round(float(item.unread_score), 4),
                "unread_badge_text": clean(item.unread_badge_text),
                "unread_elapsed_text": clean(item.unread_elapsed_text),
                "unread_hint_rect": list(item.unread_hint_rect) if item.unread_hint_rect else None,
                "rect": list(item.rect_tuple) if item.rect_tuple else None,
                "visual_blue_ratio": round(float(item.visual_blue_ratio), 4),
                "source": clean(item.source),
            }
            for item in accounts
        ]
        listen_flow_logger.info(
            "listen_flow account_scan_done request_id=%s account_count=%s real_account_count=%s active_account_id=%s active_display_name=%s unread_account_count=%s accounts=%s elapsed_ms=%.1f",
            request_id,
            len(accounts),
            len(real_accounts),
            clean(getattr(active, "account_id", "")),
            clean(getattr(active, "display_name", "")),
            len(unread_accounts),
            account_details,
            _elapsed_ms(started_at),
        )

        targets: list[AccountScanTarget] = []
        seen: set[str] = set()

        def append_account(account: AccountTab, *, has_top_unread_hint: bool) -> None:
            key = self._account_scan_target_key(account.account_id, account.display_name)
            if not key or key in seen:
                return
            seen.add(key)
            targets.append(
                AccountScanTarget(
                    account_id=clean(account.account_id) or self._account_id,
                    display_name=clean(account.display_name),
                    selected=bool(account.selected),
                    has_top_unread_hint=has_top_unread_hint,
                    unread_score=float(account.unread_score),
                    source=clean(account.source),
                    synthetic=account.source == "synthetic_single_account",
                )
            )

        if active is not None:
            append_account(active, has_top_unread_hint=False)
        for account in unread_accounts:
            append_account(account, has_top_unread_hint=True)
        if not targets:
            targets.append(
                AccountScanTarget(
                    account_id=self._account_id,
                    display_name=self._account_display_name,
                    selected=True,
                    source="current_account_fallback",
                )
            )

        if unread_accounts:
            listen_flow_logger.info(
                "listen_flow account_unread_decision request_id=%s decision=scan_plan_with_top_unread active_account_id=%s active_display_name=%s unread_account_count=%s target_accounts=%s",
                request_id,
                clean(getattr(active, "account_id", "")),
                clean(getattr(active, "display_name", "")),
                len(unread_accounts),
                [
                    {
                        "account_id": clean(item.account_id),
                        "display_name": clean(item.display_name),
                        "unread_score": round(float(item.unread_score), 4),
                    }
                    for item in unread_accounts
                ],
            )
        else:
            listen_flow_logger.info(
                "listen_flow account_unread_decision request_id=%s decision=no_top_account_unread active_account_id=%s active_display_name=%s account_count=%s real_account_count=%s",
                request_id,
                clean(getattr(active, "account_id", "")),
                clean(getattr(active, "display_name", "")),
                len(accounts),
                len(real_accounts),
            )
        listen_flow_logger.info(
            "listen_flow account_scan_plan request_id=%s plan_count=%s top_unread_account_count=%s accounts=%s",
            request_id,
            len(targets),
            len(unread_accounts),
            [
                {
                    "account_id": target.account_id,
                    "display_name": target.display_name,
                    "selected": target.selected,
                    "has_top_unread_hint": target.has_top_unread_hint,
                    "unread_score": round(float(target.unread_score), 4),
                    "source": target.source,
                }
                for target in targets
            ],
        )
        return targets, len(unread_accounts), len(accounts), len(real_accounts)

    def _select_account_scan_target(
        self,
        target: AccountScanTarget,
        *,
        request_id: str,
        top_unread_count: int,
    ) -> EnsureAccountResult:
        if target.synthetic or target.selected or not target.has_top_unread_hint:
            if target.account_id:
                self._clear_active_session_context_if_account_changed(target.account_id)
                self._account_id = target.account_id
            self._account_display_name = target.display_name
            return EnsureAccountResult(
                ok=True,
                stage="selected_account_scan" if not target.synthetic else "legacy_current_account",
                method="selected_account_scan",
                account_id=self._account_id,
                display_name=self._account_display_name,
                unread_account_count=top_unread_count,
            )

        logger.info(
            "qianniu account_unread_timing request_id=%s unread_accounts=%s target_account_id=%s target_display_name=%s unread_score=%.4f",
            request_id,
            top_unread_count,
            target.account_id,
            target.display_name,
            float(target.unread_score),
        )
        listen_flow_logger.info(
            "listen_flow account_unread_decision request_id=%s decision=switch_top_unread target_account_id=%s target_display_name=%s unread_account_count=%s unread_score=%.4f",
            request_id,
            target.account_id,
            target.display_name,
            top_unread_count,
            float(target.unread_score),
        )
        switch_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow account_switch_start request_id=%s target_account_id=%s target_display_name=%s",
            request_id,
            target.account_id,
            target.display_name,
        )
        result = self._switch_account_query(target.account_id or target.display_name)
        listen_flow_logger.info(
            "listen_flow account_switch_done request_id=%s ok=%s method=%s detail=%s result_account_id=%s result_display_name=%s switched=%s elapsed_ms=%.1f",
            request_id,
            result.ok,
            result.method,
            result.detail,
            result.account_id,
            result.display_name,
            result.switched,
            _elapsed_ms(switch_started_at),
        )
        if not result.ok:
            return EnsureAccountResult(
                ok=False,
                stage="top_unread_account_select",
                method=result.method,
                detail=result.detail or "account_switch_failed",
                account_id=target.account_id,
                display_name=target.display_name,
                unread_account_count=top_unread_count,
            )
        return EnsureAccountResult(
            ok=True,
            stage="top_unread_account_selected",
            method=result.method,
            account_id=result.account_id or target.account_id,
            display_name=result.display_name or target.display_name,
            switched=result.switched,
            unread_account_count=top_unread_count,
        )

    def _account_scan_target_key(self, account_id: str, display_name: str = "") -> str:
        return clean(account_id) or clean(display_name)

    def _prune_handled_unread_session_keys(self, account_id: str, unread_keys: set[str]) -> None:
        prefix = f"{clean(account_id) or DEFAULT_ACCOUNT_ID}:"
        self._handled_unread_session_keys = {
            key
            for key in self._handled_unread_session_keys
            if not key.startswith(prefix) or key in unread_keys
        }

    def _select_unread_account_for_scan(self, request_id: str) -> EnsureAccountResult:
        started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow account_scan_start request_id=%s current_account_id=%s current_display_name=%s activate_before_scan=%s",
            request_id,
            self._account_id,
            self._account_display_name,
            bool(getattr(self._accounts, "activate_before_scan", False)),
        )
        accounts = self._list_accounts_safely(fresh=True)
        if not accounts:
            listen_flow_logger.info(
                "listen_flow account_scan_done request_id=%s account_count=0 decision=legacy_current_account elapsed_ms=%.1f",
                request_id,
                _elapsed_ms(started_at),
            )
            return EnsureAccountResult(ok=True, stage="account_probe_unavailable", method="legacy_current_account", account_id=self._account_id)

        active = self._selected_account_from(accounts)
        self._remember_active_account(active)
        real_accounts = [item for item in accounts if item.source != "synthetic_single_account"]
        unread_accounts = [item for item in real_accounts if item.has_unread_hint and not item.selected]
        account_details = [
            {
                "account_id": clean(item.account_id),
                "display_name": clean(item.display_name),
                "selected": bool(item.selected),
                "has_unread_hint": bool(item.has_unread_hint),
                "unread_score": round(float(item.unread_score), 4),
                "unread_badge_text": clean(item.unread_badge_text),
                "unread_elapsed_text": clean(item.unread_elapsed_text),
                "unread_hint_rect": list(item.unread_hint_rect) if item.unread_hint_rect else None,
                "rect": list(item.rect_tuple) if item.rect_tuple else None,
                "visual_blue_ratio": round(float(item.visual_blue_ratio), 4),
                "source": clean(item.source),
            }
            for item in accounts
        ]
        listen_flow_logger.info(
            "listen_flow account_scan_done request_id=%s account_count=%s real_account_count=%s active_account_id=%s active_display_name=%s unread_account_count=%s accounts=%s elapsed_ms=%.1f",
            request_id,
            len(accounts),
            len(real_accounts),
            clean(getattr(active, "account_id", "")),
            clean(getattr(active, "display_name", "")),
            len(unread_accounts),
            account_details,
            _elapsed_ms(started_at),
        )
        if not unread_accounts:
            listen_flow_logger.info(
                "listen_flow account_unread_decision request_id=%s decision=no_top_account_unread active_account_id=%s active_display_name=%s account_count=%s real_account_count=%s",
                request_id,
                clean(getattr(active, "account_id", "")),
                clean(getattr(active, "display_name", "")),
                len(accounts),
                len(real_accounts),
            )
            return EnsureAccountResult(
                ok=True,
                stage="no_top_account_unread",
                method="selected_account_scan",
                account_id=clean(getattr(active, "account_id", "")) or self._account_id,
                display_name=clean(getattr(active, "display_name", "")),
            )

        target = sorted(unread_accounts, key=lambda item: (-float(item.unread_score), self._account_rect_sort_key(item)))[0]
        logger.info(
            "qianniu account_unread_timing request_id=%s unread_accounts=%s target_account_id=%s target_display_name=%s unread_score=%.4f",
            request_id,
            len(unread_accounts),
            target.account_id,
            target.display_name,
            float(target.unread_score),
        )
        listen_flow_logger.info(
            "listen_flow account_unread_decision request_id=%s decision=switch_top_unread target_account_id=%s target_display_name=%s unread_account_count=%s unread_score=%.4f",
            request_id,
            target.account_id,
            target.display_name,
            len(unread_accounts),
            float(target.unread_score),
        )
        switch_started_at = time.perf_counter()
        listen_flow_logger.info(
            "listen_flow account_switch_start request_id=%s target_account_id=%s target_display_name=%s",
            request_id,
            target.account_id,
            target.display_name,
        )
        result = self._switch_account_query(target.account_id)
        listen_flow_logger.info(
            "listen_flow account_switch_done request_id=%s ok=%s method=%s detail=%s result_account_id=%s result_display_name=%s switched=%s elapsed_ms=%.1f",
            request_id,
            result.ok,
            result.method,
            result.detail,
            result.account_id,
            result.display_name,
            result.switched,
            _elapsed_ms(switch_started_at),
        )
        if not result.ok:
            return EnsureAccountResult(
                ok=False,
                stage="top_unread_account_select",
                method=result.method,
                detail=result.detail or "account_switch_failed",
                account_id=target.account_id,
                display_name=target.display_name,
                unread_account_count=len(unread_accounts),
            )
        return EnsureAccountResult(
            ok=True,
            stage="top_unread_account_selected",
            method=result.method,
            account_id=result.account_id or target.account_id,
            display_name=result.display_name or target.display_name,
            switched=result.switched,
            unread_account_count=len(unread_accounts),
        )

    def _switch_account_query(self, query: str) -> EnsureAccountResult:
        switcher = getattr(self._accounts, "switch_account", None)
        if not callable(switcher):
            return EnsureAccountResult(ok=False, stage="switch_account", detail="account_switch_unavailable")
        try:
            ok, method = switcher(query)
        except Exception as exc:
            logger.exception("qianniu account switch failed query=%s", query)
            return EnsureAccountResult(ok=False, stage="switch_account", detail=str(exc))
        active = self._selected_account_safely(fresh=True) if ok else None
        if ok:
            self._remember_active_account(active)
            return EnsureAccountResult(
                ok=True,
                stage="switch_account",
                method=method,
                account_id=clean(getattr(active, "account_id", "")) or clean(query),
                display_name=clean(getattr(active, "display_name", "")),
                switched=method != "already_selected",
            )
        return EnsureAccountResult(
            ok=False,
            stage="switch_account",
            method=method,
            detail=method or "account_switch_failed",
            account_id=clean(query),
        )

    def _requested_account_query(self, params: dict[str, Any], *, conversation_key: str = "") -> tuple[str, str]:
        for key in (
            "target_account_id",
            "shop_account_id",
            "target_account_name",
            "shop_account_name",
            "account_display_name",
            "account_name",
        ):
            value = clean(params.get(key))
            if value:
                return value, key

        account_id, _display_name = _conversation_key_parts(conversation_key)
        if account_id:
            return account_id, "conversation_key"

        account_id_param = clean(params.get("account_id"))
        if account_id_param:
            return account_id_param, "account_id"

        payload_account_id = clean(params.get("_payload_account_id"))
        if payload_account_id and payload_account_id.startswith("qnacct_"):
            return payload_account_id, "payload_account_id"

        return "", ""

    def _sync_selected_account_from_tabs(self, *, fresh: bool) -> AccountTab | None:
        accounts = self._list_accounts_safely(fresh=fresh)
        active = self._selected_account_from(accounts)
        self._remember_active_account(active)
        return active

    def _list_accounts_safely(self, *, fresh: bool) -> list[AccountTab]:
        getter = getattr(self._accounts, "list_accounts", None)
        if not callable(getter):
            return []
        try:
            accounts = getter(fresh=fresh)
        except TypeError:
            accounts = getter()
        except Exception:
            logger.exception("qianniu account tab scan failed")
            return []
        self._sync_session_window_from_accounts()
        return list(accounts or [])

    def _selected_account_safely(self, *, fresh: bool) -> AccountTab | None:
        getter = getattr(self._accounts, "selected_account", None)
        if callable(getter):
            try:
                active = getter(fresh=fresh)
            except TypeError:
                active = getter()
            except Exception:
                logger.exception("qianniu selected account lookup failed")
                active = None
            if active is not None:
                return active
        return self._selected_account_from(self._list_accounts_safely(fresh=fresh))

    def _selected_account_from(self, accounts: list[AccountTab]) -> AccountTab | None:
        if not accounts:
            return None
        selected = [item for item in accounts if item.selected]
        if selected:
            return sorted(selected, key=lambda item: (-float(item.confidence), self._account_rect_sort_key(item)))[0]
        return accounts[0]

    def _remember_active_account(self, account: AccountTab | None) -> None:
        if account is None:
            return
        if account.source == "synthetic_single_account":
            self._clear_active_session_context_if_account_changed(DEFAULT_ACCOUNT_ID)
            if not self._account_id:
                self._account_id = DEFAULT_ACCOUNT_ID
            self._account_display_name = ""
            return
        new_account_id = clean(account.account_id) or self._account_id
        self._clear_active_session_context_if_account_changed(new_account_id)
        if clean(account.account_id):
            self._account_id = clean(account.account_id)
        self._account_display_name = clean(account.display_name)

    def _sync_session_window_from_accounts(self) -> None:
        hwnd_getter = getattr(self._accounts, "cached_window_hwnd", None)
        hwnd = 0
        if callable(hwnd_getter):
            try:
                hwnd = int(hwnd_getter() or 0)
            except Exception:
                hwnd = 0
        setter = getattr(self._sessions, "set_window_hwnd", None)
        if hwnd and callable(setter):
            try:
                setter(hwnd)
                listen_flow_logger.info(
                    "listen_flow session_window_synced hwnd=0x%X account_id=%s account_display_name=%s",
                    hwnd,
                    self._account_id,
                    self._account_display_name,
                )
            except Exception:
                logger.exception("qianniu session window hwnd sync failed")

    def _account_rect_sort_key(self, account: AccountTab) -> tuple[int, int, int]:
        rect = account.rect_tuple
        if rect is None:
            return (10**9, 10**9, 10**9)
        left, top, right, bottom = rect
        return (top // 8, left, right - left + bottom - top)

    def _account_result_payload(self, result: EnsureAccountResult) -> dict[str, Any]:
        return {
            "account_stage": result.stage,
            "account_method": result.method,
            "account_id": result.account_id,
            "account_display_name": result.display_name,
            "account_switched": result.switched,
            "account_unread_count": result.unread_account_count,
        }

    def _active_session_context_snapshot(self) -> ActiveConversationContext | None:
        with self._active_context_lock:
            return self._active_conversation_context

    def _remember_active_session(
        self,
        display_name: str,
        *,
        source: str,
        conversation_key: str = "",
        account_id: str = "",
        account_display_name: str = "",
    ) -> None:
        title = clean(display_name)
        if not title or title == "current":
            return
        key_account_id, key_title = _conversation_key_parts(conversation_key)
        if key_title and session_titles_match(key_title, title):
            title = key_title

        resolved_account_id = clean(account_id)
        if not resolved_account_id and key_account_id and key_account_id not in {DEFAULT_ACCOUNT_ID, PLATFORM_QIANNIU}:
            resolved_account_id = key_account_id
        if not resolved_account_id:
            resolved_account_id = clean(self._account_id) or clean(key_account_id) or DEFAULT_ACCOUNT_ID
        if resolved_account_id == PLATFORM_QIANNIU and key_account_id:
            resolved_account_id = key_account_id

        resolved_account_display_name = clean(account_display_name) or self._account_display_name
        resolved_conversation_key = clean(conversation_key) or _conversation_key(resolved_account_id, title)
        context = ActiveConversationContext(
            account_id=resolved_account_id,
            account_display_name=resolved_account_display_name,
            session_title=title,
            conversation_key=resolved_conversation_key,
            updated_at=time.perf_counter(),
            source=clean(source),
        )
        with self._active_context_lock:
            self._active_conversation_context = context
        listen_flow_logger.info(
            "listen_flow active_session_context_update source=%s account_id=%s account_display_name=%s session=%s conversation_key=%s",
            context.source,
            context.account_id,
            context.account_display_name,
            context.session_title,
            context.conversation_key,
        )

    def _clear_active_session_context(self, reason: str) -> None:
        with self._active_context_lock:
            context = self._active_conversation_context
            self._active_conversation_context = None
        if context is not None:
            listen_flow_logger.info(
                "listen_flow active_session_context_clear reason=%s account_id=%s session=%s conversation_key=%s source=%s",
                reason,
                context.account_id,
                context.session_title,
                context.conversation_key,
                context.source,
            )

    def _clear_active_session_context_if_account_changed(self, account_id: str) -> None:
        new_account_id = clean(account_id)
        if not new_account_id:
            return
        context = self._active_session_context_snapshot()
        if context is None:
            return
        if clean(context.account_id) and clean(context.account_id) != new_account_id:
            self._clear_active_session_context("account_changed")
            self._clear_worker_auto_reply_context("account_changed")

    def _account_query_matches_context(self, query: str, context: ActiveConversationContext) -> bool:
        normalized = clean(query)
        if not normalized or normalized in {"current", PLATFORM_QIANNIU}:
            return True
        if normalized == DEFAULT_ACCOUNT_ID:
            return clean(context.account_id) in {"", DEFAULT_ACCOUNT_ID}
        return normalized in {clean(context.account_id), clean(context.account_display_name)}

    def _session_query_matches_context(
        self,
        target: str,
        conversation_key: str,
        context: ActiveConversationContext,
    ) -> bool:
        if clean(conversation_key) and clean(conversation_key) == clean(context.conversation_key):
            return True
        return session_titles_match(context.session_title, target)

    def _resolve_worker_auto_reply_input_field(
        self,
        chat_root: Any | None,
        *,
        task: QianniuConversationTask,
        request_id: str,
        target_title: str,
    ) -> tuple[Any | None, str, float]:
        started_at = time.perf_counter()
        if chat_root is None:
            listen_flow_logger.info(
                "listen_flow worker_auto_reply_input_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_input=False source=no_chat_root elapsed_ms=0.0",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
            )
            return None, "no_chat_root", 0.0

        resolver = getattr(self._sender, "resolve_input_field", None)
        if not callable(resolver):
            resolver = getattr(self._sender, "_resolve_input_field", None)
        if not callable(resolver):
            listen_flow_logger.info(
                "listen_flow worker_auto_reply_input_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_input=False source=resolver_unavailable elapsed_ms=0.0",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
            )
            return None, "resolver_unavailable", 0.0

        try:
            input_field = resolver(chat_root)
        except Exception as exc:
            elapsed_ms = _elapsed_ms(started_at)
            logger.exception(
                "qianniu worker auto reply input cache resolve failed request_id=%s task_id=%s dedupe_key=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
            )
            listen_flow_logger.info(
                "listen_flow worker_auto_reply_input_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_input=False source=exception detail=%s elapsed_ms=%.1f",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
                clean(exc),
                elapsed_ms,
            )
            return None, "exception", elapsed_ms

        elapsed_ms = _elapsed_ms(started_at)
        source = "resolved" if input_field is not None else "not_found"
        listen_flow_logger.info(
            "listen_flow worker_auto_reply_input_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_input=%s source=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            clean(target_title),
            bool(input_field),
            source,
            elapsed_ms,
        )
        return input_field, source, elapsed_ms

    def _resolve_worker_message_web_root(
        self,
        chat_root: Any | None,
        *,
        task: QianniuConversationTask,
        request_id: str,
        target_title: str,
    ) -> tuple[Any | None, str, float]:
        started_at = time.perf_counter()
        if chat_root is None:
            listen_flow_logger.info(
                "listen_flow worker_message_web_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_web=False source=no_chat_root elapsed_ms=0.0",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
            )
            return None, "no_chat_root", 0.0

        resolver = getattr(self._reader, "resolve_message_web", None)
        if not callable(resolver):
            listen_flow_logger.info(
                "listen_flow worker_message_web_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_web=False source=resolver_unavailable elapsed_ms=0.0",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
            )
            return None, "resolver_unavailable", 0.0

        try:
            message_web, reused = resolver(chat_root)
        except Exception as exc:
            elapsed_ms = _elapsed_ms(started_at)
            logger.exception(
                "qianniu worker message web resolve failed request_id=%s task_id=%s dedupe_key=%s",
                request_id,
                task.task_id,
                task.dedupe_key,
            )
            listen_flow_logger.info(
                "listen_flow worker_message_web_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_web=False source=exception detail=%s elapsed_ms=%.1f",
                request_id,
                task.task_id,
                task.dedupe_key,
                clean(target_title),
                clean(exc),
                elapsed_ms,
            )
            return None, "exception", elapsed_ms

        elapsed_ms = _elapsed_ms(started_at)
        source = "cache" if reused and message_web is not None else "resolved" if message_web is not None else "not_found"
        listen_flow_logger.info(
            "listen_flow worker_message_web_cache_update request_id=%s task_id=%s dedupe_key=%s target_title=%s found_web=%s source=%s elapsed_ms=%.1f",
            request_id,
            task.task_id,
            task.dedupe_key,
            clean(target_title),
            bool(message_web),
            source,
            elapsed_ms,
        )
        return message_web, source, elapsed_ms

    def _worker_auto_reply_context_snapshot(self) -> WorkerAutoReplyContext | None:
        with self._active_context_lock:
            context = self._worker_auto_reply_context
        if context is None:
            return None
        age_sec = max(0.0, time.perf_counter() - context.updated_at)
        if age_sec <= WORKER_AUTO_REPLY_CONTEXT_TTL_SEC:
            return context
        self._clear_worker_auto_reply_context("expired")
        return None

    def _remember_worker_auto_reply_context(
        self,
        task: QianniuConversationTask,
        selected_title: str,
        *,
        chat_root: Any | None = None,
        input_field: Any | None = None,
        input_source: str = "",
    ) -> None:
        title = clean(selected_title) or clean(task.session_title)
        if not title:
            return
        context = WorkerAutoReplyContext(
            account_id=clean(self._account_id) or clean(task.account_id) or DEFAULT_ACCOUNT_ID,
            account_display_name=clean(self._account_display_name) or clean(task.account_display_name),
            session_title=title,
            conversation_key=clean(task.conversation_key) or _conversation_key(self._account_id, title),
            task_id=clean(task.task_id),
            dedupe_key=clean(task.dedupe_key),
            updated_at=time.perf_counter(),
            source=clean(task.source),
            chat_root=chat_root,
            input_field=input_field,
            input_source=clean(input_source),
        )
        with self._active_context_lock:
            self._worker_auto_reply_context = context
        listen_flow_logger.info(
            "listen_flow worker_auto_reply_context_update task_id=%s dedupe_key=%s account_id=%s account_display_name=%s session=%s conversation_key=%s chat_root_available=%s input_field_available=%s input_source=%s ttl_ms=%.1f",
            context.task_id,
            context.dedupe_key,
            context.account_id,
            context.account_display_name,
            context.session_title,
            context.conversation_key,
            bool(context.chat_root),
            bool(context.input_field),
            context.input_source,
            WORKER_AUTO_REPLY_CONTEXT_TTL_SEC * 1000.0,
        )

    def _clear_worker_auto_reply_context(self, reason: str) -> None:
        with self._active_context_lock:
            context = self._worker_auto_reply_context
            self._worker_auto_reply_context = None
        if context is not None:
            listen_flow_logger.info(
                "listen_flow worker_auto_reply_context_clear reason=%s task_id=%s dedupe_key=%s account_id=%s session=%s conversation_key=%s",
                reason,
                context.task_id,
                context.dedupe_key,
                context.account_id,
                context.session_title,
                context.conversation_key,
            )
            self._wake_conversation_worker()

    def _worker_auto_reply_context_matches(
        self,
        context: WorkerAutoReplyContext,
        *,
        display_name: str,
        conversation_key: str,
        params: dict[str, Any],
    ) -> bool:
        account_query, _source = self._requested_account_query(params, conversation_key=conversation_key)
        if not self._account_query_matches_context(account_query, context):
            return False
        target = _requested_display_name(display_name, conversation_key)
        if clean(conversation_key) and clean(conversation_key) == clean(context.conversation_key):
            return True
        return session_titles_match(context.session_title, target)

    def _is_auto_reply_send_request(self, params: dict[str, Any]) -> bool:
        source = clean(
            params.get("send_source")
            or params.get("source")
            or params.get("reply_source")
            or params.get("origin")
        ).lower()
        return source in {"auto_reply", "worker_auto_reply", "aggregate_auto_reply"}

    def _account_tab_payload(self, account: AccountTab) -> dict[str, Any]:
        return {
            "account_id": account.account_id,
            "display_name": account.display_name,
            "selected": bool(account.selected),
            "has_unread_hint": bool(account.has_unread_hint),
            "unread_badge_text": account.unread_badge_text,
            "unread_elapsed_text": account.unread_elapsed_text,
            "unread_score": float(account.unread_score),
        }

    def _current_session_chat_root(self) -> Any | None:
        chat_root, _source = self._current_session_chat_root_with_source()
        return chat_root

    def _current_session_chat_root_with_source(self, *, prefer_window_root: bool = False) -> tuple[Any | None, str]:
        getter = getattr(self._sessions, "current_chat_root", None)
        if callable(getter):
            try:
                chat_root = getter()
            except Exception:
                logger.exception("qianniu current chat root cache lookup failed")
                chat_root = None
            if chat_root is not None:
                return chat_root, "session_cache"

        if prefer_window_root:
            root_getter = getattr(self._sessions, "preferred_window_root", None)
            if callable(root_getter):
                try:
                    chat_root = root_getter(update_cache=False)
                except TypeError:
                    chat_root = root_getter()
                except Exception:
                    logger.exception("qianniu preferred window root lookup failed")
                    chat_root = None
                if chat_root is not None:
                    return chat_root, "preferred_window_root"

        preferred_getter = getattr(self._sessions, "chat_root_from_preferred_window", None)
        if callable(preferred_getter):
            try:
                chat_root = preferred_getter(update_cache=not prefer_window_root)
            except TypeError:
                chat_root = preferred_getter()
            except Exception:
                logger.exception("qianniu preferred window chat root lookup failed")
                chat_root = None
            if chat_root is not None:
                return chat_root, "preferred_window"

        if not callable(getter):
            chat_root = getattr(self._sessions, "last_chat_root", None)
            if chat_root is not None:
                return chat_root, "last_chat_root"
        return None, ""

    def _invalidate_account_scoped_caches(self) -> None:
        self._clear_active_session_context("account_scoped_cache_invalidated")
        self._clear_worker_auto_reply_context("account_scoped_cache_invalidated")
        for component in (getattr(self, "_accounts", None), self._reader, self._sender):
            invalidator = getattr(component, "invalidate_cache", None)
            if callable(invalidator):
                invalidator()
        listen_flow_logger.info(
            "listen_flow account_scoped_cache_invalidated preserve_session_structure_cache=True account_id=%s account_display_name=%s",
            self._account_id,
            self._account_display_name,
        )
        self._handled_unread_session_keys.clear()

    def _cached_health_snapshot(self) -> dict[str, Any]:
        with self._health_lock:
            return dict(self._cached_health)

    def _set_cached_health(self, health: dict[str, Any]) -> None:
        snapshot = dict(health or {})
        snapshot.setdefault("healthy", False)
        snapshot.setdefault("reason", "")
        snapshot.setdefault("probe_status", "success")
        snapshot["checked_at"] = _now_iso()
        with self._health_lock:
            self._cached_health = snapshot

    def _refresh_lightweight_health(self, trigger: str) -> dict[str, Any]:
        process_ids: list[int] = []
        process_error = ""
        try:
            process_ids = [int(pid) for pid in self._detector.find_process_ids()]
        except Exception as exc:
            process_error = str(exc)

        observer_thread = self._observer_thread
        observer_running = bool(observer_thread is not None and observer_thread.is_alive())
        worker = self._conversation_worker_snapshot()
        scan_finished_at = float(self._last_scan_finished_at or 0.0)
        scan_age_ms = round(max(0.0, (time.time() - scan_finished_at) * 1000.0), 1) if scan_finished_at else None
        last_scan_finished_at = (
            datetime.fromtimestamp(scan_finished_at, timezone.utc).astimezone().isoformat(timespec="milliseconds")
            if scan_finished_at
            else ""
        )
        process_running = bool(process_ids)
        healthy = bool(
            self._connected
            and process_running
            and (observer_running or bool(worker.get("worker_running")))
            and not process_error
        )
        if not self._connected:
            reason = "adapter disconnected"
        elif process_error:
            reason = f"lightweight health process check failed: {process_error}"
        elif not process_running:
            reason = "qianniu process not found"
        else:
            reason = "listener active; full UIA probe disabled during listening"

        health = {
            "healthy": healthy,
            "reason": reason,
            "probe_status": "lightweight",
            "probe_disabled": True,
            "trigger": clean(trigger),
            "process_running": process_running,
            "process_ids": process_ids,
            "process_error": process_error,
            "connected": self._connected,
            "observer_running": observer_running,
            "worker_running": bool(worker.get("worker_running")),
            "worker_processing": bool(worker.get("worker_processing")),
            "queue_depth": int(worker.get("queue_queued_count", 0) or 0),
            "queue_processing": int(worker.get("queue_processing_count", 0) or 0),
            "worker_auto_reply_pending": bool(worker.get("worker_auto_reply_pending")),
            "last_scan_started_at_epoch": float(self._last_scan_started_at or 0.0),
            "last_scan_finished_at": last_scan_finished_at,
            "last_scan_age_ms": scan_age_ms,
            "last_scan_error": clean(self._last_scan_error),
            "last_account_scan": dict(self._last_account_scan_summary),
        }
        self._set_cached_health(health)
        listen_flow_logger.info(
            "listen_flow health_lightweight_refresh trigger=%s connected=%s healthy=%s observer_running=%s worker_running=%s process_running=%s last_scan_age_ms=%s last_scan_error=%s",
            clean(trigger),
            self._connected,
            healthy,
            observer_running,
            bool(worker.get("worker_running")),
            process_running,
            "" if scan_age_ms is None else scan_age_ms,
            clean(self._last_scan_error),
        )
        return self._cached_health_snapshot()

    def _remember_scan_success(self, result: dict[str, Any], account_result: EnsureAccountResult) -> None:
        self._last_scan_finished_at = time.time()
        self._last_scan_error = ""
        self._last_account_scan_summary = {
            "account_id": clean(getattr(account_result, "account_id", "")),
            "account_display_name": clean(getattr(account_result, "display_name", "")),
            "account_stage": clean(getattr(account_result, "stage", "")),
            "account_method": clean(getattr(account_result, "method", "")),
            "account_count": int(result.get("account_count", 0) or 0),
            "real_account_count": int(result.get("real_account_count", 0) or 0),
            "account_scan_count": int(result.get("account_scan_count", 0) or 0),
            "top_unread_account_count": int(result.get("top_unread_account_count", 0) or 0),
            "unread_count": int(result.get("unread_count", 0) or 0),
            "queued_count": int(result.get("queued_count", 0) or 0),
            "queue_depth": int(result.get("queue_queued_count", 0) or 0),
        }
        self._refresh_lightweight_health("scan_success")

    def _remember_scan_error(self, detail: str) -> None:
        self._last_scan_error = clean(detail)
        self._refresh_lightweight_health("scan_error")

    def _probe(self, *, timeout_sec: float = HEALTH_PROBE_TIMEOUT_SEC, reason: str = "") -> dict[str, Any]:
        started_at = time.perf_counter()
        deadline = started_at + max(0.2, float(timeout_sec))
        timed_out = False
        timeout_stage = ""
        error_stage = ""
        error_detail = ""

        def deadline_reached(stage: str) -> bool:
            nonlocal timed_out, timeout_stage
            if time.perf_counter() < deadline:
                return False
            timed_out = True
            timeout_stage = timeout_stage or stage
            logger.warning(
                "qianniu probe timeout boundary reached reason=%s stage=%s elapsed_ms=%.1f timeout_ms=%.1f",
                reason,
                stage,
                _elapsed_ms(started_at),
                timeout_sec * 1000.0,
            )
            listen_flow_logger.warning(
                "listen_flow health_probe_timeout_boundary reason=%s stage=%s elapsed_ms=%.1f timeout_ms=%.1f",
                reason,
                stage,
                _elapsed_ms(started_at),
                timeout_sec * 1000.0,
            )
            return True

        def run_stage(stage: str, default: Any, func: Any) -> Any:
            nonlocal timed_out, timeout_stage, error_stage, error_detail
            if deadline_reached(stage):
                return default
            stage_started_at = time.perf_counter()
            logger.info(
                "qianniu probe stage start reason=%s stage=%s elapsed_ms=%.1f remaining_ms=%.1f",
                reason,
                stage,
                _elapsed_ms(started_at),
                max(0.0, (deadline - time.perf_counter()) * 1000.0),
            )
            listen_flow_logger.info(
                "listen_flow health_probe_stage_start reason=%s stage=%s elapsed_ms=%.1f remaining_ms=%.1f",
                reason,
                stage,
                _elapsed_ms(started_at),
                max(0.0, (deadline - time.perf_counter()) * 1000.0),
            )
            try:
                value = func()
            except Exception as exc:
                error_stage = stage
                error_detail = str(exc)
                logger.exception(
                    "qianniu probe stage failed reason=%s stage=%s elapsed_ms=%.1f",
                    reason,
                    stage,
                    _elapsed_ms(stage_started_at),
                )
                listen_flow_logger.exception(
                    "listen_flow health_probe_stage_failed reason=%s stage=%s elapsed_ms=%.1f",
                    reason,
                    stage,
                    _elapsed_ms(stage_started_at),
                )
                return default
            elapsed_ms = _elapsed_ms(stage_started_at)
            if elapsed_ms >= PROBE_STAGE_WARN_MS:
                logger.warning(
                    "qianniu probe stage slow reason=%s stage=%s elapsed_ms=%.1f",
                    reason,
                    stage,
                    elapsed_ms,
                )
            else:
                logger.info(
                    "qianniu probe stage done reason=%s stage=%s elapsed_ms=%.1f",
                    reason,
                    stage,
                    elapsed_ms,
                )
            listen_flow_logger.info(
                "listen_flow health_probe_stage_done reason=%s stage=%s elapsed_ms=%.1f slow=%s",
                reason,
                stage,
                elapsed_ms,
                elapsed_ms >= PROBE_STAGE_WARN_MS,
            )
            if time.perf_counter() >= deadline:
                timed_out = True
                timeout_stage = timeout_stage or stage
            return value

        process_ids = run_stage("find_process_ids", [], self._detector.find_process_ids)
        window = run_stage("find_best_window", None, self._detector.find_best_window)
        handle = run_stage("find_current_chat", None, self._detector.find_current_chat)
        chat_root = handle.chat_root if handle else None
        has_message_display = (
            bool(run_stage("find_message_display", None, lambda: self._detector.find_message_display(chat_root)))
            if chat_root and not deadline_reached("find_message_display")
            else False
        )
        has_message_web = (
            bool(run_stage("find_message_web", None, lambda: self._detector.find_message_web(chat_root)))
            if chat_root and not deadline_reached("find_message_web")
            else False
        )
        has_input_field = (
            bool(run_stage("find_input_field", None, lambda: self._detector.find_input_field(chat_root)))
            if chat_root and not deadline_reached("find_input_field")
            else False
        )
        has_send_button = (
            bool(run_stage("find_send_button", None, lambda: self._detector.find_send_button(chat_root)))
            if chat_root and not deadline_reached("find_send_button")
            else False
        )
        healthy = bool(handle and (has_message_display or has_message_web) and has_input_field)
        result_reason = "ok" if healthy else "未找到可用千牛聊天窗口"
        if timed_out and not healthy:
            result_reason = f"千牛 UIA 探测超时: {timeout_stage or 'unknown'}"
        if error_stage and not healthy:
            result_reason = f"千牛 UIA 探测失败: {error_stage}"
        logger.info(
            "qianniu probe done reason=%s healthy=%s status=%s timeout_stage=%s error_stage=%s elapsed_ms=%.1f",
            reason,
            healthy,
            "timeout" if timed_out else ("error" if error_stage else "success"),
            timeout_stage,
            error_stage,
            _elapsed_ms(started_at),
        )
        listen_flow_logger.info(
            "listen_flow health_probe_probe_done reason=%s healthy=%s status=%s timeout_stage=%s error_stage=%s elapsed_ms=%.1f",
            reason,
            healthy,
            "timeout" if timed_out else ("error" if error_stage else "success"),
            timeout_stage,
            error_stage,
            _elapsed_ms(started_at),
        )
        return {
            "healthy": healthy,
            "reason": result_reason,
            "process_ids": process_ids,
            "window_title": clean(getattr(window, "title", "")),
            "window_class_name": clean(getattr(window, "class_name", "")),
            "has_chat_root": bool(chat_root),
            "has_message_display": has_message_display,
            "has_message_web": has_message_web,
            "has_input_field": has_input_field,
            "has_send_button": has_send_button,
            "probe_status": "timeout" if timed_out else ("error" if error_stage else "success"),
            "probe_timeout_stage": timeout_stage,
            "probe_error_stage": error_stage,
            "probe_error": error_detail,
            "probe_elapsed_ms": round(_elapsed_ms(started_at), 1),
            "probe_timeout_ms": int(timeout_sec * 1000),
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
                "account_id": self._account_id,
                "account_display_name": self._account_display_name,
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

    def _message_event(
        self,
        display_name: str,
        item: MessageRecord,
        *,
        sequence_index: int | None = None,
    ) -> dict[str, Any]:
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
                "account_id": self._account_id,
                "account_display_name": self._account_display_name,
                "timestamp": clean(item.timestamp),
                "status": clean(item.status),
                "raw": clean(item.raw),
                "visible_sequence_index": sequence_index,
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
