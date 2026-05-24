from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
from typing import Any


PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.wechat_session import find_wechat_window, switch_to_contact_with_strategy
from rpa.common.wechat_uia import (
    activate_wechat_main_window,
    clear_chat_input_via_shortcuts,
    click_send_button,
    collect_visible_session_samples,
    find_chat_input,
    find_send_button,
    find_wechat_main_window_uia,
    focus_chat_input,
    get_current_chat_name,
    is_wechat_uia_supported,
    paste_text_via_clipboard,
    probe_wechat_uia_capabilities,
    send_enter_key,
)
from rpa.readers.wechat_reader_uia import build_visible_messages_payload, load_wechat_config


def _reply(request_id: int, cmd: str, *, ok: bool, data: dict[str, Any] | None = None, error: str = "") -> None:
    payload = {
        "id": request_id,
        "cmd": cmd,
        "ok": ok,
    }
    if ok:
        payload["data"] = data or {}
    else:
        payload["error"] = error or "unknown error"
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def _normalize_text(text: str) -> str:
    return " ".join(str(text or "").split()).strip()


def _uia_limits(cfg: dict[str, Any]) -> tuple[int, int]:
    uia_cfg = cfg.get("uia") or {}
    return int(uia_cfg.get("max_visits", 22000)), int(uia_cfg.get("max_depth", 40))


