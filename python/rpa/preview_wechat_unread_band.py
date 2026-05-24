"""
Capture the WeChat conversation list and unread scan band after calibration.
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

from rpa.common.screenshot import save_bgra_png
from rpa.common.wechat_capture import (
    capture_wechat_conversation_list_hwnd,
    capture_wechat_unread_scan_band,
    rect_conversation_list,
    unread_band_x_bounds,
)
from rpa.common.wechat_session import find_wechat_window


def _load_config() -> dict[str, Any]:
    path = Path(__file__).resolve().parent / "config" / "wechat_config.json"
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    result: dict[str, Any] = {
        "ok": False,
        "list_image_path": "",
        "band_image_path": "",
        "overlay_hint": "",
        "scan_x_start_ratio": 0.0,
        "scan_x_end_ratio": 0.0,
        "error": "",
    }
    try:
        cfg = _load_config()
        hwnd = find_wechat_window(cfg)
        if not hwnd:
            raise RuntimeError("未找到微信窗口，请确保微信已打开且未最小化")

        list_cfg = cfg.get("conversation_list_region") or {}
        unread_cfg = cfg.get("unread_detection") or {}
        _lx, _ly, lw, lh = rect_conversation_list(list_cfg)
        if lw <= 0 or lh <= 0:
            raise RuntimeError("conversation_list_region 无效，请先校准会话列表区域")

        x_start, x_end = unread_band_x_bounds(lw, unread_cfg)
        scan_x_ratio = float(unread_cfg.get("scan_x_ratio", 0.35))
        start_ratio = float(unread_cfg.get("scan_x_start_ratio", 0.0))
        end_ratio = float(unread_cfg.get("scan_x_end_ratio", scan_x_ratio))

        cap_list = capture_wechat_conversation_list_hwnd(hwnd, list_cfg)
        cap_band = capture_wechat_unread_scan_band(hwnd, list_cfg, unread_cfg)
        if not cap_list or not cap_band:
            raise RuntimeError("会话列表或红点带截图失败")
        list_bgra, list_w, list_h, _ = cap_list
        band_bgra, band_w, band_h, _ = cap_band

        debug_dir = Path(__file__).resolve().parent / "_debug" / "wechat"
        debug_dir.mkdir(parents=True, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        list_path = debug_dir / f"preview_unread_list_{ts}.png"
        band_path = debug_dir / f"preview_unread_band_{ts}.png"
        save_bgra_png(list_bgra, list_w, list_h, list_path)
        save_bgra_png(band_bgra, band_w, band_h, band_path)

        result["ok"] = True
        result["list_image_path"] = str(list_path)
        result["band_image_path"] = str(band_path)
        result["overlay_hint"] = f"x=[{x_start},{x_end})/{lw}"
        result["scan_x_start_ratio"] = start_ratio
        result["scan_x_end_ratio"] = end_ratio
    except Exception as ex:
        result["error"] = str(ex)

    print("UNREAD_BAND_PREVIEW_JSON=" + json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
