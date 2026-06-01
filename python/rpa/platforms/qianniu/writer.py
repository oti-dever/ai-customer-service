"""
千牛 PC writer: 轮询 messages(sync_status=10)，前台安全输入后更新状态。

与微信 Writer 对齐的能力：
  - 发送前/中校验前台为接待中心窗口，避免贴到其它应用
  - 失败重试 1 次；结束后恢复原先前台窗口
  - 可选标题 OCR：校验当前会话与 platform_conversation_id 一致
  - 发送后回执校验：检测输入框清空 / 末尾气泡变化
"""
from __future__ import annotations

import json
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator, Optional

from ...core.rpa_console_log import rpa_heartbeat, rpa_log, rpa_phase
from ...db import (
    resolved_default_db_path,
    count_pending_send,
    fetch_pending_send,
    insert_send_event,
    mark_sent_failed,
    mark_sent_ok,
    open_db,
)
from ...core.input_sim import (
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
from ...core.ocr_engine import BaseOCREngine, build_ocr_engine
from .coords import coordinate_space, point_ratio_screen, rect_from_absolute_fields, rect_ratios_to_bitmap_xywh
from .session import (
    get_input_center_px,
    get_send_button_center_screen,
    qn_platform_window_lock,
    switch_to_conversation_search,
    try_switch_session_via_list_background,
    verify_current_conversation,
)
from .clicknium import (
    resolve_clicknium_project_root,
    writer_click_send_clicknium,
    writer_fill_input_clicknium,
)
from .window import find_qianniu_hwnd
from ...core.screenshot import capture_region, is_window_valid
from ...core.win32_window import bring_to_foreground, get_foreground_window, screen_to_client
import hashlib

PLATFORM = "qianniu"

VK_DELETE = 0x2E


@contextmanager
def _writer_lock_logged(cfg: dict, owner: str) -> Iterator[object]:
    """窗口锁前后打时间戳日志，便于与 Reader 分段日志对齐排查抢锁。"""
    rpa_log(f"[千牛-Writer] 窗口锁: 等待进入 owner={owner}")
    t_wait = time.perf_counter()
    lock = qn_platform_window_lock(cfg, owner)
    with lock as plw:
        t_in = time.perf_counter()
        acq = getattr(plw, "acquired", True)
        rpa_log(
            f"[千牛-Writer] 窗口锁: 已进入 owner={owner} "
            f"wait_ms={(t_in - t_wait) * 1000.0:.0f} acquired={acq}"
        )
        yield plw
    t_out = time.perf_counter()
    rpa_log(
        f"[千牛-Writer] 窗口锁: 已释放 owner={owner} "
        f"hold_ms={(t_out - t_in) * 1000.0:.0f}"
    )
VK_RETURN = 0x0D

ERR_WINDOW_NOT_FOUND = "ERR_WINDOW_NOT_FOUND"
ERR_FOREGROUND_FAILED = "ERR_FOREGROUND_FAILED"
ERR_SESSION_MISMATCH = "ERR_SESSION_MISMATCH"
ERR_SWITCH_FAILED = "ERR_SWITCH_FAILED"
ERR_SEND_FAILED = "ERR_SEND_FAILED"
ERR_CLICKNIUM = "ERR_CLICKNIUM"


def _writer_clicknium_driver_and_locators(cfg: dict) -> tuple[str, str, str]:
    wc = cfg.get("writer_clicknium") or {}
    driver = str(wc.get("driver", "win32") or "win32").strip().lower()
    in_loc = str(wc.get("input_locator", "") or "").strip()
    send_loc = str(wc.get("send_locator", "") or "").strip()
    return driver, in_loc, send_loc


def load_qianniu_config() -> dict:
    config_path = Path(__file__).resolve().parents[2] / "config" / "qianniu_config.json"
    if config_path.exists():
        with open(config_path, encoding="utf-8") as f:
            return json.load(f)
    return {}


def _extract_conv_id_prefix(cfg: dict) -> str:
    conv = cfg.get("conversation") or {}
    return str(conv.get("conv_id_prefix", "qianniu_"))


def _writer_ocr_engine_name(cfg: dict) -> str:
    """Writer 独立 OCR 引擎名：rapidocr / paddleocr 等，与 Reader 可不同。"""
    writer_cfg = cfg.get("writer") or {}
    raw = str(writer_cfg.get("ocr_engine", "")).strip().lower()
    if raw:
        return raw
    raw = str(cfg.get("writer_ocr_engine", "")).strip().lower()
    if raw:
        return raw
    ocr_cfg = cfg.get("ocr") or {}
    raw = str(ocr_cfg.get("list_header_engine", "rapidocr")).strip().lower()
    return raw or "rapidocr"


def _build_writer_ocr_engine(cfg: dict) -> BaseOCREngine:
    ocr_cfg = cfg.get("ocr") or {}
    return build_ocr_engine(
        _writer_ocr_engine_name(cfg),
        lang=str(ocr_cfg.get("lang", "ch")),
        min_confidence=float(ocr_cfg.get("min_confidence", 0.5)),
        max_side=int(ocr_cfg.get("max_side", 960)),
        invert_for_dark_mode=bool(ocr_cfg.get("invert_for_dark_mode", True)),
        det_thresh=float(ocr_cfg.get("det_thresh", 0.2)),
        det_box_thresh=float(ocr_cfg.get("det_box_thresh", 0.4)),
    )


def _peer_name_from_conv_id(platform_conv_id: str, prefix: str) -> str:
    if platform_conv_id.startswith(prefix):
        return platform_conv_id[len(prefix) :]
    return platform_conv_id


def _space_for_region(cfg: dict, region: dict) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)

