"""
千牛会话相关公共能力：

- 标题区 OCR 读取当前会话名
- 会话列表区域 OCR 解析当前可见联系人
- 按列表点击切换会话
- Ctrl+F 搜索切换会话
"""
from __future__ import annotations

from dataclasses import dataclass
import re
import time
from pathlib import Path
from typing import Any, Optional

_LIST_CAPTURE_LAST_OK_LOG_TS = 0.0

from .input_sim import (
    ClipboardGuard,
    post_click,
    post_click_window_at_point,
    set_clipboard_text,
    simulate_click,
    simulate_key,
    simulate_key_combo,
)
from .win32_window import screen_to_client
from .ocr_engine import BaseOCREngine
from .qianniu_coords import coordinate_space, point_ratio_screen, rect_from_absolute_fields, rect_ratios_to_bitmap_xywh
from .qianniu_header import pick_peer_name_from_ocr_blocks, peer_name_is_noise
from .screenshot import capture_region, hwnd_capture_subrect_unobstructed

VK_CONTROL = 0x11
VK_A = 0x41
VK_F = 0x46
VK_V = 0x56
VK_RETURN = 0x0D

_QN_PLATFORM = "qianniu"


def _list_capture_log_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    if "list_capture_log" in dbg:
        return bool(dbg["list_capture_log"])
    return bool(dbg.get("clicknium_capture_log", False))


def _qn_log_list_capture(cfg: dict[str, Any], message: str, *, throttle_ok_sec: float = 0.0) -> None:
    """列表截图路径排查日志；成功类消息可按秒节流，失败/回退立即打印。"""
    if not _list_capture_log_enabled(cfg):
        return
    global _LIST_CAPTURE_LAST_OK_LOG_TS
    if throttle_ok_sec > 0:
        now = time.time()
        if now - _LIST_CAPTURE_LAST_OK_LOG_TS < throttle_ok_sec:
            return
        _LIST_CAPTURE_LAST_OK_LOG_TS = now
    print(f"[千牛-列表] {message}")


def qn_platform_window_lock(cfg: dict[str, Any], owner: str):
    """
    千牛 Reader/Writer 共用的平台窗口锁（文件锁 + 同进程线程互斥）。
    Writer 侧用于「仅截图瞬间」持锁、Paddle 推理在锁外执行，缩短与 Reader 互等。
    window_lock.enabled=false 时为 no-op。
    """
    lock_cfg = cfg.get("window_lock") or {}
    if not lock_cfg.get("enabled", True):

        class _NoLock:
            acquired = True

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return None

        return _NoLock()
    from .window_lock import hold_platform_window_lock

    return hold_platform_window_lock(
        platform=_QN_PLATFORM,
        owner=owner,
        timeout_sec=float(lock_cfg.get("timeout_sec", 15.0)),
        retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
    )


@dataclass(frozen=True)
class ConversationListEntry:
    name: str
    y_center: float
    row_index: int
    confidence: float


_LIST_NOISE_EXACT = {
    "商家",
    "买家",
    "消息",
}

_LIST_NOISE_SUBSTRINGS = (
    "列表分组开启",
    "最后一句消息",
    "群聊消息",
    "小二消息",
    "服务商消息",
    "1688消息",
    "淘宝购物消息",
    "全部买家",
    "全部联系人",
    "正在接待",
)


def _space_for_region(cfg: dict[str, Any], region: dict[str, Any]) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)


def _header_exclude_list(cfg: dict[str, Any]) -> list[str]:
    conv = cfg.get("conversation") or {}
    raw = conv.get("peer_name_exclude_substrings")
    if isinstance(raw, list):
        return [str(x) for x in raw if x]
    return []


def get_header_region_px(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int, int, int]:
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


