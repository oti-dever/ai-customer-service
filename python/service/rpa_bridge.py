from __future__ import annotations

import base64
import logging
import json
import os
import secrets
import threading
import time
from collections import deque
import socket
import hashlib
from typing import Any
from urllib.parse import urlparse

from rpa.platforms.qianniu.adapter import PLATFORM_QIANNIU, QianniuSidecarAdapter
from rpa.platforms.wechat.adapter import PLATFORM_WECHAT, WechatSidecarAdapter, clean, payload_status
from .truth_store import PythonServiceTruthStore


MUTATION_OBSERVATION_QUIET_SECONDS = 3.0


def normalize_platform(value: Any) -> str:
    return clean(value).lower()


class RpaEventStore:
    def __init__(self, max_events: int = 500, truth_store: PythonServiceTruthStore | None = None) -> None:
        self._lock = threading.Lock()
        self._next_seq = 1
        self._events: deque[tuple[int, dict[str, Any]]] = deque(maxlen=max_events)
        self._listeners: list["_EventPushClient"] = []
        self._listeners_lock = threading.Lock()
        self._truth_store = truth_store
        self._last_observed_at_by_platform: dict[str, float] = {}

    def append(self, event: dict[str, Any]) -> int:
        total_started_at = time.perf_counter()
        persist_ms = 0.0
        with self._lock:
            seq = self._next_seq
            self._next_seq += 1
            stored = dict(event)
            stored["seq"] = seq
            stored["cursor"] = str(seq)
            event_type = clean(stored.get("event_type"))
            platform_name = normalize_platform(stored.get("platform"))
            if event_type in {"conversation_observed", "message_observed"} and platform_name:
                self._last_observed_at_by_platform[platform_name] = time.monotonic()
            if self._truth_store is not None:
                try:
                    persist_started_at = time.perf_counter()
                    accepted = self._truth_store.persist_event(stored)
                    persist_ms = (time.perf_counter() - persist_started_at) * 1000.0
                    if accepted is False:
                        logging.info(
                            "rpa_event_store filtered event_id=%s platform=%s event_type=%s seq=%s persist_ms=%.1f",
                            stored.get("event_id", ""),
                            stored.get("platform", ""),
                            stored.get("event_type", ""),
                            seq,
                            persist_ms,
                        )
                        return 0
                    stored["truth_persisted"] = True
                except Exception:
                    stored["truth_persisted"] = False
                    logging.exception("failed to persist rpa event to Python service truth db")
            self._events.append((seq, stored))
        broadcast_started_at = time.perf_counter()
        self._broadcast(stored)
        broadcast_ms = (time.perf_counter() - broadcast_started_at) * 1000.0
        logging.info(
            "rpa_event_store timing event_id=%s platform=%s event_type=%s seq=%s total_ms=%.1f persist_ms=%.1f broadcast_ms=%.1f truth_persisted=%s",
            stored.get("event_id", ""),
            stored.get("platform", ""),
            stored.get("event_type", ""),
            seq,
            (time.perf_counter() - total_started_at) * 1000.0,
            persist_ms,
            broadcast_ms,
            stored.get("truth_persisted"),
        )
        return seq

    def register_listener(self, listener: "_EventPushClient") -> None:
        with self._listeners_lock:
            self._listeners.append(listener)

    def _broadcast(self, event: dict[str, Any]) -> None:
        with self._listeners_lock:
            listeners = list(self._listeners)
        for listener in listeners:
            listener.enqueue(event)

    def list_after(self, cursor: str | int | None, platform: str = "", limit: int = 50) -> dict[str, Any]:
        try:
            since = int(cursor or 0)
        except (TypeError, ValueError):
            since = 0
        limit = max(1, min(int(limit or 50), 200))
        platform = normalize_platform(platform)
        with self._lock:
            selected: list[dict[str, Any]] = []
            last_cursor = since
            for seq, event in self._events:
                if seq <= since:
                    continue
                if platform and normalize_platform(event.get("platform")) != platform:
                    continue
                selected.append(dict(event))
                last_cursor = seq
                if len(selected) >= limit:
                    break
            latest = self._events[-1][0] if self._events else since
        return {
            "status": "success",
            "cursor": str(last_cursor if selected else since),
            "latest_cursor": str(latest),
            "events": selected,
        }

    def seconds_since_observed_event(self, platform: str) -> float | None:
        normalized = normalize_platform(platform)
        if not normalized:
            return None
        with self._lock:
            observed_at = self._last_observed_at_by_platform.get(normalized)
        if observed_at is None:
            return None
        return max(0.0, time.monotonic() - observed_at)

    def filter_observed_message_events(
        self,
        events: list[dict[str, Any]],
        *,
        bootstrap_limit: int = 100,
        incremental_limit: int = 10,
    ) -> list[dict[str, Any]]:
        if self._truth_store is None:
            return list(events)
        return self._truth_store.filter_observed_message_events(
            events,
            bootstrap_limit=bootstrap_limit,
            incremental_limit=incremental_limit,
        )


