from __future__ import annotations

from typing import Any


def capture_bubble(control: Any, hwnd: int, padding: int = 12) -> Any | None:
    try:
        from PIL import Image
        from rpa.core.screenshot import capture_window_printwindow
    except Exception:
        return None

    if not hwnd:
        return None
    try:
        bgra, width, height = capture_window_printwindow(hwnd)
        image = Image.frombuffer("RGBA", (width, height), bgra, "raw", "BGRA", 0, 1).convert("RGB")
    except Exception:
        return None

    rect = getattr(control, "BoundingRectangle", None)
    if rect is None:
        return None

    try:
        import win32gui

        left_top = win32gui.ScreenToClient(hwnd, (int(rect.left), int(rect.top)))
        right_bottom = win32gui.ScreenToClient(hwnd, (int(rect.right), int(rect.bottom)))
        left = min(left_top[0], right_bottom[0]) - padding
        top = min(left_top[1], right_bottom[1]) - padding
        right = max(left_top[0], right_bottom[0]) + padding
        bottom = max(left_top[1], right_bottom[1]) + padding
    except Exception:
        return None

    left = max(0, left)
    top = max(0, top)
    right = min(image.width, max(left + 1, right))
    bottom = min(image.height, max(top + 1, bottom))
    if right <= left or bottom <= top:
        return None
    return image.crop((left, top, right, bottom))
