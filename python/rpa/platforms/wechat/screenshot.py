from __future__ import annotations

from typing import Any


def capture_bubble(control: Any, hwnd: int, padding: int = 12) -> Any | None:
    try:
        from PIL import Image
        from rpa.core.screenshot import capture_window_printwindow
        from rpa.platforms.wechat.uia_scoring import safe_rect_tuple
    except Exception:
        return None

    if not hwnd:
        return None
    try:
        bgra, width, height = capture_window_printwindow(hwnd)
        image = Image.frombuffer("RGBA", (width, height), bgra, "raw", "BGRA", 0, 1).convert("RGB")
    except Exception:
        return None

    rect = safe_rect_tuple(control)
    if rect is None:
        return None

    try:
        import win32gui

        left_top = win32gui.ScreenToClient(hwnd, (rect[0], rect[1]))
        right_bottom = win32gui.ScreenToClient(hwnd, (rect[2], rect[3]))
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


def trim_media_evidence(image: Any, padding: int = 6) -> Any:
    try:
        import numpy as np
        from PIL import Image
    except Exception:
        return image
    if not isinstance(image, Image.Image):
        return image

    try:
        array = np.asarray(image.convert("RGB"))
        if array.ndim != 3 or array.shape[0] < 8 or array.shape[1] < 8:
            return image

        pixels = array.reshape(-1, 3)
        colors, counts = np.unique(pixels, axis=0, return_counts=True)
        background = colors[int(counts.argmax())].astype(np.int16)
        difference = np.max(np.abs(array.astype(np.int16) - background), axis=2)
        foreground = difference > 18

        min_column_pixels = max(2, int(array.shape[0] * 0.03))
        runs = _active_runs(foreground.sum(axis=0) >= min_column_pixels)
        if not runs:
            return image

        left, right = max(runs, key=lambda run: int(foreground[:, run[0] : run[1]].sum()))
        selected = foreground[:, left:right]
        min_row_pixels = max(2, int(max(1, right - left) * 0.03))
        row_runs = _active_runs(selected.sum(axis=1) >= min_row_pixels)
        if not row_runs:
            return image
        top, bottom = max(row_runs, key=lambda run: int(selected[run[0] : run[1], :].sum()))

        left = max(0, left - padding)
        top = max(0, top - padding)
        right = min(image.width, right + padding)
        bottom = min(image.height, bottom + padding)
        if right - left < 8 or bottom - top < 8:
            return image
        return image.crop((left, top, right, bottom))
    except Exception:
        return image


def _active_runs(values: Any) -> list[tuple[int, int]]:
    runs: list[tuple[int, int]] = []
    start: int | None = None
    for index, active in enumerate(list(values) + [False]):
        if bool(active) and start is None:
            start = index
        elif not bool(active) and start is not None:
            runs.append((start, index))
            start = None
    return runs
