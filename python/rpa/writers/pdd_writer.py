"""
Pinduoduo (商家后台网页) Writer skeleton (框架版).

Reader 负责把“客户消息”写入 `rpa_inbox_messages`，Qt 侧消费并构造聚合会话。
Writer 负责从 messages 表中读取待发送消息（sync_status=10），并把内容写回拼多多网页输入框。

在你尚未完成拼多多后台账号登录与页面结构校准前：
* Writer 仍可启动并轮询队列；
* 若找不到浏览器窗口/输入框坐标不对，会将消息标记为失败（sync_status=12）并记录 error_reason。

后续只需补齐：
* window_match（title/process/hwnd_hex）
* input_region / send_button 坐标（ratio 或绝对 x,y,w,h）
* 清空/粘贴/发送的交互策略（默认为 Ctrl+A/Delete + Ctrl+V + Enter）
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import (
    resolved_default_db_path,
    fetch_pending_send,
    insert_send_event,
    mark_sent_failed,
    mark_sent_ok,
    open_db,
)
from ..common.input_sim import (
    ClipboardGuard,
    simulate_click,
    simulate_key,
    simulate_key_combo,
    set_clipboard_text,
)
from ..common.qianniu_coords import (
    coordinate_space,
    rect_from_absolute_fields,
    rect_ratios_to_bitmap_xywh,
    point_ratio_screen,
)
from ..common.screenshot import get_window_rect, is_window_minimized, is_window_valid
from ..common.win32_window import find_window, find_window_by_title_candidates, bring_to_foreground, get_foreground_window

CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"
DEFAULT_CONFIG = CONFIG_DIR / "pdd_config.json"

STATE_DIR = Path(__file__).resolve().parents[1] / "_state"
PLATFORM = "pdd_web"

ERR_WINDOW_NOT_FOUND = "ERR_WINDOW_NOT_FOUND"
ERR_FOREGROUND_FAILED = "ERR_FOREGROUND_FAILED"
ERR_SEND_FAILED = "ERR_SEND_FAILED"


def _record_send_phase(
    db: Path,
    message_id: int,
    conversation_id: int,
    phase: str,
    detail: str = "",
) -> None:
    try:
        conn = open_db(db)
        try:
            insert_send_event(conn, message_id, conversation_id, phase, detail)
        finally:
            conn.close()
    except Exception:
        pass


def load_config(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _space_for_region(cfg: dict[str, Any], region: dict[str, Any]) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)


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
    return find_window(
        title_contains=str(title_contains),
        process_name=str(process_name) if process_name else "",
    )


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


def _center_from_abs_region_px(hwnd: int, x: int, y: int, w: int, h: int) -> tuple[int, int]:
    win_x, win_y, _, _ = get_window_rect(hwnd)
    cx = win_x + x + w // 2
    cy = win_y + y + h // 2
    return cx, cy


def _get_input_click_point(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int]:
    region = cfg.get("input_region") or {}
    if not region:
        # fallback: chat_region 下方 10%～15%
        chat = cfg.get("chat_region") or {}
        x, y, w, h = _rect_from_region_px(hwnd, cfg, chat)
        return _center_from_abs_region_px(hwnd, x, y + h * 0.7, w, h * 0.3)

    x, y, w, h = _rect_from_region_px(hwnd, cfg, region)
    return _center_from_abs_region_px(hwnd, x, y, w, h)


def _get_send_button_point(hwnd: int, cfg: dict[str, Any]) -> Optional[tuple[int, int]]:
    btn = cfg.get("send_button") or {}
    if not btn:
        return None
    if btn.get("center_x_ratio") is None or btn.get("center_y_ratio") is None:
        return None
    space = _space_for_region(cfg, btn)
    sx = float(btn.get("center_x_ratio"))
    sy = float(btn.get("center_y_ratio"))
    return point_ratio_screen(hwnd, sx, sy, space)


def _send_once(hwnd: int, cfg: dict[str, Any], text: str) -> bool:
    if is_window_minimized(hwnd):
        return False
    if get_foreground_window() != hwnd:
        return False

    ix, iy = _get_input_click_point(hwnd, cfg)
    simulate_click(ix, iy, delay_ms=50)
    time.sleep(0.15)

    # Clear input and paste
    # Strategy 1 (default): Ctrl+A + Delete, then Ctrl+V
    if cfg.get("clear_method", "ctrl_a_delete") == "ctrl_a_delete":
        simulate_key_combo(0x11, 0x41)  # Ctrl + A
        time.sleep(0.05)
        simulate_key(0x2E)  # Delete
        time.sleep(0.08)

    with ClipboardGuard():
        # Prefer Ctrl+V so browser handles IME/rich input.
        ok = set_clipboard_text(text)
        _ = ok  # kept for future metrics
        time.sleep(0.05)
        simulate_key_combo(0x11, 0x56)  # Ctrl + V
        time.sleep(0.12)

    # Send: default Enter
    simulate_key(0x0D)  # Enter
    time.sleep(0.2)
    return True


def run_writer(
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 1.0,
) -> None:
    cfg_path = DEFAULT_CONFIG
    cfg = load_config(cfg_path)
    if not cfg:
        raise FileNotFoundError(f"Config not found or empty: {cfg_path}")

    db = db_path or resolved_default_db_path()

    poll_interval = float(cfg.get("writer_poll_sec", cfg.get("poll_interval_sec", poll_interval_sec)))
    writer_cfg = cfg.get("writer") or {}
    max_retries = max(0, int(writer_cfg.get("max_retries", 1)))

    print(f"[PDD-Writer] DB={db} poll={poll_interval}s retries={max_retries} platform={PLATFORM}")
    rpa_phase("pdd.writer", "poll_loop_enter", "已进入发送轮询；无任务时每 30s 输出 idle heartbeat")
    _last_idle_hb = 0.0

    while True:
        rows = []
        conn = open_db(db)
        try:
            rows = fetch_pending_send(conn, PLATFORM)
        finally:
            conn.close()

        if not rows:
            now = time.time()
            if now - _last_idle_hb >= 30.0:
                rpa_heartbeat("pdd.writer", "idle：无待发送消息，轮询 DB 中")
                _last_idle_hb = now
            time.sleep(poll_interval)
            continue

        hwnd = _find_pdd_hwnd(cfg)
        if not hwnd or not is_window_valid(hwnd):
            for msg_id, conv_id, _content, _platform_conv_id in rows:
                conn2 = open_db(db)
                try:
                    insert_send_event(
                        conn2, msg_id, conv_id, "failed", ERR_WINDOW_NOT_FOUND
                    )
                    mark_sent_failed(conn2, msg_id, ERR_WINDOW_NOT_FOUND)
                finally:
                    conn2.close()
            time.sleep(poll_interval)
            continue

        if not bring_to_foreground(hwnd):
            for msg_id, conv_id, _content, _platform_conv_id in rows:
                conn2 = open_db(db)
                try:
                    insert_send_event(
                        conn2, msg_id, conv_id, "failed", ERR_FOREGROUND_FAILED
                    )
                    mark_sent_failed(conn2, msg_id, ERR_FOREGROUND_FAILED)
                finally:
                    conn2.close()
            time.sleep(poll_interval)
            continue

        for msg_id, conv_id, content, platform_conv_id in rows:
            _record_send_phase(db, msg_id, conv_id, "dequeued", "")
            last_error = ""
            sent = False
            for attempt in range(max_retries + 1):
                _record_send_phase(
                    db,
                    msg_id,
                    conv_id,
                    "send_attempt",
                    f"attempt={attempt + 1}/{max_retries + 1}",
                )
                try:
                    if get_foreground_window() != hwnd:
                        _ = bring_to_foreground(hwnd)
                    if _send_once(hwnd, cfg, content):
                        sent = True
                        break
                    last_error = f"{ERR_SEND_FAILED}: _send_once returned False"
                except Exception as e:
                    last_error = f"{ERR_SEND_FAILED}: {e}"
                if attempt < max_retries:
                    time.sleep(1.0)

            conn3 = open_db(db)
            try:
                if sent:
                    insert_send_event(conn3, msg_id, conv_id, "success", "")
                    mark_sent_ok(conn3, msg_id)
                    print(f"[PDD-Writer] 已发送 msg_id={msg_id} platform_conv_id={platform_conv_id!r}")
                else:
                    insert_send_event(
                        conn3, msg_id, conv_id, "failed", (last_error or ERR_SEND_FAILED)[:500]
                    )
                    mark_sent_failed(conn3, msg_id, last_error or ERR_SEND_FAILED)
                    print(f"[PDD-Writer] 发送失败 msg_id={msg_id} err={last_error}")
            finally:
                conn3.close()

            time.sleep(0.2)

        time.sleep(poll_interval)


if __name__ == "__main__":
    run_writer()