def _preview_visible_sessions(cfg: dict[str, Any], win: Any | None) -> str:
    if win is None:
        return "<no-window>"
    max_visits, max_depth = _uia_limits(cfg)
    samples = collect_visible_session_samples(
        win,
        max_items=12,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    names = [_normalize_text(sample.name) for sample in samples if _normalize_text(sample.name)]
    return "、".join(names) if names else "<empty>"


def _switch_session_if_needed(cfg: dict[str, Any], session_name: str, *, exact: bool = False) -> str:
    target = _normalize_text(session_name)
    if not target:
        win = find_wechat_main_window_uia()
        return _normalize_text(get_current_chat_name(win)) if win is not None else ""

    hwnd = find_wechat_window(cfg)
    if not hwnd:
        raise RuntimeError("未找到微信窗口")
    win_before = find_wechat_main_window_uia()
    current_before = _normalize_text(get_current_chat_name(win_before)) if win_before is not None else ""
    print(
        f"[WeChatWorkbench] switch_session request target={target!r} exact={exact} "
        f"current_before={current_before!r}"
    )
    print(
        f"[WeChatWorkbench] visible_sessions_before={_preview_visible_sessions(cfg, win_before)}"
    )
    ok = switch_to_contact_with_strategy(
        None,
        hwnd,
        {},
        cfg,
        target,
        exact=exact,
        strategies=["uia"],
        max_retry=1,
    )
    if not ok:
        reason_parts: list[str] = []
        win = find_wechat_main_window_uia()
        if win is None:
            reason_parts.append("UIA 未找到微信主窗口")
        else:
            max_visits, max_depth = _uia_limits(cfg)
            visible_names = [
                _normalize_text(sample.name)
                for sample in collect_visible_session_samples(
                    win,
                    max_items=80,
                    max_visits=max_visits,
                    max_depth=max_depth,
                )
                if _normalize_text(sample.name)
            ]
            if not visible_names:
                reason_parts.append("UIA 未读取到任何可见会话")
            elif target in visible_names:
                reason_parts.append("目标会话在当前可见列表中，但 UIA 点选或切换确认未成功")
            else:
                preview = "、".join(visible_names[:8])
                reason_parts.append("目标会话不在当前可见列表中")
                if preview:
                    reason_parts.append(f"当前可见会话示例：{preview}")
        raise RuntimeError(f"仅 UIA 切换失败：{target}；" + "；".join(reason_parts))
    win = find_wechat_main_window_uia()
    if win is None:
        raise RuntimeError("切换会话后未找到微信主窗口")
    current_after = _normalize_text(get_current_chat_name(win)) or target
    print(
        f"[WeChatWorkbench] switch_session result target={target!r} current_after={current_after!r}"
    )
    print(
        f"[WeChatWorkbench] visible_sessions_after={_preview_visible_sessions(cfg, win)}"
    )
    return current_after


def _send_text_uia(cfg: dict[str, Any], text: str) -> None:
    if not is_wechat_uia_supported():
        raise RuntimeError("UIA 不可用")
    win = find_wechat_main_window_uia()
    if win is None:
        raise RuntimeError("未找到微信主窗口")
    activate_wechat_main_window(win)
    max_visits, max_depth = _uia_limits(cfg)
    input_ctrl = find_chat_input(win, max_visits=max_visits, max_depth=max_depth)
    if input_ctrl is None:
        raise RuntimeError("未找到聊天输入框")
    if not focus_chat_input(input_ctrl):
        raise RuntimeError("无法聚焦聊天输入框")
    clear_chat_input_via_shortcuts()
    paste_text_via_clipboard(text)
    prefer_click = bool((cfg.get("uia") or {}).get("prefer_send_button_click", True))
    if prefer_click:
        send_btn = find_send_button(win, max_visits=max_visits * 2, max_depth=max_depth)
        if send_btn is not None and click_send_button(send_btn):
            return
    send_enter_key()


def handle_probe_status(args: dict[str, Any]) -> dict[str, Any]:
    Q_UNUSED = args
    probe = probe_wechat_uia_capabilities()
    return {
        "probe": {
            "available": probe.available,
            "reason": probe.reason,
            "main_window_found": probe.main_window_found,
            "session_list_found": probe.session_list_found,
            "chat_input_found": probe.chat_input_found,
            "message_list_found": probe.message_list_found,
            "send_button_found": probe.send_button_found,
        },
        "env_hint": probe.env_hint,
    }


def handle_list_sessions(args: dict[str, Any]) -> dict[str, Any]:
    Q_UNUSED = args
    cfg = load_wechat_config()
    probe = probe_wechat_uia_capabilities()
    win = find_wechat_main_window_uia()
    current_chat = _normalize_text(get_current_chat_name(win)) if win is not None else ""
    sessions: list[dict[str, Any]] = []
    if win is not None:
        max_visits, max_depth = _uia_limits(cfg)
        for sample in collect_visible_session_samples(win, max_items=80, max_visits=max_visits, max_depth=max_depth):
            sessions.append(
                {
                    "name": sample.name,
                    "automation_id": sample.automation_id,
                    "class_name": sample.class_name,
                    "rect": sample.rect,
                    "selected": _normalize_text(sample.name) == current_chat,
                }
            )
    return {
        "probe": {
            "available": probe.available,
            "reason": probe.reason,
        },
        "env_hint": probe.env_hint,
        "current_session": current_chat,
        "sessions": sessions,
    }


def handle_switch_session(args: dict[str, Any]) -> dict[str, Any]:
    cfg = load_wechat_config()
    current = _switch_session_if_needed(cfg, args.get("session", ""), exact=True)
    return {
        "current_session": current,
    }


def handle_read_current_messages(args: dict[str, Any]) -> dict[str, Any]:
    cfg = load_wechat_config()
    return build_visible_messages_payload(cfg)


def handle_send_text(args: dict[str, Any]) -> dict[str, Any]:
    cfg = load_wechat_config()
    session = _normalize_text(args.get("session", ""))
    text = str(args.get("text", "") or "")
    if not text.strip():
        raise RuntimeError("发送内容不能为空")
    current = _switch_session_if_needed(cfg, session, exact=True)
    _send_text_uia(cfg, text)
    return {
        "sent": True,
        "current_session": current,
    }


HANDLERS = {
    "probe_status": handle_probe_status,
    "list_sessions": handle_list_sessions,
    "switch_session": handle_switch_session,
    "read_current_messages": handle_read_current_messages,
    "send_text": handle_send_text,
}


def main() -> int:
    sys.stderr.write("[WeChatWorkbench] service ready\n")
    sys.stderr.flush()
    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        request_id = -1
        cmd = ""
        buf = io.StringIO()
        try:
            req = json.loads(line)
            request_id = int(req.get("id", -1))
            cmd = str(req.get("cmd", "") or "")
            args = req.get("args") or {}
            if cmd not in HANDLERS:
                raise RuntimeError(f"unsupported cmd: {cmd}")

            with contextlib.redirect_stdout(buf):
                data = HANDLERS[cmd](args)
            captured = buf.getvalue().strip()
            if captured:
                sys.stderr.write(captured + "\n")
                sys.stderr.flush()
            _reply(request_id, cmd, ok=True, data=data)
        except Exception as exc:
            captured = buf.getvalue().strip()
            if captured:
                sys.stderr.write(captured + "\n")
                sys.stderr.flush()
            _reply(request_id, cmd or "unknown", ok=False, error=str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
