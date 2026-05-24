"""
微信窗口 ROI 截图：只根据配置计算矩形并调用 capture_region，不做 OCR 与业务判断。

供 Reader / Session 等编排层复用，职责与 Paddle、切换会话、写库分离。
"""
from __future__ import annotations

from typing import Any, Optional, Tuple

from .screenshot import capture_region

# bgra_bytes, width, height, capture_method
CaptureResult = Tuple[bytes, int, int, str]


# ---------------------------------------------------------------------------
# 矩形解析（便于单测与调试，不触发截图）
# ---------------------------------------------------------------------------


def rect_conversation_list(list_cfg: dict[str, Any]) -> tuple[int, int, int, int]:
    lx = int(list_cfg.get("x", 60))
    ly = int(list_cfg.get("y", 0))
    lw = int(list_cfg.get("w", 220))
    lh = int(list_cfg.get("h", 600))
    return lx, ly, lw, lh


def rect_chat(cfg: dict[str, Any]) -> tuple[int, int, int, int]:
    r = cfg.get("chat_region") or {}
    return (
        int(r.get("x", 260)),
        int(r.get("y", 90)),
        int(r.get("w", 720)),
        int(r.get("h", 760)),
    )


def rect_contact_header(header_cfg: dict[str, Any]) -> Optional[tuple[int, int, int, int]]:
    if not header_cfg:
        return None
    hw = int(header_cfg.get("w", 300))
    hh = int(header_cfg.get("h", 40))
    if hw <= 0 or hh <= 0:
        return None
    hx = int(header_cfg.get("x", 289))
    hy = int(header_cfg.get("y", 30))
    return hx, hy, hw, hh


