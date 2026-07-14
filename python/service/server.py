from __future__ import annotations

import argparse
import ctypes
import json
import logging
import os
import sys
import uuid
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit
from typing import Any
import threading
import time

from .ai_suggestion import build_ai_suggestion_response
from .cache_snapshot import build_cache_snapshot, build_conversation_list, build_conversation_messages
from .email_service import get_email_service
from .knowledge_store import get_knowledge_store
from . import rpa_bridge


SERVICE_VERSION = "0.1.0"
KNOWLEDGE_IMPORT_LOG_DIR = Path(__file__).resolve().parents[2] / "python" / "rpa" / "logs" / "knowledge_import"
_WARMUP_STATUS_LOCK = threading.Lock()
_WARMUP_STATUS: dict[str, Any] = {
    "status": "pending",
    "models": [],
    "latency_ms": 0,
    "errors": [],
}
_KNOWLEDGE_IMPORT_TASKS_LOCK = threading.Lock()
_KNOWLEDGE_IMPORT_TASKS: dict[str, dict[str, Any]] = {}


def _configure_logging() -> None:
    handlers: list[logging.Handler] = [logging.StreamHandler(sys.stdout)]
    try:
        KNOWLEDGE_IMPORT_LOG_DIR.mkdir(parents=True, exist_ok=True)
        log_path = KNOWLEDGE_IMPORT_LOG_DIR / f"knowledge_import_{time.strftime('%Y%m%d')}.log"
        handlers.append(logging.FileHandler(log_path, encoding="utf-8"))
    except Exception:
        pass
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s", handlers=handlers)


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


def _safe_list_len(value: Any) -> int:
    return len(value) if isinstance(value, list) else 0


def _set_warmup_status(status: dict[str, Any]) -> None:
    with _WARMUP_STATUS_LOCK:
        _WARMUP_STATUS.clear()
        _WARMUP_STATUS.update(status)


def _get_warmup_status() -> dict[str, Any]:
    with _WARMUP_STATUS_LOCK:
        return dict(_WARMUP_STATUS)


def _start_embedding_warmup() -> None:
    def warmup() -> None:
        _set_warmup_status({"status": "running", "models": [], "latency_ms": 0, "errors": []})
        logging.info("Knowledge embedding warmup started in background")
        try:
            result = get_knowledge_store().warm_up_embeddings()
            _set_warmup_status(result)
            logging.info(
                "Knowledge embedding warmup status=%s models=%s latency_ms=%s errors=%s",
                result.get("status"),
                result.get("models"),
                result.get("latency_ms"),
                result.get("errors"),
            )
        except Exception as exc:  # pragma: no cover - startup diagnostics only
            _set_warmup_status({"status": "error", "models": [], "latency_ms": 0, "errors": [str(exc)]})
            logging.warning("Knowledge embedding warmup failed: %s", exc)

    threading.Thread(target=warmup, name="knowledge-embedding-warmup", daemon=True).start()


def _get_knowledge_import_task(task_id: str) -> dict[str, Any] | None:
    with _KNOWLEDGE_IMPORT_TASKS_LOCK:
        task = _KNOWLEDGE_IMPORT_TASKS.get(task_id)
        return dict(task) if task else None


def _update_knowledge_import_task(task_id: str, **values: Any) -> None:
    with _KNOWLEDGE_IMPORT_TASKS_LOCK:
        task = _KNOWLEDGE_IMPORT_TASKS.get(task_id)
        if not task:
            return
        task.update(values)
        task["updated_at"] = time.time()


def _run_knowledge_import_task(task_id: str, base_id: str, directory: str) -> None:
    started = time.monotonic()
    _update_knowledge_import_task(task_id, status="running", elapsed_ms=0)
    logging.info(
        "Knowledge async import task started task_id=%s directory=%r base_id=%r",
        task_id,
        directory,
        base_id,
    )
    try:
        imported = get_knowledge_store().import_directory_with_assets(base_id, Path(directory))
    except Exception as exc:
        elapsed_ms = int((time.monotonic() - started) * 1000)
        try:
            get_knowledge_store().update_base_import_status(
                base_id,
                status="error",
                error=str(exc),
                elapsed_ms=elapsed_ms,
            )
        except Exception:
            logging.exception("Knowledge async import failed to update base status base_id=%r", base_id)
        logging.exception(
            "Knowledge async import task failed task_id=%s directory=%r base_id=%r elapsed_ms=%d",
            task_id,
            directory,
            base_id,
            elapsed_ms,
        )
        _update_knowledge_import_task(
            task_id,
            status="error",
            error="import_failed",
            detail=str(exc),
            elapsed_ms=elapsed_ms,
        )
        return

    elapsed_ms = int((time.monotonic() - started) * 1000)
    logging.info(
        "Knowledge async import task finished task_id=%s directory=%r base_id=%r documents=%s images=%s elapsed_ms=%d",
        task_id,
        directory,
        base_id,
        _safe_list_len(imported.get("documents")),
        _safe_list_len(imported.get("images")),
        elapsed_ms,
    )
    _update_knowledge_import_task(
        task_id,
        status="success",
        documents=imported["documents"],
        images=imported["images"],
        elapsed_ms=elapsed_ms,
    )


