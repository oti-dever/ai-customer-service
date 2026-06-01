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

from rpa.platforms.wechat.adapter import PLATFORM_WECHAT, WechatSidecarAdapter, clean, payload_status


class RpaEventStore:
    def __init__(self, max_events: int = 500) -> None:
        self._lock = threading.Lock()
        self._next_seq = 1
        self._events: deque[tuple[int, dict[str, Any]]] = deque(maxlen=max_events)
        self._listeners: list["_EventPushClient"] = []
        self._listeners_lock = threading.Lock()

    def append(self, event: dict[str, Any]) -> int:
        with self._lock:
            seq = self._next_seq
            self._next_seq += 1
            stored = dict(event)
            stored["seq"] = seq
            stored["cursor"] = str(seq)
            self._events.append((seq, stored))
        self._broadcast(stored)
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
        platform = clean(platform)
        with self._lock:
            selected: list[dict[str, Any]] = []
            last_cursor = since
            for seq, event in self._events:
                if seq <= since:
                    continue
                if platform and event.get("platform") != platform:
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
            "platform": PLATFORM_WECHAT,
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
        self.store = RpaEventStore()
        self._wechat = WechatSidecarAdapter(self.store)
        self._event_client = _EventPushClient()
        self._command_server = _CommandWebSocketServer(self, command_ws_host, command_ws_port)
        self._command_lock = threading.Lock()
        self._mode = clean(mode) or "debug"
        self.store.register_listener(self._event_client)
        self._command_server.start()
        self._event_client.start()

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
        with self._command_lock:
            platform = clean(payload.get("platform") or payload.get("platform_type")) or PLATFORM_WECHAT
            if platform != PLATFORM_WECHAT:
                return payload_status(
                    "error",
                    clean(payload.get("request_id")),
                    error=f"unsupported_platform:{platform}",
                    result={},
                )
            command = clean(payload.get("command") or payload.get("command_type"))
            if self._mode == "formal" and command in {"send_message", "prepare_reply_draft"}:
                return payload_status(
                    "error",
                    clean(payload.get("request_id")),
                    error="send_disabled_in_formal_mode",
                    result={},
                )
            return self._wechat.command(payload)

    def events(self, platform: str, cursor: str | int | None, limit: int) -> dict[str, Any]:
        return self.store.list_after(cursor, platform=platform, limit=limit)

    def health(self, platform: str) -> dict[str, Any]:
        if clean(platform) not in ("", PLATFORM_WECHAT):
            return {"status": "error", "error": f"unsupported_platform:{platform}"}
        payload = self._wechat.health()
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
