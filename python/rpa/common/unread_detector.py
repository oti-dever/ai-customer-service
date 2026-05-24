"""
Detect unread message indicators (red dots/badges) in conversation list screenshots.
Scans for bright red pixels characteristic of WeChat/IM unread badges,
grouped by row to identify which conversations have new messages.
"""
from __future__ import annotations

from typing import List, Tuple

import numpy as np


def _red_mask_band(
    arr: np.ndarray,
    x0: int,
    x1: int,
    *,
    use_dominance: bool,
) -> np.ndarray:
    """形状 (h, x1-x0) 的 bool 掩码：角标红 / 红字计时等。"""
    b_ch = arr[:, x0:x1, 0].astype(np.int16)
    g_ch = arr[:, x0:x1, 1].astype(np.int16)
    r_ch = arr[:, x0:x1, 2].astype(np.int16)
    if use_dominance:
        return (r_ch > 200) & (g_ch <= 120) & (b_ch <= 120) & ((r_ch - g_ch) >= 75) & ((r_ch - b_ch) >= 75)
    return (r_ch > 200) & (g_ch < 100) & (b_ch < 100)


def detect_unread_rows_dual_band(
    bgra: bytes,
    w: int,
    h: int,
    row_height: int = 65,
    *,
    left_red_threshold: int = 15,
    scan_x_start_ratio: float = 0.0,
    scan_x_end_ratio: float = 0.35,
    timer_x_start_ratio: float = 0.58,
    timer_x_end_ratio: float = 1.0,
    timer_red_threshold: int = 8,
    use_red_dominance: bool = True,
) -> List[Tuple[int, float]]:
    """
    左带（头像角标）+ 右带（如「16秒」红字计时）二选一命中即视为该行未读。
    比例均相对整张列表子图宽度 w。
    """
    if w <= 0 or h <= 0 or len(bgra) < w * h * 4:
        return []

    arr = np.frombuffer(bgra, dtype=np.uint8).reshape(h, w, 4)

    lx0 = max(0, min(w - 1, int(w * max(0.0, min(1.0, scan_x_start_ratio)))))
    lx1 = max(lx0 + 1, min(w, int(w * max(0.0, min(1.0, scan_x_end_ratio)))))
    if lx1 - lx0 < 16:
        lx1 = min(w, max(lx1, lx0 + 16, int(w * 0.12)))
    tx0 = max(0, min(w - 1, int(w * max(0.0, min(1.0, timer_x_start_ratio)))))
    tx1 = max(tx0 + 1, min(w, int(w * max(0.0, min(1.0, timer_x_end_ratio)))))
    if tx1 - tx0 < 8:
        tx1 = min(w, max(tx1, tx0 + 8))

    left_mask = _red_mask_band(arr, lx0, lx1, use_dominance=use_red_dominance)
    right_mask = _red_mask_band(arr, tx0, tx1, use_dominance=use_red_dominance)

    result: List[Tuple[int, float]] = []
    num_rows = max(1, h // row_height)

    for i in range(num_rows):
        y_start = i * row_height
        y_end = min((i + 1) * row_height, h)
        lc = int(np.sum(left_mask[y_start:y_end]))
        rc = int(np.sum(right_mask[y_start:y_end]))
        if lc >= left_red_threshold or rc >= timer_red_threshold:
            y_center = (y_start + y_end) / 2.0
            result.append((i, y_center))

    return result


def detect_unread_rows(
    bgra: bytes,
    w: int,
    h: int,
    row_height: int = 65,
    red_threshold: int = 15,
    scan_x_ratio: float = 0.35,
    scan_x_start_ratio: float = 0.0,
    scan_x_end_ratio: float | None = None,
) -> List[Tuple[int, float]]:
    """
    Scan a conversation list image for red unread indicators.

    Args:
        bgra: BGRA image bytes of the conversation list region.
        w, h: image dimensions in pixels.
        row_height: approximate height of one conversation item (px).
        red_threshold: minimum red pixel count per row to qualify as unread.
        scan_x_ratio: backward-compatible shorthand for scanning from 0 to this ratio.
        scan_x_start_ratio: left bound of horizontal scan window (0-1).
        scan_x_end_ratio: right bound of horizontal scan window (0-1).

    Returns:
        List of (row_index, y_center) for rows with detected unread indicators.
        y_center is in pixels relative to the image top.
    """
    if w <= 0 or h <= 0 or len(bgra) < w * h * 4:
        return []

    arr = np.frombuffer(bgra, dtype=np.uint8).reshape(h, w, 4)
    if scan_x_end_ratio is None:
        scan_x_end_ratio = scan_x_ratio
    x_start = max(0, min(w - 1, int(w * max(0.0, min(1.0, scan_x_start_ratio)))))
    x_end = max(x_start + 1, min(w, int(w * max(0.0, min(1.0, scan_x_end_ratio)))))

    # BGRA channel order: B=0, G=1, R=2, A=3
    b_ch = arr[:, x_start:x_end, 0].astype(np.int16)
    g_ch = arr[:, x_start:x_end, 1].astype(np.int16)
    r_ch = arr[:, x_start:x_end, 2].astype(np.int16)

    # WeChat unread badge is bright red (e.g. #FA5151):
    #   R > 200, G < 100, B < 100
    red_mask = (r_ch > 200) & (g_ch < 100) & (b_ch < 100)

    result: List[Tuple[int, float]] = []
    num_rows = max(1, h // row_height)

    for i in range(num_rows):
        y_start = i * row_height
        y_end = min((i + 1) * row_height, h)
        count = int(np.sum(red_mask[y_start:y_end]))
        if count >= red_threshold:
            y_center = (y_start + y_end) / 2.0
            result.append((i, y_center))

    return result
