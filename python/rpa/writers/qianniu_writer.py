"""
千牛 PC writer: 轮询 messages(sync_status=10)，前台安全输入后更新状态。

与微信 Writer 对齐的能力：
  - 发送前/中校验前台为接待中心窗口，避免贴到其它应用
  - 失败重试 1 次；结束后恢复原先前台窗口
  - 可选标题 OCR：校验当前会话与 platform_conversation_id 一致
"""
from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Optional

from ..common.db_helper import (
    DEFAULT_DB_PATH,
    fetch_pending_send,
    mark_sent_failed,
    mark_sent_ok,
    open_db,
)
from ..common.input_sim import (
    ClipboardGuard,
    post_clear_text,
    post_click,
    post_click_window_at_point,
    post_key,
    post_type_text,
    set_clipboard_text,
    simulate_click,
    simulate_key,
    simulate_key_combo,
)
from ..common.ocr_engine import PaddleOCREngine
from ..common.qianniu_coords import (
    coordinate_space,
    point_ratio_screen,
    rect_center_screen,
    rect_from_absolute_fields,
    rect_ratios_to_bitmap_xywh,
)
from ..common.qianniu_header import pick_peer_name_from_ocr_blocks
from ..common.qianniu_window import find_qianniu_hwnd
from ..common.screenshot import capture_region, is_window_valid
from ..common.win32_window import bring_to_foreground, get_foreground_window, screen_to_client

PLATFORM = "qianniu"

VK_CONTROL = 0x11
VK_A = 0x41
VK_F = 0x46
VK_V = 0x56
VK_DELETE = 0x2E
VK_RETURN = 0x0D

ERR_WINDOW_NOT_FOUND = "ERR_WINDOW_NOT_FOUND"
ERR_FOREGROUND_FAILED = "ERR_FOREGROUND_FAILED"
ERR_SESSION_MISMATCH = "ERR_SESSION_MISMATCH"
ERR_SWITCH_FAILED = "ERR_SWITCH_FAILED"
ERR_SEND_FAILED = "ERR_SEND_FAILED"


def load_qianniu_config() -> dict:
    config_path = Path(__file__).resolve().parents[1] / "config" / "qianniu_config.json"
    if config_path.exists():
        with open(config_path, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _extract_conv_id_prefix(cfg: dict) -> str:
    conv = cfg.get("conversation") or {}
    return str(conv.get("conv_id_prefix", "qianniu_"))


def _peer_name_from_conv_id(platform_conv_id: str, prefix: str) -> str:
    if platform_conv_id.startswith(prefix):
        return platform_conv_id[len(prefix) :]
    return platform_conv_id


def _header_exclude_list(cfg: dict) -> list[str]:
    conv = cfg.get("conversation") or {}
    raw = conv.get("peer_name_exclude_substrings")
    if isinstance(raw, list):
        return [str(x) for x in raw if x]
    return []


def _space_for_region(cfg: dict, region: dict) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)


def _header_region_px(hwnd: int, cfg: dict) -> tuple[int, int, int, int]:
    hr = cfg.get("contact_header_region") or {}
    abs_r = rect_from_absolute_fields(hr)
    if abs_r:
        return abs_r
    space = _space_for_region(cfg, hr)
    return rect_ratios_to_bitmap_xywh(
        hwnd,
        float(hr.get("left_ratio", 0.26)),
        float(hr.get("top_ratio", 0.09)),
        float(hr.get("right_ratio", 0.62)),
        float(hr.get("bottom_ratio", 0.14)),
        space,
    )


def _ocr_header_peer_name(ocr: PaddleOCREngine, hwnd: int, cfg: dict) -> Optional[str]:
    hx, hy, hw, hh = _header_region_px(hwnd, cfg)
    if hw <= 0 or hh <= 0:
        return None
    try:
        bgra, w, h, _ = capture_region(hwnd, hx, hy, hw, hh)
        blocks = ocr.recognize(bgra, w, h)
        if not blocks:
            return None
        return pick_peer_name_from_ocr_blocks(blocks, _header_exclude_list(cfg))
    except Exception:
        return None


