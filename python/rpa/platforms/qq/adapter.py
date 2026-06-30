from __future__ import annotations

import hashlib
import threading
import time
from dataclasses import asdict
from datetime import datetime, timezone
from typing import Any, Protocol

from .detector import QQDetector
from .messages import PLATFORM_QQ, QQVisibleMessage, QQVisibleMessageResult, read_visible_messages
from .navigator import QQNavigator
from .qq_logging import get_logger
from .reader import QQConversationItem, QQReader
from .sender import QQSender


logger = get_logger(__name__)

DEFAULT_ACCOUNT_ID = "local_qq"
OBSERVER_POLL_INTERVAL_SEC = 1.8
OBSERVER_AFTER_WORK_SLEEP_SEC = 0.4
OBSERVER_ERROR_SLEEP_SEC = 3.0
OBSERVER_MESSAGE_LIMIT = 40


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
    return f"{PLATFORM_QQ}:{account_id}:{safe_name}"


def _display_name_from_key(value: Any) -> str:
    raw = clean(value)
    if ":" in raw:
        return raw.rsplit(":", 1)[-1].strip()
    if raw.startswith("qq_"):
        return raw[len("qq_") :].strip()
    return raw


def _requested_display_name(display_name: Any, conversation_key: Any) -> str:
    display = clean(display_name)
    key = clean(conversation_key)
    if display and display != "current":
        if display == key or display.startswith("qq:") or display.startswith("qq_"):
            return _display_name_from_key(display)
        return display
    return _display_name_from_key(key)


def _conversation_unread_key(item: QQConversationItem) -> str:
    return clean(item.title).casefold()


def _event_confidence(value: float, *, default: int = 70) -> int:
    if value <= 0:
        return default
    if value <= 1:
        return max(1, min(int(round(value * 100)), 100))
    return max(1, min(int(round(value)), 100))