def get_input_center_px(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int]:
    """输入框中心屏幕坐标；优先校准得到的 x,y,w,h 矩形中心。"""
    region = cfg.get("input_region") or {}
    abs_r = rect_from_absolute_fields(region)
    if abs_r:
        x, y, w, h = abs_r
        from .screenshot import get_window_rect

        wx, wy, _, _ = get_window_rect(hwnd)
        return wx + x + w // 2, wy + y + h // 2
    space = _space_for_region(cfg, region)
    return point_ratio_screen(
        hwnd,
        (float(region.get("left_ratio", 0.22)) + float(region.get("right_ratio", 0.78))) / 2.0,
        (float(region.get("top_ratio", 0.77)) + float(region.get("bottom_ratio", 0.93))) / 2.0,
        space,
    )


def get_send_button_center_screen(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int]:
    """发送按钮中心屏幕坐标；优先校准得到的 send_button 矩形中心，否则用 center_*_ratio。"""
    sb = cfg.get("send_button") or {}
    abs_r = rect_from_absolute_fields(sb)
    if abs_r:
        x, y, w, h = abs_r
        from .screenshot import get_window_rect

        wx, wy, _, _ = get_window_rect(hwnd)
        return wx + x + w // 2, wy + y + h // 2
    space = _space_for_region(cfg, sb)
    return point_ratio_screen(
        hwnd,
        float(sb.get("center_x_ratio", 0.72)),
        float(sb.get("center_y_ratio", 0.945)),
        space,
    )


def _list_y_scaled_for_win32_click(
    hwnd: int,
    cfg: dict[str, Any],
    y_img: float,
    list_image_h: Optional[int],
) -> float:
    """
    Clicknium 截到的列表控件高度与 Win32 校准的 conversation_list_region 高度可能略有差异，
    将列表图内的 y 映射到与点击坐标一致的 Win32 区域高度。
    """
    if list_image_h is None or list_image_h <= 0:
        return y_img
    lc = cfg.get("list_capture") or {}
    if not lc.get("rescale_click_y_to_win32", True):
        return y_img
    _, _, _, region_h = get_conversation_list_region_px(hwnd, cfg)
    if region_h <= 0 or abs(list_image_h - region_h) < 3:
        return y_img
    out = y_img * float(region_h) / float(list_image_h)
    if _list_capture_log_enabled(cfg) and abs(out - y_img) > 1.0:
        print(
            "[千牛-列表] 点击 y 映射: 列表图高="
            f"{list_image_h}px Win32区高={region_h}px y {y_img:.1f}→{out:.1f}"
        )
    return out


def get_conversation_list_region_px(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int, int, int]:
    region = cfg.get("conversation_list_region") or {}
    abs_r = rect_from_absolute_fields(region)
    if abs_r:
        return abs_r
    space = _space_for_region(cfg, region)
    return rect_ratios_to_bitmap_xywh(
        hwnd,
        float(region.get("left_ratio", 0.01)),
        float(region.get("top_ratio", 0.12)),
        float(region.get("right_ratio", 0.19)),
        float(region.get("bottom_ratio", 0.74)),
        space,
    )


def capture_contact_header_bgra(
    hwnd: int, cfg: dict[str, Any]
) -> tuple[Optional[bytes], int, int]:
    """仅截图标题区，供锁外再跑 OCR，缩短窗口锁占用。

    始终为 capture_region（PrintWindow 优先）子区域裁剪，不走 Clicknium；
    故其它窗挡在屏幕前时仍可取千牛位图内容，无需再做「是否遮挡」路由。
    """
    hx, hy, hw, hh = get_header_region_px(hwnd, cfg)
    if hw <= 0 or hh <= 0:
        return None, 0, 0
    try:
        bgra, w, h, _ = capture_region(hwnd, hx, hy, hw, hh)
        return bgra, w, h
    except Exception:
        return None, 0, 0