def _header_matches_target(header_text: Optional[str], target: str) -> bool:
    if not header_text or not target:
        return True
    if target in header_text or header_text in target:
        return True
    # 允许 OCR 少字：目标前缀匹配
    if len(target) >= 4 and target[:4] in header_text:
        return True
    return False


def get_input_center_px(hwnd: int, cfg: dict) -> tuple[int, int]:
    region = cfg.get("input_region") or {}
    space = _space_for_region(cfg, region)
    return rect_center_screen(
        hwnd,
        float(region.get("left_ratio", 0.22)),
        float(region.get("top_ratio", 0.77)),
        float(region.get("right_ratio", 0.78)),
        float(region.get("bottom_ratio", 0.93)),
        space,
    )


def get_send_button_center_px(hwnd: int, cfg: dict) -> tuple[int, int]:
    btn = cfg.get("send_button") or {}
    space = _space_for_region(cfg, btn)
    cx_ratio = float(btn.get("center_x_ratio", 0.72))
    cy_ratio = float(btn.get("center_y_ratio", 0.945))
    return point_ratio_screen(hwnd, cx_ratio, cy_ratio, space)


def _is_fallback_conversation(platform_conv_id: str, cfg: dict) -> bool:
    fb = str((cfg.get("conversation") or {}).get("fallback_platform_conversation_id", ""))
    return bool(fb and platform_conv_id == fb)


def _verify_session_optional(
    ocr: Optional[PaddleOCREngine],
    cfg: dict,
    hwnd: int,
    target_peer: str,
    platform_conv_id: str,
) -> bool:
    if _is_fallback_conversation(platform_conv_id, cfg):
        return True
    if not ocr or not cfg.get("contact_header_region"):
        return True
    text = _ocr_header_peer_name(ocr, hwnd, cfg)
    ok = _header_matches_target(text, target_peer)
    if not ok:
        print(
            f"[千牛-Writer] 会话校验失败: 标题OCR={text!r} 期望包含={target_peer!r}"
        )
    return ok


def _err(code: str, detail: str) -> str:
    return f"{code}: {detail}"


def _switch_to_target_conversation(
    hwnd: int,
    cfg: dict,
    ocr: Optional[PaddleOCREngine],
    target_peer: str,
    platform_conv_id: str,
    timeout_sec: float,
) -> bool:
    """
    Fallback: Ctrl+F 搜索会话名并回车，随后用标题 OCR 二次确认。
    """
    if _is_fallback_conversation(platform_conv_id, cfg):
        return True
    if not ocr:
        return False
    if not target_peer.strip():
        return False

    sx, sy = get_input_center_px(hwnd, cfg)
    with ClipboardGuard():
        set_clipboard_text(target_peer)
        # 先确保焦点在千牛窗口
        simulate_click(sx, sy, delay_ms=40)
        time.sleep(0.08)
        simulate_key_combo(VK_CONTROL, VK_F)
        time.sleep(0.15)
        simulate_key_combo(VK_CONTROL, VK_A)
        time.sleep(0.05)
        simulate_key_combo(VK_CONTROL, VK_V)
        time.sleep(0.08)
        simulate_key(VK_RETURN)
        simulate_key(VK_RETURN, key_up=True)

    deadline = time.time() + max(1.0, timeout_sec)
    while time.time() < deadline:
        text = _ocr_header_peer_name(ocr, hwnd, cfg)
        if _header_matches_target(text, target_peer):
            return True
        time.sleep(0.25)
    return False


def _send_message_background(hwnd: int, cfg: dict, text: str) -> bool:
    """
    后台兜底：不依赖前台焦点，直接 PostMessage 到目标窗口。
    """
    in_sx, in_sy = get_input_center_px(hwnd, cfg)
    in_cx, in_cy = screen_to_client(hwnd, in_sx, in_sy)
    post_click(hwnd, in_cx, in_cy, delay_ms=25)
    time.sleep(0.05)

    post_clear_text(hwnd, max_chars=max(80, min(800, len(text) * 2)))
    time.sleep(0.03)
    post_type_text(hwnd, text, delay_per_char_ms=3)
    time.sleep(0.05)

    send_sx, send_sy = get_send_button_center_px(hwnd, cfg)

    # 1) 优先对屏幕点命中的真实子控件发送点击（更容易命中 Qt 按钮）
    clicked = post_click_window_at_point(send_sx, send_sy, delay_ms=25)
    time.sleep(0.10)

    # 2) 备用：对顶层 hwnd 发 Enter（部分输入框 Enter=发送）
    post_key(hwnd, VK_RETURN)
    time.sleep(0.08)

    # 3) 最后兜底：物理点击发送按钮（可能激活窗口，但成功率高）
    if not clicked:
        simulate_click(send_sx, send_sy, delay_ms=30)
        time.sleep(0.12)

    return True


