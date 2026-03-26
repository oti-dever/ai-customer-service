"""
千牛 PC reader: 接待中心截图 → PaddleOCR → 布局分析 → rpa_inbox_messages。

相对早期 MVP：
  - 标题区 OCR 当前会话对象名称 + NameStabilizer，动态 platform_conversation_id / customer_name
  - 与微信对齐的增量去重 + 模糊相似度（OCR 抖动）
  - 轻量噪声过滤（时间戳等）
"""
from __future__ import annotations

import json
import re
import time
from pathlib import Path
from typing import Any, Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import DEFAULT_DB_PATH, open_db, write_inbox_batch
from ..common.incremental import IncrementalDetector, content_hash, make_platform_msg_id
from ..common.layout_parser import parse_chat_layout
from ..common.ocr_engine import PaddleOCREngine
from ..common.qianniu_coords import coordinate_space, rect_from_absolute_fields, rect_ratios_to_bitmap_xywh
from ..common.qianniu_header import pick_peer_name_from_ocr_blocks
from ..common.qianniu_window import find_qianniu_hwnd
from ..common.screenshot import capture_region, is_window_minimized, save_bgra_png

CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"
DEFAULT_CONFIG = CONFIG_DIR / "qianniu_config.json"
STATE_DIR = Path(__file__).resolve().parents[1] / "_state"
PLATFORM = "qianniu"


class NameStabilizer:
    """连续 N 次 OCR 一致才接受改名，减少会话抖动。"""

    def __init__(self, required_consistent: int = 2):
        self.current: Optional[str] = None
        self._required = required_consistent
        self._candidate: Optional[str] = None
        self._candidate_count: int = 0

    def update(self, name: Optional[str]) -> Optional[str]:
        if not name:
            return self.current
        if self.current is None:
            self.current = name
            return self.current
        if name == self.current:
            self._candidate = None
            self._candidate_count = 0
            return self.current
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


_QN_NOISE = re.compile(
    r"^\d{1,2}:\d{2}$|"
    r"^\d{4}[年/\-]\d{1,2}|"
    r"^已读$|^未读$|"
    r"搜索人、群、聊天记录"
)

_QN_TS = (
    r"\d{4}\s*[-/年]\s*\d{1,2}\s*[-/月]\s*\d{1,2}\s*"
    r"\d{1,2}\s*[:：]\s*\d{2}(?:\s*[:：]\s*\d{2})?"
)
_QN_NAME = r"[\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{1,24}"
_QN_SPEAKER = rf"{_QN_NAME}\s*[:：]\s*{_QN_NAME}"
_QN_SPEAKER_TS_SPLIT = re.compile(
    rf"(?:{_QN_SPEAKER}\s*{_QN_TS})|(?:{_QN_TS}\s*{_QN_SPEAKER})"
)


def _is_valid_inbox_content(content: str) -> bool:
    s = content.strip()
    if len(s) < 2:
        return False
    if _QN_NOISE.search(s):
        return False
    if re.match(r"^[\d\s\.\,\:\-\+\/\(\)]+$", s):
        return False
    # 富文本/卡片被 OCR 成 html 前缀的噪声
    if re.match(r"^html\s*,", s, re.I):
        return False
    if "这类消息" in s and "手机" in s:
        return False
    return True


def _normalize_inbox_content(content: str) -> str:
    """
    清洗 OCR 结果中的“昵称前缀”噪声，降低消息被拆成碎片后的脏数据入库概率。
    """
    s = content.strip()
    if not s:
        return s

    # 纯昵称行（如：利晨运动专营店: 小彬）直接丢弃
    # 仅在“左侧像店铺名 + 右侧像昵称”时触发，避免误删“售后无忧：”这类正文前缀。
    if re.match(
        r"^[\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{2,24}(店|专营店|旗舰店|官方店|客服)\s*[:：]\s*"
        r"[\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{1,10}$",
        s,
    ):
        return ""

    # 店铺昵称头 + 冒号 + 正文，去掉“店铺:昵称”前缀。
    # 注意：不再对通用“任意前缀:正文”做剥离，防止误删“售后无忧：”“限时赠送：”等正文标签。
    m = re.match(
        r"^([\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{2,24}(店|专营店|旗舰店|官方店|客服))\s*[:：]\s*"
        r"([\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{1,10})\s+(.+)$",
        s,
    )
    if m:
        return m.group(4).strip()
    return s


