"""千牛气泡优先解析：缺头 incoming 补 sender、色带侧别。"""
from __future__ import annotations

import numpy as np

from rpa.common.layout_parser import ParsedMessage
from rpa.common.qianniu_bubble_parser import (
    bubble_first_has_incoming_body,
    parse_qianniu_bubble_first,
)


def _rgb_img_to_bgra(arr_rgb: np.ndarray) -> tuple[bytes, int, int]:
    """RGB uint8 (h,w,3) -> BGRA bytes."""
    h, w, _ = arr_rgb.shape
    bgr = np.zeros((h, w, 3), dtype=np.uint8)
    bgr[:, :, 0] = arr_rgb[:, :, 2]
    bgr[:, :, 1] = arr_rgb[:, :, 1]
    bgr[:, :, 2] = arr_rgb[:, :, 0]
    bgra = np.dstack([bgr, np.full((h, w), 255, dtype=np.uint8)])
    return bgra.tobytes(), w, h


def test_bubble_first_has_incoming_body():
    assert not bubble_first_has_incoming_body([], min_content_chars=1)
    assert not bubble_first_has_incoming_body(
        [ParsedMessage(content=" ", side="in", bbox=[])], min_content_chars=1
    )
    assert bubble_first_has_incoming_body(
        [ParsedMessage(content="你好", side="in", bbox=[])], min_content_chars=1
    )


def test_parse_single_incoming_no_tb_header_gets_default_sender():
    # 左白底气泡区 + 灰底聊天背景；OCR 块在左侧白区内（仅正文）
    h, w = 120, 280
    rgb = np.full((h, w, 3), 242, dtype=np.uint8)
    rgb[35:90, 18:160] = (255, 255, 255)
    bgra, w0, h0 = _rgb_img_to_bgra(rgb)
    blocks = [
        (
            "不嘻嘻",
            [[40.0, 50.0], [120.0, 50.0], [120.0, 75.0], [40.0, 75.0]],
            0.99,
        )
    ]
    msgs = parse_qianniu_bubble_first(
        blocks,
        bgra,
        w0,
        h0,
        default_incoming_name="tb4947894539",
        left_threshold=0.4,
        right_threshold=0.6,
        merge_y_gap=20.0,
    )
    assert len(msgs) == 1
    m = msgs[0]
    assert m.side == "in"
    assert "不嘻嘻" in m.content
    assert m.sender_name == "tb4947894539"
    assert (m.original_timestamp or "") == ""