def peer_name_from_header_bgra(
    ocr: BaseOCREngine,
    bgra: Optional[bytes],
    w: int,
    h: int,
    cfg: dict[str, Any],
) -> Optional[str]:
    """对已截取的标题图做 OCR，解析当前会话名。"""
    if not bgra or w <= 0 or h <= 0:
        return None
    try:
        blocks = ocr.recognize(bgra, w, h)
        if not blocks:
            return None
        return pick_peer_name_from_ocr_blocks(blocks, _header_exclude_list(cfg))
    except Exception:
        return None


def read_current_conversation_name(
    ocr: BaseOCREngine,
    hwnd: int,
    cfg: dict[str, Any],
) -> Optional[str]:
    bgra, w, h = capture_contact_header_bgra(hwnd, cfg)
    return peer_name_from_header_bgra(ocr, bgra, w, h, cfg)


def header_matches_target(header_text: Optional[str], target: str) -> bool:
    if not header_text or not target:
        return True
    if target in header_text or header_text in target:
        return True
    if len(target) >= 4 and target[:4] in header_text:
        return True
    return False


def verify_current_conversation(
    ocr: Optional[BaseOCREngine],
    cfg: dict[str, Any],
    hwnd: int,
    target_peer: str,
    platform_conv_id: str = "",
) -> bool:
    fallback_conv_id = str((cfg.get("conversation") or {}).get("fallback_platform_conversation_id", ""))
    if fallback_conv_id and platform_conv_id == fallback_conv_id:
        return True
    if not ocr or not cfg.get("contact_header_region"):
        return True
    with qn_platform_window_lock(cfg, "writer:verify_header"):
        bgra, w, h = capture_contact_header_bgra(hwnd, cfg)
    text = peer_name_from_header_bgra(ocr, bgra, w, h, cfg)
    ok = header_matches_target(text, target_peer)
    if not ok:
        print(f"[千牛-Session] 会话校验失败: 标题OCR={text!r} 期望包含={target_peer!r}")
    return ok


def switch_to_conversation_search(
    hwnd: int,
    cfg: dict[str, Any],
    ocr: Optional[BaseOCREngine],
    target_peer: str,
    timeout_sec: float,
    platform_conv_id: str = "",
) -> bool:
    fallback_conv_id = str((cfg.get("conversation") or {}).get("fallback_platform_conversation_id", ""))
    if fallback_conv_id and platform_conv_id == fallback_conv_id:
        return True
    if not ocr or not target_peer.strip():
        return False

    sx, sy = get_input_center_px(hwnd, cfg)
    with ClipboardGuard():
        set_clipboard_text(target_peer)
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

    scan_cfg = cfg.get("conversation_scan") or {}
    settle_ms = int(scan_cfg.get("switch_settle_ms", 800))

    deadline = time.time() + max(1.0, timeout_sec)
    while time.time() < deadline:
        text = read_current_conversation_name(ocr, hwnd, cfg)
        if header_matches_target(text, target_peer):
            time.sleep(max(0.05, settle_ms / 1000.0))
            return True
        time.sleep(0.25)
    return False


def click_conversation_list_row(
    hwnd: int,
    cfg: dict[str, Any],
    y_center: float,
    list_image_height: Optional[int] = None,
) -> None:
    y_use = _list_y_scaled_for_win32_click(hwnd, cfg, y_center, list_image_height)
    x, y, w, _h = get_conversation_list_region_px(hwnd, cfg)
    click_ratio = float((cfg.get("conversation_scan") or {}).get("list_click_offset_ratio", 0.45))
    click_ratio = min(0.9, max(0.1, click_ratio))
    from .screenshot import get_window_rect

    win_x, win_y, _win_w, _win_h = get_window_rect(hwnd)
    cx = win_x + x + int(w * click_ratio)
    cy = win_y + y + int(y_use)
    simulate_click(cx, cy, delay_ms=50)