def _split_combined_inbox_content(content: str) -> list[str]:
    """
    千牛 OCR 可能把同侧多条气泡并成一段：
    "... 店铺:昵称2026-3-11 13:23:13 第一条 店铺:昵称2026-3-11 13:26:11 第二条 ..."
    这里按“发言人+时间戳”锚点拆分，尽量还原多条消息。
    """
    s = content.strip()
    if not s:
        return []

    matches = list(_QN_SPEAKER_TS_SPLIT.finditer(s))
    if not matches:
        return [s]

    parts: list[str] = []

    # 首个锚点前的前缀（常见为上一条长消息尾部）保留为独立一条
    prefix = s[: matches[0].start()].strip()
    if prefix:
        parts.append(prefix)

    for i, m in enumerate(matches):
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(s)
        body = s[start:end].strip()
        if body:
            parts.append(body)
    return parts


def load_config(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _space_for_region(cfg: dict, region: dict) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)


def get_message_region_px(hwnd: int, cfg: dict) -> tuple[int, int, int, int]:
    region = cfg.get("chat_region") or {}
    abs_r = rect_from_absolute_fields(region)
    if abs_r:
        return abs_r
    space = _space_for_region(cfg, region)
    return rect_ratios_to_bitmap_xywh(
        hwnd,
        float(region.get("left_ratio", 0.19)),
        float(region.get("top_ratio", 0.14)),
        float(region.get("right_ratio", 0.795)),
        float(region.get("bottom_ratio", 0.74)),
        space,
    )


def get_header_region_px(hwnd: int, cfg: dict) -> tuple[int, int, int, int]:
    """中间栏店名标题（客户区比例，避开顶部 KPI 条）。"""
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


def _debug_snapshot_dir(cfg: dict) -> Path:
    dbg = cfg.get("debug") or {}
    raw = dbg.get("screenshot_dir")
    base = Path(raw) if raw else Path(__file__).resolve().parents[1] / "_debug" / "qianniu"
    if not base.is_absolute():
        root = Path(__file__).resolve().parents[3]
        base = root / base
    return base


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


def _save_qianniu_debug_frames(
    hwnd: int,
    cfg: dict,
    scan_idx: int,
    chat_bgra: bytes,
    cw: int,
    ch: int,
) -> None:
    """按配置保存聊天区 / 标题区裁剪图，便于核对比例是否对准。"""
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
        save_bgra_png(chat_bgra, cw, ch, out_dir / f"{stem}_chat.png")
        hx, hy, hw, hh = get_header_region_px(hwnd, cfg)
        if hw > 0 and hh > 0:
            hb, hw2, hh2, _ = capture_region(hwnd, hx, hy, hw, hh)
            save_bgra_png(hb, hw2, hh2, out_dir / f"{stem}_header.png")
        max_keep = int(dbg.get("max_png_files", 300))
        _prune_debug_pngs(out_dir, max_keep)
        print(f"[千牛-Reader] 调试截图已保存 {out_dir / stem}_chat.png / _header.png")
    except Exception as ex:
        print(f"[千牛-Reader] 调试截图保存失败: {ex}")


def _header_exclude_list(cfg: dict) -> list[str]:
    conv = cfg.get("conversation") or {}
    raw = conv.get("peer_name_exclude_substrings")
    if isinstance(raw, list):
        return [str(x) for x in raw if x]
    return []


def _extract_peer_name(
    ocr: PaddleOCREngine, hwnd: int, cfg: dict,
) -> Optional[str]:
    hx, hy, hw, hh = get_header_region_px(hwnd, cfg)
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


