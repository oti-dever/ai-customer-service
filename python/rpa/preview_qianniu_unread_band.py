"""
千牛：截取会话列表与红点带竖条，用于校准后自检（与微信 preview_wechat_unread_band 对应）。
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np

PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.qianniu_window import find_qianniu_hwnd
from rpa.common.screenshot import capture_region, save_bgra_png
from rpa.common.qianniu_session import get_conversation_list_region_px


def _band_x_bounds(w: int, unread_cfg: dict[str, Any]) -> tuple[int, int]:
    """与 unread_detector.detect_unread_rows 水平窗口一致。"""
    scan_x_ratio = float(unread_cfg.get("scan_x_ratio", 0.35))
    raw_end = unread_cfg.get("scan_x_end_ratio")
    end_r = scan_x_ratio if raw_end is None else float(raw_end)
    sr = float(unread_cfg.get("scan_x_start_ratio", 0.0))
    x_start = max(0, min(w - 1, int(w * max(0.0, min(1.0, sr)))))
    x_end = max(x_start + 1, min(w, int(w * max(0.0, min(1.0, end_r)))))
    return x_start, x_end


def _load_config() -> dict[str, Any]:
    path = Path(__file__).resolve().parent / "config" / "qianniu_config.json"
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
        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd:
            raise RuntimeError("未找到千牛接待中心窗口，请保持窗口可见")

        lx, ly, lw, lh = get_conversation_list_region_px(hwnd, cfg)
        if lw <= 0 or lh <= 0:
            raise RuntimeError("conversation_list_region 无效，请先校准会话列表区域")

        unread_cfg = cfg.get("unread_detection") or {}
        list_bgra, bw, bh, _method = capture_region(hwnd, lx, ly, lw, lh)
        if not list_bgra or bw <= 0 or bh <= 0:
            raise RuntimeError("会话列表截图失败")

        x0, x1 = _band_x_bounds(bw, unread_cfg)

        arr = np.frombuffer(list_bgra, dtype=np.uint8).reshape(bh, bw, 4)
        band = arr[:, x0:x1, :].tobytes()
        band_w = x1 - x0

        debug_dir = Path(__file__).resolve().parent / "_debug" / "qianniu"
        debug_dir.mkdir(parents=True, exist_ok=True)
        ts = time.strftime("%Y%m%d_%H%M%S")
        list_path = debug_dir / f"preview_unread_list_{ts}.png"
        band_path = debug_dir / f"preview_unread_band_{ts}.png"
        save_bgra_png(list_bgra, bw, bh, list_path)
        save_bgra_png(band, band_w, bh, band_path)

        result["ok"] = True
        result["list_image_path"] = str(list_path)
        result["band_image_path"] = str(band_path)
        result["overlay_hint"] = f"x=[{x0},{x1})/{bw}"
        result["scan_x_start_ratio"] = float(x0) / float(bw) if bw > 0 else 0.0
        result["scan_x_end_ratio"] = float(x1) / float(bw) if bw > 0 else 0.0
    except Exception as ex:
        result["error"] = str(ex)

    print("QIANNIU_UNREAD_BAND_PREVIEW_JSON=" + json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