def send_qianniu_message(
    hwnd: int,
    cfg: dict,
    text: str,
    ocr: Optional[PaddleOCREngine],
    target_peer: str,
    platform_conv_id: str,
    switch_timeout_sec: float = 5.0,
    require_header_match: bool = True,
    allow_background_fallback: bool = True,
    prefer_background: bool = True,
) -> bool:
    if prefer_background:
        try:
            # 后台发送前先做会话校验；不匹配则回退前台进行会话切换后再发。
            session_ok_bg = _verify_session_optional(ocr, cfg, hwnd, target_peer, platform_conv_id)
            if session_ok_bg:
                _send_message_background(hwnd, cfg, text)
                return True
            print(
                f"[千牛-Writer] WARN 后台发送前会话校验不通过，回退前台流程 target={target_peer!r}"
            )
        except Exception as e:
            print(
                f"[千牛-Writer] WARN 后台发送失败，回退前台流程 target={target_peer!r} err={e}"
            )

    fg_ok = bring_to_foreground(hwnd)
    if fg_ok:
        time.sleep(0.25)
        fg_ok = get_foreground_window() == hwnd
    if not fg_ok and not allow_background_fallback:
        raise RuntimeError(_err(
            ERR_FOREGROUND_FAILED,
            "无法将千牛接待中心置于前台。若窗口已嵌入聚合，请先切到千牛页签或恢复窗口。",
        ))

    session_ok = _verify_session_optional(ocr, cfg, hwnd, target_peer, platform_conv_id)
    if not session_ok:
        if not fg_ok:
            # 后台模式下不做搜索切换；仅允许“当前已是目标会话”再发送，避免误发。
            raise RuntimeError(_err(
                ERR_SESSION_MISMATCH,
                f"后台发送前会话不匹配（期望: {target_peer}），且前台不可用，已取消发送",
            ))
        switched = _switch_to_target_conversation(
            hwnd, cfg, ocr, target_peer, platform_conv_id, timeout_sec=switch_timeout_sec
        )
        if not switched:
            if require_header_match:
                raise RuntimeError(_err(
                    ERR_SWITCH_FAILED,
                    f"会话切换失败（期望: {target_peer}）",
                ))
            print(
                f"[千牛-Writer] WARN 会话切换失败，但按配置继续发送 target={target_peer!r}"
            )

    if not fg_ok:
        print(f"[千牛-Writer] WARN 前台不可用，改走后台发送 target={target_peer!r}")
        return _send_message_background(hwnd, cfg, text)

    sx, sy = get_input_center_px(hwnd, cfg)
    simulate_click(sx, sy, delay_ms=50)
    time.sleep(0.12)

    if get_foreground_window() != hwnd:
        raise RuntimeError(_err(ERR_FOREGROUND_FAILED, "千牛窗口在点击输入框后失去焦点，已取消发送"))

    simulate_key_combo(VK_CONTROL, VK_A)
    time.sleep(0.05)
    simulate_key(VK_DELETE)
    time.sleep(0.05)

    with ClipboardGuard():
        set_clipboard_text(text)
        time.sleep(0.05)
        simulate_key_combo(VK_CONTROL, VK_V)
        time.sleep(0.12)

    if get_foreground_window() != hwnd:
        raise RuntimeError(_err(ERR_FOREGROUND_FAILED, "粘贴前千牛失去焦点，已取消发送"))

    send_x, send_y = get_send_button_center_px(hwnd, cfg)
    simulate_click(send_x, send_y, delay_ms=50)
    time.sleep(0.25)
    return True


