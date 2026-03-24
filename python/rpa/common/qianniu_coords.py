"""
千牛截图/点击坐标：比例默认相对「客户区」（不含标题栏与边框）。

整窗比例曾导致裁到顶部 KPI 条（昨日响应率等）或聊天区水平错位。
"""
from __future__ import annotations

from typing import Any

from .screenshot import get_client_area_in_window_bitmap, get_window_rect


def rect_from_absolute_fields(region: dict[str, Any] | None) -> tuple[int, int, int, int] | None:
    """
    若 region 含有效 x,y,w,h（整窗 PrintWindow 位图像素），返回 (x,y,w,h)；否则 None。
    C++ / 手动校准写入的绝对区域优先于比例。
    """
    if not region:
        return None
    try:
        x = int(region["x"])
        y = int(region["y"])
        w = int(region["w"])
        h = int(region["h"])
    except (KeyError, TypeError, ValueError):
        return None
    if w <= 0 or h <= 0:
        return None
    return x, y, w, h


def coordinate_space(cfg: dict[str, Any]) -> str:
    r = cfg.get("chat_region") or {}
    s = str(r.get("coordinates", "client")).lower()
    return s if s in ("client", "window") else "client"


def rect_ratios_to_bitmap_xywh(
    hwnd: int,
    left: float,
    top: float,
    right: float,
    bottom: float,
    space: str,
) -> tuple[int, int, int, int]:
    """
    将比例矩形转为相对 PrintWindow 位图左上角的 (x, y, w, h)。
    space=client：比例相对客户区；space=window：相对整窗外接矩形（旧行为）。
    """
    if space == "window":
        _, _, win_w, win_h = get_window_rect(hwnd)
        x = int(win_w * left)
        y = int(win_h * top)
        w = int(win_w * (right - left))
        h = int(win_h * (bottom - top))
        return x, y, w, h
    ox, oy, cw, ch = get_client_area_in_window_bitmap(hwnd)
    x = ox + int(cw * left)
    y = oy + int(ch * top)
    w = int(cw * (right - left))
    h = int(ch * (bottom - top))
    return x, y, w, h


def rect_center_screen(
    hwnd: int,
    left: float,
    top: float,
    right: float,
    bottom: float,
    space: str,
) -> tuple[int, int]:
    """输入框等区域中心点的屏幕坐标。"""
    wx, wy, _, _ = get_window_rect(hwnd)
    bx, by, bw, bh = rect_ratios_to_bitmap_xywh(hwnd, left, top, right, bottom, space)
    return wx + bx + bw // 2, wy + by + bh // 2


def point_ratio_screen(
    hwnd: int,
    x_ratio: float,
    y_ratio: float,
    space: str,
) -> tuple[int, int]:
    """发送按钮等：用客户区或整窗比例直接映射到屏幕点。"""
    wx, wy, _, _ = get_window_rect(hwnd)
    if space == "window":
        _, _, win_w, win_h = get_window_rect(hwnd)
        return wx + int(win_w * x_ratio), wy + int(win_h * y_ratio)
    ox, oy, cw, ch = get_client_area_in_window_bitmap(hwnd)
    return wx + ox + int(cw * x_ratio), wy + oy + int(ch * y_ratio)