def click_conversation_list_row_background(
    hwnd: int,
    cfg: dict[str, Any],
    y_center: float,
    list_image_height: Optional[int] = None,
) -> None:
    """
    与微信 RPA 一致：对列表目标点使用 PostMessage 投递点击，不移动鼠标、不强制前台，
    便于客服专注聚合接待界面时后台切换千牛当前会话。
    """
    y_use = _list_y_scaled_for_win32_click(hwnd, cfg, y_center, list_image_height)
    x, y, w, _h = get_conversation_list_region_px(hwnd, cfg)
    click_ratio = float((cfg.get("conversation_scan") or {}).get("list_click_offset_ratio", 0.45))
    click_ratio = min(0.9, max(0.1, click_ratio))
    from .screenshot import get_window_rect

    win_x, win_y, _, _ = get_window_rect(hwnd)
    screen_x = win_x + x + int(w * click_ratio)
    screen_y = win_y + y + int(y_use)
    if not post_click_window_at_point(screen_x, screen_y, delay_ms=20):
        cx, cy = screen_to_client(hwnd, screen_x, screen_y)
        post_click(hwnd, cx, cy, delay_ms=20)


def switch_to_list_entry(
    hwnd: int,
    cfg: dict[str, Any],
    ocr: Optional[BaseOCREngine],
    entry: ConversationListEntry,
    timeout_sec: float,
    background: Optional[bool] = None,
    use_fine_window_lock: bool = False,
    list_image_height: Optional[int] = None,
) -> Optional[str]:
    scan_cfg = cfg.get("conversation_scan") or {}
    use_bg = bool(scan_cfg.get("use_background_list_click", True)) if background is None else background
    _qn_log_list_capture(
        cfg,
        f"列表切会话: entry={entry.name!r} y={entry.y_center:.0f} background={use_bg} "
        f"list_image_h={list_image_height if list_image_height is not None else '-'}",
    )
    if use_bg:
        if use_fine_window_lock:
            with qn_platform_window_lock(cfg, "writer:list_click"):
                click_conversation_list_row_background(
                    hwnd, cfg, entry.y_center, list_image_height=list_image_height
                )
        else:
            click_conversation_list_row_background(
                hwnd, cfg, entry.y_center, list_image_height=list_image_height
            )
    else:
        if use_fine_window_lock:
            with qn_platform_window_lock(cfg, "writer:list_click"):
                click_conversation_list_row(hwnd, cfg, entry.y_center, list_image_height=list_image_height)
        else:
            click_conversation_list_row(hwnd, cfg, entry.y_center, list_image_height=list_image_height)
    settle_ms = int((cfg.get("conversation_scan") or {}).get("switch_settle_ms", 800))
    time.sleep(max(0.05, settle_ms / 1000.0))
    if not ocr:
        return entry.name

    deadline = time.time() + max(0.8, timeout_sec)
    last_seen: Optional[str] = None
    while time.time() < deadline:
        if use_fine_window_lock:
            with qn_platform_window_lock(cfg, "writer:list_switch_header"):
                hb_b, hb_w, hb_h = capture_contact_header_bgra(hwnd, cfg)
            text = (
                peer_name_from_header_bgra(ocr, hb_b, hb_w, hb_h, cfg)
                if ocr
                else None
            )
        else:
            text = read_current_conversation_name(ocr, hwnd, cfg)
        if text:
            last_seen = text
            if header_matches_target(text, entry.name):
                return text
        time.sleep(0.2)
    return last_seen if header_matches_target(last_seen, entry.name) else None


