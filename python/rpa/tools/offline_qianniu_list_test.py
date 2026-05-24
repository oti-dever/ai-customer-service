"""
离线：对「千牛会话列表」整图或裁切图跑 OCR、红点未读行、浅黄行启发式。

示例：
  python -m rpa.tools.offline_qianniu_list_test --image path/to/list.png
  python -m rpa.tools.offline_qianniu_list_test --image path/to/list.png --config ../config/qianniu_config.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from PIL import Image

from rpa.common.ocr_engine import PaddleOCREngine
from rpa.common.qianniu_session import parse_conversation_list_blocks
from rpa.common.unread_detector import detect_unread_rows


def _pil_to_bgra(img: Image.Image) -> tuple[bytes, int, int]:
    rgba = np.array(img.convert("RGBA"))
    h, w, _ = rgba.shape
    bgra = np.empty((h, w, 4), dtype=np.uint8)
    bgra[:, :, 0] = rgba[:, :, 2]
    bgra[:, :, 1] = rgba[:, :, 1]
    bgra[:, :, 2] = rgba[:, :, 0]
    bgra[:, :, 3] = rgba[:, :, 3]
    return bgra.tobytes(), w, h


def _row_stats_cream_gray(
    bgra: bytes,
    w: int,
    h: int,
    row_height: int,
) -> list[tuple[int, float, float, float]]:
    """
    按行估计：浅黄底（奶油高亮）占比、与灰底差异的粗略分数。
    返回 [(row_i, y_center, cream_frac, grayish_frac), ...]
    """
    arr = np.frombuffer(bgra, dtype=np.uint8).reshape(h, w, 4)
    b_ch = arr[:, :, 0].astype(np.float32)
    g_ch = arr[:, :, 1].astype(np.float32)
    r_ch = arr[:, :, 2].astype(np.float32)
    # 暖色浅底：高亮 + R/G 略高于 B（截图里 #FFF4D1 一类）
    cream = (
        (r_ch > 210)
        & (g_ch > 215)
        & (b_ch > 160)
        & (r_ch - b_ch > 15)
        & (g_ch - b_ch > 10)
        & (r_ch + g_ch + b_ch > 580)
    )
    # 中性浅灰底（已读/选中常见）
    grayish = (
        (np.abs(r_ch - g_ch) < 18)
        & (np.abs(g_ch - b_ch) < 18)
        & (r_ch + g_ch + b_ch < 720)
        & (r_ch > 200)
    )

    out: list[tuple[int, float, float, float]] = []
    num_rows = max(1, h // row_height)
    for i in range(num_rows):
        y0 = i * row_height
        y1 = min((i + 1) * row_height, h)
        sub = cream[y0:y1, :]
        sub_g = grayish[y0:y1, :]
        out.append(
            (
                i,
                (y0 + y1) / 2.0,
                float(np.mean(sub)),
                float(np.mean(sub_g)),
            )
        )
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="离线测试千牛会话列表截图")
    parser.add_argument("--image", required=True, type=Path, help="列表区域 PNG/JPG")
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "config" / "qianniu_config.json",
        help="用于 OCR 与 row_height_guess（可选）",
    )
    parser.add_argument("--row-height", type=int, default=0, help="覆盖行高（0=从配置 unread_detection.row_height 或 68）")
    args = parser.parse_args()

    if not args.image.exists():
        print(f"文件不存在: {args.image}")
        return 1

    img = Image.open(args.image)
    bgra, w, h = _pil_to_bgra(img)
    print(f"图像: {args.image.name}  尺寸 {w}x{h}")

    row_h = args.row_height
    cfg: dict = {}
    if args.config.exists():
        cfg = json.loads(args.config.read_text(encoding="utf-8"))
    if row_h <= 0:
        ud = cfg.get("unread_detection") or {}
        row_h = int(ud.get("row_height", 68))
    red_th = int((cfg.get("unread_detection") or {}).get("red_threshold", 15))
    scan_left = float((cfg.get("unread_detection") or {}).get("scan_x_ratio", 0.36))

    # 左侧带：红点
    left_rows = detect_unread_rows(
        bgra,
        w,
        h,
        row_height=row_h,
        red_threshold=red_th,
        scan_x_ratio=scan_left,
    )
    # 右侧带：红色「N秒」等
    right_rows = detect_unread_rows(
        bgra,
        w,
        h,
        row_height=row_h,
        red_threshold=max(8, red_th // 2),
        scan_x_start_ratio=0.55,
        scan_x_end_ratio=1.0,
    )

    print(f"\n[红点/高亮红字] row_height={row_h} red_threshold={red_th}")
    print(f"  左带 0..{scan_left:.2f}: {left_rows or '无'}")
    print(f"  右带 0.55..1.0: {right_rows or '无'}")

    rows_meta = _row_stats_cream_gray(bgra, w, h, row_h)
    print(f"\n[行背景启发式] cream_frac / grayish_frac（整行像素比例）")
    for ri, yc, cf, gf in rows_meta:
        print(f"  row={ri} y_center={yc:.0f}  cream={cf:.3f}  gray={gf:.3f}")

    if args.config.exists() and cfg.get("conversation_list_region"):
        ocr_cfg = cfg.get("ocr") or {}
        print("\n[OCR 列表解析]（需 Paddle 模型，首次可能较慢）")
        ocr = PaddleOCREngine(
            lang="ch",
            min_confidence=float(ocr_cfg.get("min_confidence", 0.3)),
            max_side=int(ocr_cfg.get("max_side", 960)),
            invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
            det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
            det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
        )
        blocks = ocr.recognize_from_pil(img.convert("RGB"))
        entries = parse_conversation_list_blocks(blocks, cfg)
        if entries:
            for i, e in enumerate(entries, 1):
                print(f"  {i}. {e.name}  y={e.y_center:.0f}  conf={e.confidence:.2f}")
        else:
            print("  未解析到会话名（检查是否仅为列表裁切、row_height_guess）")
    else:
        print("\n[OCR] 跳过（未找到配置或 conversation_list_region）")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
