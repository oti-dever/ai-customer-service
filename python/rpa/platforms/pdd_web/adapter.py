from __future__ import annotations

import base64
import hashlib
import json
import logging
import mimetypes
import os
import socket
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Protocol

from .image_poc import DEFAULT_MEDIA_DIR, save_data_url
from .parser import conversation_key as page_conversation_key
from .parser import event_from_page_conversation, event_from_page_message
from .pdd_logging import get_logger

PLATFORM_PDD_WEB = "pdd_web"
DEFAULT_ACCOUNT_ID = "local_pdd_web"
DEFAULT_AGENT_HOST = "127.0.0.1"
DEFAULT_AGENT_PORT = 8771
AGENT_COMMAND_TIMEOUT_SEC = 8.0
OBSERVATION_MESSAGE_TYPES = {"conversation_snapshot", "message_snapshot"}
logger = get_logger(__name__)


class EventSink(Protocol):
    def append(self, event: dict[str, Any]) -> int:
        ...


def clean(value: Any) -> str:
    return "" if value is None else str(value).strip()


def image_file_to_data_url(path: str, mime_type: str = "") -> str:
    source = Path(path)
    if not source.exists() or not source.is_file():
        raise FileNotFoundError(path)
    guessed = clean(mime_type) or (mimetypes.guess_type(str(source))[0] or "")
    if not guessed.startswith("image/"):
        guessed = "image/png"
    return f"data:{guessed};base64,{base64.b64encode(source.read_bytes()).decode('ascii')}"


def payload_status(status: str, request_id: str = "", **extra: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "request_id": request_id,
        "status": status,
    }
    payload.update(extra)
    return payload


def _now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


@dataclass
class PageAgentState:
    connected: bool = False
    shop_name: str = ""
    kefu_name: str = ""
    page_url: str = ""
    tab_id: str = ""
    last_seen_at: str = ""


class _PageAgentWebSocketServer:
    def __init__(self, adapter: "PddWebSidecarAdapter", host: str, port: int) -> None:
        self._adapter = adapter
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        self._thread = threading.Thread(target=self._run, name="pdd-web-page-agent-ws", daemon=True)
        self._running = True
        self._clients: set[socket.socket] = set()
        self._clients_lock = threading.Lock()

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        self._close_socket()
        with self._clients_lock:
            clients = list(self._clients)
            self._clients.clear()
        for client in clients:
            try:
                client.close()
            except OSError:
                pass

    def send_json(self, payload: dict[str, Any]) -> bool:
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
        frame = bytes(header) + data
        sent = False
        with self._clients_lock:
            clients = list(self._clients)
        for client in clients:
            try:
                client.sendall(frame)
                sent = True
            except OSError:
                self._drop_client(client)
        return sent

    def _run(self) -> None:
        while self._running:
            try:
                self._serve_once()
            except Exception as exc:
                logger.warning("PDD page-agent WebSocket server error: %s", exc)
                self._close_socket()
                time.sleep(1.0)

    def _serve_once(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self._host, self._port))
        sock.listen(5)
        sock.settimeout(1.0)
        self._sock = sock
        logger.info("PDD page-agent WebSocket listening on ws://%s:%s/pdd_web/page_agent", self._host, self._port)
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
            with self._clients_lock:
                self._clients.add(client)
            logger.info("PDD page-agent connected from %s:%s", addr[0], addr[1])

            client.settimeout(None)
            message_parts: list[bytes] = []
            fragmented_opcode: int | None = None
            while self._running:
                frame = self._recv_frame(client)
                if frame is None:
                    break
                fin, opcode, payload = frame
                if opcode == 0x8:
                    break
                if opcode in {0x9, 0xA}:
                    continue
                if opcode == 0x1:
                    if fin:
                        message_payload = payload
                    else:
                        fragmented_opcode = opcode
                        message_parts = [payload]
                        continue
                elif opcode == 0x0 and fragmented_opcode == 0x1:
                    message_parts.append(payload)
                    if not fin:
                        continue
                    message_payload = b"".join(message_parts)
                    message_parts = []
                    fragmented_opcode = None
                else:
                    continue
                try:
                    message = json.loads(message_payload.decode("utf-8"))
                    response_json = self._adapter.handle_page_agent_message(message)
                except Exception as exc:
                    logger.exception("PDD page-agent message failed")
                    response_json = {"type": "ack", "status": "error", "error": str(exc)}
                if isinstance(response_json, dict) and response_json:
                    self._send_json_to_client(client, response_json)
        except socket.timeout:
            logger.info("PDD page-agent timed out during handshake from %s:%s", addr[0], addr[1])
        except Exception as exc:
            logger.warning("PDD page-agent client error: %s", exc)
        finally:
            self._drop_client(client)
            self._adapter.handle_page_agent_disconnected()

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

    def _recv_frame(self, client: socket.socket) -> tuple[bool, int, bytes] | None:
        head = self._recv_exact(client, 2)
        if not head:
            return None
        first, second = head[0], head[1]
        fin = bool(first & 0x80)
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
        return fin, opcode, payload

    def _recv_exact(self, client: socket.socket, size: int) -> bytes | None:
        data = bytearray()
        while len(data) < size and self._running:
            chunk = client.recv(size - len(data))
            if not chunk:
                return None
            data.extend(chunk)
        return bytes(data)

    def _send_json_to_client(self, client: socket.socket, payload: dict[str, Any]) -> None:
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

    def _drop_client(self, client: socket.socket) -> None:
        with self._clients_lock:
            self._clients.discard(client)
        try:
            client.close()
        except OSError:
            pass

    def _close_socket(self) -> None:
        if self._sock is None:
            return
        try:
            self._sock.close()
        except OSError:
            pass
        self._sock = None


