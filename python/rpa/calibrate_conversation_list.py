"""
校准微信左侧会话列表区域。
运行后解析会话列表并打印识别到的联系人，用于验证 conversation_list_region 配置。
"""
from __future__ import annotations

import sys
from pathlib import Path

PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.ocr_engine import PaddleOCREngine
from rpa.common.wechat_capture import rect_conversation_list
from rpa.common.wechat_session import find_wechat_window, parse_visible_conversation_list
from rpa.readers.wechat_reader import load_config


def main():
    config_path = Path(__file__).resolve().parents[1] / "config" / "wechat_config.json"
    if not config_path.exists():
        print(f"配置不存在: {config_path}")
        return 1

    cfg = load_config(config_path)
    list_cfg = cfg.get("conversation_list_region") or {}
    if not list_cfg:
        print("未配置 conversation_list_region")
        return 1

    _x, _y, lw, lh = rect_conversation_list(list_cfg)
    if lw <= 0 or lh <= 0:
        print("conversation_list_region 宽高无效")
        return 1

    hwnd = find_wechat_window(cfg)
    if not hwnd:
        print("未找到微信窗口，请确保微信已打开且未最小化")
        return 1

    ocr_cfg = cfg.get("ocr") or {}
    ocr = PaddleOCREngine(
        lang="ch",
        min_confidence=float(ocr_cfg.get("min_confidence", 0.3)),
        max_side=int(ocr_cfg.get("max_side", 640)),
        invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
        det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
        det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
    )

    print("解析会话列表（与 Reader parse_visible_conversation_list 同路径）...")
    entries = parse_visible_conversation_list(ocr, hwnd, cfg)
    print(f"识别到 {len(entries)} 个联系人:")
    for i, e in enumerate(entries):
        print(f"  {i + 1}. {e.name} (y={e.y_center:.0f}, conf={e.confidence:.2f})")

    if not entries:
        print("未识别到联系人，请检查 conversation_list_region 的 x,y,w,h 是否与微信窗口匹配")
        print(f"当前配置: x={list_cfg.get('x')}, y={list_cfg.get('y')}, w={list_cfg.get('w')}, h={list_cfg.get('h')}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
