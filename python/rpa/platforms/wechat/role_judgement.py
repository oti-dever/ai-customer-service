from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .uia_scoring import safe_prop, safe_rect_tuple
from .wechat_logging import get_logger


logger = get_logger(__name__)


@dataclass(frozen=True)
class MessageRoleJudgement:
    direction: str
    role: str
    method: str
    confidence: float
    left_variance: float = 0.0
    right_variance: float = 0.0


def judge_message_role(
    control: Any,
    *,
    message_list: Any | None = None,
    window_hwnd: int = 0,
) -> MessageRoleJudgement:
    rect = safe_rect_tuple(control)
    image = _capture_bubble(control, window_hwnd)
    if image is not None:
        judged = _judge_by_variance(image)
        if judged is not None:
            logger.info(
                "wechat_role_variance_result method=%s direction=%s role=%s confidence=%.3f "
                "left_variance=%.3f right_variance=%.3f rect=%s hwnd=%s name=%s",
                judged.method,
                judged.direction,
                judged.role,
                judged.confidence,
                judged.left_variance,
                judged.right_variance,
                rect,
                window_hwnd,
                safe_prop(control, "Name")[:80],
            )
            return judged
        logger.warning(
            "wechat_role_variance_unusable reason=judge_returned_none rect=%s hwnd=%s name=%s",
            rect,
            window_hwnd,
            safe_prop(control, "Name")[:80],
        )
    else:
        logger.info(
            "wechat_role_variance_skipped reason=capture_failed_or_no_hwnd rect=%s hwnd=%s name=%s",
            rect,
            window_hwnd,
            safe_prop(control, "Name")[:80],
        )
    return judge_by_position(control, message_list)


def judge_by_position(control: Any, message_list: Any | None = None) -> MessageRoleJudgement:
    rect = safe_rect_tuple(control)
    list_rect = safe_rect_tuple(message_list) if message_list is not None else None
    if rect is None:
        logger.warning("wechat_role_position_result method=unknown direction=in role=customer reason=no_rect")
        return MessageRoleJudgement(direction="in", role="customer", method="unknown", confidence=0.0)

    if list_rect is None:
        center = (rect[0] + rect[2]) / 2.0
        direction = "out" if center > 900.0 else "in"
        logger.info(
            "wechat_role_position_result method=bubble_position_absolute direction=%s role=%s "
            "confidence=0.450 control_center=%.1f threshold=900.0 rect=%s",
            direction,
            "agent" if direction == "out" else "customer",
            center,
            rect,
        )
        return MessageRoleJudgement(
            direction=direction,
            role="agent" if direction == "out" else "customer",
            method="bubble_position_absolute",
            confidence=0.45,
        )

    control_center = (rect[0] + rect[2]) / 2.0
    list_center = (list_rect[0] + list_rect[2]) / 2.0
    list_width = max(1.0, float(list_rect[2] - list_rect[0]))
    distance_ratio = min(1.0, abs(control_center - list_center) / (list_width / 2.0))
    direction = "out" if control_center >= list_center else "in"
    logger.info(
        "wechat_role_position_result method=bubble_position direction=%s role=%s confidence=%.3f "
        "control_center=%.1f list_center=%.1f distance_ratio=%.3f rect=%s list_rect=%s",
        direction,
        "agent" if direction == "out" else "customer",
        max(0.35, distance_ratio),
        control_center,
        list_center,
        distance_ratio,
        rect,
        list_rect,
    )
    return MessageRoleJudgement(
        direction=direction,
        role="agent" if direction == "out" else "customer",
        method="bubble_position",
        confidence=max(0.35, distance_ratio),
    )


def _capture_bubble(control: Any, hwnd: int) -> Any | None:
    if not hwnd:
        return None
    try:
        from rpa.platforms.wechat.screenshot import capture_bubble
    except Exception:
        return None
    try:
        return capture_bubble(control, hwnd=hwnd)
    except Exception:
        return None


def _judge_by_variance(image: Any) -> MessageRoleJudgement | None:
    try:
        import numpy as np
        from PIL import Image
    except Exception:
        return None

    try:
        if not isinstance(image, Image.Image):
            image = Image.fromarray(np.asarray(image))
        array = np.asarray(image.convert("RGB"), dtype=np.float32)
    except Exception:
        return None
    if array.ndim != 3 or array.shape[1] < 2:
        return None

    midpoint = max(1, array.shape[1] // 2)
    left_variance = _std(array[:, :midpoint, :])
    right_variance = _std(array[:, midpoint:, :])
    if left_variance <= right_variance:
        direction = "out"
        difference = right_variance - left_variance
        base = max(right_variance, 1e-6)
    else:
        direction = "in"
        difference = left_variance - right_variance
        base = max(left_variance, 1e-6)

    confidence = max(0.0, min(1.0, difference / base))
    logger.info(
        "wechat_role_variance_calculation image_size=%sx%s midpoint=%s "
        "left_variance=%.3f right_variance=%.3f difference=%.3f base=%.3f "
        "direction=%s role=%s confidence=%.3f",
        array.shape[1],
        array.shape[0],
        midpoint,
        left_variance,
        right_variance,
        difference,
        base,
        direction,
        "agent" if direction == "out" else "customer",
        confidence,
    )
    return MessageRoleJudgement(
        direction=direction,
        role="agent" if direction == "out" else "customer",
        method="bubble_variance",
        confidence=confidence,
        left_variance=left_variance,
        right_variance=right_variance,
    )


def _std(array: Any) -> float:
    try:
        import numpy as np

        if array.size == 0:
            return 0.0
        return float(np.std(array))
    except Exception:
        return 0.0
