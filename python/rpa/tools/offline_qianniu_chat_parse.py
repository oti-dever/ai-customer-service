"""离线：对千牛聊天区 PNG 跑 OCR + 与 Reader 一致的解析链（bubble / sequence / layout）。"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from PIL import Image

from rpa.common.layout_parser import parse_chat_layout
from rpa.common.ocr_engine import build_ocr_engine
from rpa.common.qianniu_bubble_parser import bubble_first_has_incoming_body, parse_qianniu_bubble_first
from rpa.common.qianniu_chat_parser import parse_qianniu_chat_blocks, to_parsed_messages


def load_bgra(path: Path) -> tuple[bytes, int, int]:
    with Image.open(path) as img:
        rgba = img.convert("RGBA")
        w, h = rgba.size
        return rgba.tobytes("raw", "BGRA"), w, h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "config" / "qianniu_config.json",
    )
    ap.add_argument(
        "--default-buyer",
        default="tb4947894539",
        help="气泡缺头时补的 sender_name，与 Reader stabilizer 一致即可",
    )
    args = ap.parse_args()
    if not args.image.exists():
        print("文件不存在:", args.image)
        return 1
    cfg = json.loads(args.config.read_text(encoding="utf-8"))
    ocr_cfg = cfg.get("ocr") or {}
    layout_cfg = cfg.get("layout") or {}
    chat_parser_cfg = cfg.get("chat_parser") or {}
    left_threshold = float(layout_cfg.get("left_threshold", 0.4))
    right_threshold = float(layout_cfg.get("right_threshold", 0.6))
    merge_y_gap = float(layout_cfg.get("merge_y_gap", 40))
    min_content_chars = int(chat_parser_cfg.get("min_content_chars", 1))
    min_incoming = int(chat_parser_cfg.get("min_incoming_messages_for_success", 1))
    drop_empty = bool(chat_parser_cfg.get("drop_empty_messages", True))
    bubble_first = bool(chat_parser_cfg.get("bubble_first", True))
    prefer_sequence = bool(chat_parser_cfg.get("prefer_sequence_parser", True))
    parser_mode = str(chat_parser_cfg.get("mode", "hybrid")).lower()

    ocr = build_ocr_engine(
        str(ocr_cfg.get("chat_engine", "rapidocr")).lower(),
        lang=str(ocr_cfg.get("lang", "ch")),
        min_confidence=float(ocr_cfg.get("min_confidence", 0.5)),
        max_side=int(ocr_cfg.get("max_side", 960)),
        invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", False)),
        det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
        det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
    )

    bgra, w, h = load_bgra(args.image)
    print("=== 截图 ===", args.image.name, f"{w}x{h}")
    blocks = ocr.recognize(bgra, w, h)
    print("=== OCR 块数 ===", len(blocks))
    for i, b in enumerate(blocks):
        t = (b[0] or "").replace("\n", "\\n")
        print(f"  [{i}] {t!r}")

    print("\n=== 气泡优先 parse_qianniu_bubble_first ===")
    bm = parse_qianniu_bubble_first(
        blocks,
        bgra,
        w,
        h,
        default_incoming_name=args.default_buyer.strip(),
        left_threshold=left_threshold,
        right_threshold=right_threshold,
        merge_y_gap=merge_y_gap,
        debug=False,
    )
    ok_body = bubble_first_has_incoming_body(bm, min_content_chars=min_content_chars)
    print("messages:", len(bm), "bubble_first_has_incoming_body:", ok_body)
    for i, m in enumerate(bm):
        print(
            f"  [{i}] side={m.side!r} sender={m.sender_name!r} "
            f"ts={m.original_timestamp!r} content={m.content!r}"
        )

    sr = parse_qianniu_chat_blocks(
        blocks,
        min_incoming_messages_for_success=min_incoming,
        drop_empty_messages=drop_empty,
        min_content_chars=min_content_chars,
    )
    print("\n=== 序列 parse_qianniu_chat_blocks ===")
    print(
        "success:",
        sr.success,
        "reason:",
        sr.reason,
        "matched_headers:",
        sr.matched_headers,
    )
    for i, m in enumerate(sr.messages):
        print(
            f"  [{i}] side={m.side!r} sender={m.sender_name!r} "
            f"ts={m.original_timestamp!r} content={m.content!r}"
        )

    lm = parse_chat_layout(
        blocks,
        region_width=float(w),
        platform="qianniu",
        left_threshold=left_threshold,
        right_threshold=right_threshold,
        merge_y_gap=merge_y_gap,
        debug=False,
    )
    print("\n=== 纯 layout parse_chat_layout ===")
    for i, m in enumerate(lm):
        print(
            f"  [{i}] side={m.side!r} sender={m.sender_name!r} "
            f"ts={m.original_timestamp!r} content={m.content!r}"
        )

    used_bubble = bool(
        bubble_first and bm and ok_body
    )
    if used_bubble:
        chosen, tag = bm, "Reader等价: bubble_first"
    elif parser_mode in ("sequence", "hybrid") and prefer_sequence and sr.success:
        chosen, tag = to_parsed_messages(sr.messages), "Reader等价: sequence"
    else:
        chosen, tag = lm, "Reader等价: layout_fallback"
    print("\n===", tag, "===")
    inc = 0
    for i, m in enumerate(chosen):
        if m.side != "in":
            continue
        inc += 1
        print(f"  in#{inc} sender={m.sender_name!r} ts={m.original_timestamp!r} content={m.content!r}")
    print("对方 in 条数:", inc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
