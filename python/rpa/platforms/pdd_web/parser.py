from __future__ import annotations

import hashlib
from datetime import datetime, timezone
from typing import Any


PLATFORM_PDD_WEB = "pdd_web"
DEFAULT_ACCOUNT_ID = "local_pdd_web"


def clean(value: Any) -> str:
    return "" if value is None else str(value).strip()


def conversation_key(account_id: str, display_name: str) -> str:
    return f"{PLATFORM_PDD_WEB}:{clean(account_id) or DEFAULT_ACCOUNT_ID}:{clean(display_name) or 'current'}"


def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def stable_message_id(*parts: Any) -> str:
    raw = "|".join(clean(part) for part in parts)
    return hashlib.sha1(raw.encode("utf-8", errors="ignore")).hexdigest()


def normalize_content_type(value: Any) -> str:
    normalized = clean(value).lower()
    aliases = {
        "msg": "text",
        "pic": "image",
        "goods": "product",
        "clue": "lead",
        "system": "notice",
    }
    normalized = aliases.get(normalized, normalized)
    if normalized in {"text", "image", "product", "order", "lead", "notice", "unknown"}:
        return normalized
    return "unknown"


def normalize_sender_role(value: Any, *, is_self: Any = None) -> str:
    role = clean(value).lower()
    if role in {"customer", "agent", "system"}:
        return role
    if is_self is True:
        return "agent"
    if is_self is False:
        return "customer"
    return "customer"


def event_from_page_conversation(
    conversation: dict[str, Any],
    *,
    account_id: str = DEFAULT_ACCOUNT_ID,
) -> dict[str, Any]:
    display_name = clean(
        conversation.get("display_name")
        or conversation.get("name")
        or conversation.get("title")
        or "current"
    )
    key = conversation_key(account_id, display_name)
    unread_count = conversation.get("unread_count")
    try:
        unread_count = int(unread_count or 0)
    except (TypeError, ValueError):
        unread_count = 0
    raw = conversation.get("raw") if isinstance(conversation.get("raw"), dict) else {}
    return {
        "event_type": "conversation_observed",
        "platform": PLATFORM_PDD_WEB,
        "account_id": clean(account_id) or DEFAULT_ACCOUNT_ID,
        "conversation_key": key,
        "occurred_at": clean(conversation.get("occurred_at")) or now_iso(),
        "payload": {
            "display_name": display_name,
            "sender_name": display_name,
            "source_type": "dom_observed",
            "confidence": int(conversation.get("confidence") or 70),
            "unread_count": unread_count,
            "raw": raw,
        },
    }


def event_from_page_message(
    message: dict[str, Any],
    *,
    account_id: str = DEFAULT_ACCOUNT_ID,
    display_name: str = "",
) -> dict[str, Any]:
    name = clean(display_name or message.get("display_name") or message.get("sender_name") or "current")
    key = conversation_key(account_id, name)
    content_type = normalize_content_type(message.get("content_type") or message.get("type"))
    sender_role = normalize_sender_role(message.get("sender_role"), is_self=message.get("is_self"))
    direction = "outbound" if sender_role == "agent" else ("system" if sender_role == "system" else "inbound")
    content = clean(message.get("content") or message.get("text"))
    time_text = clean(message.get("time_text") or message.get("time"))
    asset_url = clean(message.get("asset_url") or message.get("url"))
    platform_msg_id = clean(message.get("platform_msg_id")) or stable_message_id(
        PLATFORM_PDD_WEB,
        account_id,
        key,
        time_text,
        sender_role,
        content_type,
        content,
        asset_url,
    )
    raw = message.get("raw") if isinstance(message.get("raw"), dict) else {}
    if asset_url and "asset_url" not in raw:
        raw = {**raw, "asset_url": asset_url}
    return {
        "event_type": "message_observed",
        "platform": PLATFORM_PDD_WEB,
        "account_id": clean(account_id) or DEFAULT_ACCOUNT_ID,
        "conversation_key": key,
        "occurred_at": clean(message.get("occurred_at")) or now_iso(),
        "payload": {
            "platform_msg_id": platform_msg_id,
            "display_name": name,
            "sender_name": clean(message.get("sender_name")) or name,
            "sender_role": sender_role,
            "direction": direction,
            "content_type": content_type,
            "content": content,
            "original_timestamp": time_text,
            "source_type": "dom_observed",
            "confidence": int(message.get("confidence") or 70),
            "raw": raw,
        },
    }