class _EventPushClient:
    def __init__(self) -> None:
        self._queue: "deque[dict[str, Any]]" = deque()
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._socket: socket.socket | None = None
        self._thread = threading.Thread(target=self._run, name="rpa-event-push", daemon=True)
        self._running = True
        self._endpoint = urlparse(os.environ.get("YY_RPA_EVENT_WS_URL", "ws://127.0.0.1:8766"))
        self._host = self._endpoint.hostname or "127.0.0.1"
        self._port = int(self._endpoint.port or 8766)
        self._path = self._endpoint.path or "/"

    def start(self) -> None:
        self._thread.start()

    def enqueue(self, event: dict[str, Any]) -> None:
        with self._cond:
            self._queue.append(dict(event))
            self._cond.notify()

    def _run(self) -> None:
        while self._running:
            try:
                self._connect()
                self._drain()
            except Exception:
                self._close_socket()
                time.sleep(1.0)

    def _connect(self) -> None:
        self._close_socket()
        sock = socket.create_connection((self._host, self._port), timeout=3.0)
        sock.settimeout(3.0)
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        request = (
            f"GET {self._path} HTTP/1.1\r\n"
            f"Host: {self._host}:{self._port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        sock.sendall(request)
        response = self._recv_http_response(sock)
        status_line = response.split(b"\r\n", 1)[0]
        if b" 101 " not in status_line:
            raise RuntimeError("websocket_handshake_failed")
        self._socket = sock
        self._send_json({
            "type": "hello",
            "platform": "multi",
        })

    def _drain(self) -> None:
        while self._running and self._socket is not None:
            event = None
            with self._cond:
                if self._queue:
                    event = self._queue.popleft()
                else:
                    self._cond.wait(timeout=1.0)
                    continue
            if event is None:
                continue
            self._send_json({"type": "rpa_event", "event": event})

    def _send_json(self, payload: dict[str, Any]) -> None:
        if self._socket is None:
            raise RuntimeError("socket_closed")
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        mask = secrets.token_bytes(4)
        header = bytearray([0x81])
        length = len(data)
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(length.to_bytes(2, "big"))
        else:
            header.append(0x80 | 127)
            header.extend(length.to_bytes(8, "big"))
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self._socket.sendall(bytes(header) + mask + masked)

    def _recv_http_response(self, sock: socket.socket) -> bytes:
        data = bytearray()
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    def _close_socket(self) -> None:
        if self._socket is None:
            return
        try:
            self._socket.close()
        except OSError:
            pass
        self._socket = None


class _CommandWebSocketServer:
    def __init__(self, bridge: "RpaBridge", host: str, port: int) -> None:
        self._bridge = bridge
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        self._thread = threading.Thread(target=self._run, name="rpa-command-ws", daemon=True)
        self._running = True

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        self._close_socket()

    def _run(self) -> None:
        while self._running:
            try:
                self._serve_once()
            except Exception as exc:
                logging.warning("Command WebSocket server error: %s", exc)
                self._close_socket()
                time.sleep(1.0)

    def _serve_once(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self._host, self._port))
        sock.listen(5)
        sock.settimeout(1.0)
        self._sock = sock
        logging.info("Command WebSocket listening on ws://%s:%s", self._host, self._port)
        try:
            while self._running:
                try:
                    client, addr = sock.accept()
                except socket.timeout:
                    continue
                threading.Thread(target=self._handle_client, args=(client, addr), daemon=True).start()
        finally:
            self._close_socket()

    def _handle_client(self, client: socket.socket, addr: tuple[Any, ...]) -> None:
        try:
            client.settimeout(3.0)
            request = self._recv_http_request(client)
            if not request:
                return
            headers = self._parse_headers(request)
            key = headers.get("sec-websocket-key", "")
            if not key:
                return
            accept = base64.b64encode(
                hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
            ).decode("ascii")
            response = (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
            ).encode("ascii")
            client.sendall(response)
            logging.info("Command WebSocket connected from %s:%s", addr[0], addr[1])

            client.settimeout(None)
            while self._running:
                frame = self._recv_frame(client)
                if frame is None:
                    break
                opcode, payload = frame
                if opcode == 0x8:
                    break
                if opcode != 0x1:
                    continue
                try:
                    text = payload.decode("utf-8")
                    request_json = json.loads(text)
                except Exception as exc:
                    self._send_json(client, {
                        "status": "error",
                        "error": f"invalid_json:{exc}",
                        "result": {},
                        "request_id": "",
                    })
                    continue

                request_id = clean(request_json.get("request_id"))
                try:
                    response_json = self._bridge.command(request_json)
                except Exception as exc:
                    logging.exception("Command execution failed request_id=%s", request_id)
                    response_json = payload_status(
                        "error",
                        request_id,
                        error=clean(str(exc)) or "command_failed",
                        result={},
                    )
                self._send_json(client, response_json)
        except socket.timeout:
            logging.info("Command WebSocket client timed out during handshake from %s:%s", addr[0], addr[1])
        except Exception as exc:
            logging.warning("Command WebSocket client error: %s", exc)
        finally:
            try:
                client.close()
            except OSError:
                pass

    def _recv_http_request(self, client: socket.socket) -> bytes:
        data = bytearray()
        while b"\r\n\r\n" not in data and self._running:
            chunk = client.recv(4096)
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    def _parse_headers(self, request: bytes) -> dict[str, str]:
        head = request.split(b"\r\n\r\n", 1)[0].decode("latin1", errors="ignore")
        lines = head.split("\r\n")[1:]
        headers: dict[str, str] = {}
        for line in lines:
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            headers[key.strip().lower()] = value.strip()
        return headers

    def _recv_frame(self, client: socket.socket) -> tuple[int, bytes] | None:
        head = self._recv_exact(client, 2)
        if not head:
            return None
        first, second = head[0], head[1]
        opcode = first & 0x0F
        masked = bool(second & 0x80)
        length = second & 0x7F
        if length == 126:
            ext = self._recv_exact(client, 2)
            if not ext:
                return None
            length = int.from_bytes(ext, "big")
        elif length == 127:
            ext = self._recv_exact(client, 8)
            if not ext:
                return None
            length = int.from_bytes(ext, "big")
        mask = self._recv_exact(client, 4) if masked else b""
        payload = self._recv_exact(client, length)
        if payload is None:
            return None
        if masked and mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return opcode, payload

    def _recv_exact(self, client: socket.socket, size: int) -> bytes | None:
        data = bytearray()
        while len(data) < size and self._running:
            chunk = client.recv(size - len(data))
            if not chunk:
                return None
            data.extend(chunk)
        return bytes(data)

    def _send_json(self, client: socket.socket, payload: dict[str, Any]) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        header = bytearray([0x81])
        length = len(data)
        if length < 126:
            header.append(length)
        elif length < 65536:
            header.append(126)
            header.extend(length.to_bytes(2, "big"))
        else:
            header.append(127)
            header.extend(length.to_bytes(8, "big"))
        client.sendall(bytes(header) + data)

    def _close_socket(self) -> None:
        if self._sock is None:
            return
        try:
            self._sock.close()
        except OSError:
            pass
        self._sock = None