def _start_knowledge_import_task(base_id: str, directory: str) -> str:
    task_id = f"kbi-{uuid.uuid4().hex}"
    now = time.time()
    with _KNOWLEDGE_IMPORT_TASKS_LOCK:
        _KNOWLEDGE_IMPORT_TASKS[task_id] = {
            "task_id": task_id,
            "status": "queued",
            "base_id": base_id,
            "directory": directory,
            "documents": [],
            "images": [],
            "error": "",
            "detail": "",
            "elapsed_ms": 0,
            "created_at": now,
            "updated_at": now,
        }
    threading.Thread(
        target=_run_knowledge_import_task,
        name=f"knowledge-import-{task_id}",
        args=(task_id, base_id, directory),
        daemon=True,
    ).start()
    return task_id


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
                    "warmup": _get_warmup_status(),
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
        if path == "/api/knowledge/bases":
            self._send_json({"status": "success", "bases": get_knowledge_store().list_bases()})
            return
        if path == "/api/knowledge/import_task":
            task_id = query.get("task_id", [""])[0]
            if not task_id:
                self._send_json({"status": "error", "error": "missing_task_id"}, status_code=400)
                return
            task = _get_knowledge_import_task(task_id)
            if not task:
                self._send_json({"status": "error", "error": "task_not_found"}, status_code=404)
                return
            payload = dict(task)
            payload["request_status"] = "success"
            payload["task_status"] = payload.get("status")
            self._send_json(payload)
            return
        if path == "/api/knowledge/documents":
            base_id = query.get("base_id", [""])[0]
            self._send_json(
                {
                    "status": "success",
                    "documents": get_knowledge_store().list_documents(base_id or None),
                }
            )
            return
        if path == "/api/knowledge/images":
            base_id = query.get("base_id", [""])[0]
            self._send_json(
                {
                    "status": "success",
                    "images": get_knowledge_store().list_image_assets(base_id or None),
                }
            )
            return
        if path == "/api/knowledge/platform_bindings":
            platform = query.get("platform", [""])[0].strip().lower()
            if not platform:
                self._send_json({"status": "error", "error": "missing_platform"}, status_code=400)
                return
            self._send_json(
                {
                    "status": "success",
                    "platform": platform,
                    "base_ids": get_knowledge_store().get_platform_bindings(platform),
                }
            )
            return
        if path == "/api/email/config":
            self._send_json({"status": "success", "config": get_email_service().load_config().to_public_dict()})
            return
        if path == "/api/email/templates":
            include_body = query.get("include_body", ["1"])[0].strip().lower() not in {"0", "false", "no"}
            self._send_json(get_email_service().list_templates(include_body=include_body))
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
        if path == "/api/email/config":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().save_config(payload)
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
            return
        if path == "/api/email/test":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().test_send(str(payload.get("to") or ""))
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
            return
        if path == "/api/email/templates":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().save_template(payload)
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
            return
        if path == "/api/email/templates/delete":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().delete_template(str(payload.get("template_id") or ""))
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
            return
        if path == "/api/email/templates/import":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().import_templates(payload)
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
            return
        if path == "/api/email/send":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            result = get_email_service().send(
                to_email=str(payload.get("to") or ""),
                scene=str(payload.get("scene") or "manual"),
                trace_id=str(payload.get("trace_id") or ""),
                conversation_id=payload.get("conversation_id") if isinstance(payload.get("conversation_id"), int) else None,
                template_id=str(payload.get("template_id") or ""),
            )
            self._send_json(result, status_code=200 if result.get("status") == "success" else 400)
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
        if path == "/api/knowledge/bases":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            name = str(payload.get("name") or "").strip()
            if not name:
                self._send_json({"status": "error", "error": "missing_name"}, status_code=400)
                return
            base = get_knowledge_store().create_base(
                name=name,
                description=str(payload.get("description") or ""),
                platform=str(payload.get("platform") or ""),
                shop_id=str(payload.get("shop_id") or ""),
                robot_id=str(payload.get("robot_id") or ""),
                scene=str(payload.get("scene") or "reply_draft"),
                source_directory=str(payload.get("source_directory") or ""),
                applicable_shops=str(payload.get("applicable_shops") or ""),
                enabled=bool(payload.get("enabled", True)),
            )
            self._send_json({"status": "success", "base": base})
            return
        if path == "/api/knowledge/bases/update":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            base_id = str(payload.get("id") or "").strip()
            if not base_id:
                self._send_json({"status": "error", "error": "missing_id"}, status_code=400)
                return
            try:
                base = get_knowledge_store().update_base(
                    base_id,
                    name=str(payload.get("name")) if "name" in payload else None,
                    description=str(payload.get("description")) if "description" in payload else None,
                    source_directory=str(payload.get("source_directory")) if "source_directory" in payload else None,
                    applicable_shops=str(payload.get("applicable_shops")) if "applicable_shops" in payload else None,
                    enabled=bool(payload.get("enabled")) if "enabled" in payload else None,
                    platform=str(payload.get("platform")) if "platform" in payload else None,
                    shop_id=str(payload.get("shop_id")) if "shop_id" in payload else None,
                    robot_id=str(payload.get("robot_id")) if "robot_id" in payload else None,
                    scene=str(payload.get("scene")) if "scene" in payload else None,
                )
            except ValueError as exc:
                self._send_json({"status": "error", "error": "update_failed", "detail": str(exc)}, status_code=400)
                return
            self._send_json({"status": "success", "base": base})
            return
        if path == "/api/knowledge/bases/set_enabled":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            base_id = str(payload.get("id") or "").strip()
            if not base_id:
                self._send_json({"status": "error", "error": "missing_id"}, status_code=400)
                return
            try:
                base = get_knowledge_store().set_base_enabled(base_id, bool(payload.get("enabled")))
            except ValueError as exc:
                self._send_json({"status": "error", "error": "set_enabled_failed", "detail": str(exc)}, status_code=400)
                return
            self._send_json({"status": "success", "base": base})
            return
        if path == "/api/knowledge/bases/delete":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            base_id = str(payload.get("id") or "").strip()
            if not base_id:
                self._send_json({"status": "error", "error": "missing_id"}, status_code=400)
                return
            try:
                base = get_knowledge_store().delete_base(base_id)
            except ValueError as exc:
                self._send_json({"status": "error", "error": "delete_failed", "detail": str(exc)}, status_code=400)
                return
            self._send_json({"status": "success", "base": base})
            return
        if path == "/api/knowledge/platform_bindings":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            platform = str(payload.get("platform") or "").strip().lower()
            if not platform:
                self._send_json({"status": "error", "error": "missing_platform"}, status_code=400)
                return
            base_ids_raw = payload.get("base_ids") or []
            if not isinstance(base_ids_raw, list):
                self._send_json({"status": "error", "error": "invalid_base_ids"}, status_code=400)
                return
            try:
                base_ids = get_knowledge_store().set_platform_bindings(
                    platform,
                    [str(item) for item in base_ids_raw],
                )
            except ValueError as exc:
                self._send_json({"status": "error", "error": "save_bindings_failed", "detail": str(exc)}, status_code=400)
                return
            self._send_json({"status": "success", "platform": platform, "base_ids": base_ids})
            return
        if path == "/api/knowledge/import_directory":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            started = time.monotonic()
            name = str(payload.get("base_name") or "默认知识库")
            base_id = str(payload.get("base_id") or "")
            directory = str(payload.get("directory") or "").strip()
            async_requested = bool(payload.get("async") or payload.get("async_task"))
            logging.info(
                "Knowledge import request received directory=%r base_id=%r base_name=%r shop_id=%r scene=%r async=%s",
                directory,
                base_id,
                name,
                payload.get("shop_id"),
                payload.get("scene"),
                async_requested,
            )
            if not directory:
                logging.warning("Knowledge import rejected: missing directory")
                self._send_json({"status": "error", "error": "missing_directory"}, status_code=400)
                return
            store = get_knowledge_store()
            if not base_id:
                base = store.get_or_create_base(
                    name,
                    description=str(payload.get("description") or ""),
                    platform=str(payload.get("platform") or ""),
                    shop_id=str(payload.get("shop_id") or ""),
                    robot_id=str(payload.get("robot_id") or ""),
                    scene=str(payload.get("scene") or "reply_draft"),
                    source_directory=directory,
                    applicable_shops=str(payload.get("applicable_shops") or ""),
                )
                base_id = str(base["id"])
            else:
                try:
                    store.update_base(
                        base_id,
                        source_directory=directory,
                        applicable_shops=str(payload.get("applicable_shops"))
                        if "applicable_shops" in payload
                        else None,
                    )
                except ValueError as exc:
                    self._send_json({"status": "error", "error": "base_not_found", "detail": str(exc)}, status_code=400)
                    return
            if async_requested:
                task_id = _start_knowledge_import_task(base_id, directory)
                logging.info(
                    "Knowledge async import task accepted task_id=%s directory=%r base_id=%r",
                    task_id,
                    directory,
                    base_id,
                )
                self._send_json(
                    {
                        "status": "success",
                        "task_id": task_id,
                        "task_status": "queued",
                        "base_id": base_id,
                        "documents": [],
                        "images": [],
                    }
                )
                return
            try:
                imported = store.import_directory_with_assets(base_id, Path(directory))
            except Exception as exc:
                try:
                    store.update_base_import_status(base_id, status="error", error=str(exc))
                except Exception:
                    logging.exception("Knowledge import failed to update base status base_id=%r", base_id)
                logging.exception("Knowledge import failed directory=%r base_id=%r", directory, base_id)
                self._send_json({"status": "error", "error": "import_failed", "detail": str(exc)}, status_code=400)
                return
            logging.info(
                "Knowledge import request finished directory=%r base_id=%r documents=%s images=%s elapsed_ms=%d",
                directory,
                base_id,
                _safe_list_len(imported.get("documents")),
                _safe_list_len(imported.get("images")),
                int((time.monotonic() - started) * 1000),
            )
            self._send_json(
                {
                    "status": "success",
                    "base_id": base_id,
                    "documents": imported["documents"],
                    "images": imported["images"],
                }
            )
            return
        if path == "/api/knowledge/search":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            query = str(payload.get("query") or "").strip()
            if not query:
                self._send_json({"status": "error", "error": "missing_query"}, status_code=400)
                return
            base_ids_raw = payload.get("base_ids") or []
            base_ids = [str(item) for item in base_ids_raw] if isinstance(base_ids_raw, list) else []
            self._send_json(
                get_knowledge_store().search(
                    query=query,
                    base_ids=base_ids,
                    platform=str(payload.get("platform") or ""),
                    shop_id=str(payload.get("shop_id") or ""),
                    scene=str(payload.get("scene") or ""),
                    top_k=int(payload.get("top_k") or 5),
                    mode=str(payload.get("mode") or "hybrid"),
                )
            )
            return
        if path == "/api/knowledge/images/search":
            payload = self._read_json_body()
            if payload is None:
                self._send_json({"status": "error", "error": "invalid_json"}, status_code=400)
                return
            query = str(payload.get("query") or "").strip()
            if not query:
                self._send_json({"status": "error", "error": "missing_query"}, status_code=400)
                return
            base_ids_raw = payload.get("base_ids") or []
            base_ids = [str(item) for item in base_ids_raw] if isinstance(base_ids_raw, list) else []
            self._send_json(
                get_knowledge_store().search_image_assets(
                    query=query,
                    base_ids=base_ids,
                    platform=str(payload.get("platform") or ""),
                    shop_id=str(payload.get("shop_id") or ""),
                    scene=str(payload.get("scene") or ""),
                    top_k=int(payload.get("top_k") or 5),
                    mode=str(payload.get("mode") or "hybrid"),
                    include_risky=bool(payload.get("include_risky") or False),
                )
            )
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
        try:
            self.send_response(status_code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError, OSError) as exc:
            logging.info("Client disconnected before response could be sent path=%s error=%s", self.path, exc)


def run(
    host: str,
    port: int,
    mode: str = "debug",
    parent_pid: int | None = None,
    command_ws_host: str = "127.0.0.1",
    command_ws_port: int = 8767,
) -> None:
    _configure_logging()
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
    _start_embedding_warmup()
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
