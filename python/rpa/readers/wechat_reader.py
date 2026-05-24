"""
WeChat PC reader: capture chat region -> PaddleOCR -> layout parse -> write rpa_inbox_messages.
入库行的 original_timestamp 使用解析写入时刻（与 created_at 同批），供聚合界面展示；聊天区 OCR 时间仅作调试日志。

Key features (v2):
  - Red-dot-driven auto-switch: only switches to conversations with unread badges,
    replacing the slow round-robin cycle.
  - List OCR cache (D): periodic full conversation-list OCR; other polls reuse cache + red-dot scan.
  - Red-dot fallback (C): when a badge row does not align with cached list entries, click the row
    and resolve contact via header OCR, then enqueue.
  - Current-chat path: chat region OCR every poll; header + list reconcile throttled via reader_ocr.
  - Contact name stabilization: requires 2 consecutive consistent OCR readings
    before accepting a name change, preventing conversation splitting.
  - Fuzzy dedup: tolerates minor OCR character flicker between scans.
  - Enhanced noise filtering: filters timestamps, system prompts, UI chrome.
"""
from __future__ import annotations

from collections import deque
from datetime import datetime
import json
import re
import time
from pathlib import Path
from typing import Any, Deque, Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import open_db, resolved_default_db_path, write_inbox_batch
from ..common.incremental import IncrementalDetector, content_hash, make_platform_msg_id
from ..common.layout_parser import ParsedMessage, parse_chat_layout
from ..common.ocr_engine import PaddleOCREngine
from ..common.screenshot import get_window_rect, is_window_minimized, is_window_valid, save_bgra_png
from ..common.wechat_ocr_ops import ocr_bgra_blocks
from ..common.wechat_capture import (
    capture_wechat_chat,
    capture_wechat_contact_header,
    capture_wechat_conversation_list,
    capture_wechat_conversation_list_hwnd,
    capture_wechat_input_box,
    capture_wechat_search_box_ocr,
    capture_wechat_search_result_ocr,
    capture_wechat_unread_scan_band,
    rect_conversation_list,
    unread_band_x_bounds,
)
from ..common.wechat_session import (
    ConversationListEntry,
    build_platform_conversation_id,
    ensure_in_target_chat_background,
    find_wechat_window,
    contact_name_matches_target,
    normalize_contact_name,
    read_current_contact,
    reconcile_header_name_with_visible_list,
    switch_to_list_entry_background,
)
from .wechat_reader_discover import (
    ListOcrCache,
    execute_discover_unread_targets,
    refresh_list_cache_if_needed,
)
from ..common.window_lock import hold_platform_window_lock

CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"
DEFAULT_CONFIG = CONFIG_DIR / "wechat_config.json"
STATE_DIR = Path(__file__).resolve().parents[1] / "_state"
PLATFORM = "wechat_pc"


def _debug_snapshot_dir(cfg: dict[str, Any]) -> Path:
    dbg = cfg.get("debug") or {}
    raw = dbg.get("screenshot_dir")
    base = Path(raw) if raw else Path(__file__).resolve().parents[1] / "_debug" / "wechat"
    if not base.is_absolute():
        root = Path(__file__).resolve().parents[3]
        base = root / base
    return base


def _debug_log_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    return bool(dbg.get("log_parsed_messages", True))


def _phase_timing_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    return bool(dbg.get("log_phase_timing", False))


def _debug_log_max_len(cfg: dict[str, Any]) -> int:
    dbg = cfg.get("debug") or {}
    return max(20, int(dbg.get("log_max_content_len", 120)))


def _truncate_for_log(text: str, limit: int) -> str:
    s = str(text or "").replace("\r", " ").replace("\n", "\\n").strip()
    if len(s) <= limit:
        return s
    return s[: max(0, limit - 3)] + "..."


def _prune_debug_pngs(directory: Path, keep: int) -> None:
    if keep <= 0 or not directory.is_dir():
        return
    files = sorted(directory.glob("*.png"), key=lambda p: p.stat().st_mtime)
    while len(files) > keep:
        oldest = files.pop(0)
        try:
            oldest.unlink()
        except OSError:
            break


