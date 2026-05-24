"""
微信标题区等「暗底浅字」截图的 OCR 前处理。

将背景压成接近会话选中行的高亮绿色、前景统一为白色，提高与白字黑底训练分布的差异时的对比度；
若整图偏亮（浅色主题标题栏）则自动跳过，避免把黑字当成背景涂绿。
"""
from __future__ import annotations

import numpy as np

# 接近 PC 微信会话选中行的绿色（BGR）
_DEFAULT_BG_BGR = (88, 186, 34)


def bgra_to_selected_green_white(
    bgra: bytes,
    w: int,
    h: int,
    *,
    luma_threshold: float = 125.0,
    bg_bgr: tuple[int, int, int] = _DEFAULT_BG_BGR,
    auto_skip_light_header: bool = True,
    light_header_mean_luma: float = 118.0,
) -> tuple[bytes, bool]:
    """
    按亮度二值化：亮于阈值的像素视为文字 -> 白；其余 -> 背景绿。

    auto_skip_light_header: 当全图平均亮度较高时原样返回（浅色主题一般为白底黑字）。
    返回 (bgra, 是否做了变换)；未变换时调用方应继续走原有 invert 等逻辑。
    """
    if w <= 0 or h <= 0 or len(bgra) < w * h * 4:
        return bgra, False
    arr = np.frombuffer(bgra, dtype=np.uint8).reshape((h, w, 4))
    b = arr[:, :, 0].astype(np.float32)
    gch = arr[:, :, 1].astype(np.float32)
    r = arr[:, :, 2].astype(np.float32)
    luma = 0.114 * b + 0.587 * gch + 0.299 * r

    if auto_skip_light_header and float(np.mean(luma)) >= light_header_mean_luma:
        return bgra, False

    bb, bg, br = bg_bgr
    out = arr.copy()
    fg = luma >= luma_threshold
    out[:, :, 0] = np.where(fg, 255, bb)
    out[:, :, 1] = np.where(fg, 255, bg)
    out[:, :, 2] = np.where(fg, 255, br)
    return out.tobytes(), True
