"""
Pinduoduo (商家后台网页) Reader skeleton (框架版).

在没有实际拼多多账号登录/页面结构不确定的情况下，本 Reader 仍能：
1) 正常启动并轮询目标浏览器窗口；
2) 按配置截图 + PaddleOCR + 布局解析；
3) 将“客户消息（direction=in）”写入 `rpa_inbox_messages`，供 Qt 端适配器消费。

后续只需要补齐：
* 浏览器窗口匹配规则（title/process/hwnd_hex）
* chat_region 坐标（x,y,w,h 或比例 left/right/top/bottom）
* layout 阈值（left_threshold/right_threshold/merge_y_gap）
"""

from __future__ import annotations

import json
import re
import time
from pathlib import Path
from typing import Any, Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import open_db, resolved_default_db_path, write_inbox_batch
from ..common.incremental import IncrementalDetector, content_hash, make_platform_msg_id
from ..common.layout_parser import parse_chat_layout, ParsedMessage
from ..common.ocr_engine import PaddleOCREngine
from ..common.qianniu_coords import (
    coordinate_space,
    rect_from_absolute_fields,
    rect_ratios_to_bitmap_xywh,
)
from ..common.screenshot import capture_region, is_window_minimized, is_window_valid
from ..common.win32_window import find_window, find_window_by_title_candidates

CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"
DEFAULT_CONFIG = CONFIG_DIR / "pdd_config.json"
STATE_DIR = Path(__file__).resolve().parents[1] / "_state"

PLATFORM = "pdd_web"