# ---------------------------------------------------------------------------
# 发送后回执校验
# ---------------------------------------------------------------------------

def _get_input_region_px(hwnd: int, cfg: dict) -> tuple[int, int, int, int]:
    """获取输入框区域的像素坐标 (x, y, w, h)。"""
    region = cfg.get("input_region") or {}
    space = _space_for_region(cfg, region)
    abs_r = rect_from_absolute_fields(region)
    if abs_r:
        return abs_r
    return rect_ratios_to_bitmap_xywh(
        hwnd,
        float(region.get("left_ratio", 0.22)),
        float(region.get("top_ratio", 0.77)),
        float(region.get("right_ratio", 0.78)),
        float(region.get("bottom_ratio", 0.93)),
        space,
    )


def _get_chat_bottom_region_px(hwnd: int, cfg: dict, height_ratio: float = 0.15) -> tuple[int, int, int, int]:
    """获取聊天区底部区域的像素坐标，用于检测末尾气泡变化。"""
    chat = cfg.get("chat_region") or {}
    space = _space_for_region(cfg, chat)
    abs_r = rect_from_absolute_fields(chat)
    if abs_r:
        x, y, w, h = abs_r
        bottom_h = max(30, int(h * height_ratio))
        return x, y + h - bottom_h, w, bottom_h
    left = float(chat.get("left_ratio", 0.19))
    top = float(chat.get("top_ratio", 0.14))
    right = float(chat.get("right_ratio", 0.795))
    bottom = float(chat.get("bottom_ratio", 0.74))
    bottom_top = bottom - (bottom - top) * height_ratio
    return rect_ratios_to_bitmap_xywh(hwnd, left, bottom_top, right, bottom, space)


def _capture_region_hash(hwnd: int, x: int, y: int, w: int, h: int) -> str:
    """捕获区域并返回其内容哈希（用于快速比较变化）。"""
    try:
        bgra, _, _, _ = capture_region(hwnd, x, y, w, h)
        return hashlib.md5(bgra).hexdigest()
    except Exception:
        return ""


def _capture_input_region_hash(hwnd: int, cfg: dict) -> str:
    """输入框区域整图 MD5；用于发送后是否已脱离「发送前锚点」像素态的快速判定（先于 OCR）。"""
    try:
        x, y, w, h = _get_input_region_px(hwnd, cfg)
        if w <= 0 or h <= 0:
            return ""
        return _capture_region_hash(hwnd, x, y, w, h)
    except Exception:
        return ""


