"""
Win32 window capture: PrintWindow (preferred) + BitBlt fallback.
Platform-agnostic; works for any HWND.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from pathlib import Path
from typing import Optional

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

GA_ROOT = 2


class POINT(ctypes.Structure):
    _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]


user32.WindowFromPoint.argtypes = [POINT]
user32.WindowFromPoint.restype = wintypes.HWND


class RECT(ctypes.Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def get_window_rect(hwnd: int) -> tuple[int, int, int, int]:
    """Returns (x, y, width, height)."""
    rc = RECT()
    if not user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rc)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top


def get_client_area_in_window_bitmap(hwnd: int) -> tuple[int, int, int, int]:
    """
    客户区在 PrintWindow 整窗位图中的位置与尺寸。
    比例坐标应对齐客户区（不含标题栏/边框），否则易裁到顶部 KPI 条或错位。
    返回 (offset_x, offset_y, client_w, client_h)，offset 为相对窗口左上角。
    """
    h = wintypes.HWND(hwnd)
    wx, wy, ww, wh = get_window_rect(hwnd)
    pt = wintypes.POINT(0, 0)
    if not user32.ClientToScreen(h, ctypes.byref(pt)):
        raise ctypes.WinError(ctypes.get_last_error())
    ox = int(pt.x - wx)
    oy = int(pt.y - wy)
    rc = RECT()
    if not user32.GetClientRect(h, ctypes.byref(rc)):
        raise ctypes.WinError(ctypes.get_last_error())
    cw = int(rc.right - rc.left)
    ch = int(rc.bottom - rc.top)
    return ox, oy, cw, ch


def is_window_valid(hwnd: int) -> bool:
    return bool(hwnd) and bool(user32.IsWindow(wintypes.HWND(hwnd)))


def is_window_minimized(hwnd: int) -> bool:
    return bool(hwnd) and bool(user32.IsIconic(wintypes.HWND(hwnd)))


def hwnd_screen_rect_unobstructed(
    hwnd: int, screen_left: int, screen_top: int, screen_right: int, screen_bottom: int
) -> bool:
    """
    屏幕坐标系下给定矩形内多点 WindowFromPoint：若任一点命中 HWND 的根顶层
    与 hwnd 的根顶层不同，则认为该矩形在屏幕上被其它顶层窗遮挡（与焦点无关）。
    用于决定是否适合走依赖屏幕合成的 Clicknium 截图；PrintWindow 裁剪不受此限制。
    """
    if not is_window_valid(hwnd):
        return False
    h0 = wintypes.HWND(hwnd)
    if bool(user32.IsIconic(h0)):
        return False
    root = user32.GetAncestor(h0, GA_ROOT)
    if not root:
        return False
    left = int(min(screen_left, screen_right))
    top = int(min(screen_top, screen_bottom))
    right = int(max(screen_left, screen_right))
    bottom = int(max(screen_top, screen_bottom))
    w = right - left
    h = bottom - top
    if w < 8 or h < 8:
        return False

    margin = max(2, min(8, w // 16, h // 16))
    cx = (left + right) // 2
    cy = (top + bottom) // 2
    pts: list[tuple[int, int]] = [
        (left + margin, top + margin),
        (right - margin, top + margin),
        (left + margin, bottom - margin),
        (right - margin, bottom - margin),
        (cx, cy),
        (cx, top + margin),
        (cx, bottom - margin),
        (left + margin, cy),
        (right - margin, cy),
    ]

    for x, y in pts:
        hit = user32.WindowFromPoint(POINT(x, y))
        if not hit:
            continue
        hit_root = user32.GetAncestor(hit, GA_ROOT)
        if hit_root and int(hit_root) != int(root):
            return False
    return True


def hwnd_capture_subrect_unobstructed(hwnd: int, bx: int, by: int, bw: int, bh: int) -> bool:
    """
    与 capture_region(hwnd, bx, by, bw, bh) 相同的子矩形（相对整窗 PrintWindow 位图左上），
    换算到屏幕坐标后做 hwnd_screen_rect_unobstructed。
    """
    if bw <= 0 or bh <= 0:
        return False
    wx, wy, _, _ = get_window_rect(hwnd)
    return hwnd_screen_rect_unobstructed(
        hwnd, wx + bx, wy + by, wx + bx + bw, wy + by + bh
    )


def hwnd_screen_root_unobstructed(hwnd: int) -> bool:
    """
    判断 hwnd 所属顶层整窗在屏幕上的外接矩形是否未被其它顶层窗遮挡（采样判定）。
    与键盘焦点、前台线程（GetForegroundWindow）无关；最小化视为不可用（False）。
    """
    if not is_window_valid(hwnd):
        return False
    h0 = wintypes.HWND(hwnd)
    if bool(user32.IsIconic(h0)):
        return False
    root = user32.GetAncestor(h0, GA_ROOT)
    if not root:
        return False
    rc = RECT()
    if not user32.GetWindowRect(root, ctypes.byref(rc)):
        return False
    return hwnd_screen_rect_unobstructed(
        hwnd, int(rc.left), int(rc.top), int(rc.right), int(rc.bottom)
    )


def _get_dibits_bgra(hdc: int, hbmp: int, w: int, h: int) -> bytes:
    BI_RGB = 0
    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = w
    header.biHeight = -h
    header.biPlanes = 1
    header.biBitCount = 32
    header.biCompression = BI_RGB
    bmi = BITMAPINFO()
    bmi.bmiHeader = header

    buf_size = w * h * 4
    buf = (ctypes.c_ubyte * buf_size)()
    got = gdi32.GetDIBits(hdc, hbmp, 0, h, ctypes.byref(buf), ctypes.byref(bmi), 0)
    if got == 0:
        raise ctypes.WinError(ctypes.get_last_error())
    return bytes(buf)


def capture_window_printwindow(hwnd: int) -> tuple[bytes, int, int]:
    """
    Off-screen capture via PrintWindow.
    Returns (bgra_bytes, width, height) for the whole window.
    """
    _, _, w, h = get_window_rect(hwnd)
    if w <= 0 or h <= 0:
        raise RuntimeError("window rect invalid")

    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)

    PW_RENDERFULLCONTENT = 0x00000002
    ok = bool(user32.PrintWindow(wintypes.HWND(hwnd), wintypes.HDC(hdc_mem), PW_RENDERFULLCONTENT))
    if not ok:
        ok = bool(user32.PrintWindow(wintypes.HWND(hwnd), wintypes.HDC(hdc_mem), 0))
    if not ok:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)
        raise RuntimeError("PrintWindow failed")

    try:
        bgra = _get_dibits_bgra(hdc_mem, hbmp, w, h)
        return bgra, w, h
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)


def crop_bgra(bgra: bytes, src_w: int, src_h: int, x: int, y: int, w: int, h: int) -> bytes:
    if x < 0 or y < 0 or w <= 0 or h <= 0:
        raise ValueError("invalid crop rect")
    if x + w > src_w or y + h > src_h:
        raise ValueError("crop rect out of bounds")
    row_bytes = src_w * 4
    out = bytearray(w * h * 4)
    for row in range(h):
        src_off = (y + row) * row_bytes + x * 4
        dst_off = row * w * 4
        out[dst_off : dst_off + w * 4] = bgra[src_off : src_off + w * 4]
    return bytes(out)


def capture_region_bitblt(hwnd: int, x: int, y: int, w: int, h: int) -> bytes:
    """Screen capture fallback; window must be visible."""
    win_x, win_y, _, _ = get_window_rect(hwnd)
    abs_x = win_x + x
    abs_y = win_y + y

    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)

    SRCCOPY = 0x00CC0020
    try:
        if not gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, abs_x, abs_y, SRCCOPY):
            raise ctypes.WinError(ctypes.get_last_error())
        return _get_dibits_bgra(hdc_mem, hbmp, w, h)
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)


def _clamp_crop_rect(
    x: int, y: int, w: int, h: int, bound_w: int, bound_h: int
) -> tuple[int, int, int, int]:
    x = max(0, min(x, max(0, bound_w - 1)))
    y = max(0, min(y, max(0, bound_h - 1)))
    w = max(1, min(w, bound_w - x))
    h = max(1, min(h, bound_h - y))
    return x, y, w, h


def capture_region(
    hwnd: int, x: int, y: int, w: int, h: int
) -> tuple[bytes, int, int, str]:
    """
    Capture a region of a window.
    Prefer PrintWindow; fallback to BitBlt.
    x,y,w,h 与 PrintWindow 位图左上角对齐（通常为整窗外沿坐标系）。
    Returns (bgra_bytes, width, height, method).
    """
    try:
        win_bgra, win_w, win_h = capture_window_printwindow(hwnd)
        x, y, w, h = _clamp_crop_rect(x, y, w, h, win_w, win_h)
        chat_bgra = crop_bgra(win_bgra, win_w, win_h, x, y, w, h)
        return chat_bgra, w, h, "printwindow"
    except Exception:
        bgra = capture_region_bitblt(hwnd, x, y, w, h)
        return bgra, w, h, "bitblt"


def save_bgra_png(bgra: bytes, width: int, height: int, path: Path) -> None:
    """将 BGRA 像素保存为 PNG（依赖 Pillow）。"""
    from PIL import Image

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    img = Image.frombuffer("RGBA", (width, height), bgra, "raw", "BGRA", 0, 1)
    img.save(str(path), "PNG")