class RpaBridge:
    def __init__(self, mode: str = "debug", command_ws_host: str = "127.0.0.1", command_ws_port: int = 8767) -> None:
        self._truth_store = PythonServiceTruthStore()
        truth_db_path = self._truth_store.ensure_schema()
        logging.info("Python service truth db schema ready: %s", truth_db_path)
        self.store = RpaEventStore(truth_store=self._truth_store)
        self._wechat = WechatSidecarAdapter(self.store)
        self._qianniu = QianniuSidecarAdapter(self.store)
        self._adapters = {
            PLATFORM_WECHAT: self._wechat,
            PLATFORM_QIANNIU: self._qianniu,
        }
        self._event_client = _EventPushClient()
        self._command_server = _CommandWebSocketServer(self, command_ws_host, command_ws_port)
        self._command_lock = threading.Lock()
        self._mode = clean(mode) or "debug"
        self.store.register_listener(self._event_client)
        self._command_server.start()
        self._event_client.start()

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        with self._command_lock:
            platform = normalize_platform(payload.get("platform")) or PLATFORM_WECHAT
            adapter = self._adapters.get(platform)
            if adapter is None:
                return payload_status(
                    "error",
                    clean(payload.get("request_id")),
                    error=f"unsupported_platform:{platform}",
                    result={},
                )
            command = clean(payload.get("command"))
            if self._mode == "formal" and command in {"send_message", "prepare_reply_draft"}:
                return payload_status(
                    "error",
                    clean(payload.get("request_id")),
                    error="send_disabled_in_formal_mode",
                    result={},
                )
            normalized_payload = dict(payload)
            normalized_payload["platform"] = platform
            if command == "send_message":
                self._truth_store.persist_outbound_command(normalized_payload)
                threading.Thread(
                    target=self._run_async_send_message,
                    args=(platform, adapter, normalized_payload),
                    name=f"{platform}-send-message",
                    daemon=True,
                ).start()
                params = normalized_payload.get("parameters")
                if not isinstance(params, dict):
                    params = {}
                client_message_id = clean(
                    normalized_payload.get("client_message_id")
                    or params.get("client_message_id")
                    or normalized_payload.get("task_id")
                    or params.get("task_id")
                    or normalized_payload.get("request_id")
                )
                return payload_status(
                    "success",
                    clean(normalized_payload.get("request_id")),
                    result={
                        "accepted": True,
                        "async": True,
                        "sent": False,
                        "task_id": clean(normalized_payload.get("task_id") or params.get("task_id")),
                        "client_message_id": client_message_id,
                    },
                )
            return adapter.command(normalized_payload)

    def clear_conversation_messages(self, payload: dict[str, Any]) -> dict[str, Any]:
        return self._apply_conversation_mutation(payload, mutation_type="clear_messages")

    def delete_conversation(self, payload: dict[str, Any]) -> dict[str, Any]:
        return self._apply_conversation_mutation(payload, mutation_type="delete_conversation")

    def _apply_conversation_mutation(self, payload: dict[str, Any], *, mutation_type: str) -> dict[str, Any]:
        with self._command_lock:
            platform = normalize_platform(payload.get("platform"))
            params = payload.get("parameters")
            if not isinstance(params, dict):
                params = {}
            conversation_key = clean(
                payload.get("conversation_key")
                or params.get("conversation_key")
                or params.get("display_name")
            )
            if not platform or not conversation_key:
                return {
                    "status": "error",
                    "error": "missing_platform_or_conversation_key",
                    "result": {},
                }

            active = self._mutation_blocker(platform)
            if active:
                return {
                    "status": "error",
                    "error": active["error"],
                    "result": active,
                }

            if mutation_type == "delete_conversation":
                result = self._truth_store.delete_conversation(
                    platform,
                    conversation_key,
                    account_id=clean(payload.get("account_id") or params.get("account_id")),
                    operator=clean(payload.get("operator") or params.get("operator")),
                    reason=clean(payload.get("reason") or params.get("reason")),
                )
            else:
                result = self._truth_store.clear_conversation_messages(
                    platform,
                    conversation_key,
                    account_id=clean(payload.get("account_id") or params.get("account_id")),
                    operator=clean(payload.get("operator") or params.get("operator")),
                    reason=clean(payload.get("reason") or params.get("reason")),
                )

            event = result.get("event")
            if isinstance(event, dict):
                self.store.append(event)
            return {
                "status": result.get("status", "success"),
                "error": result.get("error", ""),
                "result": {key: value for key, value in result.items() if key != "event"},
            }

    def _mutation_blocker(self, platform: str) -> dict[str, Any] | None:
        adapter = self._adapters.get(platform)
        if adapter is None:
            return {"error": f"unsupported_platform:{platform}", "platform": platform}
        connected = bool(getattr(adapter, "_connected", False))
        observer_thread = getattr(adapter, "_observer_thread", None)
        observer_running = bool(observer_thread is not None and observer_thread.is_alive())
        if connected or observer_running:
            return {
                "error": "observer_active",
                "platform": platform,
                "connected": connected,
                "observer_running": observer_running,
                "quiet_seconds_required": MUTATION_OBSERVATION_QUIET_SECONDS,
            }

        elapsed = self.store.seconds_since_observed_event(platform)
        if elapsed is not None and elapsed < MUTATION_OBSERVATION_QUIET_SECONDS:
            return {
                "error": "recent_observation",
                "platform": platform,
                "seconds_since_observed_event": elapsed,
                "quiet_seconds_required": MUTATION_OBSERVATION_QUIET_SECONDS,
            }
        return None

    def _run_async_send_message(self, platform: str, adapter: Any, payload: dict[str, Any]) -> None:
        with self._command_lock:
            request_id = clean(payload.get("request_id"))
            try:
                response = adapter.command(payload)
            except Exception as exc:
                logging.exception("Async send_message raised request_id=%s platform=%s", request_id, platform)
                self._emit_send_failed(payload, str(exc))
                return
            if response.get("status") != "success":
                self._emit_send_failed(payload, clean(response.get("error")) or "send_message_failed")

    def _emit_send_failed(self, payload: dict[str, Any], reason: str) -> None:
        params = payload.get("parameters")
        if not isinstance(params, dict):
            params = {}
        client_message_id = clean(
            payload.get("client_message_id")
            or params.get("client_message_id")
            or payload.get("task_id")
            or params.get("task_id")
            or payload.get("request_id")
        )
        event = {
            "event_type": "send_failed",
            "platform": normalize_platform(payload.get("platform")),
            "account_id": clean(payload.get("account_id")),
            "conversation_key": clean(params.get("conversation_key") or params.get("display_name")),
            "client_message_id": client_message_id,
            "payload": {
                "client_message_id": client_message_id,
                "error_message": clean(reason) or "send_message_failed",
            },
        }
        self.store.append(event)

    def events(self, platform: str, cursor: str | int | None, limit: int) -> dict[str, Any]:
        return self.store.list_after(cursor, platform=normalize_platform(platform), limit=limit)

    def replay(self, platform: str, cursor: str | int | None, limit: int) -> dict[str, Any]:
        return self._truth_store.replay_events(platform=normalize_platform(platform), cursor=cursor, limit=limit)

    def platforms(self) -> dict[str, Any]:
        def adapter_status(platform: str, display_name: str, adapter: Any) -> dict[str, Any]:
            connected = bool(getattr(adapter, "_connected", False))
            observer_thread = getattr(adapter, "_observer_thread", None)
            observer_running = bool(observer_thread is not None and observer_thread.is_alive())
            account_id = clean(getattr(adapter, "_account_id", ""))
            return {
                "platform": platform,
                "display_name": display_name,
                "registered": True,
                "connected": connected,
                "listening": connected and observer_running,
                "observer_running": observer_running,
                "account_id": account_id,
            }

        return {
            "status": "success",
            "mode": self._mode,
            "platforms": [
                adapter_status(PLATFORM_WECHAT, "微信", self._wechat),
                adapter_status(PLATFORM_QIANNIU, "千牛", self._qianniu),
            ],
        }

    def health(self, platform: str) -> dict[str, Any]:
        normalized = normalize_platform(platform) or PLATFORM_WECHAT
        adapter = self._adapters.get(normalized)
        if adapter is None:
            return {"status": "error", "error": f"unsupported_platform:{platform}"}
        payload = adapter.health()
        payload["mode"] = self._mode
        return payload


bridge: RpaBridge | None = None


def set_bridge_mode(mode: str, command_ws_host: str = "127.0.0.1", command_ws_port: int = 8767) -> RpaBridge:
    global bridge
    bridge = RpaBridge(mode=mode, command_ws_host=command_ws_host, command_ws_port=command_ws_port)
    return bridge


def get_bridge() -> RpaBridge:
    global bridge
    if bridge is None:
        bridge = RpaBridge()
    return bridge