def _ocr_input_box_content(
    ocr: Optional[BaseOCREngine], hwnd: int, cfg: dict
) -> Optional[str]:
    """OCR 识别输入框区域的文本内容。"""
    if not ocr:
        return None
    try:
        x, y, w, h = _get_input_region_px(hwnd, cfg)
        if w <= 0 or h <= 0:
            return None
        bgra, rw, rh, _ = capture_region(hwnd, x, y, w, h)
        blocks = ocr.recognize(bgra, rw, rh)
        if not blocks:
            return ""
        return " ".join(str(b[0]).strip() for b in blocks if len(b) >= 1).strip()
    except Exception:
        return None


def _verify_send_receipt(
    hwnd: int,
    cfg: dict,
    ocr: Optional[BaseOCREngine],
    sent_text: str,
    pre_chat_hash: str,
    timeout_sec: float = 2.0,
    check_interval: float = 0.3,
) -> tuple[bool, str]:
    """
    发送后回执校验（双通道 OR、廉价通道优先）：
    1. 聊天区底部像素 hash 相对发送前变化 → 立即成功
    2. 输入框区域像素 hash 相对本轮锚点连续稳定变化 → 成功（先于 OCR，减轻 Writer OCR 轮询）
    3. 输入框 OCR：空或已不含发送正文前缀 → 成功

    配置见 receipt_verify：check_interval_sec、use_input_region_hash、input_hash_confirm_reads。
    """
    receipt_cfg = cfg.get("receipt_verify") or {}
    if not receipt_cfg.get("enabled", True):
        return True, "receipt_verify disabled"

    poll_sec = float(receipt_cfg.get("check_interval_sec", check_interval))
    poll_sec = max(0.05, min(1.0, poll_sec))
    use_input_hash = bool(receipt_cfg.get("use_input_region_hash", True))
    confirm_reads = max(1, int(receipt_cfg.get("input_hash_confirm_reads", 2)))

    deadline = time.time() + max(0.5, timeout_sec)
    input_anchor = _capture_input_region_hash(hwnd, cfg) if use_input_hash else ""
    last_input_hash = ""
    input_hash_streak = 0

    while time.time() < deadline:
        # 1) 聊天区底部：无 OCR，通常最先反映「消息已上屏」
        if pre_chat_hash:
            cx, cy, cw, ch = _get_chat_bottom_region_px(hwnd, cfg)
            post_hash = _capture_region_hash(hwnd, cx, cy, cw, ch)
            if post_hash and post_hash != pre_chat_hash:
                return True, "chat_changed"

        # 2) 输入框区域像素：与进入校验时的锚点不同且连续 N 次相同 → 视为已脱离「待发编辑」态
        if use_input_hash and input_anchor:
            h = _capture_input_region_hash(hwnd, cfg)
            if h and h != input_anchor:
                if h == last_input_hash:
                    input_hash_streak += 1
                else:
                    last_input_hash = h
                    input_hash_streak = 1
                if input_hash_streak >= confirm_reads:
                    return True, "input_region_hash_stable"
            else:
                last_input_hash = h if h else ""
                input_hash_streak = 0

        # 3) 输入框 OCR（兜底；hash 已成功则不再跑 OCR）
        if ocr:
            content = _ocr_input_box_content(ocr, hwnd, cfg)
            if content is not None and len(content.strip()) == 0:
                return True, "input_cleared"
            if content is not None and sent_text[:10] not in content:
                return True, "input_cleared"

        time.sleep(poll_sec)

    return False, "timeout: input not cleared and chat unchanged"


def _err(code: str, detail: str) -> str:
    return f"{code}: {detail}"


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

