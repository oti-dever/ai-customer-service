"""
WeChat PC writer: poll messages(sync_status=10), switch to target conversation
via search, then simulate input to send.
"""
from __future__ import annotations

import hashlib
import json
import threading
import time
from contextlib import nullcontext
from pathlib import Path
from types import SimpleNamespace
from typing import Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import (
    resolved_default_db_path,
    open_db,
    fetch_pending_send,
    insert_send_event,
    mark_sent_ok,
    mark_sent_failed,
)
from ..common.input_sim import (
    simulate_click,
    simulate_double_click,
    simulate_key_combo,
    simulate_key,
    set_clipboard_text,
    simulate_type_unicode,
    send_paste_to_window_at_point,
    ClipboardGuard,
    post_click,
    post_key,
    post_clear_text,
    post_type_text,
)
from ..common.screenshot import get_window_rect, is_window_valid
from ..common.wechat_capture import (
    capture_wechat_chat_bottom_strip,
    capture_wechat_input_region_resolved,
    rect_input_region_resolved,
)
from ..common.wechat_session import (
    ensure_in_target_chat_background as ensure_target_chat_background,
    ensure_in_target_chat_foreground as ensure_target_chat_foreground,
    extract_contact_name_from_conv_id,
    find_wechat_window,
    normalize_contact_name,
    read_current_contact,
    switch_to_contact_uia,
    switch_to_contact_background as switch_contact_background,
    switch_to_contact_foreground as switch_contact_foreground,
)
from ..common.wechat_uia import (
    activate_wechat_main_window,
    clear_chat_input_via_shortcuts,
    click_send_button,
    find_chat_input,
    find_send_button,
    find_wechat_main_window_uia,
    focus_chat_input,
    is_wechat_uia_supported,
    paste_text_via_clipboard,
    probe_wechat_uia_capabilities,
    send_enter_key,
)
from ..common.win32_window import (
    bring_to_foreground, get_foreground_window, screen_to_client,
)
from ..common.window_lock import hold_platform_window_lock
from ..common.ocr_engine import PaddleOCREngine

PLATFORM = "wechat_pc"

VK_CONTROL = 0x11
VK_A = 0x41
VK_V = 0x56
VK_F = 0x46
VK_DELETE = 0x2E
VK_RETURN = 0x0D
VK_ESCAPE = 0x1B


