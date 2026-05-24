from __future__ import annotations

import json
from pathlib import Path
import sys
from typing import Any


PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.db_helper import open_db, resolved_default_db_path, write_inbox_batch
from rpa.common.incremental import content_hash, make_platform_msg_id
from rpa.common.wechat_uia import (
    WechatUiaMessageSample,
    collect_chat_layout_samples,
    find_wechat_main_window_uia,
    get_current_chat_name,
    probe_contact_heads_by_hit_test,
    probe_wechat_uia_capabilities,
)


PLATFORM = "wechat_pc_uia"
CONFIG_PATH = Path(__file__).resolve().parents[1] / "config" / "wechat_config.json"


def _normalize_text(text: str) -> str:
    return " ".join(str(text or "").split()).strip()


def load_wechat_config() -> dict[str, Any]:
    if CONFIG_PATH.exists():
        with open(CONFIG_PATH, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _uia_reader_cfg(cfg: dict[str, Any]) -> dict[str, Any]:
    return cfg.get("uia_reader") or {}


def _center_x(item: WechatUiaMessageSample) -> float:
    return (float(item.left) + float(item.right)) / 2.0


def _center_y(item: WechatUiaMessageSample) -> float:
    return (float(item.top) + float(item.bottom)) / 2.0


def _vertical_gap(a: WechatUiaMessageSample, b: WechatUiaMessageSample) -> float:
    if a.bottom < b.top:
        return float(b.top) - float(a.bottom)
    if b.bottom < a.top:
        return float(a.top) - float(b.bottom)
    return 0.0


def _infer_direction_from_heads(
    message: WechatUiaMessageSample,
    heads: list[WechatUiaMessageSample],
    *,
    max_vertical_gap: float,
) -> tuple[str, str, str]:
    best: tuple[tuple[float, float], WechatUiaMessageSample, str] | None = None
    for head in heads:
        vertical_gap = _vertical_gap(message, head)
        msg_height = max(1.0, float(message.bottom) - float(message.top))
        head_height = max(1.0, float(head.bottom) - float(head.top))
        allowed_gap = max(max_vertical_gap, max(msg_height, head_height) * 0.8)
        if vertical_gap > allowed_gap:
            continue

        side = ""
        horizontal_gap = 0.0
        if float(head.right) <= float(message.left):
            side = "incoming"
            horizontal_gap = float(message.left) - float(head.right)
        elif float(head.left) >= float(message.right):
            side = "outgoing"
            horizontal_gap = float(head.left) - float(message.right)
        else:
            continue

        score = (vertical_gap, horizontal_gap)
        if best is None or score < best[0]:
            best = (score, head, side)

    if best is None:
        return "unknown", "", "unmatched"

    _, head, side = best
    return side, _normalize_text(head.name), "avatar_pairing"


def build_visible_messages_payload(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    cfg = cfg or load_wechat_config()
    uia_reader_cfg = _uia_reader_cfg(cfg)
    enabled = bool(uia_reader_cfg.get("enabled", True))
    only_text_messages = bool(uia_reader_cfg.get("only_text_messages", True))
    max_items = max(1, int(uia_reader_cfg.get("max_items", 60) or 60))
    direction_mode = str(uia_reader_cfg.get("direction_mode", "avatar_pairing") or "avatar_pairing")
    max_vertical_gap = float(uia_reader_cfg.get("avatar_pairing_max_vertical_gap", 48) or 48)
    enable_hit_test = bool(uia_reader_cfg.get("avatar_hit_test_enabled", True))
    probe = probe_wechat_uia_capabilities()
    payload: dict[str, Any] = {
        "probe": {
            "available": probe.available,
            "reason": probe.reason,
            "main_window_found": probe.main_window_found,
            "session_list_found": probe.session_list_found,
            "chat_input_found": probe.chat_input_found,
            "message_list_found": probe.message_list_found,
            "send_button_found": probe.send_button_found,
        },
        "conversation": {
            "name": "",
            "platform_conversation_id": "",
        },
        "messages": [],
        "write_mode": {
            "write_inbox": bool(uia_reader_cfg.get("write_inbox", False)),
            "assume_all_text_as_incoming": bool(
                uia_reader_cfg.get("assume_all_text_as_incoming", False)
            ),
        },
        "direction_mode": direction_mode,
        "summary": {
            "count": 0,
            "text_count": 0,
            "filtered_out_count": 0,
            "avatar_count": 0,
            "hit_test_avatar_count": 0,
            "incoming_count": 0,
            "outgoing_count": 0,
            "unknown_count": 0,
        },
    }
    if not enabled:
        payload["probe"]["reason"] = "uia_reader 已禁用"
        return payload
    if not probe.main_window_found:
        return payload

    win = find_wechat_main_window_uia()
    if win is None:
        return payload

    contact_name = _normalize_text(get_current_chat_name(win))
    conv_id = f"wechat_uia_{contact_name}" if contact_name else "wechat_uia_unknown"
    payload["conversation"]["name"] = contact_name
    payload["conversation"]["platform_conversation_id"] = conv_id

    samples = collect_chat_layout_samples(win, max_items=max_items)
    head_samples = [item for item in samples if item.kind == "contact_head"]
    payload["summary"]["avatar_count"] = len(head_samples)
    messages: list[dict[str, Any]] = []
    filtered_out_count = 0
    hit_test_avatar_count = 0
    for item in samples:
        if item.kind == "contact_head":
            continue
        normalized = _normalize_text(item.name)
        if only_text_messages and item.kind != "text":
            filtered_out_count += 1
            continue
        side = "unknown"
        sender_name = ""
        direction_source = "disabled"
        if direction_mode == "avatar_pairing":
            candidate_heads = head_samples
            if enable_hit_test:
                local_heads = probe_contact_heads_by_hit_test(item)
                if local_heads:
                    hit_test_avatar_count += len(local_heads)
                    candidate_heads = local_heads + head_samples
            side, sender_name, direction_source = _infer_direction_from_heads(
                item,
                candidate_heads,
                max_vertical_gap=max_vertical_gap,
            )
        digest = content_hash(side, normalized)
        messages.append(
            {
                "seq": len(messages),
                "kind": "text" if item.kind == "text" else item.kind,
                "content": normalized,
                "side": side,
                "sender_name": sender_name,
                "original_timestamp": "",
                "class_name": item.class_name,
                "automation_id": item.automation_id,
                "control_type": item.control_type,
                "rect": item.rect,
                "bbox": {
                    "left": item.left,
                    "top": item.top,
                    "right": item.right,
                    "bottom": item.bottom,
                },
                "direction_source": direction_source,
                "content_hash": digest,
                "platform_msg_id": make_platform_msg_id(PLATFORM, conv_id, digest),
            }
        )
    payload["messages"] = messages
    payload["summary"]["count"] = len(messages)
    payload["summary"]["text_count"] = sum(1 for item in messages if item["kind"] == "text")
    payload["summary"]["filtered_out_count"] = filtered_out_count
    payload["summary"]["hit_test_avatar_count"] = hit_test_avatar_count
    payload["summary"]["incoming_count"] = sum(1 for item in messages if item["side"] == "incoming")
    payload["summary"]["outgoing_count"] = sum(1 for item in messages if item["side"] == "outgoing")
    payload["summary"]["unknown_count"] = sum(1 for item in messages if item["side"] == "unknown")
    return payload


def maybe_write_visible_messages_to_inbox(
    payload: dict[str, Any],
    cfg: dict[str, Any] | None = None,
    db_path: Path | None = None,
) -> tuple[int, str]:
    cfg = cfg or load_wechat_config()
    reader_cfg = _uia_reader_cfg(cfg)
    if not bool(reader_cfg.get("write_inbox", False)):
        return 0, "safe_mode_output_only"
    if not bool(reader_cfg.get("assume_all_text_as_incoming", False)):
        return 0, "write_inbox_enabled_but_assume_all_text_as_incoming=false"

    conv = payload.get("conversation") or {}
    conv_id = _normalize_text(str(conv.get("platform_conversation_id", "")))
    customer_name = _normalize_text(str(conv.get("name", ""))) or "未知联系人"
    messages = payload.get("messages") or []
    if not conv_id or not messages:
        return 0, "no_messages"

    items: list[tuple[str, str, str, str]] = []
    for item in messages:
        content = _normalize_text(str(item.get("content", "")))
        if not content:
            continue
        pid = str(item.get("platform_msg_id", ""))
        if not pid:
            digest = content_hash("unknown", content)
            pid = make_platform_msg_id(PLATFORM, conv_id, digest)
        items.append((content, pid, "", ""))
    if not items:
        return 0, "no_text_items"

    conn = open_db(db_path or resolved_default_db_path())
    try:
        n = write_inbox_batch(conn, PLATFORM, conv_id, customer_name, items)
        return n, "written"
    finally:
        conn.close()


def main() -> int:
    cfg = load_wechat_config()
    payload = build_visible_messages_payload(cfg)
    written, write_status = maybe_write_visible_messages_to_inbox(payload, cfg)
    payload["write_mode"]["written_count"] = written
    payload["write_mode"]["status"] = write_status
    print("WECHAT_UIA_READER_JSON=" + json.dumps(payload, ensure_ascii=False))
    probe = payload.get("probe") or {}
    available = bool(probe.get("available"))
    return 0 if available else 1


if __name__ == "__main__":
    raise SystemExit(main())