def _should_click_send_after_enter(
    ocr: Optional[BaseOCREngine],
    hwnd: int,
    cfg: dict,
    text: str,
    pre_chat_hash: str,
    h_after: str,
) -> bool:
    """
    Enter 后是否仍需补点「发送」：聊天底图已变则不需要；否则用 OCR 看输入框是否仍像有待发正文。
    在窗口锁外调用，避免 OCR 推理长时间占 writer:bg_post。
    """
    if h_after and pre_chat_hash and h_after != pre_chat_hash:
        return False
    if not ocr:
        return True
    content = _ocr_input_box_content(ocr, hwnd, cfg)
    if content is None:
        return True
    t = text.strip()
    if not t:
        return False
    if len(content.strip()) == 0:
        return False
    if t[: min(12, len(t))] not in content:
        return False
    return True


def _send_message_background(
    hwnd: int,
    cfg: dict,
    text: str,
    ocr: Optional[BaseOCREngine] = None,
) -> tuple[bool, str]:
    """
    后台兜底：不依赖前台焦点，直接 PostMessage 到目标窗口。
    顺序：填入文本 → 先 Enter（千牛默认 Enter 发送）→ 若聊天区未更新且输入框仍像有正文再点「发送」坐标。
    返回 (success, pre_chat_hash) 用于后续回执校验。

    writer_clicknium：若 driver 为 auto/clicknium 且配置了 input_locator 与可用工程根，
    则优先用 Clicknium 对输入框 set_text；发送补充点击优先 send_locator，否则回退坐标 PostMessage。

    writer:bg_post 拆成两段持锁：仅截图/填字/按键/短截图在锁内；是否补点发送的 OCR 在锁外，
    最后再短持锁完成坐标点击（与 send_qianniu_message 外层不再包 writer:bg_post，避免嵌套死锁）。
    """
    cx, cy, cw, ch = _get_chat_bottom_region_px(hwnd, cfg)
    driver, in_loc, send_loc = _writer_clicknium_driver_and_locators(cfg)
    root = resolve_clicknium_project_root(cfg)
    use_cn = bool(in_loc and root and driver in ("auto", "clicknium"))
    pre_chat_hash = ""
    filled = False

    if use_cn:
        with _writer_lock_logged(cfg, "writer:bg_post"):
            pre_chat_hash = _capture_region_hash(hwnd, cx, cy, cw, ch)
            filled = writer_fill_input_clicknium(root, in_loc, text)
            if filled:
                time.sleep(0.05)
                post_key(hwnd, VK_RETURN)
                time.sleep(0.08)
                post_key(hwnd, VK_RETURN)
                time.sleep(0.18)
                send_sx, send_sy = get_send_button_center_screen(hwnd, cfg)
                h_after = _capture_region_hash(hwnd, cx, cy, cw, ch)
        if filled:
            need_send = _should_click_send_after_enter(ocr, hwnd, cfg, text, pre_chat_hash, h_after)
            with _writer_lock_logged(cfg, "writer:bg_post"):
                if need_send:
                    if send_loc and writer_click_send_clicknium(root, send_loc):
                        time.sleep(0.10)
                    else:
                        clicked = post_click_window_at_point(send_sx, send_sy, delay_ms=25)
                        time.sleep(0.10)
                        if not clicked:
                            simulate_click(send_sx, send_sy, delay_ms=30)
                            time.sleep(0.12)
            return True, pre_chat_hash
        if driver == "clicknium":
            raise RuntimeError(
                _err(ERR_CLICKNIUM, "Clicknium 写入输入框失败（请检查 input_locator 与 .locator）")
            )

    with _writer_lock_logged(cfg, "writer:bg_post"):
        if not pre_chat_hash:
            pre_chat_hash = _capture_region_hash(hwnd, cx, cy, cw, ch)
        in_sx, in_sy = get_input_center_px(hwnd, cfg)
        in_cx, in_cy = screen_to_client(hwnd, in_sx, in_sy)
        post_click(hwnd, in_cx, in_cy, delay_ms=25)
        time.sleep(0.05)

        post_clear_text(hwnd, max_chars=max(80, min(800, len(text) * 2)))
        time.sleep(0.03)
        post_type_text(hwnd, text, delay_per_char_ms=3)
        time.sleep(0.05)

        post_key(hwnd, VK_RETURN)
        time.sleep(0.08)
        post_key(hwnd, VK_RETURN)
        time.sleep(0.18)

        send_sx, send_sy = get_send_button_center_screen(hwnd, cfg)
        h_after = _capture_region_hash(hwnd, cx, cy, cw, ch)

    need_send = _should_click_send_after_enter(ocr, hwnd, cfg, text, pre_chat_hash, h_after)
    with _writer_lock_logged(cfg, "writer:bg_post"):
        if need_send:
            clicked = post_click_window_at_point(send_sx, send_sy, delay_ms=25)
            time.sleep(0.10)
            if not clicked:
                simulate_click(send_sx, send_sy, delay_ms=30)
                time.sleep(0.12)

    return True, pre_chat_hash