class PddWebSidecarAdapter:
    """Pinduoduo Web adapter skeleton.

    The real DOM reader will arrive through a browser page agent. Until that
    agent is connected, commands return explicit degraded/offline states rather
    than silently pretending to observe the platform.
    """

    def __init__(
        self,
        store: EventSink,
        *,
        start_page_agent_server: bool = True,
        agent_host: str = DEFAULT_AGENT_HOST,
        agent_port: int | None = None,
        media_dir: Path | str = DEFAULT_MEDIA_DIR,
    ) -> None:
        self._store = store
        self._connected = False
        self._account_id = DEFAULT_ACCOUNT_ID
        self._media_dir = Path(media_dir)
        self._page_agent = PageAgentState()
        self._observer_thread = None
        self._seen_message_ids: set[str] = set()
        self._latest_conversations: list[dict[str, Any]] = []
        self._latest_messages: list[dict[str, Any]] = []
        self._pending_drafts: dict[str, tuple[threading.Event, dict[str, Any]]] = {}
        self._lock = threading.Lock()
        self._agent_port = int(agent_port or os.environ.get("YY_PDD_WEB_AGENT_WS_PORT", DEFAULT_AGENT_PORT))
        self._agent_server = _PageAgentWebSocketServer(self, agent_host, self._agent_port) if start_page_agent_server else None
        if self._agent_server is not None:
            self._agent_server.start()

    def command(self, payload: dict[str, Any]) -> dict[str, Any]:
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
            "prepare_reply_draft": self._prepare_reply_draft,
            "send_message": self._send_message,
        }
        handler = handlers.get(command)
        if handler is None:
            return payload_status(
                "error",
                request_id,
                error=f"unsupported_command:{command}",
                result={},
            )
        try:
            result = handler(params, request_id)
            return payload_status("success", request_id, result=result)
        except Exception as exc:
            self._store.append(
                self._health_event(
                    healthy=False,
                    status="error",
                    message=str(exc),
                    metadata={"stage": command or "pdd_web", "detail": str(exc)},
                )
            )
            return payload_status("error", request_id, error=str(exc), result={})

    def health(self) -> dict[str, Any]:
        return {
            "status": "success",
            "platform": PLATFORM_PDD_WEB,
            "account_id": self._account_id,
            "connected": self._connected,
            "health": self._probe(),
        }

    def _connect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        self._connected = True
        self._configure_page_agent_observation(True)
        health = self._probe()
        self._store.append(
            self._health_event(
                healthy=bool(health.get("healthy")),
                status="online" if health.get("healthy") else "degraded",
                message=clean(health.get("reason")),
                metadata=health,
            )
        )
        return {"connected": True, "health": health}

    def _disconnect(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        self._configure_page_agent_observation(False)
        self._connected = False
        self._store.append(
            self._health_event(
                healthy=False,
                status="offline",
                message="adapter disconnected",
                metadata={"stage": "disconnect"},
            )
        )
        return {"connected": False}

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

    def _fetch_visible_conversations(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        if not self._page_agent.connected:
            return self._page_agent_offline_result()
        with self._lock:
            conversations = list(self._latest_conversations)
        return {"count": len(conversations), "conversations": conversations, "source": "page_agent"}

    def _fetch_visible_messages(self, _params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        if not self._page_agent.connected:
            return self._page_agent_offline_result()
        with self._lock:
            messages = list(self._latest_messages)
        return {"count": len(messages), "messages": messages, "source": "page_agent"}

    def _prepare_reply_draft(self, params: dict[str, Any], _request_id: str) -> dict[str, Any]:
        if not self._page_agent.connected:
            return self._page_agent_offline_result()
        content_type = clean(params.get("content_type")) or "text"
        file_path = clean(params.get("file_path") or params.get("image_path"))
        image_data_url = ""
        if content_type == "image":
            if not file_path:
                return {
                    "accepted": False,
                    "prepared": False,
                    "status": "error",
                    "error": "image_file_path_required",
                    "reason": "pdd_web_image_send_requires_local_file_path",
                }
            try:
                image_data_url = image_file_to_data_url(file_path, clean(params.get("mime_type")))
            except Exception as exc:
                logger.warning("PDD image draft data_url build failed file_path=%s error=%s", file_path, exc)
                return {
                    "accepted": False,
                    "prepared": False,
                    "status": "error",
                    "error": "image_file_unavailable",
                    "reason": str(exc),
                }
        request_id = f"draft-{int(time.time() * 1000)}"
        event = threading.Event()
        holder: dict[str, Any] = {}
        with self._lock:
            self._pending_drafts[request_id] = (event, holder)
        payload = {
            "type": "prepare_reply_draft",
            "request_id": request_id,
            "conversation_key": clean(params.get("conversation_key")),
            "display_name": clean(params.get("display_name")),
            "text": clean(params.get("text")),
            "content_type": content_type,
            "file_name": clean(params.get("file_name")) or Path(file_path).name,
            "image_data_url": image_data_url,
            "require_target_verification": bool(params.get("require_target_verification", True)),
            "select_conversation_before_draft": bool(params.get("select_conversation_before_draft", False)),
            "prefer_unread": bool(params.get("prefer_unread", False)),
            "switch_unread_method": clean(params.get("switch_unread_method")) or "click",
            "allow_send_click": False,
            "allow_send_enter": bool(params.get("allow_send_enter", False)),
        }
        sent = bool(self._agent_server and self._agent_server.send_json(payload))
        if not sent:
            with self._lock:
                self._pending_drafts.pop(request_id, None)
            return self._page_agent_offline_result()
        if not event.wait(AGENT_COMMAND_TIMEOUT_SEC):
            with self._lock:
                self._pending_drafts.pop(request_id, None)
            return {
                "accepted": False,
                "prepared": False,
                "status": "timeout",
                "error": "page_agent_draft_timeout",
                "reason": "page_agent_did_not_confirm_draft",
            }
        return dict(holder)

    def _send_message(self, params: dict[str, Any], request_id: str) -> dict[str, Any]:
        token = clean(params.get("confirm_token"))
        if token != "manual_confirmed_by_agent":
            raise RuntimeError("send_message_requires_manual_confirm_token")
        send_params = dict(params)
        send_params["allow_send_enter"] = True
        draft = self._prepare_reply_draft(send_params, request_id)
        if draft.get("sent"):
            client_message_id = clean(params.get("client_message_id")) or request_id
            display_name = (
                clean(draft.get("display_name"))
                or clean(params.get("display_name"))
                or clean(params.get("conversation_key"))
                or "current"
            )
            conversation_key = clean(params.get("conversation_key")) or page_conversation_key(
                self._account_id,
                display_name,
            )
            content_type = clean(params.get("content_type")) or "text"
            text = clean(params.get("text"))
            file_path = clean(params.get("file_path") or params.get("image_path"))
            self._store.append(
                {
                    "event_type": "send_result_observed",
                    "platform": PLATFORM_PDD_WEB,
                    "account_id": self._account_id,
                    "conversation_key": conversation_key,
                    "client_message_id": client_message_id,
                    "payload": {
                        "status": "sent",
                        "send_method": "enter_key",
                        "client_message_id": client_message_id,
                        "display_name": display_name,
                        "content_type": content_type,
                    },
                }
            )
            self._store.append(
                {
                    "event_type": "message_sent",
                    "platform": PLATFORM_PDD_WEB,
                    "account_id": self._account_id,
                    "conversation_key": conversation_key,
                    "client_message_id": client_message_id,
                    "payload": {
                        "status": "sent",
                        "send_method": "enter_key",
                        "client_message_id": client_message_id,
                        "display_name": display_name,
                        "content_type": content_type,
                        "content": text if content_type == "text" else "[image]",
                        "content_image_path": file_path if content_type == "image" else "",
                        "evidence_ref": file_path if content_type == "image" else "",
                    },
                }
            )
            logger.info(
                "PDD send_message sent request_id=%s client_message_id=%s conversation_key=%s display_name=%s content_type=%s",
                request_id,
                client_message_id,
                conversation_key,
                display_name,
                content_type,
            )
            draft.update(
                {
                    "accepted": True,
                    "sent": True,
                    "client_message_id": client_message_id,
                    "method": "enter_key",
                }
            )
            return draft
        draft.update({"accepted": False, "sent": False})
        return draft

    def _probe(self) -> dict[str, Any]:
        if not self._connected:
            return {
                "healthy": False,
                "reason": "adapter_disconnected",
                "stage": "adapter",
                "page_agent_connected": False,
            }
        if not self._page_agent.connected:
            return {
                "healthy": False,
                "reason": "page_agent_offline",
                "stage": "page_agent",
                "page_agent_connected": False,
            }
        return {
            "healthy": True,
            "reason": "page_agent_online",
            "stage": "page_agent",
            "page_agent_connected": True,
            "shop_name": self._page_agent.shop_name,
            "kefu_name": self._page_agent.kefu_name,
            "page_url": self._page_agent.page_url,
            "tab_id": self._page_agent.tab_id,
            "last_seen_at": self._page_agent.last_seen_at,
        }

    def _page_agent_offline_result(self) -> dict[str, Any]:
        return {
            "accepted": False,
            "status": "degraded",
            "error": "page_agent_offline",
            "reason": "pdd_web_page_agent_not_connected",
            "page_agent_connected": False,
            "agent_ws_url": f"ws://{DEFAULT_AGENT_HOST}:{self._agent_port}/pdd_web/page_agent",
        }

    def handle_page_agent_disconnected(self) -> None:
        with self._lock:
            was_connected = self._page_agent.connected
            self._page_agent.connected = False
        if was_connected:
            logger.info("PDD page-agent disconnected")
        if self._connected:
            self._store.append(
                self._health_event(
                    healthy=False,
                    status="degraded",
                    message="page_agent_disconnected",
                    metadata=self._probe(),
                )
            )

    def handle_page_agent_message(self, message: dict[str, Any]) -> dict[str, Any]:
        msg_type = clean(message.get("type"))
        logger.info(
            "PDD page-agent message type=%s keys=%s page_url=%s tab_id=%s",
            msg_type,
            sorted(message.keys()),
            clean(message.get("page_url") or message.get("url")),
            clean(message.get("tab_id")),
        )
        if msg_type in {"hello", "page_ready"}:
            self._mark_page_agent_ready(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type}
        if msg_type == "shop_info":
            self._update_shop_info(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type}
        if msg_type in OBSERVATION_MESSAGE_TYPES and not self._connected:
            logger.info("PDD page-agent observation ignored type=%s reason=adapter_not_listening", msg_type)
            return {
                "type": "ack",
                "status": "ignored",
                "ack_type": msg_type,
                "reason": "adapter_not_listening",
                "count": 0,
            }
        if msg_type == "conversation_snapshot":
            count = self._handle_conversation_snapshot(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type, "count": count}
        if msg_type == "message_snapshot":
            source = clean(message.get("source"))
            if source != "unread_switch":
                logger.info(
                    "PDD message_snapshot ignored source=%s reason=not_from_unread_switch",
                    source or "-",
                )
                return {
                    "type": "ack",
                    "status": "ignored",
                    "ack_type": msg_type,
                    "reason": "message_snapshot_not_from_unread_switch",
                    "count": 0,
                }
            count = self._handle_message_snapshot(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type, "count": count}
        if msg_type == "debug_snapshot":
            self._handle_debug_snapshot(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type}
        if msg_type == "draft_result":
            self._handle_draft_result(message)
            return {"type": "ack", "status": "success", "ack_type": msg_type}
        return {"type": "ack", "status": "error", "error": f"unsupported_page_agent_message:{msg_type}"}

    def _mark_page_agent_ready(self, message: dict[str, Any]) -> None:
        now = _now_iso()
        with self._lock:
            self._page_agent.connected = True
            self._page_agent.page_url = clean(message.get("page_url") or message.get("url")) or self._page_agent.page_url
            self._page_agent.tab_id = clean(message.get("tab_id")) or self._page_agent.tab_id
            self._page_agent.last_seen_at = now
        logger.info(
            "PDD page-agent ready tab_id=%s page_url=%s",
            self._page_agent.tab_id,
            self._page_agent.page_url,
        )
        if self._connected:
            self._configure_page_agent_observation(True)
            self._store.append(
                self._health_event(
                    healthy=True,
                    status="online",
                    message="page_agent_online",
                    metadata=self._probe(),
                )
            )

    def _configure_page_agent_observation(self, enabled: bool) -> None:
        if self._agent_server is None:
            return
        sent = self._agent_server.send_json(
            {
                "type": "configure_observation",
                "enabled": enabled,
                "mode": "unread_then_messages" if enabled else "idle",
            }
        )
        logger.info(
            "PDD page-agent observation configure enabled=%s sent=%s",
            enabled,
            sent,
        )

    def _update_shop_info(self, message: dict[str, Any]) -> None:
        with self._lock:
            self._page_agent.shop_name = clean(message.get("shop_name")) or self._page_agent.shop_name
            self._page_agent.kefu_name = clean(message.get("kefu_name")) or self._page_agent.kefu_name
            self._page_agent.page_url = clean(message.get("page_url") or message.get("url")) or self._page_agent.page_url
            self._page_agent.last_seen_at = _now_iso()
        logger.info(
            "PDD shop_info shop_name=%s kefu_name=%s page_url=%s",
            self._page_agent.shop_name,
            self._page_agent.kefu_name,
            self._page_agent.page_url,
        )

    def _handle_conversation_snapshot(self, message: dict[str, Any]) -> int:
        items = message.get("conversations")
        if not isinstance(items, list):
            items = []
        events: list[dict[str, Any]] = []
        for item in items:
            if not isinstance(item, dict):
                continue
            event = event_from_page_conversation(item, account_id=self._account_id)
            unread_count = (event.get("payload") or {}).get("unread_count")
            try:
                unread_count = int(unread_count or 0)
            except (TypeError, ValueError):
                unread_count = 0
            if unread_count <= 0:
                continue
            events.append(event)
            self._store.append(event)
        with self._lock:
            self._latest_conversations = events
            self._page_agent.last_seen_at = _now_iso()
        logger.info(
            "PDD conversation_snapshot received=%s emitted=%s names=%s",
            len(items),
            len(events),
            [clean((event.get("payload") or {}).get("display_name")) for event in events[:5]],
        )
        return len(events)

    def _handle_message_snapshot(self, message: dict[str, Any]) -> int:
        items = message.get("messages")
        if not isinstance(items, list):
            items = []
        display_name = clean(message.get("display_name") or message.get("conversation_name"))
        image_items = [
            item
            for item in items
            if isinstance(item, dict) and clean(item.get("content_type") or item.get("type")).lower() in {"image", "pic"}
        ]
        events: list[dict[str, Any]] = []
        appended = 0
        for item in items:
            if not isinstance(item, dict):
                continue
            normalized_item = self._persist_message_media(item)
            event = event_from_page_message(normalized_item, account_id=self._account_id, display_name=display_name)
            platform_msg_id = clean(event.get("payload", {}).get("platform_msg_id"))
            events.append(event)
            if platform_msg_id and platform_msg_id in self._seen_message_ids:
                continue
            seq = self._store.append(event)
            if seq and platform_msg_id:
                self._seen_message_ids.add(platform_msg_id)
                appended += 1
        with self._lock:
            self._latest_messages = events
            self._page_agent.last_seen_at = _now_iso()
        logger.info(
            "PDD message_snapshot display_name=%s received=%s converted=%s appended=%s types=%s "
            "image_count=%s image_data_urls=%s image_fetch_errors=%s image_asset_urls=%s",
            display_name,
            len(items),
            len(events),
            appended,
            [clean((event.get("payload") or {}).get("content_type")) for event in events[:8]],
            len(image_items),
            sum(1 for item in image_items if clean(item.get("asset_data_url"))),
            [
                clean(item.get("asset_fetch_error"))[:160]
                for item in image_items
                if clean(item.get("asset_fetch_error"))
            ][:5],
            [
                clean(item.get("asset_url"))[:160]
                for item in image_items
                if clean(item.get("asset_url"))
            ][:5],
        )
        return appended

    def _persist_message_media(self, item: dict[str, Any]) -> dict[str, Any]:
        if clean(item.get("content_type") or item.get("type")).lower() not in {"image", "pic"}:
            return item
        asset_url = clean(item.get("asset_url"))
        data_url = clean(item.get("asset_data_url"))
        if not data_url and asset_url.startswith("data:"):
            data_url = asset_url
        if not data_url:
            return item

        normalized = dict(item)
        normalized.pop("asset_data_url", None)
        prefix_seed = clean(item.get("platform_msg_id")) or clean(item.get("asset_url")) or str(time.time())
        prefix = "pdd_web_image_" + hashlib.sha1(prefix_seed.encode("utf-8", errors="ignore")).hexdigest()[:16]
        try:
            saved = save_data_url(data_url, self._media_dir, prefix=prefix)
        except Exception as exc:  # noqa: BLE001 - adapter should keep message flow alive if media saving fails.
            raw = normalized.get("raw") if isinstance(normalized.get("raw"), dict) else {}
            normalized["raw"] = {
                **raw,
                "asset_save_error": str(exc),
                "asset_url": clean(item.get("asset_url")),
            }
            logger.warning(
                "PDD image asset save failed platform_msg_id=%s asset_url=%s error=%s",
                clean(item.get("platform_msg_id")),
                clean(item.get("asset_url")),
                exc,
            )
            return normalized

        path = clean(saved.get("path"))
        raw = normalized.get("raw") if isinstance(normalized.get("raw"), dict) else {}
        normalized["content_image_path"] = path
        normalized["evidence_ref"] = path
        normalized["raw"] = {
            **raw,
            "asset_url": clean(item.get("asset_url")),
            "asset_source_kind": clean(item.get("asset_source_kind") or item.get("source_kind")),
            "asset_capture_method": clean(item.get("asset_capture_method")) or clean(saved.get("method")),
            "asset_mime_type": clean(saved.get("mime_type")),
            "asset_sha1": clean(saved.get("sha1")),
            "asset_bytes": int(saved.get("bytes") or 0),
        }
        logger.info(
            "PDD image asset saved platform_msg_id=%s path=%s bytes=%s source=%s",
            clean(item.get("platform_msg_id")),
            path,
            saved.get("bytes"),
            clean(item.get("asset_url"))[:160],
        )
        return normalized

    def _handle_debug_snapshot(self, message: dict[str, Any]) -> None:
        conversation_hits = message.get("conversation_selector_hits")
        message_hits = message.get("message_selector_hits")
        logger.info(
            "PDD debug_snapshot frame_url=%s ready_state=%s iframe_count=%s conversation_hits=%s message_hits=%s "
            "panel_hits=%s conversation_samples=%s message_samples=%s panel_samples=%s",
            clean(message.get("frame_url") or message.get("page_url") or message.get("url")),
            clean(message.get("ready_state")),
            message.get("iframe_count", ""),
            conversation_hits if isinstance(conversation_hits, dict) else {},
            message_hits if isinstance(message_hits, dict) else {},
            message.get("panel_selector_hits") if isinstance(message.get("panel_selector_hits"), dict) else {},
            message.get("conversation_selector_samples") if isinstance(message.get("conversation_selector_samples"), dict) else {},
            message.get("message_selector_samples") if isinstance(message.get("message_selector_samples"), dict) else {},
            message.get("panel_selector_samples") if isinstance(message.get("panel_selector_samples"), dict) else {},
        )

    def _handle_draft_result(self, message: dict[str, Any]) -> None:
        request_id = clean(message.get("request_id"))
        with self._lock:
            pending = self._pending_drafts.pop(request_id, None)
        if pending is None:
            logger.info("PDD draft_result ignored request_id=%s reason=no_pending_request", request_id)
            return
        event, holder = pending
        holder.update(
            {
                "accepted": bool(message.get("accepted", True)),
                "prepared": bool(message.get("prepared")),
                "sent": bool(message.get("sent", False)),
                "status": clean(message.get("status")) or ("success" if message.get("prepared") else "error"),
                "error": clean(message.get("error")),
                "reason": clean(message.get("reason")),
                "conversation_key": clean(message.get("conversation_key")),
                "display_name": clean(message.get("display_name")),
            }
        )
        logger.info(
            "PDD draft_result request_id=%s prepared=%s status=%s error=%s reason=%s display_name=%s",
            request_id,
            holder.get("prepared"),
            holder.get("status"),
            holder.get("error"),
            holder.get("reason"),
            holder.get("display_name"),
        )
        event.set()

    def _health_event(
        self,
        *,
        healthy: bool,
        status: str,
        message: str,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        return {
            "event_type": "account_health_changed",
            "platform": PLATFORM_PDD_WEB,
            "account_id": self._account_id,
            "occurred_at": _now_iso(),
            "payload": {
                "healthy": healthy,
                "status": status,
                "message": message,
                "metadata": metadata or {},
            },
        }
