"""
微信 Reader 未读发现：列表 OCR 缓存、红点扫描、与队列入队。

与 wechat_reader.run_reader 主循环解耦，仅通过 ListOcrCache / 回调协作。
"""
from __future__ import annotations

from dataclasses import dataclass, field
import time
from typing import Any, Callable, List, Optional, Set

from ..common.unread_detector import detect_unread_rows
from ..common.wechat_capture import CaptureResult, capture_wechat_conversation_list_hwnd
from ..common.wechat_ocr_ops import ocr_bgra_blocks, ocr_from_capture
from ..common.wechat_session import (
    ConversationListEntry,
    click_conversation_row_background,
    match_unread_rows_to_entries,
    merge_ocr_blocks_to_line,
    normalize_contact_name,
    parse_conversation_list_blocks,
    parse_visible_conversation_list,
    read_current_contact,
)


def _bgra_horizontal_band(bgra: bytes, w: int, h: int, y0: int, y1: int) -> tuple[bytes, int, int]:
    """裁出 [y0, y1) 行（含上不含下），BGRA 每像素 4 字节。"""
    y0 = max(0, min(h, y0))
    y1 = max(y0 + 1, min(h, y1))
    stride = w * 4
    return bgra[y0 * stride : y1 * stride], w, y1 - y0