def load_config(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _space_for_region(cfg: dict[str, Any], region: dict[str, Any]) -> str:
    """
    PDD 坐标支持：
    - coordinates=client：比例相对客户区（不含标题栏/KPI）
    - coordinates=window：比例相对整窗
    """
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    # fallback：优先使用 chat_region 的 coordinates
    return coordinate_space(cfg)


def _rect_from_region_px(hwnd: int, cfg: dict[str, Any], region: dict[str, Any]) -> tuple[int, int, int, int]:
    abs_r = rect_from_absolute_fields(region)
    if abs_r:
        return abs_r

    space = _space_for_region(cfg, region)
    left = float(region.get("left_ratio", 0.1))
    top = float(region.get("top_ratio", 0.2))
    right = float(region.get("right_ratio", 0.9))
    bottom = float(region.get("bottom_ratio", 0.8))
    return rect_ratios_to_bitmap_xywh(hwnd, left, top, right, bottom, space)


def _find_pdd_hwnd(cfg: dict[str, Any]) -> Optional[int]:
    hwnd_hex = cfg.get("hwnd_hex")
    if hwnd_hex:
        try:
            hwnd = int(str(hwnd_hex), 16)
            if is_window_valid(hwnd):
                return hwnd
        except (ValueError, TypeError):
            pass

    wm = cfg.get("window_match") or {}
    process_name = wm.get("process_name", "")

    fallback_list = wm.get("title_fallback_list") or []
    if fallback_list:
        return find_window_by_title_candidates(
            title_substrings=[str(x) for x in fallback_list],
            process_name=str(process_name) if process_name else "",
        )

    title_contains = wm.get("title_contains") or "拼多多"
    return find_window(title_contains=str(title_contains), process_name=str(process_name) if process_name else "")


# ---------------------------------------------------------------------------
# Message content filtering
# ---------------------------------------------------------------------------

_PDD_NOISE_PATTERNS = [
    r"^\s*$",
    r"^\d{1,2}:\d{2}$",
    r"^\d{4}[年/\-]\d{1,2}[月/\-]\d{1,2}",
    r"^(昨天|前天|星期[一二三四五六日天])\s*\d{1,2}:\d{2}$",
    r"^更多消息$",
    r"^以下是新消息$",
]
_PDD_NOISE_RE = re.compile("|".join(_PDD_NOISE_PATTERNS))


def _is_valid_inbox_content(content: str) -> bool:
    s = content.strip()
    if len(s) < 2:
        return False
    if _PDD_NOISE_RE.search(s):
        return False
    # avoid "only punctuation/number" junk
    if re.match(r"^[\d\s\.\,\:\-\+\/\(\)\[\]x×_]+$", s):
        return False
    return True


def run_reader(
    config_path: Path = DEFAULT_CONFIG,
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 3,
) -> None:
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    cfg = load_config(config_path)
    region_cfg = cfg.get("chat_region") or {}
    layout_cfg = cfg.get("layout") or {}
    ocr_cfg = cfg.get("ocr") or {}
    conv_cfg = cfg.get("conversation") or {}

    platform_conv_id = str(
        conv_cfg.get("platform_conversation_id")
        or conv_cfg.get("conv_id")
        or conv_cfg.get("fallback_platform_conversation_id")
        or "pdd_default"
    )
    customer_name = str(
        conv_cfg.get("customer_name")
        or conv_cfg.get("default_customer_name")
        or "未知买家"
    )

    left_threshold = float(layout_cfg.get("left_threshold", 0.4))
    right_threshold = float(layout_cfg.get("right_threshold", 0.6))
    merge_y_gap = float(layout_cfg.get("merge_y_gap", 40))

    min_confidence = float(ocr_cfg.get("min_confidence", 0.5))
    max_side = int(ocr_cfg.get("max_side", 960))
    invert_for_dark_mode = bool(ocr_cfg.get("invert_for_dark_mode", True))
    det_thresh = float(ocr_cfg.get("det_thresh", 0.2))
    det_box_thresh = float(ocr_cfg.get("det_box_thresh", 0.4))

    fuzzy_threshold = float(layout_cfg.get("fuzzy_dedup_threshold", 0.85))

    poll_interval = float(cfg.get("poll_interval_sec", poll_interval_sec))

    state_path = STATE_DIR / "pdd_last_messages.json"
    STATE_DIR.mkdir(parents=True, exist_ok=True)

    rpa_phase(
        "pdd.reader",
        "ocr_init_start",
        "正在创建 PaddleOCR 引擎（首次加载可能较慢）",
    )
    ocr = PaddleOCREngine(
        lang=str(ocr_cfg.get("lang", "ch")),
        min_confidence=min_confidence,
        max_side=max_side,
        invert_for_dark_mode=invert_for_dark_mode,
        det_thresh=det_thresh,
        det_box_thresh=det_box_thresh,
    )
    ocr.warmup()
    rpa_phase("pdd.reader", "ocr_init_done", "OCR 引擎已就绪，即将进入截图轮询")
    detector = IncrementalDetector(
        max_window=100,
        state_path=state_path,
        fuzzy_threshold=fuzzy_threshold,
    )

    db = db_path or resolved_default_db_path()

    _scan_count = 0
    _last_hb = time.time()
    HB_SEC = 30.0
    _last_wait_hwnd_log = 0.0

    print(f"[PDD-Reader] DB={db} platform={PLATFORM} conv={platform_conv_id!r} interval={poll_interval}s")
    rpa_phase("pdd.reader", "poll_loop_enter", "已进入 while True")

    while True:
        hwnd = _find_pdd_hwnd(cfg)
        if not hwnd:
            now = time.time()
            if now - _last_wait_hwnd_log >= HB_SEC:
                rpa_heartbeat(
                    "pdd.reader",
                    "等待拼多多商家后台窗口（未匹配到 hwnd）；请检查 pdd_config.json 窗口规则",
                )
                _last_wait_hwnd_log = now
            print("[PDD-Reader] 未找到拼多多商家后台窗口，等待...")
            time.sleep(poll_interval)
            continue
        if is_window_minimized(hwnd):
            time.sleep(poll_interval)
            continue

        try:
            _scan_count += 1
            now = time.time()
            verbose = (_scan_count <= 3) or (now - _last_hb >= HB_SEC)
            if verbose:
                _last_hb = now
                print(f"[PDD-Reader] 扫描#{_scan_count} conv={platform_conv_id!r}")

            x, y, w, h = _rect_from_region_px(hwnd, cfg, region_cfg)
            if w <= 0 or h <= 0:
                time.sleep(poll_interval)
                continue

            bgra, rr_w, rr_h, _ = capture_region(hwnd, x, y, w, h)
            blocks = ocr.recognize(bgra, rr_w, rr_h)
            if not blocks:
                time.sleep(poll_interval)
                continue

            parsed: list[ParsedMessage] = parse_chat_layout(
                blocks,
                region_width=float(rr_w),
                left_threshold=left_threshold,
                right_threshold=right_threshold,
                merge_y_gap=merge_y_gap,
            )

            new_items: list[tuple[str, str]] = []
            new_messages = detector.filter_new(parsed, conv_id=platform_conv_id)
            for msg in new_messages:
                if msg.side != "in":
                    continue
                if not _is_valid_inbox_content(msg.content):
                    continue
                chash = content_hash(msg.side, msg.content)
                pid = make_platform_msg_id(PLATFORM, platform_conv_id, chash)
                new_items.append((msg.content, pid))

            if not new_items:
                time.sleep(poll_interval)
                continue

            conn = open_db(db)
            try:
                n = write_inbox_batch(conn, PLATFORM, platform_conv_id, customer_name, new_items)
                if verbose:
                    print(f"[PDD-Reader] 写入 {n} 条 inbox")
            finally:
                conn.close()

        except Exception as e:
            print(f"[PDD-Reader] 失败: {e}")

        time.sleep(poll_interval)


if __name__ == "__main__":
    run_reader()

