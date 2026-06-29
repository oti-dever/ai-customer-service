from __future__ import annotations

import argparse
import logging
import queue
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.pdd_web.adapter import DEFAULT_AGENT_PORT, PddWebSidecarAdapter


class ProbeStore:
    def __init__(self, max_events: int = 200) -> None:
        self._next_seq = 1
        self.events: deque[dict[str, Any]] = deque(maxlen=max_events)

    def append(self, event: dict[str, Any]) -> int:
        seq = self._next_seq
        self._next_seq += 1
        stored = dict(event)
        stored["seq"] = seq
        stored["cursor"] = str(seq)
        self.events.append(stored)
        event_type = stored.get("event_type", "")
        platform = stored.get("platform", "")
        conversation = stored.get("conversation_key", "")
        payload = stored.get("payload") if isinstance(stored.get("payload"), dict) else {}
        summary = payload.get("content") or payload.get("display_name") or payload.get("message") or ""
        details: list[str] = []
        if event_type == "message_observed":
            for key in ("content_type", "sender_role", "direction"):
                value = payload.get(key)
                if value:
                    details.append(f"{key}={value}")
        elif event_type == "conversation_observed":
            display_name = payload.get("display_name")
            if display_name:
                details.append(f"display_name={display_name}")
            if "unread_count" in payload:
                details.append(f"unread={payload.get('unread_count')}")
        detail_text = f" {' '.join(details)}" if details else ""
        print(
            f"[event #{seq}] type={event_type} platform={platform} "
            f"conversation={conversation}{detail_text} summary={str(summary)[:100]}",
            flush=True,
        )
        return seq

    def latest_conversation(self) -> tuple[str, str]:
        for event in reversed(self.events):
            payload = event.get("payload") if isinstance(event.get("payload"), dict) else {}
            display_name = str(payload.get("display_name") or "").strip()
            conversation_key = str(event.get("conversation_key") or "").strip()
            if display_name and event.get("event_type") in {"conversation_observed", "message_observed"}:
                return display_name, conversation_key
        return "", ""


def _health_line(adapter: PddWebSidecarAdapter) -> str:
    payload = adapter.health()
    health = payload.get("health") if isinstance(payload.get("health"), dict) else {}
    return (
        f"connected={payload.get('connected')} "
        f"healthy={health.get('healthy')} "
        f"reason={health.get('reason')} "
        f"page_agent={health.get('page_agent_connected')} "
        f"shop={health.get('shop_name') or '-'} "
        f"kefu={health.get('kefu_name') or '-'} "
        f"url={health.get('page_url') or '-'} "
        f"last_seen={health.get('last_seen_at') or '-'}"
    )


def _start_stdin_reader(commands: "queue.Queue[str]") -> None:
    def _read() -> None:
        while True:
            line = sys.stdin.readline()
            if not line:
                return
            commands.put(line.rstrip("\r\n"))

    threading.Thread(target=_read, name="pdd-web-probe-stdin", daemon=True).start()


def _prepare_draft(
    adapter: PddWebSidecarAdapter,
    *,
    conversation_key: str,
    display_name: str,
    text: str,
    source: str,
    prefer_unread: bool = False,
    select_conversation_before_draft: bool = False,
    switch_unread_method: str = "shortcut",
) -> None:
    response = adapter.command(
        {
            "request_id": f"probe-draft-{int(time.time() * 1000)}",
            "platform": "pdd_web",
            "command": "prepare_reply_draft",
            "parameters": {
                "conversation_key": conversation_key,
                "display_name": display_name,
                "text": text,
                "content_type": "text",
                "require_target_verification": True,
                "prefer_unread": prefer_unread,
                "select_conversation_before_draft": select_conversation_before_draft,
                "switch_unread_method": switch_unread_method,
            },
        }
    )
    result = response.get("result") if isinstance(response.get("result"), dict) else response
    print(
        f"[draft:{source}] "
        f"target={display_name or '-'} prepared={result.get('prepared')} "
        f"status={result.get('status')} error={result.get('error') or '-'} "
        f"reason={result.get('reason') or '-'} "
        f"display_name={result.get('display_name') or '-'} "
        f"conversation_key={result.get('conversation_key') or '-'}",
        flush=True,
    )