def try_switch_session_via_list_background(
    ocr: Optional[BaseOCREngine],
    hwnd: int,
    cfg: dict[str, Any],
    target_peer: str,
    switch_timeout_sec: float,
    platform_conv_id: str = "",
) -> bool:
    """
    Writer 兜底：OCR 当前可见会话列表，匹配 target 后后台点行切换，再校验标题。
    不抢前台，适用于嵌入聚合等「前台不可用」场景（目标须在可见列表中）。
    """
    if not ocr or not str(target_peer or "").strip():
        return False
    if not cfg.get("conversation_list_region"):
        return False
    target = str(target_peer).strip()
    with qn_platform_window_lock(cfg, "writer:list_cap_for_switch"):
        lb_bgra, lbw, lbh = capture_conversation_list_bgra(hwnd, cfg)
    if not lb_bgra or lbw <= 0 or lbh <= 0:
        _qn_log_list_capture(cfg, f"Writer 列表切换: 列表截图为空，放弃 target={target!r}")
        return False
    entries = parse_visible_conversation_list_from_bgra(ocr, lb_bgra, lbw, lbh, cfg)
    if not entries:
        _qn_log_list_capture(
            cfg,
            f"Writer 列表切换: OCR 无可见行 list={lbw}x{lbh} target={target!r}",
        )
        return False
    chosen: Optional[ConversationListEntry] = None
    for e in entries:
        if header_matches_target(e.name, target) or header_matches_target(target, e.name):
            chosen = e
            break
    if chosen is None:
        _qn_log_list_capture(
            cfg,
            f"Writer 列表切换: 未匹配到目标 target={target!r} 可见昵称数={len(entries)}",
        )
        return False
    print(
        f"[千牛-Session] 列表后台切换尝试: 匹配行={chosen.name!r} y={chosen.y_center:.0f} "
        f"-> 目标={target!r}"
    )
    _qn_log_list_capture(
        cfg,
        f"Writer 列表切换: 将点击行 y≈{chosen.y_center:.0f} list_h={lbh} name={chosen.name!r}",
    )
    switched = switch_to_list_entry(
        hwnd,
        cfg,
        ocr,
        chosen,
        timeout_sec=switch_timeout_sec,
        background=True,
        use_fine_window_lock=True,
        list_image_height=lbh,
    )
    if not switched:
        print("[千牛-Session] 列表后台切换未通过标题校验")
        return False
    ok = verify_current_conversation(ocr, cfg, hwnd, target_peer, platform_conv_id)
    if ok:
        print(f"[千牛-Session] 列表后台切换成功，标题已对齐 {target!r}")
    return ok


def _bbox_center_y(bbox: Any) -> float:
    try:
        ys = [float(pt[1]) for pt in bbox]
    except Exception:
        return 0.0
    return sum(ys) / len(ys) if ys else 0.0


def _bbox_center_x(bbox: Any) -> float:
    try:
        xs = [float(pt[0]) for pt in bbox]
    except Exception:
        return 0.0
    return sum(xs) / len(xs) if xs else 0.0


def _normalize_list_entry_name(text: str) -> str:
    s = str(text).strip()
    s = re.sub(r"^[▶▸▼◆•·\-\s]+", "", s)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"^(商家|买家)\s*", "", s)
    s = re.sub(r"\(\d+\)$", "", s).strip()
    s = re.sub(r"（\d+）$", "", s).strip()
    s = s.strip(" :：-")
    return s.strip()


def _list_entry_is_noise(text: str, extra_excludes: list[str]) -> bool:
    s = _normalize_list_entry_name(text)
    if not s:
        return True
    if s in _LIST_NOISE_EXACT:
        return True
    if re.fullmatch(r"\d{1,2}:\d{2}", s):
        return True
    if re.fullmatch(r"\d{2}-\d{2}", s):
        return True
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}", s):
        return True
    # 时间/状态短词、末条预览单字（易与买家昵称抢分）
    if s in ("昨天", "今天", "前天", "刚刚", "聊天", "标为未读", "已读", "未读"):
        return True
    if re.fullmatch(r"\d", s):
        return True
    if re.match(r"^\d{1,3}秒前$", s) or s.endswith("分钟前") or s.endswith("小时前"):
        return True
    for sub in _LIST_NOISE_SUBSTRINGS:
        if sub in s:
            return True
    return peer_name_is_noise(s, extra_excludes)