def rect_search_box_ocr(search_cfg: dict[str, Any]) -> Optional[tuple[int, int, int, int]]:
    if not search_cfg:
        return None
    sx = int(search_cfg.get("ocr_x", int(search_cfg.get("x", 145)) - int(search_cfg.get("w", 160)) // 2))
    sy = int(search_cfg.get("ocr_y", int(search_cfg.get("y", 48)) - int(search_cfg.get("h", 30)) // 2))
    sw = int(search_cfg.get("ocr_w", search_cfg.get("w", 160)))
    sh = int(search_cfg.get("ocr_h", search_cfg.get("h", 30)))
    if sw <= 0 or sh <= 0:
        return None
    return sx, sy, sw, sh


def rect_search_result_ocr(search_cfg: dict[str, Any]) -> Optional[tuple[int, int, int, int]]:
    if not search_cfg:
        return None
    rw = int(search_cfg.get("result_w", max(180, int(search_cfg.get("w", 160)) + 40)))
    rh = int(search_cfg.get("result_h", 46))
    if rw <= 0 or rh <= 0:
        return None
    rx = int(
        search_cfg.get(
            "result_x",
            int(search_cfg.get("first_result_x", search_cfg.get("x", 145))) - rw // 2,
        )
    )
    ry = int(
        search_cfg.get(
            "result_y",
            int(search_cfg.get("first_result_y", 130)) - rh // 2,
        )
    )
    return rx, ry, rw, rh


def rect_input_box(ibox: dict[str, Any]) -> Optional[tuple[int, int, int, int]]:
    if ibox.get("x") is None or ibox.get("y") is None:
        return None
    iw = int(ibox.get("w", 500))
    ih = int(ibox.get("h", 80))
    if iw <= 0 or ih <= 0:
        return None
    ix = int(ibox.get("x", 289))
    iy = int(ibox.get("y", 615))
    return ix, iy, iw, ih


def rect_input_region_resolved(cfg: dict[str, Any], win_h: int = 0) -> tuple[int, int, int, int]:
    """
    输入框区域（窗口相对坐标）。
    显式 input_box.x/y 时直接用；否则放在 chat_region 下方，并可按 win_h 上推避免越界。
    与 wechat_session / wechat_writer 原逻辑一致。
    """
    ib = cfg.get("input_box") or {}
    if ib.get("x") is not None and ib.get("y") is not None:
        return (
            int(ib.get("x", 289)),
            int(ib.get("y", 615)),
            int(ib.get("w", 500)),
            int(ib.get("h", 80)),
        )
    region = cfg.get("chat_region") or {}
    x = int(region.get("x", 289))
    y = int(region.get("y", 69))
    w = int(region.get("w", 846))
    h = int(region.get("h", 546))
    input_y = y + h
    input_h = max(60, int(ib.get("h", 80)))
    if win_h > 0:
        input_y = min(input_y, win_h - input_h)
    return x, input_y, w, input_h


def unread_band_x_bounds(list_w: int, unread_cfg: dict[str, Any]) -> tuple[int, int]:
    """相对会话列表子图左缘的 [x_start, x_end)，用于红点扫描带截图。"""
    scan_x_ratio = float(unread_cfg.get("scan_x_ratio", 0.35))
    scan_x_start_ratio = float(unread_cfg.get("scan_x_start_ratio", 0.0))
    scan_x_end_ratio = float(unread_cfg.get("scan_x_end_ratio", scan_x_ratio))
    x_start = max(0, min(list_w - 1, int(list_w * max(0.0, min(1.0, scan_x_start_ratio)))))
    x_end = max(x_start + 1, min(list_w, int(list_w * max(0.0, min(1.0, scan_x_end_ratio)))))
    return x_start, x_end


# ---------------------------------------------------------------------------
# 截图 API（失败时向调用方传播 capture_region 的异常）
# ---------------------------------------------------------------------------


def capture_wechat_conversation_list_hwnd(hwnd: int, list_cfg: dict[str, Any]) -> Optional[CaptureResult]:
    lx, ly, lw, lh = rect_conversation_list(list_cfg)
    if lw <= 0 or lh <= 0:
        return None
    return capture_region(hwnd, lx, ly, lw, lh)


def capture_wechat_conversation_list(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    list_cfg = cfg.get("conversation_list_region") or {}
    return capture_wechat_conversation_list_hwnd(hwnd, list_cfg)


def capture_wechat_chat(hwnd: int, cfg: dict[str, Any]) -> CaptureResult:
    x, y, w, h = rect_chat(cfg)
    return capture_region(hwnd, x, y, w, h)


def capture_wechat_contact_header(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    r = rect_contact_header(cfg.get("contact_header_region") or {})
    if not r:
        return None
    hx, hy, hw, hh = r
    return capture_region(hwnd, hx, hy, hw, hh)


def capture_wechat_search_box_ocr(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    r = rect_search_box_ocr(cfg.get("search_box") or {})
    if not r:
        return None
    sx, sy, sw, sh = r
    return capture_region(hwnd, max(0, sx), max(0, sy), sw, sh)


def capture_wechat_search_result_ocr(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    r = rect_search_result_ocr(cfg.get("search_box") or {})
    if not r:
        return None
    return capture_region(hwnd, r[0], r[1], r[2], r[3])


def capture_wechat_input_box(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    r = rect_input_box(cfg.get("input_box") or {})
    if not r:
        return None
    return capture_region(hwnd, r[0], r[1], r[2], r[3])


def capture_wechat_input_region_resolved(
    hwnd: int, cfg: dict[str, Any], win_h: int,
) -> Optional[CaptureResult]:
    """输入框截图（含「仅配 chat_region 时」的推算位置）。"""
    ix, iy, iw, ih = rect_input_region_resolved(cfg, win_h)
    return capture_wechat_window_rect(hwnd, ix, iy, iw, ih)


def capture_wechat_unread_scan_band(
    hwnd: int, list_cfg: dict[str, Any], unread_cfg: dict[str, Any],
) -> Optional[CaptureResult]:
    """红点检测用的窄带：与会话列表同高、横向为 scan_x_* 比例。"""
    lx, ly, lw, lh = rect_conversation_list(list_cfg)
    if lw <= 0 or lh <= 0:
        return None
    x_start, x_end = unread_band_x_bounds(lw, unread_cfg)
    band_w = max(1, x_end - x_start)
    return capture_region(hwnd, lx + x_start, ly, band_w, lh)


def capture_wechat_window_rect(
    hwnd: int, x: int, y: int, w: int, h: int,
) -> Optional[CaptureResult]:
    """窗口客户区相对坐标的任意矩形截图（供 Session 内动态计算后的区域）。"""
    if w <= 0 or h <= 0:
        return None
    return capture_region(hwnd, x, y, w, h)


def rect_chat_bottom_strip(cfg: dict[str, Any]) -> Optional[tuple[int, int, int, int]]:
    """
    聊天区底部条（用于回执/风险快照与 writer 底部签名），与 wechat_session._save_switch_risk_snapshot 几何一致。
    """
    region = cfg.get("chat_region") or {}
    x = int(region.get("x", 289))
    y = int(region.get("y", 69))
    w = int(region.get("w", 846))
    h = int(region.get("h", 546))
    receipt_cfg = cfg.get("receipt_verify") or {}
    bottom_h = max(40, int(receipt_cfg.get("chat_bottom_height", 96)))
    sample_h = min(h, bottom_h)
    sample_y = y + max(0, h - sample_h)
    if w <= 0 or sample_h <= 0:
        return None
    return x, sample_y, w, sample_h


def capture_wechat_chat_bottom_strip(hwnd: int, cfg: dict[str, Any]) -> Optional[CaptureResult]:
    r = rect_chat_bottom_strip(cfg)
    if not r:
        return None
    x, sy, w, sh = r
    return capture_region(hwnd, x, sy, w, sh)