def _save_wechat_debug_frames(
    hwnd: int,
    cfg: dict[str, Any],
    scan_idx: int,
    chat_bgra: bytes,
    chat_w: int,
    chat_h: int,
) -> None:
    dbg = cfg.get("debug") or {}
    if not dbg.get("save_screenshots", False):
        return
    every = max(1, int(dbg.get("every_n_scans", 1)))
    if scan_idx % every != 0:
        return

    out_dir = _debug_snapshot_dir(cfg)
    ts = time.strftime("%Y%m%d_%H%M%S")
    stem = f"scan{scan_idx:06d}_{ts}"
    try:
        save_bgra_png(chat_bgra, chat_w, chat_h, out_dir / f"{stem}_chat.png")

        cap_h = capture_wechat_contact_header(hwnd, cfg)
        if cap_h:
            hb, hw2, hh2, _ = cap_h
            save_bgra_png(hb, hw2, hh2, out_dir / f"{stem}_header.png")

        cap_l = capture_wechat_conversation_list(hwnd, cfg)
        if cap_l:
            lb, lw2, lh2, _ = cap_l
            save_bgra_png(lb, lw2, lh2, out_dir / f"{stem}_list.png")

        cap_sb = capture_wechat_search_box_ocr(hwnd, cfg)
        if cap_sb:
            sb, sw2, sh2, _ = cap_sb
            save_bgra_png(sb, sw2, sh2, out_dir / f"{stem}_search_box.png")

        cap_sr = capture_wechat_search_result_ocr(hwnd, cfg)
        if cap_sr:
            rb, rw2, rh2, _ = cap_sr
            save_bgra_png(rb, rw2, rh2, out_dir / f"{stem}_search_result.png")

        cap_ib = capture_wechat_input_box(hwnd, cfg)
        if cap_ib:
            inp, iw2, ih2, _ = cap_ib
            save_bgra_png(inp, iw2, ih2, out_dir / f"{stem}_input_box.png")

        max_keep = int(dbg.get("max_png_files", 300))
        _prune_debug_pngs(out_dir, max_keep)
        print(
            f"[WeChat-Reader] 调试截图已保存 {out_dir / stem}_chat.png / _header.png / "
            "_list.png / _search_box.png / _search_result.png / _input_box.png"
        )
    except Exception as ex:
        print(f"[WeChat-Reader] 调试截图保存失败: {ex}")


def _save_wechat_unread_debug_frames(
    hwnd: int,
    cfg: dict[str, Any],
    scan_idx: int,
    unread_rows: list[tuple[int, float]],
) -> None:
    dbg = cfg.get("debug") or {}
    if not dbg.get("save_screenshots", False):
        return
    every = max(1, int(dbg.get("every_n_scans", 1)))
    if scan_idx % every != 0:
        return

    list_cfg = cfg.get("conversation_list_region") or {}
    unread_cfg = cfg.get("unread_detection") or {}
    _, _, lw, lh = rect_conversation_list(list_cfg)
    if lw <= 0 or lh <= 0:
        return

    x_start, x_end = unread_band_x_bounds(lw, unread_cfg)

    out_dir = _debug_snapshot_dir(cfg)
    ts = time.strftime("%Y%m%d_%H%M%S")
    stem = f"scan{scan_idx:06d}_{ts}"
    try:
        cap_list = capture_wechat_conversation_list_hwnd(hwnd, list_cfg)
        if not cap_list:
            return
        list_bgra, list_w, list_h, _ = cap_list
        save_bgra_png(list_bgra, list_w, list_h, out_dir / f"{stem}_unread_list.png")

        cap_band = capture_wechat_unread_scan_band(hwnd, list_cfg, unread_cfg)
        if cap_band:
            band_bgra, band_w, band_h, _ = cap_band
            save_bgra_png(band_bgra, band_w, band_h, out_dir / f"{stem}_unread_scan_band.png")

        try:
            from PIL import Image, ImageDraw

            img = Image.frombuffer("RGBA", (list_w, list_h), list_bgra, "raw", "BGRA", 0, 1).copy()
            draw = ImageDraw.Draw(img)
            draw.rectangle(
                [(x_start, 0), (max(x_start + 1, x_end - 1), max(1, list_h - 1))],
                outline=(255, 80, 80, 255),
                width=2,
            )
            row_height = max(1, int(unread_cfg.get("row_height", 65)))
            for row_idx, y_center in unread_rows:
                y0 = max(0, int(row_idx * row_height))
                y1 = min(list_h - 1, int((row_idx + 1) * row_height) - 1)
                draw.rectangle([(0, y0), (list_w - 1, y1)], outline=(80, 220, 120, 220), width=2)
                draw.text((x_end + 4, max(0, int(y_center) - 10)), f"row {row_idx}", fill=(80, 220, 120, 255))
            img.save(str(out_dir / f"{stem}_unread_overlay.png"), "PNG")
        except Exception as overlay_ex:
            print(f"[WeChat-Reader] 未读调试叠加图保存失败: {overlay_ex}")

        max_keep = int(dbg.get("max_png_files", 300))
        _prune_debug_pngs(out_dir, max_keep)
        print(
            "[WeChat-Reader] 未读调试截图已保存 "
            f"band=({x_start},{x_end}) rows={unread_rows!r} "
            f"-> {out_dir / (stem + '_unread_overlay.png')}"
        )
    except Exception as ex:
        print(f"[WeChat-Reader] 未读调试截图保存失败: {ex}")


# ---------------------------------------------------------------------------
# Contact name stabilization
# ---------------------------------------------------------------------------