def _pick_row_name(
    row_blocks: list[tuple[str, Any, float]],
    row_height_guess: float,
    cfg: dict[str, Any],
) -> tuple[Optional[str], float]:
    if not row_blocks:
        return None, 0.0
    blocks = sorted(row_blocks, key=lambda item: (_bbox_center_y(item[1]), _bbox_center_x(item[1])))
    top_y = min(_bbox_center_y(item[1]) for item in blocks)
    candidate: Optional[str] = None
    best_score = -999.0
    best_conf = 0.0
    extra_excludes = _header_exclude_list(cfg)

    for text, bbox, conf in blocks:
        raw_name = str(text).strip()
        name = _normalize_list_entry_name(raw_name)
        if _list_entry_is_noise(raw_name, extra_excludes):
            continue
        cy = _bbox_center_y(bbox)
        cx = _bbox_center_x(bbox)
        top_bonus = 0.18 if cy <= top_y + max(10.0, row_height_guess * 0.30) else 0.0
        left_bonus = 0.08 if 45.0 <= cx <= 170.0 else 0.0
        # 千牛默认买家名 tb********，避免被右侧时间、预览抢走
        tb_bonus = 0.25 if re.match(r"(?i)^tb[0-9]{5,}$", name) else 0.0
        length_penalty = min(0.15, max(0.0, (len(name) - 12) * 0.01))
        short_penalty = 0.18 if len(name) <= 2 else 0.0
        score = float(conf) + top_bonus + left_bonus + tb_bonus - length_penalty - short_penalty
        if score > best_score:
            candidate = name
            best_score = score
            best_conf = float(conf)

    return candidate, best_conf


def parse_conversation_list_blocks(
    blocks: list[tuple[str, Any, float]],
    cfg: dict[str, Any],
) -> list[ConversationListEntry]:
    if not blocks:
        return []
    scan_cfg = cfg.get("conversation_scan") or {}
    row_height_guess = max(36.0, float(scan_cfg.get("row_height_guess", 56)))
    group_gap = max(16.0, row_height_guess * 0.40)

    rows: list[list[tuple[str, Any, float]]] = []
    current: list[tuple[str, Any, float]] = []
    last_cy: Optional[float] = None
    for block in sorted(blocks, key=lambda item: _bbox_center_y(item[1])):
        cy = _bbox_center_y(block[1])
        if not current or last_cy is None or abs(cy - last_cy) <= group_gap:
            current.append(block)
        else:
            rows.append(current)
            current = [block]
        last_cy = cy
    if current:
        rows.append(current)

    entries: list[ConversationListEntry] = []
    seen_names: set[str] = set()
    for idx, row_blocks in enumerate(rows):
        name, confidence = _pick_row_name(row_blocks, row_height_guess, cfg)
        if not name or name in seen_names:
            continue
        avg_y = sum(_bbox_center_y(item[1]) for item in row_blocks) / len(row_blocks)
        entries.append(
            ConversationListEntry(
                name=name,
                y_center=avg_y,
                row_index=idx,
                confidence=confidence,
            )
        )
        seen_names.add(name)

    page_size_guess = int(scan_cfg.get("page_size_guess", 0))
    if page_size_guess > 0:
        return entries[:page_size_guess]
    return entries


