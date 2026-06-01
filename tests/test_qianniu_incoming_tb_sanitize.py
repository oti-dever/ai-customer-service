"""千牛 incoming 合并后去掉正文里重复的 tb 账号。"""
from __future__ import annotations

import sys
from pathlib import Path

PYTHON_DIR = Path(__file__).resolve().parents[1] / "python"
sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.layout_parser import _merge_blocks, _sanitize_qianniu_incoming_tb_leak


def _box(x0: float, y0: float, x1: float, y1: float) -> list[list[float]]:
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]


def test_merge_incoming_strips_duplicate_tb_tokens():
    blocks = [
        ("tb4947894539", _box(10, 10, 80, 20), 1.0),
        ("2026-4-25 09:13:56", _box(10, 21, 120, 32), 1.0),
        ("tb4947894539", _box(10, 40, 80, 50), 1.0),
        ("测试消息", _box(10, 51, 80, 62), 1.0),
    ]
    m = _merge_blocks(blocks, "in", platform="qianniu", debug=False)
    assert m.sender_name == "tb4947894539"
    assert m.original_timestamp == "2026-4-25 09:13:56"
    assert m.content == "测试消息"


def test_merge_incoming_tb_only_content_becomes_empty():
    blocks = [
        ("tb4947894539", _box(10, 10, 80, 20), 1.0),
        ("2026-4-25 09:13:56", _box(10, 21, 120, 32), 1.0),
        ("tb4947894539", _box(10, 40, 80, 50), 1.0),
    ]
    m = _merge_blocks(blocks, "in", platform="qianniu", debug=False)
    assert m.sender_name == "tb4947894539"
    assert m.content == ""


def test_merge_incoming_no_sender_strips_leading_tb():
    blocks = [
        ("tb4947894539", _box(10, 10, 80, 20), 1.0),
        ("测试消息", _box(10, 51, 80, 62), 1.0),
    ]
    m = _merge_blocks(blocks, "in", platform="qianniu", debug=False)
    assert m.sender_name == ""
    assert m.content == "测试消息"


def test_sanitize_after_default_tb_sender_like_bubble_flush():
    """气泡路径先合并再补 default sender；补完后须再洗 content。"""
    blocks = [
        ("一条消息", _box(10, 40, 80, 50), 1.0),
        ("tb4947894539", _box(10, 51, 80, 62), 1.0),
    ]
    m = _merge_blocks(blocks, "in", platform="qianniu", debug=False)
    assert m.sender_name == ""
    assert m.content == "一条消息 tb4947894539"
    content = _sanitize_qianniu_incoming_tb_leak("tb4947894539", m.content)
    assert content == "一条消息"


def test_merge_incoming_strips_tb_glued_without_space():
    blocks = [
        ("tb4947894539", _box(10, 10, 80, 20), 1.0),
        ("2026-4-25 09:14:43", _box(10, 21, 120, 32), 1.0),
        ("一条消息tb4947894539", _box(10, 40, 160, 52), 1.0),
    ]
    m = _merge_blocks(blocks, "in", platform="qianniu", debug=False)
    assert m.sender_name == "tb4947894539"
    assert m.content == "一条消息"


if __name__ == "__main__":
    test_merge_incoming_strips_duplicate_tb_tokens()
    test_merge_incoming_tb_only_content_becomes_empty()
    test_merge_incoming_no_sender_strips_leading_tb()
    test_sanitize_after_default_tb_sender_like_bubble_flush()
    test_merge_incoming_strips_tb_glued_without_space()
    print("ok")
