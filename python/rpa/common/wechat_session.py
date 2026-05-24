"""
微信会话相关公共能力：

- 标题区 OCR 读取当前联系人
- 左侧会话列表点击切换
- 前台 / 后台搜索切换联系人
- 校验当前是否已在目标会话
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import time
from typing import Any, Optional

from .input_sim import (
    ClipboardGuard,
    get_window_at_point,
    post_clear_text,
    post_click,
    post_key,
    post_type_text,
    set_clipboard_text,
    simulate_click,
    simulate_key,
    simulate_key_combo,
)
from .header_ocr_preprocess import bgra_to_selected_green_white
from .ocr_engine import PaddleOCREngine
from .screenshot import get_window_rect, is_window_minimized, is_window_valid, save_bgra_png
from .wechat_ocr_ops import (
    ocr_bgra_blocks,
    ocr_wechat_conversation_list_blocks,
    ocr_wechat_window_rect_blocks,
)
from .wechat_capture import (
    capture_wechat_chat_bottom_strip,
    capture_wechat_search_box_ocr,
    capture_wechat_window_rect,
    rect_input_region_resolved,
)
from .wechat_uia import (
    UIA_ENV_HINT,
    activate_wechat_main_window,
    collect_visible_session_samples,
    ensure_chats_tab_selected,
    find_session_candidate,
    find_wechat_main_window_uia,
    get_current_chat_name,
    is_wechat_uia_supported,
    click_session_candidate,
)
from .win32_window import (
    bring_to_foreground,
    find_window,
    get_foreground_window,
    get_window_text,
    is_window_visible,
    screen_to_client,
)

VK_CONTROL = 0x11
VK_A = 0x41
VK_V = 0x56
VK_F = 0x46
VK_DELETE = 0x2E
VK_ESCAPE = 0x1B
VK_RETURN = 0x0D
VK_DOWN = 0x28


def _wechat_uia_cfg(cfg: dict[str, Any]) -> dict[str, Any]:
    return cfg.get("uia") or {}


def _wechat_uia_enabled(cfg: dict[str, Any]) -> bool:
    return bool(_wechat_uia_cfg(cfg).get("enabled", False))


@dataclass(frozen=True)
class ConversationListEntry:
    name: str
    y_center: float
    row_index: int
    confidence: float


_LIST_NOISE_EXACT = {
    "搜索",
    "更多",
    "微信",
}

_LIST_NOISE_SUBSTRINGS = (
    "按住说话",
    "搜索指定内容",
    "条新消息",
    "微信号",
)
_TRAILING_TIME_RE = re.compile(
    r"(?:\s*(?:昨天|前天|星期[一二三四五六日天])?\s*\d{1,2}:\d{2})$"
)


def normalize_contact_name(name: str) -> str:
    return " ".join(str(name or "").split()).strip()


_TRAILING_GROUP_COUNT_RE = re.compile(r"[\(（]\d+[\)）]$")


def normalize_chat_title_name(name: str) -> str:
    s = normalize_contact_name(name)
    s = _TRAILING_GROUP_COUNT_RE.sub("", s).strip()
    return s


def build_platform_conversation_id(contact_name: str, prefix: str = "wechat_") -> str:
    return prefix + normalize_contact_name(contact_name)


def extract_contact_name_from_conv_id(platform_conv_id: str, prefix: str = "wechat_") -> str:
    if str(platform_conv_id or "").startswith(prefix):
        return normalize_contact_name(platform_conv_id[len(prefix):])
    return normalize_contact_name(platform_conv_id)


def _bbox_center_xy(bbox: Any) -> tuple[float, float]:
    try:
        xs = [float(pt[0]) for pt in bbox]
        ys = [float(pt[1]) for pt in bbox]
    except (TypeError, ValueError, IndexError):
        return 0.0, 0.0
    if not xs or not ys:
        return 0.0, 0.0
    return sum(xs) / len(xs), sum(ys) / len(ys)


def merge_ocr_blocks_to_line(blocks: list[tuple[Any, Any, float]]) -> str:
    """按阅读顺序拼接同一区域内的 OCR 块（标题可能被切成多框）。"""
    if not blocks:
        return ""
    ordered = sorted(blocks, key=lambda b: (_bbox_center_xy(b[1])[1], _bbox_center_xy(b[1])[0]))
    parts = [str(b[0]).strip() for b in ordered if str(b[0]).strip()]
    return normalize_contact_name("".join(parts))


def reconcile_header_name_with_visible_list(
    header_name: Optional[str],
    entries: list[ConversationListEntry],
) -> Optional[str]:
    """标题区 OCR 易与列表不一致（如 邬/郭）；若与可见列表中唯一一条匹配则采用列表写法。"""
    if not header_name or not entries:
        return header_name
    h = normalize_contact_name(header_name)
    if not h:
        return header_name
    names = [normalize_contact_name(e.name) for e in entries if normalize_contact_name(e.name)]
    if not names:
        return header_name
    if h in names:
        return h
    candidates = [n for n in names if contact_name_matches_target(h, n)]
    if len(candidates) == 1:
        return candidates[0]
    return header_name


def find_wechat_window(cfg: dict) -> Optional[int]:
    hwnd_hex = cfg.get("hwnd_hex")
    if hwnd_hex:
        try:
            hwnd = int(hwnd_hex, 16)
            if is_window_valid(hwnd):
                return hwnd
        except (ValueError, TypeError):
            pass
    wm = cfg.get("window_match") or {}
    if wm.get("title_contains") or wm.get("process_name"):
        return find_window(
            title_contains=wm.get("title_contains", "微信"),
            process_name=wm.get("process_name", ""),
        )
    return find_window(title_contains="微信")


def read_current_contact(
    ocr: PaddleOCREngine,
    hwnd: int,
    header_cfg: dict,
) -> Optional[str]:
    """OCR 标题区，识别当前打开的联系人。"""
    hx = int(header_cfg.get("x", 289))
    hy = int(header_cfg.get("y", 30))
    hw = int(header_cfg.get("w", 300))
    hh = int(header_cfg.get("h", 40))
    try:
        cap = capture_wechat_window_rect(hwnd, hx, hy, hw, hh)
        if not cap:
            return None
        bgra, w, h, _ = cap
        raw_mode = str(header_cfg.get("ocr_preprocess", "selected_green_white")).strip().lower()
        use_green = raw_mode not in ("none", "off", "false", "0", "")
        skip_invert = False
        if use_green:
            bgra, did_style = bgra_to_selected_green_white(
                bgra,
                w,
                h,
                luma_threshold=float(header_cfg.get("header_luma_threshold", 125.0)),
                bg_bgr=(
                    int(header_cfg.get("header_bg_b", 88)),
                    int(header_cfg.get("header_bg_g", 186)),
                    int(header_cfg.get("header_bg_r", 34)),
                ),
                auto_skip_light_header=bool(header_cfg.get("header_auto_skip_light", True)),
                light_header_mean_luma=float(header_cfg.get("light_header_mean_luma", 118.0)),
            )
            if did_style:
                # 已是绿底白字，勿再对整图 invert（否则会反色破坏对比）
                skip_invert = True
        blocks = ocr_bgra_blocks(
            ocr, bgra, w, h, skip_dark_invert=skip_invert,
        )
        if not blocks:
            return None
        merged = merge_ocr_blocks_to_line(blocks)
        if not merged:
            best = max(blocks, key=lambda b: b[2])
            merged = normalize_contact_name(str(best[0]))
        if re.match(r"^\d{1,2}:\d{2}$", merged) or len(merged) > 30 or not merged:
            return None
        return merged
    except Exception:
        return None


def _read_region_text(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    x: int,
    y: int,
    w: int,
    h: int,
) -> str:
    if not ocr or w <= 0 or h <= 0:
        return ""
    try:
        blocks = ocr_wechat_window_rect_blocks(ocr, hwnd, x, y, w, h)
    except Exception:
        return ""
    if not blocks:
        return ""
    texts = [
        str(item[0]).strip()
        for item in sorted(blocks, key=lambda item: (_bbox_center_y(item[1]), _bbox_center_x(item[1])))
        if str(item[0]).strip()
    ]
    return normalize_contact_name(" ".join(texts))


def _read_region_blocks(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    x: int,
    y: int,
    w: int,
    h: int,
) -> list[tuple[str, Any, float]]:
    if not ocr or w <= 0 or h <= 0:
        return []
    try:
        blocks = ocr_wechat_window_rect_blocks(ocr, hwnd, x, y, w, h)
    except Exception:
        return []
    return list(blocks or [])


_SEARCH_RESULT_NOISE_EXACT = {
    "联系人",
    "群聊",
    "聊天记录",
    "搜索网络结果",
}


def _normalize_search_result_text(text: str) -> str:
    s = normalize_contact_name(text)
    s = re.sub(r"^[🔍⌕◦•·\-\s]+", "", s)
    s = re.sub(r"^(联系人|群聊|聊天记录)\s*", "", s)
    s = re.sub(r"^包含[：:]\s*", "", s)
    return s.strip()


def _normalize_search_box_text(text: str) -> str:
    s = normalize_contact_name(text)
    s = re.sub(r"^[^0-9A-Za-z\u4e00-\u9fa5]+", "", s)
    s = re.sub(r"^[A-Za-z]\s+", "", s)
    s = re.sub(r"^搜索\s*", "", s)
    return s.strip()


def _search_box_text_matches_target(search_text: str, target_name: str) -> bool:
    normalized = _normalize_search_box_text(search_text)
    if not normalized:
        return False
    return contact_name_matches_target(normalized, target_name)


def _search_result_click_target(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    search_cfg: dict,
    target_name: str,
) -> tuple[Optional[tuple[int, int]], str]:
    result_rect_x = max(0, int(search_cfg.get("result_x", 55)))
    result_rect_y = max(0, int(search_cfg.get("result_y", 107)))
    result_rect_w = int(search_cfg.get("result_w", 220))
    result_rect_h = int(search_cfg.get("result_h", 46))
    blocks = _read_region_blocks(
        ocr,
        hwnd,
        result_rect_x,
        result_rect_y,
        result_rect_w,
        result_rect_h,
    )
    if not blocks:
        return None, ""

    candidates: list[tuple[float, float, float, str, Any]] = []
    for raw_text, bbox, conf in blocks:
        raw = normalize_contact_name(str(raw_text or ""))
        if not raw or raw in _SEARCH_RESULT_NOISE_EXACT or "搜索网络结果" in raw:
            continue
        normalized = _normalize_search_result_text(raw)
        if not normalized:
            continue

        score = -999.0
        if normalized == target_name:
            score = 100.0
        elif contact_name_matches_target(normalized, target_name):
            score = 80.0 - min(20.0, abs(len(normalized) - len(target_name)) * 2.0)
        else:
            tokens = [tok.strip() for tok in re.split(r"[\s,，:：]+", normalized) if tok.strip()]
            if any(contact_name_matches_target(tok, target_name) for tok in tokens):
                score = 48.0
        if score <= -999.0:
            continue

        if "包含" in raw:
            score -= 22.0
        if "拍了拍" in raw or "来了" in raw:
            score -= 12.0
        if len(normalized) > max(len(target_name) + 8, 20):
            score -= 10.0

        cy = _bbox_center_y(bbox)
        cx = _bbox_center_x(bbox)
        candidates.append((score, cy, cx, raw, bbox))

    if not candidates:
        joined = " | ".join(normalize_contact_name(str(item[0])) for item in blocks[:12] if str(item[0]).strip())
        return None, joined

    candidates.sort(key=lambda item: (-item[0], item[1], item[2]))
    best_score, _cy, _cx, best_text, best_bbox = candidates[0]
    if best_score < 35.0:
        return None, best_text

    click_x = result_rect_x + int(_bbox_center_x(best_bbox))
    click_y = result_rect_y + int(_bbox_center_y(best_bbox))
    return (click_x, click_y), best_text


def _populate_search_box_background(
    hwnd: int,
    win_x: int,
    win_y: int,
    search_x: int,
    search_y: int,
    contact_name: str,
    clear_chars: int,
) -> int:
    screen_x = win_x + search_x
    screen_y = win_y + search_y
    target = get_window_at_point(screen_x, screen_y)
    if target:
        target_hwnd, client_x, client_y = target
        post_click(target_hwnd, client_x, client_y, delay_ms=20)
    else:
        cx, cy = _to_client(hwnd, win_x, win_y, search_x, search_y)
        post_click(hwnd, cx, cy, delay_ms=20)
        target_hwnd = hwnd
    time.sleep(0.15)
    post_clear_text(target_hwnd, max_chars=clear_chars)
    time.sleep(0.05)
    post_type_text(target_hwnd, contact_name, delay_per_char_ms=5)
    time.sleep(0.35)
    return target_hwnd


def _wechat_debug_snapshot_dir(cfg: dict) -> Path:
    dbg = cfg.get("debug") or {}
    raw = dbg.get("screenshot_dir")
    base = Path(raw) if raw else Path(__file__).resolve().parents[1] / "_debug" / "wechat"
    if not base.is_absolute():
        root = Path(__file__).resolve().parents[3]
        base = root / base
    return base


def _get_input_region(cfg: dict, win_h: int = 0) -> tuple[int, int, int, int]:
    return rect_input_region_resolved(cfg, win_h)


def _get_input_click_point(cfg: dict, ix: int, iy: int, iw: int, ih: int) -> tuple[int, int]:
    ib = cfg.get("input_box") or {}
    offset_x = int(ib.get("click_offset_x", 80))
    offset_y = int(ib.get("click_offset_y", ih // 2))
    return ix + offset_x, iy + offset_y


def _ocr_chat_input_text(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    cfg: dict,
) -> str:
    if not ocr:
        return ""
    try:
        _win_x, _win_y, _win_w, win_h = get_window_rect(hwnd)
        ix, iy, iw, ih = _get_input_region(cfg, win_h)
        return _read_region_text(ocr, hwnd, ix, iy, iw, ih)
    except Exception:
        return ""


def _text_contains_target(text: str, target_name: str) -> bool:
    normalized_text = normalize_contact_name(text)
    normalized_target = normalize_contact_name(target_name)
    if not normalized_text or not normalized_target:
        return False
    if normalized_target in normalized_text:
        return True
    for token in re.split(r"[\s,，:：]+", normalized_text):
        token = token.strip()
        if token and contact_name_matches_target(token, normalized_target):
            return True
    return False


def _save_switch_risk_snapshot(
    hwnd: int,
    cfg: dict,
    stage: str,
    target_name: str,
    attempt: int,
    search_text: str,
    input_text: str,
) -> None:
    dbg = cfg.get("debug") or {}
    if not dbg.get("save_screenshots", False):
        return
    out_dir = _wechat_debug_snapshot_dir(cfg)
    out_dir.mkdir(parents=True, exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    safe_target = re.sub(r"[^0-9A-Za-z\u4e00-\u9fa5]+", "_", normalize_contact_name(target_name)) or "target"
    stem = f"risk_{ts}_{safe_target}_a{attempt + 1}_{stage}"

    try:
        cap_sb = capture_wechat_search_box_ocr(hwnd, cfg)
        if cap_sb:
            sb, sw, sh, _ = cap_sb
            save_bgra_png(sb, sw, sh, out_dir / f"{stem}_search_box.png")
    except Exception as ex:
        print(f"[WeChat-Session] 风险快照保存失败(search_box): {ex}")

    try:
        _win_x, _win_y, _win_w, win_h = get_window_rect(hwnd)
        ix, iy, iw, ih = _get_input_region(cfg, win_h)
        cap_ib = capture_wechat_window_rect(hwnd, ix, iy, iw, ih)
        if cap_ib:
            ib, rw, rh, _ = cap_ib
            save_bgra_png(ib, rw, rh, out_dir / f"{stem}_chat_input.png")
    except Exception as ex:
        print(f"[WeChat-Session] 风险快照保存失败(chat_input): {ex}")

    try:
        cap_cb = capture_wechat_chat_bottom_strip(hwnd, cfg)
        if cap_cb:
            cb, cw, ch, _ = cap_cb
            save_bgra_png(cb, cw, ch, out_dir / f"{stem}_chat_bottom.png")
    except Exception as ex:
        print(f"[WeChat-Session] 风险快照保存失败(chat_bottom): {ex}")

    try:
        with open(out_dir / f"{stem}.txt", "w", encoding="utf-8") as f:
            f.write(f"stage={stage}\n")
            f.write(f"target={target_name}\n")
            f.write(f"attempt={attempt + 1}\n")
            f.write(f"search_text={search_text!r}\n")
            f.write(f"input_text={input_text!r}\n")
    except OSError as ex:
        print(f"[WeChat-Session] 风险快照文本保存失败: {ex}")


def _clear_chat_input_if_misrouted(
    hwnd: int,
    cfg: dict,
    ocr: Optional[PaddleOCREngine],
    target_name: str,
) -> tuple[bool, str]:
    try:
        win_x, win_y, _win_w, win_h = get_window_rect(hwnd)
        ix, iy, iw, ih = _get_input_region(cfg, win_h)
        click_x, click_y = _get_input_click_point(cfg, ix, iy, iw, ih)
        screen_x = win_x + click_x
        screen_y = win_y + click_y
        target = get_window_at_point(screen_x, screen_y)
        if target:
            target_hwnd, client_x, client_y = target
            post_click(target_hwnd, client_x, client_y, delay_ms=20)
        else:
            cx, cy = _to_client(hwnd, win_x, win_y, click_x, click_y)
            post_click(hwnd, cx, cy, delay_ms=20)
            target_hwnd = hwnd
        time.sleep(0.12)
        post_clear_text(target_hwnd, max_chars=max(80, len(target_name) * 4 + 20))
        time.sleep(0.18)
        input_text = _ocr_chat_input_text(ocr, hwnd, cfg)
        cleared = not _text_contains_target(input_text, target_name)
        return cleared, input_text
    except Exception:
        return False, ""


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
    s = normalize_contact_name(text)
    s = re.sub(r"^[▶▸▼◆•·\-\s]+", "", s)
    s = re.sub(r"\(\d+\)$", "", s).strip()
    s = re.sub(r"（\d+）$", "", s).strip()
    s = _TRAILING_TIME_RE.sub("", s).strip()
    s = s.strip(" :：-")
    return s.strip()


def _list_entry_is_noise(text: str) -> bool:
    s = _normalize_list_entry_name(text)
    if not s:
        return True
    if s in _LIST_NOISE_EXACT:
        return True
    if re.fullmatch(r"\d{1,2}:\d{2}", s):
        return True
    if re.fullmatch(r"(昨天|前天|星期[一二三四五六日天])", s):
        return True
    if re.fullmatch(r"\d{1,4}", s):
        return True
    for sub in _LIST_NOISE_SUBSTRINGS:
        if sub in s:
            return True
    return False


def _pick_row_name(
    row_blocks: list[tuple[str, Any, float]],
    row_height_guess: float,
) -> tuple[Optional[str], float]:
    if not row_blocks:
        return None, 0.0
    blocks = sorted(row_blocks, key=lambda item: (_bbox_center_y(item[1]), _bbox_center_x(item[1])))
    top_y = min(_bbox_center_y(item[1]) for item in blocks)
    top_band_limit = top_y + max(10.0, row_height_guess * 0.32)

    def _choose(block_candidates: list[tuple[str, Any, float]]) -> tuple[Optional[str], float]:
        candidate: Optional[str] = None
        best_score = -999.0
        best_conf = 0.0

        for text, bbox, conf in block_candidates:
            raw_name = str(text).strip()
            name = _normalize_list_entry_name(raw_name)
            if _list_entry_is_noise(raw_name):
                continue
            cy = _bbox_center_y(bbox)
            cx = _bbox_center_x(bbox)
            top_bonus = 0.24 if cy <= top_y + max(8.0, row_height_guess * 0.20) else 0.0
            left_bonus = 0.14 if 24.0 <= cx <= 150.0 else 0.0
            right_penalty = 0.18 if cx >= 170.0 else 0.0
            lower_row_penalty = min(0.45, max(0.0, (cy - top_y) / max(1.0, row_height_guess)) * 0.9)
            length_penalty = min(0.18, max(0.0, (len(name) - 14) * 0.015))
            short_penalty = 0.12 if len(name) <= 1 else 0.0
            score = (
                float(conf)
                + top_bonus
                + left_bonus
                - right_penalty
                - lower_row_penalty
                - length_penalty
                - short_penalty
            )
            if score > best_score:
                candidate = name
                best_score = score
                best_conf = float(conf)
        return candidate, best_conf

    top_band_blocks = [item for item in blocks if _bbox_center_y(item[1]) <= top_band_limit]
    candidate, conf = _choose(top_band_blocks)
    if candidate:
        return candidate, conf
    return _choose(blocks)


def parse_conversation_list_blocks(
    blocks: list[tuple[str, Any, float]],
    cfg: dict,
) -> list[ConversationListEntry]:
    if not blocks:
        return []
    list_cfg = cfg.get("conversation_list_region") or {}
    row_height_guess = max(40.0, float(list_cfg.get("row_height_guess", 65)))
    group_gap = max(18.0, row_height_guess * 0.42)

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
        name, confidence = _pick_row_name(row_blocks, row_height_guess)
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
    return entries


def parse_visible_conversation_list(
    ocr: PaddleOCREngine,
    hwnd: int,
    cfg: dict,
) -> list[ConversationListEntry]:
    try:
        blocks = ocr_wechat_conversation_list_blocks(ocr, hwnd, cfg)
    except Exception:
        return []
    return parse_conversation_list_blocks(blocks, cfg)


def match_unread_rows_to_entries(
    entries: list[ConversationListEntry],
    unread_rows: list[tuple[int, float]],
    row_height_guess: float,
) -> list[ConversationListEntry]:
    if not entries or not unread_rows:
        return []
    tolerance = max(26.0, row_height_guess * 0.75)
    ordered: list[ConversationListEntry] = []
    for _row_idx, unread_y in unread_rows:
        nearest = min(entries, key=lambda item: abs(item.y_center - unread_y), default=None)
        if nearest and abs(nearest.y_center - unread_y) <= tolerance:
            if all(existing.name != nearest.name for existing in ordered):
                ordered.append(nearest)
    return ordered


def _single_char_ocr_tolerable(current_name: str, target_name: str) -> bool:
    """Allow a tiny OCR wobble like 邬鸿涛 -> 郭鸿涛."""
    if len(current_name) != len(target_name) or len(target_name) < 3:
        return False
    mismatch = sum(1 for a, b in zip(current_name, target_name) if a != b)
    return mismatch <= 1


def contact_name_matches_target(current: Optional[str], target: str) -> bool:
    current_name = normalize_chat_title_name(current or "")
    target_name = normalize_chat_title_name(target)
    if not current_name or not target_name:
        return False
    if current_name == target_name:
        return True
    if target_name in current_name or current_name in target_name:
        return True
    if len(target_name) >= 4 and target_name[:4] in current_name:
        return True
    if _single_char_ocr_tolerable(current_name, target_name):
        return True
    return False


def _read_current_contact_best_effort(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: Optional[dict],
) -> Optional[str]:
    if ocr and header_cfg:
        return read_current_contact(ocr, hwnd, header_cfg)
    try:
        win = find_wechat_main_window_uia()
        if win is not None:
            current = normalize_contact_name(get_current_chat_name(win))
            if current:
                return current
    except Exception:
        pass
    return None


def _uia_ctrl_debug_summary(ctrl: Any) -> str:
    if ctrl is None:
        return "<none>"
    try:
        cn = getattr(ctrl, "ClassName", "") or ""
    except Exception:
        cn = ""
    try:
        name = normalize_contact_name(getattr(ctrl, "Name", "") or "")
    except Exception:
        name = ""
    try:
        aid = normalize_contact_name(getattr(ctrl, "AutomationId", "") or "")
    except Exception:
        aid = ""
    try:
        r = ctrl.BoundingRectangle
        rect = f"({int(r.left)},{int(r.top)},{int(r.right)},{int(r.bottom)})"
    except Exception:
        rect = "<rect unavailable>"
    return f"class={cn!r} name={name!r} aid={aid!r} rect={rect}"


def _log_visible_sessions_for_uia(win: Any, cfg: dict, *, max_items: int = 12) -> None:
    try:
        uia_cfg = _wechat_uia_cfg(cfg)
        max_visits = int(uia_cfg.get("max_visits", 22000))
        max_depth = int(uia_cfg.get("max_depth", 40))
        samples = collect_visible_session_samples(
            win,
            max_items=max_items,
            max_visits=max_visits,
            max_depth=max_depth,
        )
        parts = [
            f"{sample.name}<{sample.class_name}|{sample.automation_id}>"
            for sample in samples
            if normalize_contact_name(sample.name)
        ]
        print(
            "[WeChat-Session] UIA 当前可见会话: "
            + ("、".join(parts) if parts else "<empty>")
        )
    except Exception as e:
        print(f"[WeChat-Session] UIA 打印可见会话失败 err={e}")


def log_wechat_window_state(stage: str, hwnd: int) -> None:
    if not hwnd:
        print(f"[WeChat-Session] {stage} hwnd=0")
        return
    try:
        title = get_window_text(hwnd)
    except Exception:
        title = ""
    print(
        "[WeChat-Session] "
        f"{stage} hwnd=0x{hwnd:X} "
        f"valid={is_window_valid(hwnd)} "
        f"visible={is_window_visible(hwnd)} "
        f"minimized={is_window_minimized(hwnd)} "
        f"title={title!r}"
    )


def click_conversation_row(hwnd: int, list_cfg: dict, y_center: float) -> None:
    """点击左侧会话列表的一行。"""
    lx = int(list_cfg.get("x", 60))
    ly = int(list_cfg.get("y", 0))
    lw = int(list_cfg.get("w", 220))
    win_x, win_y, _, _ = get_window_rect(hwnd)
    cx = win_x + lx + lw // 2
    cy = win_y + ly + int(y_center)
    simulate_click(cx, cy, delay_ms=50)


def click_conversation_row_background(hwnd: int, list_cfg: dict, y_center: float) -> None:
    """后台点击左侧会话列表的一行。"""
    lx = int(list_cfg.get("x", 60))
    ly = int(list_cfg.get("y", 0))
    lw = int(list_cfg.get("w", 220))
    click_x = lx + max(24, min(lw - 24, lw // 2))
    click_y = ly + int(y_center)
    win_x, win_y, _, _ = get_window_rect(hwnd)
    target = get_window_at_point(win_x + click_x, win_y + click_y)
    if target:
        target_hwnd, client_x, client_y = target
        post_click(target_hwnd, client_x, client_y, delay_ms=20)
        return
    cx, cy = _to_client(hwnd, win_x, win_y, click_x, click_y)
    post_click(hwnd, cx, cy, delay_ms=20)


def switch_to_list_entry(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    list_cfg: dict,
    entry: ConversationListEntry,
    settle_sec: float = 0.45,
    max_retry: int = 2,
) -> bool:
    target_name = normalize_contact_name(entry.name)
    if not target_name:
        return False

    for attempt in range(max_retry + 1):
        click_conversation_row(hwnd, list_cfg, entry.y_center)
        time.sleep(settle_sec)
        if not ocr or not header_cfg:
            return True
        current = read_current_contact(ocr, hwnd, header_cfg)
        ok = contact_name_matches_target(current, target_name)
        print(
            f"[WeChat-Session] 列表切换校验 target={target_name!r} "
            f"current={current!r} ok={ok} attempt={attempt + 1}"
        )
        if ok:
            return True
        time.sleep(0.15)
    return False


def switch_to_list_entry_background(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    list_cfg: dict,
    entry: ConversationListEntry,
    settle_sec: float = 0.42,
    max_retry: int = 2,
) -> bool:
    """后台点击左侧列表切换当前可见会话。"""
    target_name = normalize_contact_name(entry.name)
    if not target_name:
        return False

    for attempt in range(max_retry + 1):
        click_conversation_row_background(hwnd, list_cfg, entry.y_center)
        time.sleep(settle_sec)
        if not ocr or not header_cfg:
            return True
        current = read_current_contact(ocr, hwnd, header_cfg)
        ok = contact_name_matches_target(current, target_name)
        print(
            f"[WeChat-Session] 后台列表切换校验 target={target_name!r} "
            f"current={current!r} ok={ok} attempt={attempt + 1}"
        )
        if ok:
            return True
        time.sleep(0.12)
    return False


def switch_to_contact_foreground(hwnd: int, cfg: dict, contact_name: str) -> bool:
    """前台搜索切联系人：Ctrl+F -> 粘贴 -> 点击第一条结果。"""
    if not bring_to_foreground(hwnd):
        return False
    time.sleep(0.2)

    if get_foreground_window() != hwnd:
        return False

    search_cfg = cfg.get("search_box") or {}
    search_x = int(search_cfg.get("x", 145))
    search_y = int(search_cfg.get("y", 48))
    first_result_y = int(search_cfg.get("first_result_y", 130))
    win_x, win_y, _, _ = get_window_rect(hwnd)

    simulate_click(win_x + search_x, win_y + search_y, delay_ms=50)
    time.sleep(0.3)

    simulate_key_combo(VK_CONTROL, VK_A)
    time.sleep(0.05)
    simulate_key(VK_DELETE)
    time.sleep(0.1)

    with ClipboardGuard():
        set_clipboard_text(contact_name)
        time.sleep(0.05)
        simulate_key_combo(VK_CONTROL, VK_V)
        time.sleep(0.8)

    simulate_click(win_x + search_x, win_y + first_result_y, delay_ms=50)
    time.sleep(0.5)
    simulate_key(VK_ESCAPE)
    time.sleep(0.3)
    return True


def switch_to_contact_uia(
    hwnd: int,
    cfg: dict,
    contact_name: str,
    *,
    exact: bool = False,
    ocr: Optional[PaddleOCREngine] = None,
    header_cfg: Optional[dict] = None,
    max_retry: int = 2,
) -> bool:
    """前台 UIA 列表点选会话。"""
    if not _wechat_uia_enabled(cfg):
        return False
    if not is_wechat_uia_supported():
        print(f"[WeChat-Session] UIA 不可用：未安装依赖或平台不支持。{UIA_ENV_HINT}")
        return False
    uia_cfg = _wechat_uia_cfg(cfg)
    exact = exact or str(uia_cfg.get("session_match_mode", "contains")).strip().lower() == "exact"
    settle_sec = float(uia_cfg.get("session_settle_sec", 0.35))
    max_visits = int(uia_cfg.get("max_visits", 22000))
    max_depth = int(uia_cfg.get("max_depth", 40))
    session_ratio = float(uia_cfg.get("session_list_max_x_ratio", 0.42))
    session_class = uia_cfg.get("session_class")
    win = find_wechat_main_window_uia()
    if win is None:
        print(f"[WeChat-Session] UIA 未找到微信主窗口。{UIA_ENV_HINT}")
        return False

    activate_wechat_main_window(win)
    ensure_chats_tab_selected(win)
    _log_visible_sessions_for_uia(win, cfg)

    target_name = normalize_contact_name(contact_name)
    for attempt in range(max_retry + 1):
        row = find_session_candidate(
            win,
            target_name,
            exact=exact,
            session_class=session_class if session_class else None,
            session_list_max_x_ratio=session_ratio,
            max_visits=max_visits,
            max_depth=max_depth,
        )
        if row is None:
            print(
                f"[WeChat-Session] UIA 未找到目标会话 target={target_name!r} "
                f"attempt={attempt + 1}{UIA_ENV_HINT}"
            )
            return False
        print(
            f"[WeChat-Session] UIA 命中候选 target={target_name!r} "
            f"attempt={attempt + 1} {_uia_ctrl_debug_summary(row)}"
        )
        try:
            click_session_candidate(row)
        except Exception as e:
            print(
                f"[WeChat-Session] UIA 点击会话失败 target={target_name!r} "
                f"attempt={attempt + 1} err={e}"
            )
            continue
        print(
            f"[WeChat-Session] UIA 已执行点击 target={target_name!r} "
            f"attempt={attempt + 1} {_uia_ctrl_debug_summary(row)}"
        )
        time.sleep(settle_sec)
        current = _read_current_contact_best_effort(ocr, hwnd, header_cfg)
        if not current:
            print(
                f"[WeChat-Session] UIA 点击后无法确认当前会话 target={target_name!r} "
                f"attempt={attempt + 1}"
            )
            time.sleep(0.12)
            continue
        ok = contact_name_matches_target(current, target_name)
        print(
            f"[WeChat-Session] UIA 列表切换校验 target={target_name!r} "
            f"current={current!r} ok={ok} attempt={attempt + 1}"
        )
        if ok:
            return True
        time.sleep(0.12)
    return False


def ensure_in_target_chat_foreground(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
    max_retry: int = 2,
) -> bool:
    """确保前台已切换到目标联系人。"""
    for _ in range(max_retry + 1):
        current = _read_current_contact_best_effort(ocr, hwnd, header_cfg)
        if current == target_name:
            return True
        if contact_name_matches_target(current, target_name):
            return True
        if not switch_to_contact_foreground(hwnd, cfg, target_name):
            continue
        time.sleep(0.5)
    return False


def switch_to_contact_with_strategy(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
    *,
    exact: bool = False,
    strategies: Optional[list[str]] = None,
    max_retry: int = 2,
) -> bool:
    """按配置顺序尝试 UIA / 前台搜索 / 后台搜索切换会话。"""
    if ocr and header_cfg:
        current = read_current_contact(ocr, hwnd, header_cfg)
        if contact_name_matches_target(current, target_name):
            return True

    uia_cfg = _wechat_uia_cfg(cfg)
    if strategies is None:
        if _wechat_uia_enabled(cfg):
            strategies = [
                str(s).strip().lower()
                for s in uia_cfg.get("session_switch_strategy", ["uia", "foreground", "background"])
                if str(s).strip()
            ]
        else:
            strategies = ["foreground", "background"]

    for strategy in strategies:
        ok = False
        if strategy == "uia":
            ok = switch_to_contact_uia(
                hwnd,
                cfg,
                target_name,
                exact=exact,
                ocr=ocr,
                header_cfg=header_cfg,
                max_retry=max_retry,
            )
        elif strategy == "foreground":
            ok = ensure_in_target_chat_foreground(
                ocr, hwnd, header_cfg, cfg, target_name, max_retry=max_retry
            )
        elif strategy == "background":
            ok = ensure_in_target_chat_background(
                ocr, hwnd, header_cfg, cfg, target_name
            )
        if ok:
            print(
                f"[WeChat-Session] 切会话策略命中 strategy={strategy!r} "
                f"target={target_name!r}"
            )
            return True
    return False


def _to_client(hwnd: int, win_x: int, win_y: int, rel_x: int, rel_y: int) -> tuple[int, int]:
    return screen_to_client(hwnd, win_x + rel_x, win_y + rel_y)


def switch_to_contact_background(
    hwnd: int,
    cfg: dict,
    contact_name: str,
    ocr: Optional[PaddleOCREngine] = None,
    header_cfg: Optional[dict] = None,
    max_retry: int = 2,
) -> bool:
    """后台搜索切联系人：优先回车直达，失败后尝试方向键导航，再兜底搜索结果区域点击。"""
    log_wechat_window_state("后台切会话前", hwnd)
    search_cfg = cfg.get("search_box") or {}
    search_x = int(search_cfg.get("x", 145))
    search_y = int(search_cfg.get("y", 48))
    search_w = int(search_cfg.get("w", 160))
    search_h = int(search_cfg.get("h", 30))
    win_x, win_y, _, _ = get_window_rect(hwnd)

    clear_chars = max(50, min(200, len(contact_name) * 3 + 12))
    search_rect_x = max(0, int(search_cfg.get("ocr_x", search_x - search_w // 2)))
    search_rect_y = max(0, int(search_cfg.get("ocr_y", search_y - search_h // 2)))
    search_rect_w = int(search_cfg.get("ocr_w", search_w))
    search_rect_h = int(search_cfg.get("ocr_h", search_h))
    use_enter_first = bool(search_cfg.get("use_enter_first", True))
    keyboard_nav_max_steps = max(0, int(search_cfg.get("keyboard_nav_max_steps", 8)))
    use_result_region_ocr = bool(search_cfg.get("use_result_region_ocr", True))

    for attempt in range(max_retry + 1):
        input_hwnd = _populate_search_box_background(
            hwnd, win_x, win_y, search_x, search_y, contact_name, clear_chars
        )

        search_text = _read_region_text(
            ocr,
            hwnd,
            search_rect_x,
            search_rect_y,
            search_rect_w,
            search_rect_h,
        )
        input_text = _ocr_chat_input_text(ocr, hwnd, cfg)
        search_ok = _search_box_text_matches_target(search_text, contact_name) if ocr else True
        chat_input_safe = (not _text_contains_target(input_text, contact_name)) if ocr else True
        print(
            f"[WeChat-Session] 后台搜索框校验 target={contact_name!r} "
            f"search_text={search_text!r} ok={search_ok} "
            f"chat_input_safe={chat_input_safe} input_text={input_text!r} "
            f"attempt={attempt + 1}"
        )
        if ocr and (not search_ok or not chat_input_safe):
            stage = "misrouted_input" if not chat_input_safe else "search_focus_invalid"
            _save_switch_risk_snapshot(hwnd, cfg, stage, contact_name, attempt, search_text, input_text)
            if not chat_input_safe:
                print(
                    f"[WeChat-Session] 检测到目标串进入聊天输入框，开始清理并安全中止 "
                    f"target={contact_name!r} input_text={input_text!r} attempt={attempt + 1}"
                )
                cleared, cleared_text = _clear_chat_input_if_misrouted(hwnd, cfg, ocr, contact_name)
                print(
                    f"[WeChat-Session] 聊天输入框清理结果 target={contact_name!r} "
                    f"cleared={cleared} input_text_after={cleared_text!r}"
                )
                _save_switch_risk_snapshot(
                    hwnd,
                    cfg,
                    "misrouted_input_cleared" if cleared else "misrouted_input_uncleared",
                    contact_name,
                    attempt,
                    search_text,
                    cleared_text,
                )
                return False

            print(
                f"[WeChat-Session] 搜索框真实性校验失败，安全中止本次切换 "
                f"target={contact_name!r} search_text={search_text!r} attempt={attempt + 1}"
            )
            return False

        if use_enter_first:
            post_key(input_hwnd, VK_RETURN)
            time.sleep(0.45)
            current = read_current_contact(ocr, hwnd, header_cfg) if ocr and header_cfg else None
            enter_ok = contact_name_matches_target(current, contact_name) if current else False
            print(
                f"[WeChat-Session] 后台回车切换校验 target={contact_name!r} "
                f"current={current!r} ok={enter_ok} attempt={attempt + 1}"
            )
            if enter_ok:
                log_wechat_window_state("后台回车切会话后", hwnd)
                return True
            print(
                f"[WeChat-Session] 后台回车未命中，准备进入方向键导航 "
                f"target={contact_name!r} attempt={attempt + 1}"
            )
            if current:
                print(
                    f"[WeChat-Session] 后台回车切换未确认，继续尝试方向键导航/结果区 "
                    f"target={contact_name!r} current={current!r}"
                )

        for down_steps in range(1, keyboard_nav_max_steps + 1):
            print(
                f"[WeChat-Session] 后台方向键导航尝试 "
                f"target={contact_name!r} down_steps={down_steps} "
                f"attempt={attempt + 1}"
            )
            input_hwnd = _populate_search_box_background(
                hwnd, win_x, win_y, search_x, search_y, contact_name, clear_chars
            )
            nav_search_text = _read_region_text(
                ocr,
                hwnd,
                search_rect_x,
                search_rect_y,
                search_rect_w,
                search_rect_h,
            )
            nav_input_text = _ocr_chat_input_text(ocr, hwnd, cfg)
            nav_search_ok = _search_box_text_matches_target(nav_search_text, contact_name) if ocr else True
            nav_chat_input_safe = (not _text_contains_target(nav_input_text, contact_name)) if ocr else True
            print(
                f"[WeChat-Session] 后台方向键前校验 target={contact_name!r} "
                f"search_text={nav_search_text!r} ok={nav_search_ok} "
                f"chat_input_safe={nav_chat_input_safe} input_text={nav_input_text!r} "
                f"down_steps={down_steps} attempt={attempt + 1}"
            )
            if ocr and (not nav_search_ok or not nav_chat_input_safe):
                stage = "misrouted_input_nav" if not nav_chat_input_safe else "search_focus_invalid_nav"
                _save_switch_risk_snapshot(
                    hwnd, cfg, stage, contact_name, attempt, nav_search_text, nav_input_text
                )
                if not nav_chat_input_safe:
                    cleared, cleared_text = _clear_chat_input_if_misrouted(hwnd, cfg, ocr, contact_name)
                    print(
                        f"[WeChat-Session] 方向键前误写清理结果 target={contact_name!r} "
                        f"cleared={cleared} input_text_after={cleared_text!r}"
                    )
                    _save_switch_risk_snapshot(
                        hwnd,
                        cfg,
                        "misrouted_input_nav_cleared" if cleared else "misrouted_input_nav_uncleared",
                        contact_name,
                        attempt,
                        nav_search_text,
                        cleared_text,
                    )
                print(
                    f"[WeChat-Session] 方向键前安全校验失败，终止本次切换 "
                    f"target={contact_name!r} down_steps={down_steps} attempt={attempt + 1}"
                )
                return False
            for _ in range(down_steps):
                post_key(input_hwnd, VK_DOWN)
                time.sleep(0.08)
            post_key(input_hwnd, VK_RETURN)
            time.sleep(0.45)
            current = read_current_contact(ocr, hwnd, header_cfg) if ocr and header_cfg else None
            ok = contact_name_matches_target(current, contact_name) if current else False
            print(
                f"[WeChat-Session] 后台方向键切换校验 target={contact_name!r} "
                f"current={current!r} ok={ok} down_steps={down_steps} attempt={attempt + 1}"
            )
            if ok:
                print(
                    f"[WeChat-Session] 后台方向键导航命中目标 "
                    f"target={contact_name!r} down_steps={down_steps} "
                    f"attempt={attempt + 1}"
                )
                log_wechat_window_state("后台方向键切会话后", hwnd)
                return True

        if use_result_region_ocr:
            click_target, result_text = _search_result_click_target(ocr, hwnd, search_cfg, contact_name)
            result_ok = bool(click_target)
            print(
                f"[WeChat-Session] 后台搜索结果定位 target={contact_name!r} "
                f"matched_text={result_text!r} ok={result_ok} attempt={attempt + 1}"
            )
            if click_target:
                click_rel_x = click_target[0]
                click_rel_y = click_target[1]
                rcx, rcy = _to_client(hwnd, win_x, win_y, click_rel_x, click_rel_y)
                post_click(hwnd, rcx, rcy, delay_ms=20)
                time.sleep(0.35)
                post_key(input_hwnd, VK_ESCAPE)
                time.sleep(0.1)
                log_wechat_window_state("后台结果区切会话后", hwnd)
                return True

        post_key(input_hwnd, VK_ESCAPE)
        time.sleep(0.15)

    print(f"[WeChat-Session] 后台搜索切换失败：未能切换到目标会话 target={contact_name!r}")
    log_wechat_window_state("后台切会话失败", hwnd)
    return False


def ensure_in_target_chat_background(
    ocr: Optional[PaddleOCREngine],
    hwnd: int,
    header_cfg: dict,
    cfg: dict,
    target_name: str,
) -> bool:
    """确保后台已切换到目标联系人。"""
    log_wechat_window_state(f"后台校验前 target={target_name}", hwnd)
    current = _read_current_contact_best_effort(ocr, hwnd, header_cfg)
    if contact_name_matches_target(current, target_name):
        print(f"[WeChat-Session] 后台已在目标会话: {target_name}")
        return True

    if not switch_to_contact_background(hwnd, cfg, target_name, ocr=ocr, header_cfg=header_cfg):
        print(f"[WeChat-Session] 后台切会话执行失败 target={target_name!r}")
        return False
    time.sleep(0.5)

    current = _read_current_contact_best_effort(ocr, hwnd, header_cfg)
    ok = contact_name_matches_target(current, target_name)
    print(
        f"[WeChat-Session] 后台切会话校验 target={target_name!r} "
        f"current={current!r} ok={ok}"
    )
    log_wechat_window_state(f"后台校验后 target={target_name}", hwnd)
    return ok