def send_qianniu_message(
    hwnd: int,
    cfg: dict,
    text: str,
    ocr: Optional[BaseOCREngine],
    target_peer: str,
    platform_conv_id: str,
    switch_timeout_sec: float = 5.0,
    require_header_match: bool = True,
    allow_background_fallback: bool = True,
    prefer_background: bool = True,
    verify_receipt: bool = True,
) -> bool:
    receipt_cfg = cfg.get("receipt_verify") or {}
    receipt_enabled = verify_receipt and receipt_cfg.get("enabled", True)
    receipt_timeout = float(receipt_cfg.get("timeout_sec", 2.0))
    
    def _do_receipt_check(pre_hash: str) -> None:
        if not receipt_enabled:
            return
        t0 = time.perf_counter()
        ok, reason = _verify_send_receipt(
            hwnd, cfg, ocr, text, pre_hash, timeout_sec=receipt_timeout
        )
        ms = (time.perf_counter() - t0) * 1000.0
        if ok:
            print(f"[千牛-Writer] 发送回执校验通过: {reason} wall_ms={ms:.0f}")
        else:
            print(
                f"[千牛-Writer] WARN 发送回执校验失败: {reason} wall_ms={ms:.0f}"
                "（消息可能仍已发送）"
            )
    
    rpa_log(
        f"[千牛-Writer] send_qianniu_message 开始 target={target_peer!r} "
        f"prefer_bg={prefer_background} require_header_match={require_header_match}"
    )
    t_send = time.perf_counter()

    if prefer_background:
        try:
            # 后台发送前先做会话校验；不匹配则尝试「OCR 可见列表 + 后台点行」，再校验。
            session_ok_bg = verify_current_conversation(ocr, cfg, hwnd, target_peer, platform_conv_id)
            if session_ok_bg:
                _, pre_hash = _send_message_background(hwnd, cfg, text, ocr=ocr)
                _do_receipt_check(pre_hash)
                rpa_log(
                    f"[千牛-Writer] send_qianniu_message 结束(后台直发) target={target_peer!r} "
                    f"total_ms={(time.perf_counter() - t_send) * 1000.0:.0f}"
                )
                return True
            if try_switch_session_via_list_background(
                ocr, hwnd, cfg, target_peer, switch_timeout_sec, platform_conv_id
            ):
                if verify_current_conversation(ocr, cfg, hwnd, target_peer, platform_conv_id):
                    _, pre_hash = _send_message_background(hwnd, cfg, text, ocr=ocr)
                    _do_receipt_check(pre_hash)
                    rpa_log(
                        f"[千牛-Writer] send_qianniu_message 结束(后台切换后) target={target_peer!r} "
                        f"total_ms={(time.perf_counter() - t_send) * 1000.0:.0f}"
                    )
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

    session_ok = verify_current_conversation(ocr, cfg, hwnd, target_peer, platform_conv_id)
    if not session_ok:
        if try_switch_session_via_list_background(
            ocr, hwnd, cfg, target_peer, switch_timeout_sec, platform_conv_id
        ):
            session_ok = verify_current_conversation(ocr, cfg, hwnd, target_peer, platform_conv_id)
    if not session_ok:
        if not fg_ok:
            # 列表兜底失败且无法前台时：不做 Ctrl+F，避免误发。
            raise RuntimeError(_err(
                ERR_SESSION_MISMATCH,
                f"会话不匹配（期望: {target_peer}），列表切换失败且前台不可用，已取消发送",
            ))
        with _writer_lock_logged(cfg, "writer:ctrl_f_switch"):
            switched = switch_to_conversation_search(
                hwnd, cfg, ocr, target_peer, timeout_sec=switch_timeout_sec, platform_conv_id=platform_conv_id
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
        _, pre_hash = _send_message_background(hwnd, cfg, text, ocr=ocr)
        _do_receipt_check(pre_hash)
        rpa_log(
            f"[千牛-Writer] send_qianniu_message 结束(前台不可用→后台) target={target_peer!r} "
            f"total_ms={(time.perf_counter() - t_send) * 1000.0:.0f}"
        )
        return True

    with _writer_lock_logged(cfg, "writer:fg_send"):
        # 前台发送前捕获聊天区底部哈希
        cx, cy, cw, ch = _get_chat_bottom_region_px(hwnd, cfg)
        pre_chat_hash = _capture_region_hash(hwnd, cx, cy, cw, ch)

        driver, in_loc, send_loc = _writer_clicknium_driver_and_locators(cfg)
        root = resolve_clicknium_project_root(cfg)
        use_cn = bool(in_loc and root and driver in ("auto", "clicknium"))
        filled_cn = False
        if use_cn:
            filled_cn = writer_fill_input_clicknium(root, in_loc, text)
            if filled_cn:
                time.sleep(0.12)
                if get_foreground_window() != hwnd:
                    if driver == "clicknium":
                        raise RuntimeError(
                            _err(ERR_FOREGROUND_FAILED, "Clicknium 填字后千牛失去前台焦点，已取消发送")
                        )
                    filled_cn = False
            elif driver == "clicknium":
                raise RuntimeError(
                    _err(ERR_CLICKNIUM, "Clicknium 写入输入框失败（请检查 input_locator 与 .locator）")
                )

        if not filled_cn:
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

        send_x, send_y = get_send_button_center_screen(hwnd, cfg)
        if filled_cn and send_loc and root:
            if not writer_click_send_clicknium(root, send_loc):
                simulate_click(send_x, send_y, delay_ms=50)
        else:
            simulate_click(send_x, send_y, delay_ms=50)
        time.sleep(0.25)

    # 发送后回执校验（输入框/聊天区截图在锁外轮询，避免长时间占锁）
    _do_receipt_check(pre_chat_hash)
    rpa_log(
        f"[千牛-Writer] send_qianniu_message 结束(前台发送) target={target_peer!r} "
        f"total_ms={(time.perf_counter() - t_send) * 1000.0:.0f}"
    )
    return True


def _process_pending_message(
    db: Path,
    hwnd: int,
    cfg: dict,
    ocr: Optional[BaseOCREngine],
    msg_id: int,
    conv_id: int,
    content: str,
    platform_conv_id: str,
    prefix: str,
    max_retries: int,
    switch_timeout_sec: float,
    require_header_match: bool,
    allow_background_fallback: bool,
    prefer_background: bool,
) -> tuple[bool, str, str]:
    target_peer = _peer_name_from_conv_id(platform_conv_id, prefix)
    sent = False
    last_error = ""
    original_fg = get_foreground_window()
    rpa_log(
        f"[千牛-Writer] 进入发送流程 msg_id={msg_id} target={target_peer!r} "
        f"attempts_max={max_retries + 1}"
    )

    for attempt in range(max_retries + 1):
        _record_send_phase(
            db,
            msg_id,
            conv_id,
            "send_attempt",
            f"attempt={attempt + 1}/{max_retries + 1}",
        )
        rpa_log(
            f"[千牛-Writer] 尝试发送 msg_id={msg_id} attempt={attempt + 1}/{max_retries + 1}"
        )
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
    return sent, last_error, target_peer


def run_writer(
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 1.0,
) -> None:
    db = db_path or resolved_default_db_path()
    cfg = load_qianniu_config()
    prefix = _extract_conv_id_prefix(cfg)
    header_cfg = cfg.get("contact_header_region")
    receipt_cfg = cfg.get("receipt_verify") or {}
    needs_ocr = bool(header_cfg) or bool(receipt_cfg.get("enabled", True))
    lazy_ocr = bool(cfg.get("writer_lazy_ocr_init", True))
    prewarm_ocr_at_start = bool(cfg.get("writer_prewarm_ocr_at_start", True))
    ocr: Optional[BaseOCREngine] = None
    ocr_lock = threading.Lock()
    _writer_engine_label = _writer_ocr_engine_name(cfg)

    def _ensure_writer_ocr() -> Optional[BaseOCREngine]:
        nonlocal ocr
        if not needs_ocr:
            return None
        if not lazy_ocr:
            return ocr
        with ocr_lock:
            if ocr is not None:
                return ocr
            rpa_phase(
                "qianniu.writer",
                "ocr_init_start",
                f"Writer：正在加载 OCR 引擎 {_writer_engine_label!r}（独立实例；与 Reader 分离）",
            )
            ocr = _build_writer_ocr_engine(cfg)
            ocr.warmup()
            rpa_phase(
                "qianniu.writer",
                "ocr_init_done",
                "Writer OCR 已就绪（与 Reader 仍为独立实例）",
            )
            return ocr

    if needs_ocr and not lazy_ocr:
        rpa_phase(
            "qianniu.writer",
            "ocr_init_start",
            f"会话标题校验需要 OCR，正在加载 {_writer_engine_label!r}",
        )
        ocr = _build_writer_ocr_engine(cfg)
        ocr.warmup()
        rpa_phase("qianniu.writer", "ocr_init_done", "Writer OCR 模型已加载（与 Reader 各一套实例）")

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
        f"(session_verify={'on' if needs_ocr else 'off'} lazy_ocr={lazy_ocr})"
    )
    if needs_ocr and lazy_ocr:
        if prewarm_ocr_at_start:
            print(
                f"[千牛-Writer] writer_lazy_ocr_init=true 且 writer_prewarm_ocr_at_start=true："
                f"将在进入轮询前同步加载 Writer OCR（{_writer_engine_label}，避免拖到首条待发才冷启动）"
            )
        else:
            print(
                "[千牛-Writer] writer_lazy_ocr_init=true：OCR 将在首次需要 Writer OCR 时加载；"
                "启动即加载请设 writer_prewarm_ocr_at_start=true 或 writer_lazy_ocr_init=false"
            )
    if needs_ocr and lazy_ocr and prewarm_ocr_at_start:
        _ensure_writer_ocr()
    rpa_phase("qianniu.writer", "poll_loop_enter", "已进入发送轮询；无任务时每 30s 输出 idle heartbeat")
    _last_idle_hb = 0.0
    _prewarm_thread: Optional[threading.Thread] = None

    def _maybe_prewarm_writer_ocr() -> None:
        """DB 有待发时后台预热 Writer OCR，避免与抢锁叠在同一段首次加载。"""
        nonlocal _prewarm_thread
        if not needs_ocr or not lazy_ocr:
            return
        with ocr_lock:
            if ocr is not None:
                return
        if _prewarm_thread is not None and _prewarm_thread.is_alive():
            return
        conn = open_db(db)
        try:
            n = count_pending_send(conn, PLATFORM)
        finally:
            conn.close()
        if n <= 0:
            return

        def _run() -> None:
            rpa_log(f"[千牛-Writer] OCR 后台预热线程启动（待发 n={n}）")
            _ensure_writer_ocr()

        _prewarm_thread = threading.Thread(
            target=_run,
            name="qianniu_writer_ocr_prewarm",
            daemon=True,
        )
        _prewarm_thread.start()

    while True:
        _maybe_prewarm_writer_ocr()

        rows = []
        conn = open_db(db)
        try:
            rows = fetch_pending_send(conn, PLATFORM)
        finally:
            conn.close()

        if not rows:
            now = time.time()
            if now - _last_idle_hb >= 30.0:
                rpa_heartbeat("qianniu.writer", "idle：无待发送消息，轮询 DB 中")
                _last_idle_hb = now
            time.sleep(poll_interval_sec)
            continue

        ids = [r[0] for r in rows]
        convs = [r[3] for r in rows]
        rpa_log(
            f"[千牛-Writer] 检出待发 n={len(rows)} msg_id={ids} platform_conv_id={convs}"
        )

        t_find_hwnd = time.perf_counter()
        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd or not is_window_valid(hwnd):
            rpa_log(
                f"[千牛-Writer] WARN 千牛窗口无效 hwnd={hwnd!r}，本批 {len(rows)} 条标记失败"
            )
            for msg_id, conv_id, content, _ in rows:
                conn = open_db(db)
                try:
                    insert_send_event(
                        conn,
                        msg_id,
                        conv_id,
                        "failed",
                        _err(ERR_WINDOW_NOT_FOUND, "千牛接待中心窗口未找到"),
                    )
                    mark_sent_failed(
                        conn,
                        msg_id,
                        _err(ERR_WINDOW_NOT_FOUND, "千牛接待中心窗口未找到"),
                    )
                finally:
                    conn.close()
            time.sleep(poll_interval_sec)
            continue

        rpa_log(
            f"[千牛-Writer] 窗口就绪 hwnd={hwnd} find_hwnd_ms="
            f"{(time.perf_counter() - t_find_hwnd) * 1000.0:.0f}，开始依次发送"
        )

        for msg_id, conv_id, content, platform_conv_id in rows:
            t_one = time.perf_counter()
            rpa_log(
                f"[千牛-Writer]  dequeue msg_id={msg_id} conv_id={conv_id} "
                f"platform_conv_id={platform_conv_id!r} text_len={len(content or '')}"
            )
            _record_send_phase(db, msg_id, conv_id, "dequeued", "")
            t_ocr0 = time.perf_counter()
            active_ocr = _ensure_writer_ocr() if needs_ocr else None
            if needs_ocr and active_ocr is not None:
                rpa_log(
                    f"[千牛-Writer] OCR 就绪检查 msg_id={msg_id} "
                    f"ensure_ms={(time.perf_counter() - t_ocr0) * 1000.0:.0f}"
                )
            sent, last_error, target_peer = _process_pending_message(
                db=db,
                hwnd=hwnd,
                cfg=cfg,
                ocr=active_ocr,
                msg_id=msg_id,
                conv_id=conv_id,
                content=content,
                platform_conv_id=platform_conv_id,
                prefix=prefix,
                max_retries=max_retries,
                switch_timeout_sec=switch_timeout_sec,
                require_header_match=require_header_match,
                allow_background_fallback=allow_background_fallback,
                prefer_background=prefer_background,
            )

            conn = open_db(db)
            try:
                if sent:
                    insert_send_event(conn, msg_id, conv_id, "success", "")
                    mark_sent_ok(conn, msg_id)
                    print(
                        f"[千牛-Writer] 已发送 msg_id={msg_id} conv_id={platform_conv_id!r} "
                        f"target={target_peer!r}"
                    )
                else:
                    insert_send_event(
                        conn, msg_id, conv_id, "failed", (last_error or "")[:500]
                    )
                    mark_sent_failed(conn, msg_id, last_error)
                    print(
                        f"[千牛-Writer] 发送失败 msg_id={msg_id} conv_id={platform_conv_id!r} "
                        f"target={target_peer!r} err={last_error}"
                    )
            finally:
                conn.close()

            rpa_log(
                f"[千牛-Writer] 单条结束 msg_id={msg_id} sent={sent} "
                f"wall_ms={(time.perf_counter() - t_one) * 1000.0:.0f}"
            )
            time.sleep(0.3)

        time.sleep(poll_interval_sec)


if __name__ == "__main__":
    run_writer()