def capture_conversation_list_bgra(
    hwnd: int, cfg: dict[str, Any]
) -> tuple[Optional[bytes], int, int]:
    """
    会话列表位图：优先 list_capture（Clicknium 控件整图），失败或未配置时回退 Win32 裁剪。
    列表 OCR、红点检测、Writer 列表切换共用同一来源逻辑。

    driver=auto 且 chat_capture.auto_clicknium_route_by_unobstructed 为 true（默认）时：
    若列表区在屏幕采样上被其它顶层窗遮挡，则跳过 Clicknium，直接 Win32 PrintWindow 裁剪列表区。
    """
    lc = cfg.get("list_capture") or {}
    driver = str(lc.get("driver", "win32") or "win32").strip().lower()
    locator = str(lc.get("clicknium_list_locator", "") or "").strip()
    route_unob = bool((cfg.get("chat_capture") or {}).get("auto_clicknium_route_by_unobstructed", True))
    skip_clicknium_list = False
    if driver == "auto" and route_unob and locator:
        lx, ly, lw, lh = get_conversation_list_region_px(hwnd, cfg)
        if lw > 0 and lh > 0 and not hwnd_capture_subrect_unobstructed(hwnd, lx, ly, lw, lh):
            skip_clicknium_list = True
            _qn_log_list_capture(
                cfg,
                "列表: auto 判定列表区在屏幕采样点上存在遮挡 "
                "→ 直接使用 PrintWindow 列表区，跳过 Clicknium",
            )

    if driver in ("auto", "clicknium") and locator and not skip_clicknium_list:
        from .qianniu_clicknium import capture_locator_bgra, resolve_clicknium_project_root

        root = resolve_clicknium_project_root(cfg)
        if root is None:
            _qn_log_list_capture(
                cfg,
                f"Clicknium 列表: 未解析工程根 driver={driver!r} locator={locator!r} → 将尝试 Win32",
            )
        elif not (root / ".locator").is_dir():
            _qn_log_list_capture(
                cfg,
                f"Clicknium 列表: 工程根无 .locator root={root!r} → 将尝试 Win32",
            )
        else:
            tmp = Path(__file__).resolve().parents[1] / "_state" / "qianniu_clicknium_list_capture.png"
            bgra, cw, ch = capture_locator_bgra(root, locator, tmp)
            if bgra and cw > 0 and ch > 0:
                _qn_log_list_capture(
                    cfg,
                    f"Clicknium 列表截图 OK size={cw}x{ch} locator={locator!r} tmp={tmp.name}",
                    throttle_ok_sec=45.0,
                )
                return bgra, cw, ch
            _qn_log_list_capture(
                cfg,
                f"Clicknium 列表截图失败或为空 driver={driver!r} locator={locator!r} "
                f"（详见 tmp={tmp}）→ {'结束' if driver == 'clicknium' else '回退 Win32'}",
            )
            if driver == "clicknium":
                return None, 0, 0
    x, y, w, h = get_conversation_list_region_px(hwnd, cfg)
    if w <= 0 or h <= 0:
        _qn_log_list_capture(cfg, f"Win32 列表区无效 hwnd={hwnd} rect=({x},{y},{w},{h})")
        return None, 0, 0
    try:
        bgra, rw, rh, _ = capture_region(hwnd, x, y, w, h)
        if driver == "win32" or not (driver in ("auto", "clicknium") and locator):
            _qn_log_list_capture(
                cfg,
                f"Win32 列表截图 OK size={rw}x{rh} client_rect=({x},{y},{w},{h})",
                throttle_ok_sec=45.0,
            )
        else:
            _qn_log_list_capture(
                cfg,
                f"Win32 列表截图 OK（Clicknium 回退）size={rw}x{rh} rect=({x},{y},{w},{h})",
                throttle_ok_sec=45.0,
            )
        return bgra, rw, rh
    except Exception as ex:
        _qn_log_list_capture(cfg, f"Win32 列表截图异常: {type(ex).__name__}: {ex}")
        return None, 0, 0


def parse_visible_conversation_list_from_bgra(
    ocr: BaseOCREngine,
    bgra: bytes,
    rw: int,
    rh: int,
    cfg: dict[str, Any],
) -> list[ConversationListEntry]:
    try:
        blocks = ocr.recognize(bgra, rw, rh)
    except Exception:
        return []
    return parse_conversation_list_blocks(blocks, cfg)


def parse_visible_conversation_list(
    ocr: BaseOCREngine,
    hwnd: int,
    cfg: dict[str, Any],
) -> list[ConversationListEntry]:
    bgra, rw, rh = capture_conversation_list_bgra(hwnd, cfg)
    if not bgra or rw <= 0 or rh <= 0:
        return []
    return parse_visible_conversation_list_from_bgra(ocr, bgra, rw, rh, cfg)
