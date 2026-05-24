"""
校准千牛左侧会话列表区域。

运行后 OCR 当前可见列表并打印识别到的联系人，辅助调试：
1. `conversation_list_region`
2. `conversation_scan.row_height_guess`
3. `unread_detection`
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from PIL import Image

from rpa.common.ocr_engine import PaddleOCREngine
from rpa.common.qianniu_session import (
    get_conversation_list_region_px,
    parse_conversation_list_blocks,
    parse_visible_conversation_list,
)
from rpa.common.qianniu_window import find_qianniu_hwnd
from rpa.common.screenshot import capture_region, save_bgra_png
from rpa.readers.qianniu_reader import _debug_snapshot_dir, _scan_for_unread_rows, load_config


def main() -> int:
    parser = argparse.ArgumentParser(description="校准千牛会话列表区域或解析离线截图")
    parser.add_argument("--image", help="离线图片路径：直接 OCR 并解析会话列表")
    args = parser.parse_args()

    config_path = Path(__file__).resolve().parent / "config" / "qianniu_config.json"
    if not config_path.exists():
        print(f"配置不存在: {config_path}")
        return 1

    cfg = load_config(config_path)
    list_cfg = cfg.get("conversation_list_region") or {}
    if not list_cfg:
        print("未配置 conversation_list_region")
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

    debug_dir = _debug_snapshot_dir(cfg)
    entries = []
    if args.image:
        image_path = Path(args.image)
        if not image_path.exists():
            print(f"离线图片不存在: {image_path}")
            return 1
        print(f"解析离线图片: {image_path}")
        img = Image.open(image_path).convert("RGB")
        blocks = ocr.recognize_from_pil(img)
        entries = parse_conversation_list_blocks(blocks, cfg)
    else:
        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd:
            print("未找到千牛接待中心窗口，请确保窗口已打开且未最小化")
            return 1

        print("解析千牛会话列表...")
        try:
            lx, ly, lw, lh = get_conversation_list_region_px(hwnd, cfg)
            if lw > 0 and lh > 0:
                bgra, w, h, _ = capture_region(hwnd, lx, ly, lw, lh)
                out_path = debug_dir / "calibration_conversation_list.png"
                save_bgra_png(bgra, w, h, out_path)
                print(f"已保存列表截图: {out_path}")
        except Exception as ex:
            print(f"保存列表截图失败: {ex}")

        entries = parse_visible_conversation_list(ocr, hwnd, cfg)

    if entries:
        print(f"识别到 {len(entries)} 个会话:")
        for i, entry in enumerate(entries, start=1):
            print(
                f"  {i}. {entry.name} "
                f"(row={entry.row_index}, y={entry.y_center:.0f}, conf={entry.confidence:.2f})"
            )
    else:
        print("未识别到联系人，请检查 conversation_list_region 和 row_height_guess 是否匹配当前千牛布局")

    if not args.image:
        unread_rows = _scan_for_unread_rows(hwnd, cfg)
        if unread_rows:
            print("检测到可能存在未读红点的行:")
            for row_idx, y_center in unread_rows:
                print(f"  row={row_idx} y={y_center:.0f}")
        else:
            print("未检测到未读红点（若当前配置关闭 unread_detection.enabled，这一项为空属正常）")
    else:
        print("离线图片模式下跳过未读红点检测")

    print(f"若需核对截图，可查看: {_debug_snapshot_dir(cfg)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