class NameStabilizer:
    """
    Prevents conversation splitting from OCR flicker by requiring
    multiple consecutive consistent readings before accepting a name change.
    """

    def __init__(self, required_consistent: int = 2):
        self.current: Optional[str] = None
        self._required = required_consistent
        self._candidate: Optional[str] = None
        self._candidate_count: int = 0

    def update(self, name: Optional[str]) -> Optional[str]:
        """Feed an OCR reading. Returns the stabilized name."""
        if not name:
            return self.current
        if self.current is None:
            self.current = name
            return self.current
        if name == self.current:
            self._candidate = None
            self._candidate_count = 0
            return self.current
        # Name differs — require N consecutive identical readings
        if name == self._candidate:
            self._candidate_count += 1
            if self._candidate_count >= self._required:
                self.current = name
                self._candidate = None
                self._candidate_count = 0
        else:
            self._candidate = name
            self._candidate_count = 1
        return self.current

    def force_set(self, name: str) -> None:
        """Force-accept name after an intentional conversation switch."""
        if name:
            self.current = name
            self._candidate = None
            self._candidate_count = 0


# ---------------------------------------------------------------------------
# Config & window discovery
# ---------------------------------------------------------------------------

def load_config(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def find_wechat_hwnd(cfg: dict) -> Optional[int]:
    return find_wechat_window(cfg)


# ---------------------------------------------------------------------------
# Noise filtering
# ---------------------------------------------------------------------------

_NOISE_PATTERNS = [
    # Debug / internal labels
    r"\[WeChat-Reader\]",
    r"扫描#\d+",
    r"识别\s*\d*\s*块",
    r"联系人\s*=",
    r"人\s*=\s*[^)]+\)",
    r"^\d+\s*[x×]\s*[\dY]",
    r"^\s*[\[\]\(\)]+\s*$",
    # Timestamps & date labels
    r"^\d{1,2}:\d{2}$",
    r"^\d{4}[年/\-]\d{1,2}[月/\-]\d{1,2}",
    r"^(昨天|前天|星期[一二三四五六日天])\s*\d{1,2}:\d{2}$",
    # WeChat system / UI chrome
    r"^查看更多消息$",
    r"^以下是新消息$",
    r"^以上是打招呼的内容",
    r"^消息已发出.*可能对方不是你的朋友",
    r"^对方正在输入",
    r"^你已添加了.+现在可以开始聊天了",
]
_NOISE_RE = re.compile("|".join(_NOISE_PATTERNS))
# 单字回复：中文 / 字母 / 数字（如「来」「好」「1」）
_WECHAT_SHORT_REPLY_RE = re.compile(r"^[\u4e00-\u9fa5A-Za-z0-9]$")


def _is_valid_inbox_content(content: str) -> bool:
    """Filter garbled text, debug artifacts and UI chrome; keep real chat messages.

    不再按「纯数字串」过滤：用户可能发手机号、验证码等；去重由 IncrementalDetector 按正文处理。
    """
    s = content.strip()
    if len(s) < 2 and not _WECHAT_SHORT_REPLY_RE.match(s):
        return False
    if _NOISE_RE.search(s):
        return False
    if "∞" in s or (s.count(")") > 2 and len(s) < 10):
        return False
    return True


# ---------------------------------------------------------------------------
# Header OCR: extract current contact name
# ---------------------------------------------------------------------------

def _extract_contact_name(
    ocr: PaddleOCREngine, hwnd: int, header_cfg: dict,
) -> Optional[str]:
    """OCR the header region to read the currently active contact name."""
    return read_current_contact(ocr, hwnd, header_cfg)


# ---------------------------------------------------------------------------
# Red-dot-based unread detection
# ---------------------------------------------------------------------------

def _drain_wechat_incremental_purge_file(
    detector: IncrementalDetector,
    state_dir: Path,
) -> None:
    """
    消费 Qt 侧「清空/删除会话」时写入的 purge 文件，丢弃该 platform_conversation_id
    在 wechat_last_messages.json 中的增量去重状态；否则仅删 DB 时 OCR 仍会被判重复。
    """
    path = state_dir / "reader_incremental_purge_wechat_pc.txt"
    if not path.exists():
        return
    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        path.unlink()
    except OSError as ex:
        print(f"[WeChat-Reader] 读取增量 purge 文件失败: {ex}")
        return
    for line in lines:
        pcid = line.strip()
        if not pcid or pcid.startswith("#"):
            continue
        detector.purge_conversation(pcid)
        print(
            "[WeChat-Reader] 已丢弃增量去重状态 "
            f"platform_conversation_id={pcid!r}（与聚合界面清空/删会话同步）"
        )


# ---------------------------------------------------------------------------
# Core: process current chat
# ---------------------------------------------------------------------------

def _process_current_chat(
    ocr: PaddleOCREngine,
    hwnd: int,
    left_threshold: float, right_threshold: float, merge_y_gap: float,
    detector: IncrementalDetector,
    contact_name: str,
    conv_id_prefix: str,
    db: Path,
    cfg: dict[str, Any],
    scan_idx: int,
    verbose: bool = False,
) -> int:
    """OCR chat region, parse layout, write new inbound messages. Returns count written."""
    platform_conv_id = build_platform_conversation_id(contact_name, conv_id_prefix)
    log_enabled = _debug_log_enabled(cfg)
    log_limit = _debug_log_max_len(cfg)
    timing = _phase_timing_enabled(cfg)
    t0 = time.monotonic() if timing else 0.0
    bgra, w, h, _ = capture_wechat_chat(hwnd, cfg)
    t1 = time.monotonic() if timing else 0.0
    _save_wechat_debug_frames(hwnd, cfg, scan_idx, bgra, w, h)
    t2 = time.monotonic() if timing else 0.0
    blocks = ocr_bgra_blocks(ocr, bgra, w, h)
    t3 = time.monotonic() if timing else 0.0
    if not blocks:
        if timing:
            print(
                "[WeChat-Reader] 耗时 聊天区 "
                f"scan#{scan_idx} contact={contact_name!r} "
                f"capture={(t1 - t0) * 1000:.0f}ms "
                f"debug_png={(t2 - t1) * 1000:.0f}ms "
                f"ocr={(t3 - t2) * 1000:.0f}ms "
                "parse=0ms db=0ms (无识别块)"
            )
        return 0

    messages = parse_chat_layout(
        blocks,
        region_width=float(w),
        platform="wechat",
        left_threshold=left_threshold,
        right_threshold=right_threshold,
        merge_y_gap=merge_y_gap,
    )
    new_messages = detector.filter_new(messages, conv_id=platform_conv_id)
    new_msg_ids = {id(m) for m in new_messages}
    t4 = time.monotonic() if timing else 0.0

    if verbose:
        in_count = sum(1 for m in messages if m.side == "in")
        print(
            f"[WeChat-Reader] 解析: {len(messages)} 条消息 "
            f"(in={in_count}), 新增={len(new_messages)}"
        )
    if log_enabled and messages:
        for idx, msg in enumerate(messages, start=1):
            print(
                "[WeChat-Reader] OCR消息 "
                f"#{idx} side={msg.side} "
                f"sender={_truncate_for_log(msg.sender_name or contact_name, log_limit)!r} "
                f"ts={_truncate_for_log(msg.original_timestamp, log_limit)!r} "
                f"content={_truncate_for_log(msg.content, log_limit)!r}"
            )

    if log_enabled:
        for msg in messages:
            if msg.side != "in":
                continue
            if id(msg) in new_msg_ids:
                continue
            print(
                "[WeChat-Reader] 跳过消息 "
                f"sender={_truncate_for_log(msg.sender_name or contact_name, log_limit)!r} "
                f"ts={_truncate_for_log(msg.original_timestamp, log_limit)!r} "
                f"content={_truncate_for_log(msg.content, log_limit)!r} "
                "(原因=增量去重：已见于 wechat_last_messages.json 或本扫描内已收录)"
            )

    if not new_messages:
        if timing:
            print(
                "[WeChat-Reader] 耗时 聊天区 "
                f"scan#{scan_idx} contact={contact_name!r} "
                f"capture={(t1 - t0) * 1000:.0f}ms "
                f"debug_png={(t2 - t1) * 1000:.0f}ms "
                f"ocr={(t3 - t2) * 1000:.0f}ms "
                f"parse={(t4 - t3) * 1000:.0f}ms "
                "db=0ms (无新消息)"
            )
        return 0

    # 入库时间与展示时间：用解析写入时刻，不用聊天区 OCR 到的时间标签
    batch_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    items: list[tuple[str, str, str, str]] = []
    for msg in new_messages:
        if msg.side != "in":
            continue
        if not _is_valid_inbox_content(msg.content):
            if log_enabled:
                print(
                    "[WeChat-Reader] 跳过消息 "
                    f"sender={_truncate_for_log(msg.sender_name or contact_name, log_limit)!r} "
                    f"ts={_truncate_for_log(msg.original_timestamp, log_limit)!r} "
                    f"content={_truncate_for_log(msg.content, log_limit)!r} "
                    "(原因=噪声过滤)"
                )
            continue
        chash = content_hash(msg.side, msg.content)
        pid = make_platform_msg_id(PLATFORM, platform_conv_id, chash)
        items.append((msg.content, pid, msg.sender_name, batch_time))
        if log_enabled:
            print(
                "[WeChat-Reader] 新入站消息 "
                f"contact={_truncate_for_log(contact_name, log_limit)!r} "
                f"sender={_truncate_for_log(msg.sender_name or contact_name, log_limit)!r} "
                f"ingest_ts={batch_time!r} "
                f"ocr_ts={_truncate_for_log(msg.original_timestamp, log_limit)!r} "
                f"content={_truncate_for_log(msg.content, log_limit)!r}"
            )

    if not items:
        if timing:
            print(
                "[WeChat-Reader] 耗时 聊天区 "
                f"scan#{scan_idx} contact={contact_name!r} "
                f"capture={(t1 - t0) * 1000:.0f}ms "
                f"debug_png={(t2 - t1) * 1000:.0f}ms "
                f"ocr={(t3 - t2) * 1000:.0f}ms "
                f"parse={(t4 - t3) * 1000:.0f}ms "
                "db=0ms (噪声过滤后无入库)"
            )
        return 0

    conn = open_db(db)
    try:
        n = write_inbox_batch(
            conn, PLATFORM, platform_conv_id, contact_name, items, at_time=batch_time
        )
        if n > 0:
            print(f"[WeChat-Reader] 写入 {n} 条 inbox (联系人={contact_name})")
        t5 = time.monotonic() if timing else 0.0
        if timing:
            print(
                "[WeChat-Reader] 耗时 聊天区 "
                f"scan#{scan_idx} contact={contact_name!r} "
                f"capture={(t1 - t0) * 1000:.0f}ms "
                f"debug_png={(t2 - t1) * 1000:.0f}ms "
                f"ocr={(t3 - t2) * 1000:.0f}ms "
                f"parse={(t4 - t3) * 1000:.0f}ms "
                f"db={(t5 - t4) * 1000:.0f}ms "
                f"written={n}"
            )
        return n
    finally:
        conn.close()


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def run_reader(
    config_path: Path = DEFAULT_CONFIG,
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 3,
    debug_dir: Optional[Path] = None,
) -> None:
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    cfg = load_config(config_path)
    if bool(cfg.get("production_mode", False)):
        dbg = dict(cfg.get("debug") or {})
        dbg["save_screenshots"] = False
        dbg["log_parsed_messages"] = False
        cfg["debug"] = dbg

    region = cfg.get("chat_region") or {}
    conv = cfg.get("conversation") or {}
    layout_cfg = cfg.get("layout") or {}
    ocr_cfg = cfg.get("ocr") or {}
    header_cfg = cfg.get("contact_header_region") or {}
    list_cfg = cfg.get("conversation_list_region") or {}
    unread_cfg = cfg.get("unread_detection") or {}
    reader_ocr = cfg.get("reader_ocr") or {}
    list_refresh_sec = float(reader_ocr.get("list_full_refresh_sec", 12.0))
    list_refresh_scans = int(reader_ocr.get("list_full_refresh_scans", 0))
    header_refresh_sec = float(reader_ocr.get("current_chat_header_refresh_sec", 8.0))
    header_refresh_scans = int(reader_ocr.get("current_chat_header_refresh_scans", 4))
    red_dot_header_settle_sec = float(reader_ocr.get("red_dot_header_click_settle_sec", 0.45))
    list_switch_settle_sec = float(reader_ocr.get("list_switch_settle_sec", 0.42))
    list_switch_max_retry = max(0, int(reader_ocr.get("list_switch_max_retry", 2)))

    chat_x = int(region.get("x", 260))
    chat_y = int(region.get("y", 90))
    chat_w = int(region.get("w", 720))
    chat_h = int(region.get("h", 760))

    default_customer_name = str(
        conv.get("default_customer_name", conv.get("customer_name", "未知联系人"))
    )
    conv_id_prefix = str(conv.get("conv_id_prefix", "wechat_"))

    left_threshold = float(layout_cfg.get("left_threshold", 0.4))
    right_threshold = float(layout_cfg.get("right_threshold", 0.6))
    merge_y_gap = float(layout_cfg.get("merge_y_gap", 40))

    min_confidence = float(ocr_cfg.get("min_confidence", 0.5))
    max_side = int(ocr_cfg.get("max_side", 960))
    invert_for_dark_mode = bool(ocr_cfg.get("invert_for_dark_mode", True))
    det_thresh = float(ocr_cfg.get("det_thresh", 0.2))
    det_box_thresh = float(ocr_cfg.get("det_box_thresh", 0.4))
    poll_interval = float(cfg.get("poll_interval_sec", poll_interval_sec))

    auto_switch = bool(unread_cfg.get("enabled", list_cfg.get("auto_switch", False)))
    exclude_names = {
        normalize_contact_name(x)
        for x in (list_cfg.get("exclude") or [])
        if normalize_contact_name(str(x))
    }
    lock_cfg = cfg.get("window_lock") or {}
    unread_cooldown_sec = float(unread_cfg.get("cooldown_sec", max(1.2, poll_interval)))
    max_pending_targets = max(1, int(unread_cfg.get("max_pending_targets", 12)))
    scan_current_chat = bool(unread_cfg.get("scan_current_chat", True))
    row_height_guess = max(
        40.0,
        float(unread_cfg.get("row_height", list_cfg.get("row_height_guess", 65))),
    )

    state_path = STATE_DIR / "wechat_last_messages.json"
    STATE_DIR.mkdir(parents=True, exist_ok=True)

    rpa_phase(
        "wechat.reader",
        "ocr_init_start",
        "正在创建 PaddleOCR 引擎（首次加载可能较慢）",
    )
    ocr = PaddleOCREngine(
        lang="ch",
        min_confidence=min_confidence,
        max_side=max_side,
        invert_for_dark_mode=invert_for_dark_mode,
        det_thresh=det_thresh,
        det_box_thresh=det_box_thresh,
    )
    ocr.warmup()
    rpa_phase("wechat.reader", "ocr_init_done", "OCR 引擎已就绪，即将进入截图轮询")
    detector = IncrementalDetector(
        max_window=100, state_path=state_path, fuzzy_threshold=0.85,
    )
    stabilizer = NameStabilizer()

    db = db_path or resolved_default_db_path()
    dbg = cfg.get("debug") or {}

    print(
        f"[WeChat-Reader] DB={db} region=({chat_x},{chat_y},{chat_w}x{chat_h}) "
        f"interval={poll_interval}s"
    )
    if bool(cfg.get("production_mode", False)):
        print(
            "[WeChat-Reader] production_mode=已开启：已关闭每轮调试截图与 OCR 逐条解析日志；"
            "需要排障时在 wechat_config.json 将 production_mode 设为 false"
        )
    if dbg.get("save_screenshots"):
        print(
            f"[WeChat-Reader] 调试截图已开启 → {_debug_snapshot_dir(cfg)} "
            f"(每 {max(1, int(dbg.get('every_n_scans', 1)))} 次扫描保存 chat/header/list)"
        )
    if _debug_log_enabled(cfg):
        print(
            "[WeChat-Reader] OCR解析日志已开启 "
            f"(max_content_len={_debug_log_max_len(cfg)})"
        )
    if _phase_timing_enabled(cfg):
        print(
            "[WeChat-Reader] 阶段耗时日志已开启 (debug.log_phase_timing)："
            "每轮「发现未读」与「处理会话」及聊天区分项 ms"
        )
    if auto_switch:
        print("[WeChat-Reader] 红点检测自动切换已启用")
    if list_refresh_sec > 0 or list_refresh_scans > 0:
        print(
            "[WeChat-Reader] 会话列表OCR缓存刷新 "
            f"sec={list_refresh_sec} scans={list_refresh_scans}"
        )
    else:
        print("[WeChat-Reader] 会话列表OCR每轮全量（reader_ocr 未限制 list 刷新）")
    if scan_current_chat:
        if header_refresh_sec <= 0 and header_refresh_scans <= 0:
            print("[WeChat-Reader] 当前会话标题OCR每轮执行（reader_ocr 未限制 header 刷新）")
        else:
            print(
                "[WeChat-Reader] 当前会话标题OCR降频 "
                f"sec={header_refresh_sec} scans={header_refresh_scans}"
            )
    if exclude_names:
        print(f"[WeChat-Reader] 已排除 {len(exclude_names)} 个非联系人: {', '.join(sorted(exclude_names))}")

    _scan_count = 0
    _last_heartbeat = time.time()
    HEARTBEAT_INTERVAL = 30
    _last_wait_hwnd_log = 0.0
    _pending_targets: Deque[ConversationListEntry] = deque()
    _pending_target_set: set[str] = set()
    _last_processed_at: dict[str, float] = {}
    _list_cache = ListOcrCache()
    _last_header_refresh_time: float = 0.0
    _cached_contact_display: Optional[str] = None
    _force_next_header_ocr: bool = False
    rpa_phase("wechat.reader", "poll_loop_enter", "已进入 while True")

    def _with_window_lock(hwnd: int, action: str, fn):
        if lock_cfg.get("enabled", True):
            with hold_platform_window_lock(
                platform=PLATFORM,
                owner=f"reader:{action}",
                timeout_sec=float(lock_cfg.get("timeout_sec", 15.0)),
                retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
            ) as lock:
                if not lock.acquired:
                    print(f"[WeChat-Reader] 等待窗口锁超时，跳过{action}")
                    return None
                return fn(hwnd)
        return fn(hwnd)

    def _read_active_contact(hwnd: int) -> str:
        nonlocal _last_header_refresh_time, _cached_contact_display, _force_next_header_ocr
        now = time.time()
        need_header = bool(_force_next_header_ocr)
        _force_next_header_ocr = False
        if _cached_contact_display is None:
            need_header = True
        if not need_header:
            if header_refresh_sec <= 0 and header_refresh_scans <= 0:
                need_header = True
            else:
                if header_refresh_sec > 0 and (now - _last_header_refresh_time >= header_refresh_sec):
                    need_header = True
                if header_refresh_scans > 0 and _scan_count > 0 and _scan_count % header_refresh_scans == 0:
                    need_header = True
        if not need_header:
            out = _cached_contact_display or stabilizer.current or default_customer_name
            if _debug_log_enabled(cfg):
                print(
                    "[WeChat-Reader] 标题OCR跳过(降频)，沿用缓存显示名 "
                    f"display={_truncate_for_log(out, _debug_log_max_len(cfg))!r}"
                )
            return out

        raw: Optional[str] = None
        if header_cfg:
            raw = _extract_contact_name(ocr, hwnd, header_cfg)
        if raw and list_cfg:
            try:
                entries = refresh_list_cache_if_needed(
                    ocr,
                    hwnd,
                    cfg,
                    _list_cache,
                    _scan_count,
                    list_refresh_sec,
                    list_refresh_scans,
                    _debug_log_enabled(cfg),
                )
                if entries:
                    fixed = reconcile_header_name_with_visible_list(raw, entries)
                    if fixed and fixed != raw and _debug_log_enabled(cfg):
                        print(
                            "[WeChat-Reader] 标题与会话列表校对 "
                            f"header={_truncate_for_log(raw, _debug_log_max_len(cfg))!r} "
                            f"-> list={_truncate_for_log(fixed, _debug_log_max_len(cfg))!r}"
                        )
                    raw = fixed or raw
            except Exception:
                pass
        stable = stabilizer.update(raw)
        if _debug_log_enabled(cfg) and (raw or stable):
            print(
                "[WeChat-Reader] 标题OCR "
                f"raw={_truncate_for_log(raw or '', _debug_log_max_len(cfg))!r} "
                f"stable={_truncate_for_log(stable or '', _debug_log_max_len(cfg))!r}"
            )
        display = stable if stable else default_customer_name
        _cached_contact_display = display
        _last_header_refresh_time = now
        return display

    def _queue_target(entry: ConversationListEntry, force: bool = False) -> bool:
        normalized = normalize_contact_name(entry.name)
        if not normalized or normalized in exclude_names:
            return False
        if normalized in _pending_target_set:
            return False
        if not force:
            last_processed = _last_processed_at.get(normalized, 0.0)
            if time.time() - last_processed < unread_cooldown_sec:
                return False
        if len(_pending_targets) >= max_pending_targets:
            return False
        _pending_targets.append(
            ConversationListEntry(
                name=normalized,
                y_center=entry.y_center,
                row_index=entry.row_index,
                confidence=entry.confidence,
            )
        )
        _pending_target_set.add(normalized)
        return True

    def _pop_pending_target() -> Optional[ConversationListEntry]:
        while _pending_targets:
            entry = _pending_targets.popleft()
            _pending_target_set.discard(entry.name)
            if entry.name:
                return entry
        return None

    def _discover_unread_targets(hwnd: int) -> int:
        return execute_discover_unread_targets(
            ocr=ocr,
            hwnd=hwnd,
            cfg=cfg,
            scan_count=_scan_count,
            auto_switch=auto_switch,
            list_cfg=list_cfg,
            unread_cfg=unread_cfg,
            row_height_guess=row_height_guess,
            exclude_names=exclude_names,
            max_pending_targets=max_pending_targets,
            red_dot_header_settle_sec=red_dot_header_settle_sec,
            header_cfg=header_cfg or {},
            list_refresh_sec=list_refresh_sec,
            list_refresh_scans=list_refresh_scans,
            list_cache=_list_cache,
            pending_len=lambda: len(_pending_targets),
            queue_target=_queue_target,
            save_unread_debug_frames=lambda rows: _save_wechat_unread_debug_frames(
                hwnd, cfg, _scan_count, rows,
            ),
            debug_log_enabled=lambda: _debug_log_enabled(cfg),
            pending_names_snapshot=lambda: [item.name for item in _pending_targets],
        )

    def _process_target(
        hwnd: int,
        target_name: str,
        verbose: bool,
        entry: Optional[ConversationListEntry] = None,
    ) -> bool:
        target_name = normalize_contact_name(target_name)
        if not target_name or target_name in exclude_names:
            return False
        if entry:
            print(
                "[WeChat-Reader] 红点目标准备后台切换 "
                f"name={target_name} row={entry.row_index} "
                f"y={entry.y_center:.1f} conf={entry.confidence:.2f}"
            )
            list_ok = switch_to_list_entry_background(
                ocr,
                hwnd,
                header_cfg,
                list_cfg,
                entry,
                settle_sec=list_switch_settle_sec,
                max_retry=list_switch_max_retry,
            )
            if list_ok:
                print(f"[WeChat-Reader] 红点目标已通过后台列表切换命中: {target_name}")
                stabilizer.force_set(target_name)
                print(f"[WeChat-Reader] 处理会话: {target_name}")
                _process_current_chat(
                    ocr, hwnd,
                    left_threshold, right_threshold, merge_y_gap,
                    detector, target_name, conv_id_prefix, db, cfg, _scan_count,
                    verbose=verbose,
                )
                _last_processed_at[target_name] = time.time()
                return True
            if ocr and header_cfg:
                time.sleep(0.12)
                cur_after_list = read_current_contact(ocr, hwnd, header_cfg)
                if cur_after_list and contact_name_matches_target(
                    cur_after_list, target_name
                ):
                    print(
                        "[WeChat-Reader] 列表切换校验未通过，但标题区已匹配目标，跳过搜索 "
                        f"target={target_name!r} current={cur_after_list!r}"
                    )
                    stabilizer.force_set(target_name)
                    print(f"[WeChat-Reader] 处理会话: {target_name}")
                    _process_current_chat(
                        ocr, hwnd,
                        left_threshold, right_threshold, merge_y_gap,
                        detector, target_name, conv_id_prefix, db, cfg, _scan_count,
                        verbose=verbose,
                    )
                    _last_processed_at[target_name] = time.time()
                    return True
            print(f"[WeChat-Reader] 后台列表切换未命中，回退搜索框路由: {target_name}")
        if not ensure_in_target_chat_background(
            ocr, hwnd, header_cfg, cfg, target_name
        ):
            print(f"[WeChat-Reader] 未能切换到目标会话: {target_name}")
            return False
        stabilizer.force_set(target_name)
        print(f"[WeChat-Reader] 处理会话: {target_name}")
        _process_current_chat(
            ocr, hwnd,
            left_threshold, right_threshold, merge_y_gap,
            detector, target_name, conv_id_prefix, db, cfg, _scan_count,
            verbose=verbose,
        )
        _last_processed_at[target_name] = time.time()
        return True

    while True:
        _drain_wechat_incremental_purge_file(detector, STATE_DIR)

        hwnd = find_wechat_hwnd(cfg)
        if not hwnd:
            now = time.time()
            if now - _last_wait_hwnd_log >= HEARTBEAT_INTERVAL:
                rpa_heartbeat("wechat.reader", "等待微信窗口（未找到句柄）；请打开主窗口")
                _last_wait_hwnd_log = now
            print("[WeChat-Reader] 未找到微信窗口，等待...")
            time.sleep(poll_interval)
            continue
        if is_window_minimized(hwnd):
            time.sleep(poll_interval)
            continue

        try:
            _scan_count += 1
            now = time.time()
            verbose = _scan_count <= 3

            if verbose or now - _last_heartbeat >= HEARTBEAT_INTERVAL:
                queued = [item.name for item in _pending_targets]
                print(f"[WeChat-Reader] 扫描#{_scan_count} (待处理={queued!r})")
                _last_heartbeat = now

            _ph_time = _phase_timing_enabled(cfg)
            if _ph_time:
                _t_disc = time.monotonic()
            _with_window_lock(hwnd, "发现未读目标", _discover_unread_targets)
            if _ph_time:
                print(
                    "[WeChat-Reader] 耗时 发现未读 "
                    f"scan#{_scan_count} {(time.monotonic() - _t_disc) * 1000:.0f}ms"
                )

            target_entry = _pop_pending_target()
            if target_entry:
                if _ph_time:
                    _t_proc = time.monotonic()
                _with_window_lock(
                    hwnd,
                    f"处理队列会话:{target_entry.name}",
                    lambda locked_hwnd: _process_target(
                        locked_hwnd,
                        target_entry.name,
                        verbose=True,
                        entry=target_entry,
                    ),
                )
                if _ph_time:
                    print(
                        "[WeChat-Reader] 耗时 处理队列 "
                        f"scan#{_scan_count} target={target_entry.name!r} "
                        f"{(time.monotonic() - _t_proc) * 1000:.0f}ms"
                    )
                _force_next_header_ocr = True
            elif scan_current_chat:
                def _scan_current(locked_hwnd: int) -> None:
                    current_name = _read_active_contact(locked_hwnd)
                    if normalize_contact_name(current_name) in exclude_names:
                        print(f"[WeChat-Reader] 跳过当前非联系人: {current_name}")
                        return
                    _process_target(locked_hwnd, current_name, verbose=verbose)

                if _ph_time:
                    _t_cur = time.monotonic()
                _with_window_lock(hwnd, "处理当前会话", _scan_current)
                if _ph_time:
                    print(
                        "[WeChat-Reader] 耗时 处理当前会话 "
                        f"scan#{_scan_count} {(time.monotonic() - _t_cur) * 1000:.0f}ms"
                    )
        except Exception as e:
            print(f"[WeChat-Reader] 失败: {e}")

        time.sleep(poll_interval)


if __name__ == "__main__":
    run_reader()