def run_reader(
    config_path: Path = DEFAULT_CONFIG,
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 3,
    debug_dir: Optional[Path] = None,
) -> None:
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    cfg = load_config(config_path)
    conv = cfg.get("conversation") or {}
    layout_cfg = cfg.get("layout") or {}
    ocr_cfg = cfg.get("ocr") or {}

    default_customer_name = str(conv.get("default_customer_name", "未知会话"))
    conv_id_prefix = str(conv.get("conv_id_prefix", "qianniu_"))
    fallback_conv_id = str(
        conv.get("fallback_platform_conversation_id", f"{conv_id_prefix}default")
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

    state_path = STATE_DIR / "qianniu_last_messages.json"
    STATE_DIR.mkdir(parents=True, exist_ok=True)

    rpa_phase(
        "qianniu.reader",
        "ocr_init_start",
        "正在创建 PaddleOCR 引擎（首次会下载/加载模型，可能持续数分钟；此期间控制台可能只有第三方库日志）",
    )
    ocr = PaddleOCREngine(
        lang="ch",
        min_confidence=min_confidence,
        max_side=max_side,
        invert_for_dark_mode=invert_for_dark_mode,
        det_thresh=det_thresh,
        det_box_thresh=det_box_thresh,
    )
    rpa_phase("qianniu.reader", "ocr_init_done", "OCR 引擎已就绪，即将进入截图轮询")
    detector = IncrementalDetector(
        max_window=100,
        state_path=state_path,
        fuzzy_threshold=fuzzy_threshold,
    )
    stabilizer = NameStabilizer(required_consistent=2)

    db = db_path or DEFAULT_DB_PATH
    dbg = cfg.get("debug") or {}
    if dbg.get("save_screenshots"):
        print(
            f"[千牛-Reader] 调试截图已开启 → {_debug_snapshot_dir(cfg)} "
            f"(每 {max(1, int(dbg.get('every_n_scans', 1)))} 次扫描保存一对 png)"
        )
    print(
        f"[千牛-Reader] DB={db} interval={poll_interval}s "
        f"fuzzy={fuzzy_threshold} coords={coordinate_space(cfg)}"
    )

    _scan_count = 0
    _last_hb = time.time()
    HB_SEC = 30.0
    _last_wait_hwnd_log = 0.0
    rpa_phase("qianniu.reader", "poll_loop_enter", "已进入 while True；若未找到窗口会周期性 heartbeat")

    while True:
        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd:
            now = time.time()
            if now - _last_wait_hwnd_log >= HB_SEC:
                rpa_heartbeat(
                    "qianniu.reader",
                    "等待千牛「接待中心」窗口（未找到句柄）；请打开并保持可见",
                )
                _last_wait_hwnd_log = now
            print("[千牛-Reader] 未找到千牛接待中心窗口，等待...")
            time.sleep(poll_interval)
            continue
        if is_window_minimized(hwnd):
            print("[千牛-Reader] 千牛窗口已最小化，请保持窗口可见")
            time.sleep(poll_interval)
            continue

        try:
            _scan_count += 1
            now = time.time()
            verbose = _scan_count <= 3 or (now - _last_hb >= HB_SEC)

            raw_name = _extract_peer_name(ocr, hwnd, cfg)
            stabilizer.update(raw_name)
            customer_name = stabilizer.current or default_customer_name
            platform_conv_id = (
                f"{conv_id_prefix}{customer_name}"
                if stabilizer.current
                else fallback_conv_id
            )

            if verbose:
                print(
                    f"[千牛-Reader] 扫描#{_scan_count} "
                    f"会话={customer_name!r} conv_id={platform_conv_id!r}"
                )
                _last_hb = now

            x, y, w, h = get_message_region_px(hwnd, cfg)
            if w <= 0 or h <= 0:
                time.sleep(poll_interval)
                continue

            bgra, _, _, _ = capture_region(hwnd, x, y, w, h)
            _save_qianniu_debug_frames(hwnd, cfg, _scan_count, bgra, w, h)
            blocks = ocr.recognize(bgra, w, h)
            messages = parse_chat_layout(
                blocks,
                region_width=float(w),
                left_threshold=left_threshold,
                right_threshold=right_threshold,
                merge_y_gap=merge_y_gap,
            )
            new_messages = detector.filter_new(messages, conv_id=platform_conv_id)

            if not new_messages:
                time.sleep(poll_interval)
                continue

            items = []
            for msg in new_messages:
                if msg.side != "in":
                    continue
                for chunk in _split_combined_inbox_content(msg.content):
                    content = _normalize_inbox_content(chunk)
                    if not _is_valid_inbox_content(content):
                        continue
                    ch = content_hash(msg.side, content)
                    pid = make_platform_msg_id(PLATFORM, platform_conv_id, ch)
                    items.append((content, pid))

            if items:
                conn = open_db(db)
                try:
                    n = write_inbox_batch(
                        conn,
                        PLATFORM,
                        platform_conv_id,
                        customer_name,
                        items,
                    )
                    print(f"[千牛-Reader] 写入 {n} 条 inbox ({customer_name})")
                finally:
                    conn.close()

        except Exception as e:
            print(f"[千牛-Reader] 失败: {e}")

        time.sleep(poll_interval)


if __name__ == "__main__":
    run_reader()
