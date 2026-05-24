"""
微信 ROI 截图后的 Paddle 推理入口集中于此，与 wechat_capture（纯截图）分层。

业务侧再对 blocks 做 parse_conversation_list_blocks / parse_chat_layout 等。
"""
from __future__ import annotations

from typing import Any, List, Optional

from .ocr_engine import PaddleOCREngine
from .wechat_capture import (
    CaptureResult,
    capture_wechat_chat,
    capture_wechat_conversation_list,
    capture_wechat_window_rect,
)


def ocr_bgra_blocks(
    ocr: PaddleOCREngine,
    bgra: bytes,
    w: int,
    h: int,
    *,
    skip_dark_invert: bool = False,
) -> List[Any]:
    """对已有像素缓冲执行 recognize，统一空结果处理。"""
    blocks = ocr.recognize(bgra, w, h, skip_dark_invert=skip_dark_invert)
    return list(blocks or [])


def ocr_from_capture(
    ocr: PaddleOCREngine,
    cap: Optional[CaptureResult],
    *,
    skip_dark_invert: bool = False,
) -> List[Any]:
    if not cap:
        return []
    bgra, w, h, _ = cap
    return ocr_bgra_blocks(ocr, bgra, w, h, skip_dark_invert=skip_dark_invert)


def ocr_wechat_conversation_list_blocks(
    ocr: PaddleOCREngine, hwnd: int, cfg: dict[str, Any],
) -> List[Any]:
    cap = capture_wechat_conversation_list(hwnd, cfg)
    return ocr_from_capture(ocr, cap)


def ocr_wechat_chat_blocks(
    ocr: PaddleOCREngine, hwnd: int, cfg: dict[str, Any],
) -> tuple[List[Any], int, int]:
    """截图聊天区并识别，返回 (blocks, width, height)。"""
    bgra, w, h, _ = capture_wechat_chat(hwnd, cfg)
    blocks = ocr_bgra_blocks(ocr, bgra, w, h)
    return blocks, w, h


def ocr_wechat_window_rect_blocks(
    ocr: PaddleOCREngine,
    hwnd: int,
    x: int,
    y: int,
    w: int,
    h: int,
) -> List[Any]:
    cap = capture_wechat_window_rect(hwnd, x, y, w, h)
    return ocr_from_capture(ocr, cap)
