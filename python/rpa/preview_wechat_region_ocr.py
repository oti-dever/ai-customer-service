"""
Capture one calibrated WeChat region, save a debug PNG, and print OCR preview.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any

PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.ocr_engine import PaddleOCREngine
from rpa.common.screenshot import save_bgra_png
from rpa.common.wechat_capture import (
    CaptureResult,
    capture_wechat_chat,
    capture_wechat_contact_header,
    capture_wechat_conversation_list,
    capture_wechat_input_box,
    capture_wechat_search_box_ocr,
    capture_wechat_search_result_ocr,
    capture_wechat_window_rect,
)
from rpa.common.wechat_session import find_wechat_window


def _load_config() -> dict[str, Any]:
    path = Path(__file__).resolve().parent / "config" / "wechat_config.json"
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _capture_preview_region(hwnd: int, cfg: dict[str, Any], region_id: str) -> CaptureResult:
    """与主程序 wechat_capture 同一套矩形，避免预览与 Reader 漂移。"""
    if region_id == "chat_region":
        return capture_wechat_chat(hwnd, cfg)
    if region_id == "contact_header_region":
        cap = capture_wechat_contact_header(hwnd, cfg)
        if not cap:
            raise RuntimeError("contact_header_region 无效或未配置 w/h")
        return cap
    if region_id == "conversation_list_region":
        cap = capture_wechat_conversation_list(hwnd, cfg)
        if not cap:
            raise RuntimeError("conversation_list_region 无效")
        return cap
    if region_id == "input_box":
        cap = capture_wechat_input_box(hwnd, cfg)
        if not cap:
            cap = capture_wechat_window_rect(hwnd, 289, 615, 500, 80)
        if not cap:
            raise RuntimeError("input_box 截图失败")
        return cap
    if region_id == "search_box":
        cap = capture_wechat_search_box_ocr(hwnd, cfg)
        if not cap:
            raise RuntimeError("search_box OCR 区域无效或未配置")
        return cap
    if region_id == "search_result_region":
        cap = capture_wechat_search_result_ocr(hwnd, cfg)
        if not cap:
            raise RuntimeError("search_result OCR 区域无效或未配置")
        return cap
    raise ValueError(f"unsupported region_id: {region_id}")


def main() -> int:
    region_id = sys.argv[1] if len(sys.argv) > 1 else ""
    result: dict[str, Any] = {
        "ok": False,
        "region_id": region_id,
        "image_path": "",
        "text": "",
        "blocks": [],
        "error": "",
    }
    try:
        if not region_id:
            raise ValueError("missing region_id")
        cfg = _load_config()
        hwnd = find_wechat_window(cfg)
        if not hwnd:
            raise RuntimeError("未找到微信窗口，请确保微信已打开且未最小化")

        bgra, rw, rh, _ = _capture_preview_region(hwnd, cfg, region_id)
        debug_dir = Path(__file__).resolve().parent / "_debug" / "wechat"
        debug_dir.mkdir(parents=True, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        image_path = debug_dir / f"preview_{region_id}_{ts}.png"
        save_bgra_png(bgra, rw, rh, image_path)

        ocr_cfg = cfg.get("ocr") or {}
        ocr = PaddleOCREngine(
            lang="ch",
            min_confidence=float(ocr_cfg.get("min_confidence", 0.3)),
            max_side=int(ocr_cfg.get("max_side", 960)),
            invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
            det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
            det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
        )
        blocks = ocr.recognize(bgra, rw, rh)
        texts = [str(block[0]).strip() for block in blocks if str(block[0]).strip()]

        result["ok"] = True
        result["image_path"] = str(image_path)
        result["text"] = " | ".join(texts)
        result["blocks"] = texts[:20]
    except Exception as ex:
        result["error"] = str(ex)

    print("OCR_PREVIEW_JSON=" + json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