class QQSidecarAdapter:
    def __init__(self, store: EventSink) -> None:
        self._store = store
        self._connected = False
        self._account_id = DEFAULT_ACCOUNT_ID
        self._detector = QQDetector()
        self._reader = QQReader(self._detector.config)
        self._navigator = QQNavigator(self._detector.config)
        self._sender = QQSender(self._detector.config)
        self._handled_unread_conversation_keys: set[str] = set()
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
            "switch_conversation": self._switch_conversation,
            "prepare_reply_draft": self._prepare_reply_draft,
            "send_message": self._send_message,
        }
        handler = handlers.get(command)
        if handler is None:
            logger.warning("qq command unsupported request_id=%s command=%s", request_id, command)
            return payload_status("error", request_id, error=f"unsupported_command:{command}", result={})
        try:
            logger.info(
                "qq command start request_id=%s command=%s account_id=%s params=%s",
                request_id,
                command,
                self._account_id,
                list(params.keys()),
            )
            result = handler(params, request_id)
            logger.info(
                "qq command done request_id=%s command=%s status=success elapsed_ms=%.1f keys=%s",
                request_id,
                command,
                _elapsed_ms(started_at),
                list(result.keys()) if isinstance(result, dict) else [],
            )
            return payload_status("success", request_id, result=result)
        except Exception as exc:
            logger.exception(
                "qq command failed request_id=%s command=%s elapsed_ms=%.1f",
                request_id,
                command,
                _elapsed_ms(started_at),
            )
            self._store.append(
                self._health_event(
                    healthy=False,
                    status="error",
                    message=str(exc),
                    metadata={"stage": command or "qq", "detail": str(exc)},
                )
            )
            return payload_status("error", request_id, error=str(exc), result={})

    def health(self) -> dict[str, Any]:
        result = self._probe()
        return {
            "status": "success",
            "platform": PLATFORM_QQ,
            "account_id": self._account_id,
            "connected": self._connected,
            "health": result,
        }

    def _connect(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        self._connected = True
        self._handled_unread_conversation_keys.clear()
        poll_interval = float(params.get("poll_interval_sec", OBSERVER_POLL_INTERVAL_SEC) or OBSERVER_POLL_INTERVAL_SEC)
        message_limit = int(params.get("message_limit", OBSERVER_MESSAGE_LIMIT) or OBSERVER_MESSAGE_LIMIT)
        if params.get("quick_connect", False):
            self._start_observer(poll_interval=poll_interval, message_limit=message_limit)
            health = {
                "healthy": True,
                "reason": "observer_started",
                "quick_connect": True,
                "observer_running": bool(self._observer_thread and self._observer_thread.is_alive()),
            }
            self._store.append(
                self._health_event(
                    healthy=True,
                    status="online",
                    message=clean(health.get("reason")),
                    metadata=health,
                )
            )
            return {"connected": True, "health": health}

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
            self._fetch_visible_conversations({"limit": params.get("limit", 30)}, request_id)
            self._fetch_visible_messages(
                {
                    "limit": params.get("message_limit", 30),
                    "include_media": params.get("include_media", True),
                    "capture_evidence": params.get("capture_evidence", False),
                },
                request_id,
            )
        self._start_observer(
            poll_interval=poll_interval,
            message_limit=message_limit,
        )
        return {"connected": True, "health": health}

    def _disconnect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        self._connected = False
        self._stop_observer()
        self._handled_unread_conversation_keys.clear()
        self._store.append(
            self._health_event(
                healthy=False,
                status="offline",
                message="adapter disconnected",
                metadata={"stage": "disconnect"},
            )
        )
        return {"connected": False}

    def _start_observer(self, *, poll_interval: float, message_limit: int) -> None:
        with self._observer_lock:
            if self._observer_thread and self._observer_thread.is_alive():
                return
            self._observer_stop.clear()
            self._observer_thread = threading.Thread(
                target=self._observer_loop,
                kwargs={
                    "poll_interval": max(0.5, min(float(poll_interval), 10.0)),
                    "message_limit": max(1, min(int(message_limit), 120)),
                },
                name="qq-observer",
                daemon=True,
            )
            self._observer_thread.start()
        logger.info("qq observer started poll_interval=%.1fs message_limit=%s", poll_interval, message_limit)

    def _stop_observer(self) -> None:
        with self._observer_lock:
            thread = self._observer_thread
            self._observer_stop.set()
        if thread and thread.is_alive():
            thread.join(timeout=2.0)
        with self._observer_lock:
            if self._observer_thread is thread:
                self._observer_thread = None
        logger.info("qq observer stopped")

    def _observer_loop(self, *, poll_interval: float, message_limit: int) -> None:
        while not self._observer_stop.is_set():
            if not self._connected:
                self._observer_stop.wait(poll_interval)
                continue
            try:
                request_id = f"observer-{int(time.time() * 1000)}"
                result = self._scan_unread_and_fetch(
                    {
                        "message_limit": message_limit,
                        "include_media": True,
                        "capture_evidence": True,
                    },
                    request_id,
                )
                had_work = bool(result.get("message_count") or result.get("conversation_count") or result.get("processed_count"))
                logger.info(
                    "qq observer timing request_id=%s had_work=%s unread=%s conversations=%s messages=%s processed=%s",
                    request_id,
                    had_work,
                    result.get("unread_count"),
                    result.get("conversation_count"),
                    result.get("message_count"),
                    result.get("processed_count"),
                )
                self._observer_stop.wait(OBSERVER_AFTER_WORK_SLEEP_SEC if had_work else poll_interval)
            except Exception as exc:
                logger.exception("qq observer tick failed: %s", exc)
                self._store.append(
                    self._health_event(
                        healthy=False,
                        status="degraded",
                        message=str(exc),
                        metadata={"stage": "qq_observer", "detail": str(exc)},
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

    def _fetch_visible_conversations(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 40) or 40), 120))
        result = self._reader.read_conversations(limit=limit)
        conversations: list[dict[str, Any]] = []
        for item in result.conversations:
            if not clean(item.title):
                continue
            event = self._conversation_event(item, current_title=result.current_title)
            self._store.append(event)
            conversations.append(event)
        logger.info(
            "qq fetch_visible_conversations timing request_id=%s total_ms=%.1f read_ok=%s count=%s emitted=%s",
            request_id,
            _elapsed_ms(started_at),
            result.ok,
            len(result.conversations),
            len(conversations),
        )
        return {
            "count": len(conversations),
            "conversations": conversations,
            "read_result": {
                "ok": result.ok,
                "source": result.source,
                "current_title": result.current_title,
                "detail": result.detail,
            },
        }

    def _fetch_visible_messages(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        started_at = time.perf_counter()
        limit = max(1, min(int(params.get("limit", 40) or 40), 120))
        include_media = bool(params.get("include_media", True))
        capture_evidence = bool(
            params.get("capture_evidence", params.get("evidence", self._detector.config.qq.media_capture_evidence))
        )
        evidence_dir = clean(params.get("evidence_dir")) or None
        requested_name = _requested_display_name(params.get("display_name"), params.get("conversation_key"))
        result = read_visible_messages(
            limit=limit,
            include_media=include_media,
            capture_evidence=capture_evidence,
            evidence_dir=evidence_dir,
            reader=self._reader,
        )
        display_name = requested_name or result.title or "current"
        raw_message_events = [
            self._message_event(display_name, item, sequence_index=sequence_index)
            for sequence_index, item in enumerate(result.messages)
        ]
        filtered_message_events = self._filter_unread_message_events(raw_message_events)
        emitted: list[dict[str, Any]] = []
        for event in filtered_message_events:
            self._store.append(event)
            emitted.append(event)
        logger.info(
            "qq fetch_visible_messages timing request_id=%s total_ms=%.1f read_ok=%s source=%s display_name=%s parsed=%s emitted=%s filtered=%s include_media=%s evidence=%s",
            request_id,
            _elapsed_ms(started_at),
            result.ok,
            result.source,
            display_name,
            len(raw_message_events),
            len(emitted),
            len(raw_message_events) - len(filtered_message_events),
            include_media,
            capture_evidence,
        )
        return {
            "count": len(emitted),
            "messages": emitted,
            "display_name": display_name,
            "read_result": self._visible_message_read_result(result),
        }

    def _scan_unread_and_fetch(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        total_started_at = time.perf_counter()
        conversation_limit = max(1, min(int(params.get("conversation_limit", 40) or 40), 120))
        message_limit = max(1, min(int(params.get("message_limit", params.get("limit", 40)) or 40), 120))
        resolve_current_title = bool(params.get("resolve_title", False))
        conversations_result = self._reader.read_conversations(
            limit=conversation_limit,
            resolve_title=resolve_current_title,
        )
        unread_items = [item for item in conversations_result.conversations if clean(item.unread_hint)]
        unread_keys = {_conversation_unread_key(item) for item in unread_items}
        self._handled_unread_conversation_keys.intersection_update(unread_keys)
        if not unread_items:
            return {
                "unread_count": 0,
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "processed": [],
                "read_result": {
                    "ok": conversations_result.ok,
                    "source": conversations_result.source,
                    "current_title": conversations_result.current_title,
                    "detail": conversations_result.detail,
                },
            }

        current_title = conversations_result.current_title
        target = next((item for item in unread_items if item.is_current_candidate), None)
        prefer_current_unread = bool(params.get("prefer_current_unread", False))
        if target is None and prefer_current_unread and not resolve_current_title:
            current_result = self._reader.read_conversations(limit=conversation_limit, resolve_title=True)
            if current_result.ok and current_result.current_title:
                conversations_result = current_result
                current_title = current_result.current_title
                unread_items = [item for item in current_result.conversations if clean(item.unread_hint)]
                target = next((item for item in unread_items if item.is_current_candidate), None)
        switch_method = "current_unread"
        if target is None:
            target = next(
                (
                    item
                    for item in unread_items
                    if _conversation_unread_key(item) not in self._handled_unread_conversation_keys
                ),
                None,
            )
        if target is None:
            return {
                "unread_count": len(unread_items),
                "conversation_count": 0,
                "message_count": 0,
                "processed_count": 0,
                "skipped_count": len(unread_items),
                "processed": [],
            }

        switch_payload: dict[str, Any] = {}
        if not target.is_current_candidate:
            switch_result = self._navigator.switch_to_conversation_item(
                target,
                current_title=current_title,
                wait_seconds=float(params.get("wait_seconds", 0.6) or 0.6),
                activate=bool(params.get("activate", True)),
                use_uia=bool(params.get("use_uia", True)),
                uia_only=bool(params.get("uia_only", False)),
            )
            switch_payload = asdict(switch_result)
            switch_method = switch_result.method
            if not switch_result.ok:
                return {
                    "unread_count": len(unread_items),
                    "conversation_count": 0,
                    "message_count": 0,
                    "processed_count": 1,
                    "processed": [
                        {
                            "display_name": target.title,
                            "error": switch_result.detail or switch_result.stage,
                            "switch": switch_payload,
                        }
                    ],
                }

        self._handled_unread_conversation_keys.add(_conversation_unread_key(target))
        conversation_event = self._conversation_event(target, current_title=current_title)
        self._store.append(conversation_event)
        messages_result = self._fetch_visible_messages(
                {
                    "limit": message_limit,
                    "display_name": target.title,
                    "include_media": params.get("include_media", True),
                    "capture_evidence": params.get(
                        "capture_evidence",
                        params.get("evidence", self._detector.config.qq.media_capture_evidence),
                    ),
                    "evidence_dir": params.get("evidence_dir"),
                },
                request_id,
        )
        logger.info(
            "qq scan_unread_and_fetch timing request_id=%s total_ms=%.1f unread=%s selected=%s method=%s messages=%s",
            request_id,
            _elapsed_ms(total_started_at),
            len(unread_items),
            target.title,
            switch_method,
            messages_result.get("count"),
        )
        return {
            "unread_count": len(unread_items),
            "conversation_count": 1,
            "message_count": int(messages_result.get("count") or 0),
            "processed_count": 1,
            "processed": [
                {
                    "display_name": target.title,
                    "unread_hint": target.unread_hint,
                    "switch_method": switch_method,
                    "switch": switch_payload,
                    "messages": int(messages_result.get("count") or 0),
                    "read_result": messages_result.get("read_result", {}),
                }
            ],
        }

    def _switch_conversation(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        target = _requested_display_name(params.get("display_name") or params.get("title"), params.get("conversation_key"))
        if not target:
            raise RuntimeError("empty_title")
        result = self._navigator.switch_to_conversation(
            target,
            wait_seconds=float(params.get("wait_seconds", 0.6) or 0.6),
            activate=bool(params.get("activate", True)),
            use_uia=bool(params.get("use_uia", True)),
            uia_only=bool(params.get("uia_only", False)),
        )
        if not result.ok:
            raise RuntimeError(result.detail or result.stage)
        return asdict(result)

    def _prepare_reply_draft(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        text = clean(params.get("text"))
        if not text:
            raise RuntimeError("empty_text")
        display_name = _requested_display_name(params.get("display_name"), params.get("conversation_key")) or "current"
        switch_payload: dict[str, Any] = {}
        if display_name != "current":
            switch_result = self._navigator.switch_to_conversation(
                display_name,
                wait_seconds=float(params.get("wait_seconds", 0.6) or 0.6),
                activate=bool(params.get("switch_activate", True)),
                use_uia=bool(params.get("use_uia_switch", True)),
                uia_only=bool(params.get("uia_only_switch", False)),
            )
            switch_payload = asdict(switch_result)
            if not switch_result.ok:
                raise RuntimeError(switch_result.detail or switch_result.stage)
            display_name = switch_result.after_title or switch_result.matched_title or display_name

        result = self._sender.prepare_reply_draft(
            text,
            activate=bool(params.get("activate", True)),
            use_uia=bool(params.get("use_uia", False)),
            uia_only=bool(params.get("uia_only", False)),
        )
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
                    "stage": result.stage,
                    "switch": switch_payload,
                    "requires_foreground_window": result.method in {"foreground_clipboard_paste", "direct_clipboard_paste"},
                },
            )
        )
        return {
            "prepared": True,
            "task_id": task_id,
            "method": result.method,
            "stage": result.stage,
            "switch": switch_payload,
        }

    def _send_message(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        token = clean(params.get("confirm_token"))
        if token != "manual_confirmed_by_agent":
            raise RuntimeError("send_message_requires_manual_confirm_token")
        content_type = clean(params.get("content_type")) or "text"
        if content_type != "text":
            raise RuntimeError("qq_send_only_supports_text")
        text = clean(params.get("text"))
        if not text:
            raise RuntimeError("empty_text")

        display_name = _requested_display_name(params.get("display_name"), params.get("conversation_key")) or "current"
        switch_payload: dict[str, Any] = {}
        if display_name != "current":
            switch_result = self._navigator.switch_to_conversation(
                display_name,
                wait_seconds=float(params.get("wait_seconds", 0.6) or 0.6),
                activate=bool(params.get("switch_activate", True)),
                use_uia=bool(params.get("use_uia_switch", True)),
                uia_only=bool(params.get("uia_only_switch", False)),
            )
            switch_payload = asdict(switch_result)
            if not switch_result.ok:
                raise RuntimeError(switch_result.detail or switch_result.stage)
            display_name = switch_result.after_title or switch_result.matched_title or display_name

        result = self._sender.send_text(
            text,
            activate=bool(params.get("activate", True)),
            verify=bool(params.get("verify", False)),
            verify_wait_seconds=float(params.get("verify_wait_seconds", 0.8) or 0.8),
        )
        if not result.ok:
            raise RuntimeError(result.detail or result.stage)

        task_id = clean(params.get("task_id")) or clean(params.get("client_message_id")) or request_id
        client_message_id = clean(params.get("client_message_id")) or task_id
        self._store.append(
            self._task_result_event(
                "send_result_observed",
                display_name,
                task_id=task_id,
                status="sent",
                metadata={
                    "method": result.method,
                    "stage": result.stage,
                    "draft_method": result.draft_method,
                    "verified": result.verified,
                    "switch": switch_payload,
                    "client_message_id": client_message_id,
                    "content_type": content_type,
                },
            )
        )
        sent_event = self._event(
            "message_sent",
            _conversation_key(self._account_id, display_name),
            {
                "status": "sent",
                "send_method": result.method,
                "draft_method": result.draft_method,
                "verified": result.verified,
                "client_message_id": client_message_id,
                "content_type": content_type,
                "content": text,
                "metadata": {
                    "switch": switch_payload,
                    "stage": result.stage,
                    "verification_method": "visible_message_read" if result.verified else "action_result",
                },
            },
        )
        sent_event["client_message_id"] = client_message_id
        if task_id:
            sent_event["task_id"] = task_id
        self._store.append(sent_event)
        return {
            "sent": True,
            "task_id": task_id,
            "client_message_id": client_message_id,
            "method": result.method,
            "draft_method": result.draft_method,
            "verified": result.verified,
            "content_type": content_type,
        }

    def _probe(self) -> dict[str, Any]:
        windows = self._detector.find_process_windows()
        process_ids = sorted({window.pid for window in windows})
        window = self._detector.find_best_window()
        handle = self._detector.find_current_chat()
        chat_root = handle.chat_root if handle else None
        title = ""
        has_message_area = False
        has_input_area = False
        if chat_root is not None:
            title_candidates = self._detector.find_title_candidates(chat_root, limit=1)
            title = title_candidates[0][1] if title_candidates else ""
            has_message_area = bool(self._detector.find_message_area_candidates(chat_root))
            has_input_area = bool(self._detector.find_input_area_candidates(chat_root))
        healthy = bool(handle and has_message_area)
        reason = "ok" if healthy else "qq_window_not_found_or_unreadable"
        return {
            "healthy": healthy,
            "reason": reason,
            "process_ids": process_ids,
            "window_title": clean(getattr(getattr(window, "window", None), "title", "")),
            "window_class_name": clean(getattr(getattr(window, "window", None), "class_name", "")),
            "current_title": title,
            "has_chat_root": bool(chat_root),
            "has_message_area": has_message_area,
            "has_input_area": has_input_area,
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
            "media_message_evidence": True,
                "visible_conversation_scan": True,
                "visible_unread_scan": True,
                "switch_conversation": True,
                "fill_draft": True,
                "send_text": True,
                "background_switch": True,
            "background_send": False,
            "media_evidence_default": self._detector.config.qq.media_capture_evidence,
                "requires_foreground_window_for_draft": True,
                "requires_ocr": False,
            },
            "metadata": metadata or {},
        }
        return self._event("account_health_changed", "", payload)

    def _conversation_event(self, item: QQConversationItem, *, current_title: str = "") -> dict[str, Any]:
        payload = {
            "display_name": clean(item.title),
            "unread_count": _unread_count(item.unread_hint),
            "source_type": "ui_observed",
            "confidence": 70,
            "verification_status": "unverified",
            "metadata": {
                "observation_method": "uia",
                "time_text": clean(item.time_text),
                "preview": clean(item.preview),
                "rect": clean(item.rect),
                "unread_hint": clean(item.unread_hint),
                "is_current_candidate": bool(item.is_current_candidate),
                "current_title": clean(current_title),
                "visible_sequence_index": item.index,
            },
        }
        return self._event("conversation_observed", _conversation_key(self._account_id, item.title), payload)

    def _message_event(
        self,
        display_name: str,
        item: QQVisibleMessage,
        *,
        sequence_index: int | None = None,
    ) -> dict[str, Any]:
        direction = _normalize_direction(item.direction)
        sender_role = "customer" if direction == "inbound" else "agent" if direction == "outbound" else "unknown"
        content_type = clean(item.content_type) or "text"
        content = clean(item.text)
        if not content and content_type != "text":
            content = _media_content_text(content_type, item.file_name, item.file_size)
        platform_msg_id = _platform_message_id(
            account_id=self._account_id,
            conversation=_conversation_key(self._account_id, display_name),
            item=item,
            content=content,
            direction=direction,
            sequence_index=sequence_index,
        )
        metadata = {
            "observation_method": "uia",
            "rect": clean(item.rect),
            "media_rect": clean(item.media_rect),
            "file_name": clean(item.file_name),
            "file_size": clean(item.file_size),
            "time_text": clean(item.time_text),
            "visible_sequence_index": sequence_index,
            "raw_metadata": item.raw_metadata,
        }
        payload = {
            "platform_msg_id": platform_msg_id,
            "display_name": display_name,
            "direction": direction,
            "sender_role": sender_role,
            "sender_name": clean(item.sender),
            "content_type": content_type,
            "content": content,
            "content_image_path": clean(item.content_image_path),
            "evidence_ref": clean(item.evidence_ref or item.content_image_path),
            "source_type": "ui_observed",
            "confidence": _event_confidence(item.confidence),
            "verification_status": "unverified",
            "original_timestamp": clean(item.time_text),
            "metadata": metadata,
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
            logger.exception("qq unread message filter failed; emitting parsed messages")
            return events
        return list(filtered)

    def _task_result_event(
        self,
        event_type: str,
        display_name: str,
        *,
        task_id: str,
        status: str,
        metadata: dict[str, Any],
    ) -> dict[str, Any]:
        payload = {
            "status": status,
            "error_message": "",
            "verification_status": "auto_verified" if status == "success" else "unverified",
            "metadata": metadata,
        }
        event = self._event(event_type, _conversation_key(self._account_id, display_name), payload)
        if task_id:
            event["task_id"] = task_id
        return event

    def _event(self, event_type: str, conversation_key: str, payload: dict[str, Any]) -> dict[str, Any]:
        occurred_at = _now_iso()
        raw_id = f"{event_type}|{conversation_key}|{occurred_at}|{payload}"
        event = {
            "event_id": "evt_" + _sha1(raw_id)[:24],
            "event_type": event_type,
            "platform": PLATFORM_QQ,
            "account_id": self._account_id,
            "occurred_at": occurred_at,
            "payload": payload,
            "seq": None,
            "cursor": "",
        }
        if conversation_key:
            event["conversation_key"] = conversation_key
        return event

    def _visible_message_read_result(self, result: QQVisibleMessageResult) -> dict[str, Any]:
        return {
            "ok": result.ok,
            "source": result.source,
            "title": result.title,
            "detail": result.detail,
            "media_count": result.media_count,
        }


def _normalize_direction(value: Any) -> str:
    raw = clean(value).lower()
    if raw in {"out", "outbound", "outgoing", "sent"}:
        return "outbound"
    if raw in {"in", "inbound", "incoming", "received"}:
        return "inbound"
    return "unknown"


def _platform_message_id(
    *,
    account_id: str,
    conversation: str,
    item: QQVisibleMessage,
    content: str,
    direction: str,
    sequence_index: int | None,
) -> str:
    raw_id = clean(item.platform_msg_id)
    if raw_id:
        return raw_id if raw_id.startswith("qq_") else f"qq_{raw_id}"
    fingerprint = "|".join(
        [
            PLATFORM_QQ,
            account_id,
            conversation,
            direction,
            clean(item.content_type) or "text",
            clean(item.time_text),
            clean(item.sender),
            content,
            clean(item.rect),
            str(sequence_index if sequence_index is not None else item.index),
        ]
    )
    return "qq_" + _sha1(fingerprint)[:24]


def _media_content_text(content_type: str, file_name: str, file_size: str) -> str:
    if content_type == "image":
        return "[image]"
    if content_type == "video":
        return f"[video] {clean(file_name)}".strip()
    if content_type == "file":
        name = clean(file_name) or "file"
        size = clean(file_size)
        return f"[file] {name} {size}".strip()
    return f"[{content_type}]"


def _unread_count(value: Any) -> int:
    text = clean(value)
    if text.isdigit():
        return int(text)
    return 1 if text else 0