def run_writer(
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 1.0,
) -> None:
    db = db_path or DEFAULT_DB_PATH
    cfg = load_qianniu_config()
    prefix = _extract_conv_id_prefix(cfg)
    header_cfg = cfg.get("contact_header_region")
    ocr: Optional[PaddleOCREngine] = None
    if header_cfg:
        ocr_cfg = cfg.get("ocr") or {}
        ocr = PaddleOCREngine(
            lang="ch",
            min_confidence=float(ocr_cfg.get("min_confidence", 0.5)),
            max_side=int(ocr_cfg.get("max_side", 960)),
            invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
            det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
            det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
        )

    poll_interval_sec = float(
        cfg.get("writer_poll_sec", cfg.get("poll_interval_sec", poll_interval_sec))
    )
    writer_cfg = cfg.get("writer") or {}
    max_retries = max(0, int(writer_cfg.get("max_retries", 1)))
    switch_timeout_sec = float(writer_cfg.get("switch_timeout_sec", 6.0))
    require_header_match = bool(writer_cfg.get("require_header_match", True))
    allow_background_fallback = bool(writer_cfg.get("allow_background_fallback", True))
    prefer_background = bool(writer_cfg.get("prefer_background", True))
    print(
        f"[千牛-Writer] DB={db} poll={poll_interval_sec}s retries={max_retries} "
        f"switch_timeout={switch_timeout_sec}s require_header_match={require_header_match} "
        f"prefer_bg={prefer_background} bg_fallback={allow_background_fallback} "
        f"(session_verify={'on' if ocr else 'off'})"
    )

    while True:
        rows = []
        conn = open_db(db)
        try:
            rows = fetch_pending_send(conn, PLATFORM)
        finally:
            conn.close()

        if not rows:
            time.sleep(poll_interval_sec)
            continue

        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd or not is_window_valid(hwnd):
            for msg_id, conv_id, content, _ in rows:
                conn = open_db(db)
                try:
                    mark_sent_failed(
                        conn,
                        msg_id,
                        _err(ERR_WINDOW_NOT_FOUND, "千牛接待中心窗口未找到"),
                    )
                finally:
                    conn.close()
            time.sleep(poll_interval_sec)
            continue

        for msg_id, conv_id, content, platform_conv_id in rows:
            target_peer = _peer_name_from_conv_id(platform_conv_id, prefix)
            sent = False
            last_error = ""
            original_fg = get_foreground_window()

            for attempt in range(max_retries + 1):
                try:
                    if send_qianniu_message(
                        hwnd,
                        cfg,
                        content,
                        ocr,
                        target_peer,
                        platform_conv_id,
                        switch_timeout_sec=switch_timeout_sec,
                        require_header_match=require_header_match,
                        allow_background_fallback=allow_background_fallback,
                        prefer_background=prefer_background,
                    ):
                        sent = True
                        break
                    last_error = _err(ERR_SEND_FAILED, "发送模拟返回失败")
                except Exception as e:
                    last_error = str(e)
                    print(
                        f"[千牛-Writer] 发送尝试失败 msg_id={msg_id} attempt={attempt + 1}/{max_retries + 1} "
                        f"conv_id={platform_conv_id!r} target={target_peer!r} err={last_error}"
                    )
                if attempt < max_retries:
                    time.sleep(1.0)

            if original_fg and original_fg != hwnd:
                try:
                    bring_to_foreground(original_fg)
                except Exception:
                    pass

            conn = open_db(db)
            try:
                if sent:
                    mark_sent_ok(conn, msg_id)
                    print(
                        f"[千牛-Writer] 已发送 msg_id={msg_id} conv_id={platform_conv_id!r} "
                        f"target={target_peer!r}"
                    )
                else:
                    mark_sent_failed(conn, msg_id, last_error)
                    print(
                        f"[千牛-Writer] 发送失败 msg_id={msg_id} conv_id={platform_conv_id!r} "
                        f"target={target_peer!r} err={last_error}"
                    )
            finally:
                conn.close()

            time.sleep(0.3)

        time.sleep(poll_interval_sec)


if __name__ == "__main__":
    run_writer()