def list_row_title_from_list_capture(
    ocr: Any,
    cap: CaptureResult,
    y_center: float,
    row_height: int,
) -> Optional[str]:
    """在会话列表子图上按红点 y 裁一条再 OCR，用于策略 C 点击前预检排除项（不切会话）。"""
    try:
        bgra, w, h, _ = cap
        if w <= 0 or h <= 0 or not bgra:
            return None
        half = max(14, int(row_height) // 2)
        cy = float(y_center)
        y0 = int(max(0, min(h - 1, cy - half)))
        y1 = int(max(y0 + 1, min(h, cy + half)))
        band, bw, bh = _bgra_horizontal_band(bgra, w, h, y0, y1)
        blocks = ocr_bgra_blocks(ocr, band, bw, bh)
        line = merge_ocr_blocks_to_line(blocks)
        return line if line else None
    except Exception:
        return None


def list_row_match_tolerance(row_height_guess: float) -> float:
    return max(26.0, float(row_height_guess) * 0.75)


def unread_rows_not_matched_by_entries(
    entries: list[ConversationListEntry],
    unread: list[tuple[int, float]],
    row_height_guess: float,
) -> list[tuple[int, float]]:
    if not unread:
        return []
    if not entries:
        return list(unread)
    tol = list_row_match_tolerance(row_height_guess)
    unmatched: list[tuple[int, float]] = []
    for ridx, uy in unread:
        nearest = min(entries, key=lambda e: abs(e.y_center - uy))
        if abs(nearest.y_center - uy) > tol:
            unmatched.append((ridx, uy))
    return unmatched


def unread_rows_from_conversation_list_capture(
    cap: Optional[CaptureResult],
    unread_cfg: dict,
) -> List[tuple[int, float]]:
    if not cap:
        return []
    row_height = int(unread_cfg.get("row_height", 65))
    red_threshold = int(unread_cfg.get("red_threshold", 15))
    scan_x_ratio = float(unread_cfg.get("scan_x_ratio", 0.35))
    scan_x_start_ratio = float(unread_cfg.get("scan_x_start_ratio", 0.0))
    scan_x_end_ratio = float(unread_cfg.get("scan_x_end_ratio", scan_x_ratio))
    try:
        bgra, w, h, _ = cap
        return detect_unread_rows(
            bgra,
            w,
            h,
            row_height,
            red_threshold,
            scan_x_ratio,
            scan_x_start_ratio=scan_x_start_ratio,
            scan_x_end_ratio=scan_x_end_ratio,
        )
    except Exception:
        return []


def scan_conversation_list_unread(
    hwnd: int, list_cfg: dict, unread_cfg: dict,
) -> List[tuple[int, float]]:
    """独立截列表图并扫红点（每轮会多一次截图）；主循环 discover 已改为单 capture 复用。"""
    try:
        cap = capture_wechat_conversation_list_hwnd(hwnd, list_cfg)
    except Exception:
        return []
    return unread_rows_from_conversation_list_capture(cap, unread_cfg)


@dataclass
class ListOcrCache:
    entries: list[ConversationListEntry] = field(default_factory=list)
    cache_time: float = 0.0


def should_refresh_list_cache(
    cache: ListOcrCache,
    scan_count: int,
    list_refresh_sec: float,
    list_refresh_scans: int,
    now: float,
) -> bool:
    if not cache.entries:
        return True
    if list_refresh_sec <= 0 and list_refresh_scans <= 0:
        return True
    if list_refresh_sec > 0 and (now - cache.cache_time >= list_refresh_sec):
        return True
    if list_refresh_scans > 0 and scan_count > 0 and scan_count % list_refresh_scans == 0:
        return True
    return False


def refresh_list_cache_if_needed(
    ocr: Any,
    hwnd: int,
    cfg: dict[str, Any],
    cache: ListOcrCache,
    scan_count: int,
    list_refresh_sec: float,
    list_refresh_scans: int,
    log_refresh: bool,
    log_prefix: str = "[WeChat-Reader]",
) -> list[ConversationListEntry]:
    now = time.time()
    if not should_refresh_list_cache(
        cache, scan_count, list_refresh_sec, list_refresh_scans, now,
    ):
        return cache.entries
    try:
        cache.entries = parse_visible_conversation_list(ocr, hwnd, cfg)
        cache.cache_time = now
        if log_refresh:
            print(
                f"{log_prefix} 会话列表缓存全量OCR刷新 "
                f"entries={len(cache.entries)}"
            )
    except Exception:
        pass
    return cache.entries


def execute_discover_unread_targets(
    *,
    ocr: Any,
    hwnd: int,
    cfg: dict[str, Any],
    scan_count: int,
    auto_switch: bool,
    list_cfg: dict,
    unread_cfg: dict,
    row_height_guess: float,
    exclude_names: Set[str],  # 已 normalize_contact_name（由 reader 构造）
    max_pending_targets: int,
    red_dot_header_settle_sec: float,
    header_cfg: dict,
    list_refresh_sec: float,
    list_refresh_scans: int,
    list_cache: ListOcrCache,
    pending_len: Callable[[], int],
    queue_target: Callable[[ConversationListEntry, bool], bool],
    save_unread_debug_frames: Callable[[list[tuple[int, float]]], None],
    debug_log_enabled: Callable[[], bool],
    pending_names_snapshot: Callable[[], list[str]],
    log_prefix: str = "[WeChat-Reader]",
) -> int:
    if not auto_switch or not list_cfg:
        return 0
    now = time.time()
    need_refresh = should_refresh_list_cache(
        list_cache,
        scan_count,
        list_refresh_sec,
        list_refresh_scans,
        now,
    )
    try:
        cap = capture_wechat_conversation_list_hwnd(hwnd, list_cfg)
    except Exception:
        cap = None

    unread = unread_rows_from_conversation_list_capture(cap, unread_cfg)
    save_unread_debug_frames(unread)

    # A：无红点且缓存仍有效 → 跳过列表 Paddle；B：否则同一张列表图做 OCR 更新缓存
    need_list_ocr = bool(unread) or need_refresh or not list_cache.entries
    if need_list_ocr and cap:
        try:
            blocks = ocr_from_capture(ocr, cap)
            list_cache.entries = parse_conversation_list_blocks(blocks, cfg)
            list_cache.cache_time = now
            if debug_log_enabled():
                print(
                    f"{log_prefix} 会话列表OCR更新缓存 "
                    f"entries={len(list_cache.entries)}"
                )
        except Exception:
            pass

    entries = list_cache.entries
    if debug_log_enabled() and entries:
        preview = ", ".join(entry.name for entry in entries[:8])
        if preview:
            print(f"{log_prefix} 可见会话(缓存)={preview}" + (" ..." if len(entries) > 8 else ""))
        list_width = max(1, int((list_cfg.get("w", 220))))
        sx = int(list_width * max(0.0, min(1.0, float(unread_cfg.get("scan_x_start_ratio", 0.0)))))
        ex = int(
            list_width
            * max(
                0.0,
                min(
                    1.0,
                    float(unread_cfg.get("scan_x_end_ratio", unread_cfg.get("scan_x_ratio", 0.35))),
                ),
            )
        )
        print(
            f"{log_prefix} 红点扫描带 "
            f"x=[{sx},{ex})/{list_width} "
            f"row_height={unread_cfg.get('row_height', 65)} "
            f"red_threshold={unread_cfg.get('red_threshold', 15)}"
        )
    if not unread:
        return 0
    print(
        f"{log_prefix} 检测到红点行="
        + ", ".join(f"row={row_idx}, y={y_center:.1f}" for row_idx, y_center in unread)
    )
    matched_entries = (
        match_unread_rows_to_entries(entries, unread, row_height_guess) if entries else []
    )
    unmatched = unread_rows_not_matched_by_entries(entries, unread, row_height_guess)

    if matched_entries:
        print(
            f"{log_prefix} 红点命中联系人="
            + ", ".join(
                f"{entry.name}(row={entry.row_index}, y={entry.y_center:.1f}, conf={entry.confidence:.2f})"
                for entry in matched_entries
            )
        )
    elif entries:
        print(f"{log_prefix} 未读红点未对齐列表OCR条目 unread={unread!r}，将尝试标题区兜底")
    else:
        print(f"{log_prefix} 会话列表缓存为空，红点将仅用标题区兜底")

    discovered = 0
    for entry in matched_entries:
        if pending_len() >= max_pending_targets:
            break
        if normalize_contact_name(entry.name) in exclude_names:
            print(f"{log_prefix} 未读命中排除项，忽略: {entry.name}")
            continue
        if queue_target(entry, False):
            discovered += 1

    for ridx, uy in unmatched:
        if pending_len() >= max_pending_targets:
            break
        if not header_cfg:
            print(
                f"{log_prefix} 红点行未对齐且无标题区配置，跳过兜底 row={ridx} y={uy:.1f}"
            )
            continue
        row_h = int(unread_cfg.get("row_height", 65))
        if cap and exclude_names:
            pre_title = list_row_title_from_list_capture(ocr, cap, uy, row_h)
            if pre_title:
                pre_n = normalize_contact_name(pre_title)
                if pre_n and pre_n in exclude_names:
                    print(
                        f"{log_prefix} 列表行预读命中排除，跳过点击: "
                        f"{pre_n!r} row={ridx} y={uy:.1f}"
                    )
                    continue
        print(f"{log_prefix} 红点标题区兜底 点击列表行 row={ridx} y={uy:.1f}")
        click_conversation_row_background(hwnd, list_cfg, uy)
        time.sleep(red_dot_header_settle_sec)
        raw_fb = read_current_contact(ocr, hwnd, header_cfg)
        if not raw_fb:
            print(f"{log_prefix} 兜底标题OCR为空，跳过")
            continue
        normalized_fb = normalize_contact_name(raw_fb)
        if not normalized_fb or normalized_fb in exclude_names:
            continue
        fb_entry = ConversationListEntry(
            name=normalized_fb,
            y_center=float(uy),
            row_index=ridx,
            confidence=0.35,
        )
        if queue_target(fb_entry, True):
            discovered += 1

    if discovered:
        print(
            f"{log_prefix} 发现 {discovered} 个未读目标，"
            f"待处理队列={pending_names_snapshot()!r}"
        )
    return discovered