def _handle_command(line: str, adapter: PddWebSidecarAdapter, store: ProbeStore) -> None:
    command = line.strip()
    if not command:
        return
    if command in {"help", "?"}:
        print(
            "Commands: draft <text> | draft <display_name>|<text> | "
            "draft-click <display_name>|<text> | draft-unread <text> | "
            "draft-unread-click <text> | health | help",
            flush=True,
        )
        return
    if command == "health":
        print(f"[health] {_health_line(adapter)} events={len(store.events)}", flush=True)
        return
    if command.startswith("draft "):
        value = command[len("draft ") :].strip()
        if not value:
            print("[draft] ignored: empty text", flush=True)
            return
        latest_name, latest_key = store.latest_conversation()
        display_name = latest_name
        text = value
        if "|" in value:
            maybe_name, maybe_text = value.split("|", 1)
            display_name = maybe_name.strip() or latest_name
            text = maybe_text.strip()
        if not text:
            print("[draft] ignored: empty text", flush=True)
            return
        if not display_name:
            print("[draft] ignored: no observed conversation yet", flush=True)
            return
        _prepare_draft(adapter, conversation_key=latest_key, display_name=display_name, text=text, source="manual")
        return
    if command.startswith("draft-click "):
        value = command[len("draft-click ") :].strip()
        if "|" not in value:
            print("[draft-click] ignored: expected draft-click <display_name>|<text>", flush=True)
            return
        display_name, text = [part.strip() for part in value.split("|", 1)]
        if not display_name or not text:
            print("[draft-click] ignored: empty display_name or text", flush=True)
            return
        _prepare_draft(
            adapter,
            conversation_key="",
            display_name=display_name,
            text=text,
            source="target-click",
            select_conversation_before_draft=True,
            switch_unread_method="click",
        )
        return
    if command.startswith("draft-unread "):
        text = command[len("draft-unread ") :].strip()
        if not text:
            print("[draft-unread] ignored: empty text", flush=True)
            return
        _prepare_draft(
            adapter,
            conversation_key="",
            display_name="",
            text=text,
            source="unread",
            prefer_unread=True,
            select_conversation_before_draft=True,
            switch_unread_method="shortcut",
        )
        return
    if command.startswith("draft-unread-click "):
        text = command[len("draft-unread-click ") :].strip()
        if not text:
            print("[draft-unread-click] ignored: empty text", flush=True)
            return
        _prepare_draft(
            adapter,
            conversation_key="",
            display_name="",
            text=text,
            source="unread-click",
            prefer_unread=True,
            select_conversation_before_draft=True,
            switch_unread_method="click",
        )
        return
    print(f"[command] unsupported: {command}. Type help for commands.", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe PDD Web browser page-agent events.")
    parser.add_argument("--host", default="127.0.0.1", help="WebSocket listen host.")
    parser.add_argument("--port", type=int, default=DEFAULT_AGENT_PORT, help="WebSocket listen port.")
    parser.add_argument("--interval", type=float, default=1.0, help="Status print interval seconds.")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    parser.add_argument("--draft-text", default="", help="Write this reply draft once after a conversation is observed.")
    parser.add_argument("--draft-target", default="", help="Optional display name for --draft-text target verification.")
    parser.add_argument("--draft-unread-text", default="", help="Select the first unread conversation and write this draft once.")
    parser.add_argument(
        "--draft-unread-method",
        default="shortcut",
        choices=["shortcut", "click"],
        help="Switch method used with --draft-unread-text.",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(message)s",
    )

    store = ProbeStore()
    commands: queue.Queue[str] = queue.Queue()
    _start_stdin_reader(commands)
    adapter = PddWebSidecarAdapter(
        store,
        start_page_agent_server=True,
        agent_host=args.host,
        agent_port=args.port,
    )
    adapter.command({"request_id": "probe-connect", "platform": "pdd_web", "command": "connect"})

    ws_url = f"ws://{args.host}:{args.port}/pdd_web/page_agent"
    extension_dir = REPO_ROOT / "browser_agents" / "pdd_web"
    print("PDD Web page-agent probe started.", flush=True)
    print(f"WebSocket: {ws_url}", flush=True)
    print(f"Load Chrome/Edge extension directory: {extension_dir}", flush=True)
    print("Open a Pinduoduo customer-service page and watch events below.", flush=True)
    if args.draft_text:
        print("[draft:auto] waiting for first observed conversation before writing draft.", flush=True)
    if args.draft_unread_text:
        print(
            f"[draft:unread] waiting for page agent before selecting unread conversation "
            f"method={args.draft_unread_method}.",
            flush=True,
        )
    print("Type 'draft 测试回复' to write a reply draft. It will not click Send.", flush=True)

    auto_draft_sent = False
    unread_draft_sent = False
    try:
        while True:
            while True:
                try:
                    _handle_command(commands.get_nowait(), adapter, store)
                except queue.Empty:
                    break
            if args.draft_text and not auto_draft_sent:
                latest_name, latest_key = store.latest_conversation()
                target_name = args.draft_target.strip() or latest_name
                page_agent_connected = adapter.health().get("health", {}).get("page_agent_connected")
                if args.draft_target.strip() and page_agent_connected:
                    _prepare_draft(
                        adapter,
                        conversation_key="",
                        display_name=target_name,
                        text=args.draft_text,
                        source="auto-target",
                        select_conversation_before_draft=True,
                        switch_unread_method="click",
                    )
                    auto_draft_sent = True
                elif latest_name and latest_key:
                    _prepare_draft(
                        adapter,
                        conversation_key=latest_key if not args.draft_target.strip() else "",
                        display_name=target_name,
                        text=args.draft_text,
                        source="auto",
                        select_conversation_before_draft=bool(args.draft_target.strip()),
                        switch_unread_method="click",
                    )
                    auto_draft_sent = True
            if args.draft_unread_text and not unread_draft_sent and adapter.health().get("health", {}).get("page_agent_connected"):
                _prepare_draft(
                    adapter,
                    conversation_key="",
                    display_name="",
                    text=args.draft_unread_text,
                    source="unread-auto",
                    prefer_unread=True,
                    select_conversation_before_draft=True,
                    switch_unread_method=args.draft_unread_method,
                )
                unread_draft_sent = True
            print(f"[health] {_health_line(adapter)} events={len(store.events)}", flush=True)
            time.sleep(max(0.2, args.interval))
    except KeyboardInterrupt:
        print("Stopping probe.", flush=True)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
