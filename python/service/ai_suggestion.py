from __future__ import annotations

from dataclasses import dataclass
from typing import Any


SENSITIVE_KEYWORDS = ("退款", "退货", "投诉", "赔偿", "金额", "发票", "隐私", "手机号", "地址")


@dataclass(frozen=True)
class ChatMessage:
    role: str
    content: str


@dataclass(frozen=True)
class Suggestion:
    content: str
    confidence: int


def _clean_text(value: Any) -> str:
    if value is None:
        return ""
    return str(value).replace("\r\n", "\n").replace("\r", "\n").strip()


def _normalize_messages(raw_messages: Any) -> list[ChatMessage]:
    if not isinstance(raw_messages, list):
        return []

    messages: list[ChatMessage] = []
    for item in raw_messages:
        if not isinstance(item, dict):
            continue
        role = _clean_text(item.get("role")) or "user"
        content = _clean_text(item.get("content"))
        if content:
            messages.append(ChatMessage(role=role, content=content))
    return messages


def _last_customer_message(messages: list[ChatMessage]) -> str:
    for message in reversed(messages):
        if message.role in {"user", "customer", "in", "inbound"}:
            return message.content
    return messages[-1].content if messages else ""


def _risk_flags(text: str) -> list[str]:
    return [keyword for keyword in SENSITIVE_KEYWORDS if keyword in text]


def _build_template_suggestions(customer_text: str, platform: str, max_suggestions: int) -> list[Suggestion]:
    platform_hint = {
        "pdd_web": "拼多多后台",
        "wechat": "微信",
        "qianniu": "千牛",
        "mock": "当前平台",
    }.get(platform, "当前平台")

    if not customer_text:
        base = [
            "您好，我在的。请您补充一下具体问题，我会尽快帮您核实处理。",
            "您好，请问需要咨询订单、物流还是售后问题？您可以把相关信息发我，我马上帮您看。",
        ]
    else:
        base = [
            f"您好，您反馈的“{customer_text[:40]}”我已经看到。我先为您核实一下，请您稍等。",
            f"您好，这边会根据您在{platform_hint}的会话信息帮您确认，处理结果会尽快回复您。",
            "您好，感谢您的反馈。为了避免误处理，我会先核对订单和当前状态，再给您准确答复。",
        ]

    return [Suggestion(content=text, confidence=80) for text in base[: max(1, max_suggestions)]]


def build_ai_suggestion_response(payload: dict[str, Any]) -> dict[str, Any]:
    request_id = _clean_text(payload.get("request_id"))
    platform = _clean_text(payload.get("platform")) or "unknown"
    max_suggestions = payload.get("max_suggestions", 3)
    try:
        max_suggestions = int(max_suggestions)
    except (TypeError, ValueError):
        max_suggestions = 3
    max_suggestions = max(1, min(max_suggestions, 3))

    messages = _normalize_messages(payload.get("messages"))
    customer_text = _last_customer_message(messages)
    flags = _risk_flags(customer_text)
    suggestions = _build_template_suggestions(customer_text, platform, max_suggestions)

    if flags:
        suggestions[0] = Suggestion(
            content=f"{suggestions[0].content} 涉及{','.join(flags)}等敏感信息，我会先核实后再处理。",
            confidence=75,
        )

    return {
        "request_id": request_id,
        "status": "success",
        "suggestions": [
            {"content": suggestion.content, "confidence": suggestion.confidence}
            for suggestion in suggestions
        ],
        "metadata": {
            "service": "python-ai-service",
            "mode": "template_fallback",
            "risk_flags": flags,
            "message_count": len(messages),
            "platform": platform,
        },
    }

