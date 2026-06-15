from __future__ import annotations

import argparse
import ctypes
import json
import logging
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit
from typing import Any
import threading
import time

from .ai_suggestion import build_ai_suggestion_response
from .cache_snapshot import build_cache_snapshot, build_conversation_list, build_conversation_messages
from . import rpa_bridge


SERVICE_VERSION = "0.1.0"


def _is_process_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if sys.platform == "win32":
        process_query_limited_information = 0x1000
        still_active = 259
        handle = ctypes.windll.kernel32.OpenProcess(
            process_query_limited_information,
            False,
            pid,
        )
        if not handle:
            return False
        try:
            exit_code = ctypes.c_ulong()
            if not ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return False
            return exit_code.value == still_active
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _start_parent_watchdog(server: ThreadingHTTPServer, parent_pid: int | None) -> None:
    if not parent_pid:
        return

    def watch() -> None:
        logging.info("Parent process watchdog started for pid=%s", parent_pid)
        while True:
            time.sleep(2.0)
            if _is_process_alive(parent_pid):
                continue
            logging.warning("Parent process pid=%s disappeared; stopping sidecar", parent_pid)
            server.shutdown()
            return

    threading.Thread(target=watch, name="parent-watchdog", daemon=True).start()


class AiServiceHandler(BaseHTTPRequestHandler):
    server_version = "YYAiCustomerService/0.1"

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        if path == "/api/health":
            self._send_json(
                {
                    "healthy": True,
                    "version": SERVICE_VERSION,
                    "service": "python-ai-service",
                }
            )
            return
        if path in {"/api/platforms", "/api/platform/list"}:
            self._send_json(rpa_bridge.get_bridge().platforms())
            return
        if path in {"/api/platform/health", "/api/rpa/health"}:
            platform = query.get("platform", ["wechat"])[0]
            self._send_json(rpa_bridge.get_bridge().health(platform))
            return
        if path in {"/api/platform/events", "/api/rpa/events"}:
            platform = query.get("platform", ["wechat"])[0]
            cursor = query.get("cursor", ["0"])[0]
            limit_raw = query.get("limit", ["50"])[0]
            try:
                limit = int(limit_raw)
            except ValueError:
                limit = 50
            self._send_json(rpa_bridge.get_bridge().events(platform, cursor, limit))
            return
        if path in {"/api/platform/replay", "/api/rpa/replay"}:
            platform = query.get("platform", [""])[0]
            cursor = query.get("cursor", ["0"])[0]
            limit_raw = query.get("limit", ["50"])[0]
            try:
                limit = int(limit_raw)
            except ValueError:
                limit = 50
            self._send_json(rpa_bridge.get_bridge().replay(platform, cursor, limit))
            return
        if path == "/api/cache/snapshot":
            platform = query.get("platform", [""])[0]
            cursor = query.get("cursor", [""])[0]
            conversation_limit = query.get("conversation_limit", ["100"])[0]
            message_limit = query.get("message_limit", ["200"])[0]
            self._send_json(
                build_cache_snapshot(
                    platform=platform,
                    cursor=cursor,
                    conversation_limit=conversation_limit,
                    message_limit=message_limit,
                )
            )
            return
        if path == "/api/conversations/list":
            platform = query.get("platform", [""])[0]
            conversation_limit = query.get("conversation_limit", ["100"])[0]
            self._send_json(
                build_conversation_list(
                    platform=platform,
                    conversation_limit=conversation_limit,
                )
            )
            return
        if path == "/api/conversations/messages":
            platform = query.get("platform", [""])[0]
            conversation_key = query.get("conversation_key", [""])[0]
            message_limit = query.get("message_limit", ["300"])[0]
            self._send_json(
                build_conversation_messages(
                    platform=platform,
                    conversation_key=conversation_key,
                    message_limit=message_limit,
                )
            )
            return
        self._send_json({"status": "error", "error": "not_found"}, status_code=404)

    def do_POST(self) -> None:
        parsed = urlsplit(self.path)
        path = parsed.path

        if path == "/api/ai/suggestion":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            self._send_json(build_ai_suggestion_response(payload))
            return
        if path in {"/api/platform/command", "/api/rpa/command"}:
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            self._send_json(rpa_bridge.get_bridge().command(payload))
            return
        if path == "/api/conversations/clear_messages":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            self._send_json(rpa_bridge.get_bridge().clear_conversation_messages(payload))
            return
        if path == "/api/conversations/delete":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            self._send_json(rpa_bridge.get_bridge().delete_conversation(payload))
            return
        self._send_json({"status": "error", "error": "not_found"}, status_code=404)

    def log_message(self, format: str, *args: Any) -> None:
        logging.info("%s - %s", self.address_string(), format % args)

    def _read_json_body(self) -> dict[str, Any] | None:
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if content_length <= 0:
            return {}

        raw = self.rfile.read(content_length)
        try:
            value = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None
        return value if isinstance(value, dict) else None

    def _send_json(self, payload: dict[str, Any], status_code: int = 200) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def run(
    host: str,
    port: int,
    mode: str = "debug",
    parent_pid: int | None = None,
    command_ws_host: str = "127.0.0.1",
    command_ws_port: int = 8767,
) -> None:
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s")
    rpa_bridge.set_bridge_mode(mode, command_ws_host=command_ws_host, command_ws_port=command_ws_port)
    server = ThreadingHTTPServer((host, port), AiServiceHandler)
    _start_parent_watchdog(server, parent_pid)
    logging.info("Python AI service listening on http://%s:%s mode=%s", host, port, mode)
    logging.info("Python RPA command WebSocket listening on ws://%s:%s", command_ws_host, command_ws_port)
    if parent_pid:
        logging.info("Python AI service managed by parent pid=%s", parent_pid)
    else:
        logging.info("Python AI service running standalone")
    logging.info("Python RPA command WebSocket listening on ws://%s:%s", command_ws_host, command_ws_port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("Python AI service stopped by user")
    finally:
        server.server_close()


def main() -> None:
    parser = argparse.ArgumentParser(description="YY AI customer service Python sidecar")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--mode", choices=["debug", "formal"], default="debug")
    parser.add_argument("--parent-pid", type=int, default=None)
    parser.add_argument("--command-ws-host", default="127.0.0.1")
    parser.add_argument("--command-ws-port", type=int, default=8767)
    args = parser.parse_args()
    parent_pid = args.parent_pid
    if parent_pid is None:
        env_pid = os.environ.get("YY_PARENT_PID")
        if env_pid:
            try:
                parent_pid = int(env_pid)
            except ValueError:
                logging.warning("Ignoring invalid YY_PARENT_PID=%r", env_pid)
    run(
        args.host,
        args.port,
        args.mode,
        parent_pid,
        command_ws_host=args.command_ws_host,
        command_ws_port=args.command_ws_port,
    )


if __name__ == "__main__":
    main()