def load_wechat_config() -> dict:
    config_path = Path(__file__).resolve().parents[1] / "config" / "wechat_config.json"
    if config_path.exists():
        with open(config_path, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _extract_conv_id_prefix(cfg: dict) -> str:
    conv = cfg.get("conversation") or {}
    return str(conv.get("conv_id_prefix", "wechat_"))


def _contact_name_from_conv_id(platform_conv_id: str, prefix: str) -> str:
    """Extract contact name from platform_conversation_id like 'wechat_邬鸿涛'."""
    return extract_contact_name_from_conv_id(platform_conv_id, prefix)


def _get_current_contact(ocr: PaddleOCREngine, hwnd: int, header_cfg: dict) -> Optional[str]:
    """OCR the header to detect which contact is currently open."""
    return read_current_contact(ocr, hwnd, header_cfg)


def _log_send_event(stage: str, **kwargs) -> None:
    details = " ".join(f"{k}={v!r}" for k, v in kwargs.items() if v not in (None, ""))
    print(f"[WeChat-Writer] {stage} {details}".rstrip())


def _switch_to_contact(hwnd: int, cfg: dict, contact_name: str) -> bool:
    """
    Switch to a contact via WeChat search: Ctrl+F -> paste name -> wait -> click first result.
    Returns True if the switch action was performed.
    """
    return switch_contact_foreground(hwnd, cfg, contact_name)


def _get_input_click_point(cfg: dict, ix: int, iy: int, iw: int, ih: int) -> tuple[int, int]:
    """
    Returns (click_x, click_y) relative to window for input box focus.
    默认点击左偏 80px、垂直居中，避免点到右侧发送按钮。
    """
    ib = cfg.get("input_box") or {}
    offset_x = int(ib.get("click_offset_x", 80))
    offset_y = int(ib.get("click_offset_y", ih // 2))
    return ix + offset_x, iy + offset_y


def _send_text(hwnd: int, cfg: dict, text: str) -> bool:
    """Type text into the current conversation and send via Enter."""
    # 1. 激活微信窗口，确保能收到点击和键盘
    if not bring_to_foreground(hwnd):
        raise RuntimeError(
            "无法将微信窗口置于前台。若微信已嵌入聚合平台，"
            "请确保发送时微信窗口页面可见（如先切换到微信页面再发送）。"
        )
    time.sleep(0.4)

    if get_foreground_window() != hwnd:
        raise RuntimeError(
            "微信窗口未能保持前台焦点，取消发送以防止输入到错误窗口"
        )

    win_x, win_y, win_w, win_h = get_window_rect(hwnd)
    ix, iy, iw, ih = rect_input_region_resolved(cfg, win_h)
    cx, cy = _get_input_click_point(cfg, ix, iy, iw, ih)
    sx, sy = win_x + cx, win_y + cy

    ib = cfg.get("input_box") or {}
    use_double_click = bool(ib.get("double_click", False))
    click_before_paste = bool(ib.get("click_before_paste", True))

    if use_double_click:
        simulate_double_click(sx, sy, delay_ms=50)
    else:
        simulate_click(sx, sy, delay_ms=50)

    # 2. 等待输入框获得焦点（微信自定义控件可能响应较慢）
    time.sleep(0.4)

    # 粘贴前再次点击，确保焦点在输入框（若仍无效可设 click_before_paste: false）
    if click_before_paste:
        simulate_click(sx, sy, delay_ms=30)
        time.sleep(0.2)

    simulate_key_combo(VK_CONTROL, VK_A)
    time.sleep(0.06)
    simulate_key(VK_DELETE)
    time.sleep(0.08)

    # 安全检查：输入文本前再次确认前台窗口是微信
    if get_foreground_window() != hwnd:
        raise RuntimeError("微信窗口在点击后失去焦点，取消发送以防误操作")

    # 3. 输入文本：微信自定义输入框对 Ctrl+V 不响应，改用 Unicode 逐字输入
    input_method = (ib.get("input_method") or "unicode").lower()
    if input_method == "unicode":
        simulate_type_unicode(text, delay_per_char_ms=12)
    elif input_method == "wm_paste":
        with ClipboardGuard():
            set_clipboard_text(text)
            time.sleep(0.1)
            send_paste_to_window_at_point(sx, sy)
    else:
        with ClipboardGuard():
            set_clipboard_text(text)
            time.sleep(0.12)
            simulate_key_combo(VK_CONTROL, VK_V)
    time.sleep(0.15)

    simulate_key(VK_RETURN)
    time.sleep(0.3)
    return True


def _send_text_uia(hwnd: int, cfg: dict, text: str) -> bool:
    """Use UIA to focus the real input field and click send."""
    if not is_wechat_uia_supported():
        raise RuntimeError("UIA 不可用：未安装 uiautomation 或当前平台不支持")
    win = find_wechat_main_window_uia()
    if win is None:
        raise RuntimeError("UIA 未找到微信主窗口")
    activate_wechat_main_window(win)
    uia_cfg = cfg.get("uia") or {}
    max_visits = int(uia_cfg.get("max_visits", 22000))
    max_depth = int(uia_cfg.get("max_depth", 40))
    input_ctrl = find_chat_input(win, max_visits=max_visits, max_depth=max_depth)
    if input_ctrl is None:
        raise RuntimeError("UIA 未找到聊天输入框 chat_input_field")
    if not focus_chat_input(input_ctrl):
        raise RuntimeError("UIA 无法聚焦聊天输入框")
    clear_chat_input_via_shortcuts()
    paste_text_via_clipboard(text)
    prefer_send_button = bool(uia_cfg.get("prefer_send_button_click", True))
    if prefer_send_button:
        send_btn = find_send_button(win, max_visits=max_visits * 2, max_depth=max_depth)
        if send_btn is not None and click_send_button(send_btn):
            return True
    send_enter_key()
    return True


def _ensure_in_target_chat(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
    max_retry: int = 2,
) -> bool:
    """
    Ensure WeChat is currently showing target chat.
    If OCR is available, validate via header; otherwise best-effort.
    """
    return ensure_target_chat_foreground(
        ocr, hwnd, header_cfg, cfg, target_name, max_retry=max_retry
    )


def _ensure_in_target_chat_uia(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
    max_retry: int = 2,
) -> bool:
    return switch_to_contact_uia(
        hwnd,
        cfg,
        target_name,
        ocr=ocr,
        header_cfg=header_cfg,
        max_retry=max_retry,
    )


# ---------------------------------------------------------------------------
# Background send via PostMessage (no foreground activation)
# ---------------------------------------------------------------------------

def _to_client(hwnd: int, win_x: int, win_y: int, rel_x: int, rel_y: int) -> tuple[int, int]:
    """Convert window-relative coordinates to client coordinates for PostMessage."""
    return screen_to_client(hwnd, win_x + rel_x, win_y + rel_y)


def _switch_to_contact_background(hwnd: int, cfg: dict, contact_name: str) -> bool:
    """Switch to a contact using PostMessage (background, no foreground needed).

    Uses only single-key operations (no Ctrl+A/V combos, which don't work via
    PostMessage because modifier state is not reflected in GetKeyState).
    """
    return switch_contact_background(hwnd, cfg, contact_name)


def _send_text_background(hwnd: int, cfg: dict, text: str) -> bool:
    """Send text to current WeChat conversation via PostMessage (background).

    Uses only single-key operations: click → End+Backspace (clear) →
    WM_CHAR (type) → Enter (send).  No Ctrl+A/V combos.
    """
    win_x, win_y, win_w, win_h = get_window_rect(hwnd)
    ix, iy, iw, ih = rect_input_region_resolved(cfg, win_h)
    cx, cy = _get_input_click_point(cfg, ix, iy, iw, ih)

    client_x, client_y = _to_client(hwnd, win_x, win_y, cx, cy)
    post_click(hwnd, client_x, client_y, delay_ms=20)
    time.sleep(0.15)

    post_clear_text(hwnd)
    time.sleep(0.05)

    post_type_text(hwnd, text, delay_per_char_ms=5)
    time.sleep(0.05)

    post_key(hwnd, VK_RETURN)
    time.sleep(0.05)
    return True


def _normalize_receipt_text(text: str) -> str:
    return " ".join(str(text or "").split()).strip()


def _ocr_input_box_content(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    cfg: dict,
) -> str:
    if not ocr:
        return ""
    try:
        _win_x, _win_y, _win_w, win_h = get_window_rect(hwnd)
        cap = capture_wechat_input_region_resolved(hwnd, cfg, win_h)
        if not cap:
            return ""
        bgra, w, h, _ = cap
        blocks = ocr.recognize(bgra, w, h)
        texts = [str(block[0]).strip() for block in blocks if str(block[0]).strip()]
        return _normalize_receipt_text(" ".join(texts))
    except Exception:
        return ""


def _capture_chat_bottom_signature(hwnd: int, cfg: dict) -> str:
    try:
        cap = capture_wechat_chat_bottom_strip(hwnd, cfg)
        if not cap:
            return ""
        bgra, _rw, _rh, _ = cap
        return hashlib.sha1(bytes(bgra)).hexdigest()
    except Exception:
        return ""


def _verify_send_receipt(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    cfg: dict,
    content: str,
    before_chat_sig: str,
) -> tuple[bool, str]:
    receipt_cfg = cfg.get("receipt_verify") or {}
    if not receipt_cfg.get("enabled", True):
        return True, "receipt_verify disabled"

    deadline = time.time() + max(0.5, float(receipt_cfg.get("timeout_sec", 2.0)))
    expected = _normalize_receipt_text(content)
    last_input = ""
    last_chat_sig = before_chat_sig

    while time.time() < deadline:
        current_input = _ocr_input_box_content(ocr, hwnd, cfg)
        current_chat_sig = _capture_chat_bottom_signature(hwnd, cfg)

        input_cleared = not current_input
        input_changed = bool(current_input) and current_input != expected
        chat_changed = bool(before_chat_sig) and bool(current_chat_sig) and current_chat_sig != before_chat_sig

        if input_cleared or input_changed or chat_changed:
            signals: list[str] = []
            if input_cleared:
                signals.append("input_cleared")
            elif input_changed:
                signals.append(f"input_changed:{current_input}")
            if chat_changed:
                signals.append("chat_bottom_changed")
            return True, ", ".join(signals)

        last_input = current_input
        last_chat_sig = current_chat_sig
        time.sleep(0.25)

    detail = []
    detail.append(f"input={last_input!r}" if last_input else "input_empty")
    if before_chat_sig and last_chat_sig:
        detail.append("chat_unchanged" if last_chat_sig == before_chat_sig else "chat_changed_late")
    elif not before_chat_sig:
        detail.append("chat_signature_unavailable")
    return False, ", ".join(detail)


def _ensure_in_target_chat_background(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
) -> bool:
    """Ensure target chat is active using background PostMessage + OCR verification."""
    return ensure_target_chat_background(ocr, hwnd, header_cfg, cfg, target_name)


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


def _wechat_try_send_outbound(
    db: Path,
    msg_id: int,
    conv_id: int,
    hwnd: int,
    cfg: dict,
    ocr: Optional[PaddleOCREngine],
    header_cfg: dict,
    content: str,
    target_name: str,
    send_mode: str,
    allow_foreground_fallback: bool,
) -> tuple[bool, str]:
    def _ph(phase: str, detail: str = "") -> None:
        _record_send_phase(db, msg_id, conv_id, phase, detail)

    sent = False
    last_error = ""
    uia_cfg = cfg.get("uia") or {}
    use_uia = bool(uia_cfg.get("enabled", False))
    if use_uia and bool(uia_cfg.get("require_probe_ok", False)):
        probe = probe_wechat_uia_capabilities(
            max_visits=int(uia_cfg.get("max_visits", 22000)),
            max_depth=int(uia_cfg.get("max_depth", 40)),
        )
        if not probe.available:
            use_uia = False
            last_error = f"UIA 自检未通过: {probe.reason}"

    if use_uia:
        original_fg = get_foreground_window()
        for attempt in range(2):
            try:
                _log_send_event(
                    "准备UIA发送",
                    msg_id=msg_id,
                    conv_id=conv_id,
                    target=target_name,
                    attempt=attempt + 1,
                )
                _ph("switch_chat", f"uia attempt={attempt + 1}")
                if not _ensure_in_target_chat_uia(
                    ocr, hwnd, header_cfg, cfg, target_name
                ):
                    last_error = f"UIA 会话切换失败：未进入目标联系人({target_name})"
                    if attempt < 1:
                        time.sleep(0.6)
                    continue
                before_chat_sig = _capture_chat_bottom_signature(hwnd, cfg)
                _ph("send_text", "uia")
                if _send_text_uia(hwnd, cfg, content):
                    sent = True
                    _ph("receipt_check")
                    receipt_ok, receipt_msg = _verify_send_receipt(
                        ocr, hwnd, cfg, content, before_chat_sig
                    )
                    _ph(
                        "receipt_result",
                        f"{'OK' if receipt_ok else 'WARN'} {receipt_msg}",
                    )
                    level = "OK" if receipt_ok else "WARN"
                    print(
                        f"[WeChat-Writer] UIA 回执校验({level}) "
                        f"msg_id={msg_id} target={target_name}: {receipt_msg}"
                    )
                    break
                last_error = "UIA 发送返回失败"
            except Exception as e:
                last_error = f"UIA 发送异常: {e}"
            if attempt < 1:
                time.sleep(0.6)
        if original_fg and original_fg != hwnd:
            try:
                bring_to_foreground(original_fg)
            except Exception:
                pass

    if not sent and send_mode == "background":
        try:
            _log_send_event(
                "准备后台发送",
                msg_id=msg_id,
                conv_id=conv_id,
                target=target_name,
            )
            _ph("switch_chat", "background")
            if _ensure_in_target_chat_background(
                ocr, hwnd, header_cfg, cfg, target_name
            ):
                before_chat_sig = _capture_chat_bottom_signature(hwnd, cfg)
                _ph("send_text", "background")
                if _send_text_background(hwnd, cfg, content):
                    sent = True
                    _ph("receipt_check")
                    receipt_ok, receipt_msg = _verify_send_receipt(
                        ocr, hwnd, cfg, content, before_chat_sig
                    )
                    _ph(
                        "receipt_result",
                        f"{'OK' if receipt_ok else 'WARN'} {receipt_msg}",
                    )
                    level = "OK" if receipt_ok else "WARN"
                    print(
                        f"[WeChat-Writer] 回执校验({level}) "
                        f"msg_id={msg_id} target={target_name}: {receipt_msg}"
                    )
            if not sent:
                last_error = (
                    "后台发送未成功"
                    if not allow_foreground_fallback
                    else "后台发送未成功，将回退到前台模式"
                )
        except Exception as e:
            last_error = f"后台发送异常: {e}"

    if not sent and (send_mode == "foreground" or allow_foreground_fallback):
        original_fg = get_foreground_window()
        for attempt in range(2):
            try:
                _log_send_event(
                    "准备前台发送",
                    msg_id=msg_id,
                    conv_id=conv_id,
                    target=target_name,
                    attempt=attempt + 1,
                )
                _ph("switch_chat", f"foreground attempt={attempt + 1}")
                if not _ensure_in_target_chat(
                    ocr, hwnd, header_cfg, cfg, target_name
                ):
                    last_error = (
                        f"会话切换失败：未进入目标联系人({target_name})"
                    )
                    if attempt < 1:
                        time.sleep(1)
                    continue

                before_chat_sig = _capture_chat_bottom_signature(hwnd, cfg)
                _ph("send_text", "foreground")
                if _send_text(hwnd, cfg, content):
                    sent = True
                    _ph("receipt_check")
                    receipt_ok, receipt_msg = _verify_send_receipt(
                        ocr, hwnd, cfg, content, before_chat_sig
                    )
                    _ph(
                        "receipt_result",
                        f"{'OK' if receipt_ok else 'WARN'} {receipt_msg}",
                    )
                    level = "OK" if receipt_ok else "WARN"
                    print(
                        f"[WeChat-Writer] 回执校验({level}) "
                        f"msg_id={msg_id} target={target_name}: {receipt_msg}"
                    )
                    break
                last_error = "发送模拟返回失败"
            except Exception as e:
                last_error = str(e)
            if attempt < 1:
                time.sleep(1)

        if original_fg and original_fg != hwnd:
            try:
                bring_to_foreground(original_fg)
            except Exception:
                pass
    elif not sent and send_mode == "background":
        _log_send_event(
            "跳过前台兜底",
            msg_id=msg_id,
            conv_id=conv_id,
            target=target_name,
            reason=last_error or "allow_foreground_fallback disabled",
        )

    return sent, last_error


# ---------------------------------------------------------------------------
# Main writer loop
# ---------------------------------------------------------------------------

def run_writer(
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 1.0,
) -> None:
    db = db_path or resolved_default_db_path()
    cfg = load_wechat_config()
    header_cfg = cfg.get("contact_header_region") or {}
    receipt_cfg = cfg.get("receipt_verify") or {}
    conv_id_prefix = _extract_conv_id_prefix(cfg)

    needs_ocr = bool(header_cfg) or bool(receipt_cfg.get("enabled", True))
    lazy_ocr = bool(cfg.get("writer_lazy_ocr_init", True))
    ocr: Optional[PaddleOCREngine] = None
    ocr_lock = threading.Lock()

    def _ensure_writer_ocr() -> Optional[PaddleOCREngine]:
        nonlocal ocr
        if not needs_ocr:
            return None
        if not lazy_ocr:
            return ocr
        with ocr_lock:
            if ocr is not None:
                return ocr
            ocr_cfg = cfg.get("ocr") or {}
            rpa_phase(
                "wechat.writer",
                "ocr_init_start",
                "首次发送前加载 PaddleOCR（writer_lazy_ocr_init=true 时延迟到此）",
            )
            ocr = PaddleOCREngine(
                lang="ch",
                min_confidence=float(ocr_cfg.get("min_confidence", 0.3)),
                max_side=int(ocr_cfg.get("max_side", 640)),
                invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
                det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
                det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
            )
            ocr.warmup()
            rpa_phase(
                "wechat.writer",
                "ocr_init_done",
                "Writer OCR 已就绪（与 Reader 仍为独立实例，未合并）",
            )
            return ocr

    if needs_ocr and not lazy_ocr:
        ocr_cfg = cfg.get("ocr") or {}
        rpa_phase("wechat.writer", "ocr_init_start", "会话标题校验需要 OCR，正在加载 PaddleOCR")
        ocr = PaddleOCREngine(
            lang="ch",
            min_confidence=float(ocr_cfg.get("min_confidence", 0.3)),
            max_side=int(ocr_cfg.get("max_side", 640)),
            invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
            det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
            det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
        )
        ocr.warmup()
        rpa_phase("wechat.writer", "ocr_init_done", "Writer OCR 模型已加载（与 Reader 各一套实例）")

    send_mode = cfg.get("send_mode", "background")
    allow_foreground_fallback = bool(cfg.get("allow_foreground_fallback", False))
    lock_cfg = cfg.get("window_lock") or {}
    poll_interval_sec = float(
        cfg.get("writer_poll_sec", cfg.get("poll_interval_sec", poll_interval_sec))
    )
    print(
        "[WeChat-Writer] "
        f"DB={db} poll={poll_interval_sec}s mode={send_mode} "
        f"allow_foreground_fallback={allow_foreground_fallback} "
        f"uia_enabled={bool((cfg.get('uia') or {}).get('enabled', False))}"
    )
    if receipt_cfg.get("enabled", True):
        print(
            "[WeChat-Writer] 发送回执校验已开启 "
            f"(timeout={float(receipt_cfg.get('timeout_sec', 2.0))}s)"
        )
    if needs_ocr and lazy_ocr:
        print(
            "[WeChat-Writer] writer_lazy_ocr_init=true：OCR 将在首条待发送任务 dequeue 时再加载，"
            "可与 Reader 错峰；若需启动即加载请设为 false"
        )
    rpa_phase("wechat.writer", "poll_loop_enter", "已进入发送轮询；无任务时每 30s 输出 idle heartbeat")
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
                rpa_heartbeat("wechat.writer", "idle：无待发送消息，轮询 DB 中")
                _last_idle_hb = now
            time.sleep(poll_interval_sec)
            continue

        hwnd = find_wechat_window(cfg)
        if not hwnd or not is_window_valid(hwnd):
            for msg_id, conv_id, content, _ in rows:
                conn = open_db(db)
                try:
                    insert_send_event(
                        conn, msg_id, conv_id, "failed", "微信窗口未找到"
                    )
                    mark_sent_failed(conn, msg_id, "微信窗口未找到")
                finally:
                    conn.close()
            time.sleep(poll_interval_sec)
            continue

        for msg_id, conv_id, content, platform_conv_id in rows:
            target_name = normalize_contact_name(
                _contact_name_from_conv_id(platform_conv_id, conv_id_prefix)
            )
            sent = False
            last_error = ""
            lock_busy = False

            _record_send_phase(db, msg_id, conv_id, "dequeued", "")

            lock_ctx = (
                hold_platform_window_lock(
                    platform=PLATFORM,
                    owner="writer",
                    timeout_sec=float(lock_cfg.get("timeout_sec", 15.0)),
                    retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                )
                if lock_cfg.get("enabled", True)
                else nullcontext(SimpleNamespace(acquired=True))
            )
            with lock_ctx as lock:
                if not lock.acquired:
                    print("[WeChat-Writer] 等待窗口锁超时，稍后重试待发消息")
                    _record_send_phase(db, msg_id, conv_id, "lock_timeout", "")
                    lock_busy = True
                else:
                    _record_send_phase(db, msg_id, conv_id, "lock_acquired", "")
                    active_ocr = _ensure_writer_ocr() if needs_ocr else None
                    sent, last_error = _wechat_try_send_outbound(
                        db=db,
                        msg_id=msg_id,
                        conv_id=conv_id,
                        hwnd=hwnd,
                        cfg=cfg,
                        ocr=active_ocr,
                        header_cfg=header_cfg,
                        content=content,
                        target_name=target_name,
                        send_mode=send_mode,
                        allow_foreground_fallback=allow_foreground_fallback,
                    )

            if lock_busy:
                time.sleep(poll_interval_sec)
                break

            conn = open_db(db)
            try:
                if sent:
                    insert_send_event(conn, msg_id, conv_id, "success", "")
                    mark_sent_ok(conn, msg_id)
                    print(
                        f"[WeChat-Writer] 已发送 msg_id={msg_id} -> {target_name}"
                    )
                else:
                    insert_send_event(
                        conn, msg_id, conv_id, "failed", last_error or "unknown"
                    )
                    mark_sent_failed(conn, msg_id, last_error)
                    print(
                        f"[WeChat-Writer] 发送失败 msg_id={msg_id}: {last_error}"
                    )
            finally:
                conn.close()

            time.sleep(0.3)

        time.sleep(poll_interval_sec)


if __name__ == "__main__":
    run_writer()
