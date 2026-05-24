"""
千牛 PC reader: 接待中心截图 → PaddleOCR → 布局分析 → rpa_inbox_messages。

当前版本在单会话链路基础上新增：
  - 会话列表 OCR 解析
  - 当前可见页串行轮询读取
  - 可选未读优先扫描
  - 与聚合界面「清空聊天记录 / 删除会话」同步：消费 Qt 写入的增量 purge 文件（见 _drain_qianniu_incremental_purge_file）
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Any, Optional

from ..common.rpa_console_log import rpa_heartbeat, rpa_phase
from ..common.db_helper import (
    resolved_default_db_path,
    count_pending_send,
    open_db,
    write_inbox_message,
)
from ..common.incremental import IncrementalDetector, content_hash, make_platform_msg_id
from ..common.layout_parser import ParsedMessage, parse_chat_layout
from ..common.ocr_engine import BaseOCREngine, PaddleOCREngine, build_ocr_engine
from ..common.qianniu_chat_parser import (
    parse_qianniu_chat_blocks,
    to_parsed_messages,
)
from ..common.qianniu_coords import coordinate_space, rect_from_absolute_fields, rect_ratios_to_bitmap_xywh
from ..common.qianniu_session import (
    ConversationListEntry,
    capture_contact_header_bgra,
    capture_conversation_list_bgra,
    get_conversation_list_region_px,
    header_matches_target,
    parse_visible_conversation_list,
    parse_visible_conversation_list_from_bgra,
    peer_name_from_header_bgra,
    switch_to_conversation_search,
    switch_to_list_entry,
)
from ..common.qianniu_window import find_qianniu_hwnd
from ..common.screenshot import (
    capture_region,
    hwnd_capture_subrect_unobstructed,
    is_window_minimized,
    save_bgra_png,
)
from ..common.unread_detector import detect_unread_rows, detect_unread_rows_dual_band
from ..common.window_lock import hold_platform_window_lock

CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"
DEFAULT_CONFIG = CONFIG_DIR / "qianniu_config.json"
STATE_DIR = Path(__file__).resolve().parents[1] / "_state"
PLATFORM = "qianniu"
_LAST_QN_PARSER_DIAG_SIGNATURE = ""
_LAST_QN_PARSER_DIAG_REPEAT = 0
_LAST_QN_CLICKNIUM_CAPTURE_DIAG_SIGNATURE = ""
_LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT = 0


def _reader_segment_timing_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    if "reader_segment_timing" in dbg:
        return bool(dbg["reader_segment_timing"])
    return not bool(cfg.get("production_mode", True))


def _reader_event_log_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    if "reader_event_log" in dbg:
        return bool(dbg["reader_event_log"])
    return not bool(cfg.get("production_mode", True))


def _chat_parser_failure_log_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    if "chat_parser_failure_log" in dbg:
        return bool(dbg["chat_parser_failure_log"])
    return _reader_event_log_enabled(cfg)


def _clicknium_capture_log_enabled(cfg: dict[str, Any]) -> bool:
    dbg = cfg.get("debug") or {}
    if "clicknium_capture_log" in dbg:
        return bool(dbg["clicknium_capture_log"])
    return _reader_event_log_enabled(cfg)


def _log_qianniu_clicknium_capture_diag(cfg: dict[str, Any], message: str) -> None:
    global _LAST_QN_CLICKNIUM_CAPTURE_DIAG_SIGNATURE
    global _LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT

    if not _clicknium_capture_log_enabled(cfg):
        return
    signature = hashlib.md5(message.encode("utf-8", errors="ignore")).hexdigest()
    if signature == _LAST_QN_CLICKNIUM_CAPTURE_DIAG_SIGNATURE:
        _LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT += 1
        if _LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT % 5 != 0:
            return
        print(
            "[千牛-Reader] Clicknium 聊天截图重复失败 "
            f"count={_LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT} {message}"
        )
        return

    _LAST_QN_CLICKNIUM_CAPTURE_DIAG_SIGNATURE = signature
    _LAST_QN_CLICKNIUM_CAPTURE_DIAG_REPEAT = 1
    print(f"[千牛-Reader] Clicknium 聊天截图: {message}")


def _log_qianniu_chat_parser_failure(
    cfg: dict[str, Any],
    customer_name: str,
    platform_conv_id: str,
    seq_result,
) -> None:
    global _LAST_QN_PARSER_DIAG_SIGNATURE
    global _LAST_QN_PARSER_DIAG_REPEAT

    if not _chat_parser_failure_log_enabled(cfg):
        return

    normalized_lines = list(seq_result.normalized_lines or [])
    line_preview = " | ".join(repr(x) for x in normalized_lines[:12])
    if len(normalized_lines) > 12:
        line_preview += f" | ...( +{len(normalized_lines) - 12} )"
    msg_preview = " ; ".join(
        f"{m.side}:{(m.sender_name or '-')}@{(m.original_timestamp or '-')}:"
        f"{(m.content or '').replace(chr(10), ' / ')[:40]!r}"
        for m in (seq_result.messages or [])[:4]
    )
    signature_src = "|".join(
        [
            str(seq_result.reason),
            str(seq_result.matched_headers),
            str(seq_result.incoming_count),
            str(seq_result.outgoing_count),
            *normalized_lines[:12],
        ]
    )
    signature = hashlib.md5(signature_src.encode("utf-8", errors="ignore")).hexdigest()
    if signature == _LAST_QN_PARSER_DIAG_SIGNATURE:
        _LAST_QN_PARSER_DIAG_REPEAT += 1
        if _LAST_QN_PARSER_DIAG_REPEAT % 5 != 0:
            return
        print(
            "[千牛-Reader] chat parser 失败重复 "
            f"count={_LAST_QN_PARSER_DIAG_REPEAT} reason={seq_result.reason}"
        )
        return

    _LAST_QN_PARSER_DIAG_SIGNATURE = signature
    _LAST_QN_PARSER_DIAG_REPEAT = 1
    print(
        "[千牛-Reader] chat parser 失败 "
        f"conv={platform_conv_id!r} customer={customer_name!r} "
        f"reason={seq_result.reason} headers={seq_result.matched_headers} "
        f"in={seq_result.incoming_count} out={seq_result.outgoing_count} "
        f"lines={seq_result.total_lines}"
    )
    print(f"[千牛-Reader] chat parser 行预览: {line_preview or '无'}")
    if msg_preview:
        print(f"[千牛-Reader] chat parser 已识别消息预览: {msg_preview}")


def _fmt_unread_rows(rows: list[tuple[int, float]], limit: int = 8) -> str:
    if not rows:
        return "无"
    parts = [f"y={float(y):.0f}" for _i, y in rows[:limit]]
    if len(rows) > limit:
        parts.append(f"+{len(rows) - limit}")
    return "[" + ", ".join(parts) + "]"


def _print_reader_segment_timing(
    scan_idx: int,
    segments: dict[str, float],
    round_ms: float,
    note: str = "",
) -> None:
    parts = [f"{k}={v:.0f}ms" for k, v in segments.items()]
    msg = f"[千牛-Reader] 分段耗时 scan#{scan_idx} round={round_ms:.0f}ms"
    if parts:
        msg += " | " + " ".join(parts)
    if note:
        msg += f" | {note}"
    print(msg)


def _merge_chat_timing(dst: dict[str, float], src: dict[str, float], prefix: str) -> None:
    for k, v in src.items():
        key = f"{prefix}_{k}"
        dst[key] = dst.get(key, 0.0) + float(v)


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

    def force_set(self, name: str) -> None:
        if not name:
            return
        self.current = name
        self._candidate = None
        self._candidate_count = 0


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
    if re.match(r"^html\s*,", s, re.I):
        return False
    if "这类消息" in s and "手机" in s:
        return False
    return True


def _normalize_inbox_content(content: str) -> str:
    s = content.strip()
    if not s:
        return s
    if re.match(
        r"^[\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{2,24}(店|专营店|旗舰店|官方店|客服)\s*[:：]\s*"
        r"[\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{1,10}$",
        s,
    ):
        return ""
    m = re.match(
        r"^([\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{2,24}(店|专营店|旗舰店|官方店|客服))\s*[:：]\s*"
        r"([\u4e00-\u9fa5A-Za-z0-9·\-_（）()]{1,10})\s+(.+)$",
        s,
    )
    if m:
        return m.group(4).strip()
    return s


def _split_combined_inbox_content(content: str) -> list[str]:
    s = content.strip()
    if not s:
        return []
    matches = list(_QN_SPEAKER_TS_SPLIT.finditer(s))
    if not matches:
        return [s]

    parts: list[str] = []
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


def _drain_qianniu_incremental_purge_file(
    detector: IncrementalDetector,
    state_dir: Path,
) -> None:
    """
    消费 Qt 侧「清空聊天记录 / 删除会话」时追加的 platform_conversation_id 列表，
    清除 qianniu_last_messages.json 中对应会话的增量去重状态。
    实现位置与 MessageDao::notifyReaderIncrementalStatePurge（reader_incremental_purge_qianniu.txt）对应。
    """
    path = state_dir / "reader_incremental_purge_qianniu.txt"
    if not path.exists():
        return
    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        path.unlink()
    except OSError as ex:
        print(f"[千牛-Reader] 读取增量 purge 文件失败: {ex}")
        return
    for line in lines:
        pcid = line.strip()
        if not pcid or pcid.startswith("#"):
            continue
        detector.purge_conversation(pcid)
        print(
            "[千牛-Reader] 已丢弃增量去重状态 "
            f"platform_conversation_id={pcid!r}（与聚合清空/删会话同步）"
        )


def _space_for_region(cfg: dict[str, Any], region: dict[str, Any]) -> str:
    s = str(region.get("coordinates", "")).lower()
    if s in ("client", "window"):
        return s
    return coordinate_space(cfg)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _chat_capture_cfg(cfg: dict[str, Any]) -> dict[str, Any]:
    return cfg.get("chat_capture") or {}


def _resolve_clicknium_project_root(cfg: dict[str, Any]) -> Optional[Path]:
    capture_cfg = _chat_capture_cfg(cfg)
    env_name = str(capture_cfg.get("clicknium_project_root_env", "QN_CLICKNIUM_PROJECT")).strip()
    raw = ""
    if env_name:
        raw = str(os.environ.get(env_name, "") or "").strip()
    if not raw:
        raw = str(capture_cfg.get("clicknium_project_root", "") or "").strip()
    if raw:
        path = Path(raw).expanduser()
        if not path.is_absolute():
            path = _repo_root() / path
        return path
    repo_root = _repo_root()
    if (repo_root / ".locator").is_dir():
        return repo_root
    return None


def _load_png_as_bgra(path: Path) -> tuple[Optional[bytes], int, int]:
    try:
        from PIL import Image
    except ImportError:
        return None, 0, 0
    try:
        with Image.open(path) as img:
            rgba = img.convert("RGBA")
            w, h = rgba.size
            return rgba.tobytes("raw", "BGRA"), w, h
    except Exception:
        return None, 0, 0


def _is_clicknium_locator_quota_error(ex: BaseException) -> bool:
    """Clicknium 免费/试用许可对工程内 locator 条数有限制时抛出的错误。"""
    if "UnAuthorized" in type(ex).__name__ or "Unauthorized" in type(ex).__name__:
        return True
    s = str(ex).lower()
    return "locator" in s and ("allowance" in s or "license" in s or "upgrade" in s)


def _capture_chat_bgra_clicknium(cfg: dict[str, Any]) -> tuple[Optional[bytes], int, int]:
    capture_cfg = _chat_capture_cfg(cfg)
    locator = str(
        capture_cfg.get(
            "clicknium_chat_locator",
            "aliworkbench.list_uiwindow_centralwidget_subchatview_chatdisplaywidget_chatco",
        )
        or ""
    ).strip()
    project_root = _resolve_clicknium_project_root(cfg)
    if not locator:
        _log_qianniu_clicknium_capture_diag(cfg, "未配置 clicknium_chat_locator")
        return None, 0, 0
    if project_root is None:
        _log_qianniu_clicknium_capture_diag(cfg, "未配置 Clicknium 工程根（project_root / env）")
        return None, 0, 0
    locator_dir = project_root / ".locator"
    if not locator_dir.is_dir():
        _log_qianniu_clicknium_capture_diag(
            cfg,
            f"工程根缺少 .locator 目录 root={str(project_root)!r}",
        )
        return None, 0, 0

    tmp_path = STATE_DIR / "qianniu_clicknium_chat_capture.png"
    old_sys_path_0 = sys.path[0] if sys.path else ""
    try:
        if sys.path:
            sys.path[0] = str(project_root)
        else:
            sys.path.insert(0, str(project_root))
        from clicknium import find_element

        el = find_element(locator)
        if el is None:
            _log_qianniu_clicknium_capture_diag(
                cfg,
                f"未找到定位器 locator={locator!r}",
            )
            return None, 0, 0
        tmp_path.parent.mkdir(parents=True, exist_ok=True)
        el.save_to_image(str(tmp_path))
        bgra, w, h = _load_png_as_bgra(tmp_path)
        if not bgra or w <= 0 or h <= 0:
            _log_qianniu_clicknium_capture_diag(
                cfg,
                f"截图文件无效 locator={locator!r} path={str(tmp_path)!r}",
            )
            return None, 0, 0
        return bgra, w, h
    except Exception as ex:
        msg = f"异常={type(ex).__name__}: {ex}"
        if _is_clicknium_locator_quota_error(ex):
            msg += "；将回退 Win32 截图。若需继续用 Clicknium：升级许可或减少 .locator 中 locator 条数，或设 chat_capture.driver=win32"
        _log_qianniu_clicknium_capture_diag(cfg, msg)
        return None, 0, 0
    finally:
        if sys.path:
            sys.path[0] = old_sys_path_0


def _capture_chat_bgra(hwnd: int, cfg: dict[str, Any]) -> tuple[Optional[bytes], int, int]:
    """仅截图聊天区，供锁外再跑 OCR。"""
    capture_cfg = _chat_capture_cfg(cfg)
    driver = str(capture_cfg.get("driver", "auto") or "auto").strip().lower()
    route_auto = bool(capture_cfg.get("auto_clicknium_route_by_unobstructed", True))
    # auto：顶层窗在屏幕上未被其它顶层窗遮挡时用 Clicknium（与焦点无关）；否则直接 PrintWindow，避免挡窗误采。
    mx, my, mw, mh = get_message_region_px(hwnd, cfg)
    if (
        driver == "auto"
        and route_auto
        and mw > 0
        and mh > 0
        and not hwnd_capture_subrect_unobstructed(hwnd, mx, my, mw, mh)
    ):
        if _clicknium_capture_log_enabled(cfg):
            print(
                "[千牛-Reader] 聊天截图: auto 判定聊天子区域在屏幕采样点上存在遮挡 "
                "→ 直接使用 PrintWindow，跳过 Clicknium"
            )
        x, y, w, h = mx, my, mw, mh
        if w <= 0 or h <= 0:
            return None, 0, 0
        try:
            bgra, cw, ch, _ = capture_region(hwnd, x, y, w, h)
            return bgra, cw, ch
        except Exception:
            return None, 0, 0

    if driver in ("auto", "clicknium"):
        bgra, w, h = _capture_chat_bgra_clicknium(cfg)
        if bgra is not None and w > 0 and h > 0:
            return bgra, w, h
        if driver == "clicknium":
            return None, 0, 0
    x, y, w, h = get_message_region_px(hwnd, cfg)
    if w <= 0 or h <= 0:
        return None, 0, 0
    try:
        bgra, cw, ch, _ = capture_region(hwnd, x, y, w, h)
        return bgra, cw, ch
    except Exception:
        return None, 0, 0


def get_message_region_px(hwnd: int, cfg: dict[str, Any]) -> tuple[int, int, int, int]:
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


def _debug_snapshot_dir(cfg: dict[str, Any]) -> Path:
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
    cfg: dict[str, Any],
    scan_idx: int,
    chat_bgra: bytes,
    cw: int,
    ch: int,
    header_bgra: Optional[bytes] = None,
    header_w: int = 0,
    header_h: int = 0,
    list_bgra: Optional[bytes] = None,
    list_w: int = 0,
    list_h: int = 0,
) -> None:
    dbg = cfg.get("debug") or {}
    save_all = bool(dbg.get("save_screenshots", False))
    save_chat_only = bool(dbg.get("save_chat_png", False))
    if not save_all and not save_chat_only:
        return
    every = max(1, int(dbg.get("every_n_scans", 1)))
    if scan_idx % every != 0:
        return
    out_dir = _debug_snapshot_dir(cfg)
    ts = time.strftime("%Y%m%d_%H%M%S")
    stem = f"scan{scan_idx:06d}_{ts}"
    try:
        save_bgra_png(chat_bgra, cw, ch, out_dir / f"{stem}_chat.png")

        if save_all:
            from ..common.qianniu_session import get_header_region_px

            if header_bgra and header_w > 0 and header_h > 0:
                save_bgra_png(header_bgra, header_w, header_h, out_dir / f"{stem}_header.png")
            else:
                hx, hy, hw, hh = get_header_region_px(hwnd, cfg)
                if hw > 0 and hh > 0:
                    hb, hw2, hh2, _ = capture_region(hwnd, hx, hy, hw, hh)
                    save_bgra_png(hb, hw2, hh2, out_dir / f"{stem}_header.png")

            list_cfg = cfg.get("conversation_list_region") or {}
            if list_cfg:
                if list_bgra and list_w > 0 and list_h > 0:
                    save_bgra_png(list_bgra, list_w, list_h, out_dir / f"{stem}_list.png")
                else:
                    lx, ly, lw, lh = get_conversation_list_region_px(hwnd, cfg)
                    if lw > 0 and lh > 0:
                        lb, lw2, lh2, _ = capture_region(hwnd, lx, ly, lw, lh)
                        save_bgra_png(lb, lw2, lh2, out_dir / f"{stem}_list.png")

        max_keep = int(dbg.get("max_png_files", 300))
        _prune_debug_pngs(out_dir, max_keep)
        if save_all:
            print(f"[千牛-Reader] 调试截图已保存 {out_dir / stem}_chat.png / _header.png / _list.png")
        else:
            print(f"[千牛-Reader] 聊天区调试图已保存 {out_dir / f'{stem}_chat.png'}")
    except Exception as ex:
        print(f"[千牛-Reader] 调试截图保存失败: {ex}")


def _chat_image_media_root(cfg: dict[str, Any]) -> Path:
    raw = cfg.get("chat_image_media_dir")
    if raw:
        return Path(str(raw)).expanduser()
    return Path(__file__).resolve().parents[3] / "python" / "rpa" / "_media" / "qianniu_chat"


def _safe_conv_dir_segment(platform_conv_id: str) -> str:
    s = re.sub(r"[^\w\-.]", "_", str(platform_conv_id).strip())[:120]
    return s or "default"


def _prune_chat_png_dir(conv_dir: Path, max_keep: int = 10) -> None:
    """按修改时间只保留最近 max_keep 个 PNG（文件名可排序时亦一致）。"""
    try:
        if not conv_dir.is_dir():
            return
        files = sorted(
            [p for p in conv_dir.iterdir() if p.suffix.lower() == ".png"],
            key=lambda p: p.stat().st_mtime,
        )
        while len(files) > max_keep:
            oldest = files.pop(0)
            try:
                oldest.unlink()
            except OSError:
                break
    except OSError:
        pass


def _process_current_chat_image_only(
    detector: IncrementalDetector,
    customer_name: str,
    platform_conv_id: str,
    db: Path,
    cfg: dict[str, Any],
    bgra: bytes,
    w: int,
    h: int,
    _timing: Optional[dict[str, float]] = None,
) -> int:
    """聊天区仅落 PNG，不做 OCR；去重用 img:sha256 合成 ParsedMessage。"""
    t0 = time.perf_counter()
    digest = hashlib.sha256(bgra).hexdigest()
    synthetic = ParsedMessage(
        content=f"img:{digest}",
        side="in",
        bbox=[],
        sender_name="",
        original_timestamp="",
    )
    new_messages = detector.filter_new([synthetic], conv_id=platform_conv_id)
    if _timing is not None:
        _timing["dedup"] = (time.perf_counter() - t0) * 1000.0
    if not new_messages:
        return 0

    root = _chat_image_media_root(cfg) / _safe_conv_dir_segment(platform_conv_id)
    try:
        root.mkdir(parents=True, exist_ok=True)
    except OSError:
        print(f"[千牛-Reader] WARN 无法创建聊天截图目录 {root}")
        return 0

    ts = int(time.time() * 1000)
    out_path = root / f"{ts}_{digest[:16]}.png"
    t1 = time.perf_counter()
    try:
        save_bgra_png(bgra, w, h, out_path)
    except Exception as ex:
        print(f"[千牛-Reader] WARN 保存聊天 PNG 失败: {ex}")
        return 0
    if _timing is not None:
        _timing["png_write"] = (time.perf_counter() - t1) * 1000.0

    _prune_chat_png_dir(root, max_keep=int(cfg.get("chat_image_max_per_conversation", 10)))

    placeholder = str(cfg.get("chat_image_inbox_placeholder", "[图片消息]"))
    pid = make_platform_msg_id(PLATFORM, platform_conv_id, digest)
    abs_path = str(out_path.resolve())

    t2 = time.perf_counter()
    conn = open_db(db)
    written_count = 0
    try:
        if write_inbox_message(
            conn,
            PLATFORM,
            platform_conv_id,
            customer_name,
            placeholder,
            pid,
            sender_name="",
            original_timestamp="",
            content_image_path=abs_path,
        ):
            written_count = 1
            print(f"[千牛-Reader] 写入 1 条 inbox 图片 ({customer_name}) → {out_path.name}")
    finally:
        conn.close()
    if _timing is not None:
        _timing["db"] = (time.perf_counter() - t2) * 1000.0
    return written_count


def _process_current_chat(
    ocr: BaseOCREngine,
    fallback_ocr: Optional[BaseOCREngine],
    hwnd: int,
    cfg: dict[str, Any],
    detector: IncrementalDetector,
    customer_name: str,
    platform_conv_id: str,
    db: Path,
    left_threshold: float,
    right_threshold: float,
    merge_y_gap: float,
    debug_layout: bool,
    scan_idx: int,
    chat_bgra: Optional[bytes] = None,
    chat_w: int = 0,
    chat_h: int = 0,
    header_bgra: Optional[bytes] = None,
    header_hw: int = 0,
    header_hh: int = 0,
    list_bgra: Optional[bytes] = None,
    list_w: int = 0,
    list_h: int = 0,
    _timing: Optional[dict[str, float]] = None,
) -> int:
    if chat_bgra is not None and chat_w > 0 and chat_h > 0:
        bgra, w, h = chat_bgra, chat_w, chat_h
    else:
        x, y, w, h = get_message_region_px(hwnd, cfg)
        if w <= 0 or h <= 0:
            return 0
        bgra, _, _, _ = capture_region(hwnd, x, y, w, h)

    _save_qianniu_debug_frames(
        hwnd,
        cfg,
        scan_idx,
        bgra,
        w,
        h,
        header_bgra=header_bgra,
        header_w=header_hw,
        header_h=header_hh,
        list_bgra=list_bgra,
        list_w=list_w,
        list_h=list_h,
    )
    chat_mode = str(cfg.get("chat_content_mode", "text")).lower()
    if chat_mode == "image_only":
        return _process_current_chat_image_only(
            detector,
            customer_name,
            platform_conv_id,
            db,
            cfg,
            bgra,
            w,
            h,
            _timing,
        )

    ocr_cfg = cfg.get("ocr") or {}
    chat_parser_cfg = cfg.get("chat_parser") or {}
    bubble_first = bool(chat_parser_cfg.get("bubble_first", True))
    parser_mode = str(chat_parser_cfg.get("mode", "hybrid")).lower()
    prefer_sequence = bool(chat_parser_cfg.get("prefer_sequence_parser", True))
    allow_layout_fallback = bool(chat_parser_cfg.get("allow_layout_fallback", True))
    drop_empty_messages = bool(chat_parser_cfg.get("drop_empty_messages", True))
    min_incoming_for_success = int(chat_parser_cfg.get("min_incoming_messages_for_success", 1))
    min_content_chars = int(chat_parser_cfg.get("min_content_chars", 1))
    double_read_stable = bool(ocr_cfg.get("double_read_stable", False))
    double_read_delay_sec = float(ocr_cfg.get("double_read_delay_sec", 0.25))

    t0 = time.perf_counter()
    blocks = ocr.recognize(bgra, w, h)
    if double_read_stable:
        time.sleep(max(0.0, double_read_delay_sec))
        blocks2 = ocr.recognize(bgra, w, h)
        if [b[0] for b in blocks2] == [b[0] for b in blocks]:
            blocks = blocks2
    if _timing is not None:
        _timing["ocr"] = (time.perf_counter() - t0) * 1000.0
    t1 = time.perf_counter()
    messages: list[ParsedMessage] = []
    used_sequence_parser = False
    used_bubble_first = False
    if bubble_first and bgra and w > 0 and h > 0:
        from ..common.qianniu_bubble_parser import (
            bubble_first_has_incoming_body,
            parse_qianniu_bubble_first,
        )

        bubble_msgs = parse_qianniu_bubble_first(
            blocks,
            bgra,
            w,
            h,
            default_incoming_name=(customer_name or "").strip(),
            left_threshold=left_threshold,
            right_threshold=right_threshold,
            merge_y_gap=merge_y_gap,
            debug=debug_layout,
        )
        if bubble_msgs and bubble_first_has_incoming_body(
            bubble_msgs, min_content_chars=min_content_chars
        ):
            messages = bubble_msgs
            used_bubble_first = True

    if not used_bubble_first and parser_mode in ("sequence", "hybrid") and prefer_sequence:
        seq_result = parse_qianniu_chat_blocks(
            blocks,
            min_incoming_messages_for_success=min_incoming_for_success,
            drop_empty_messages=drop_empty_messages,
            min_content_chars=min_content_chars,
        )
        if seq_result.success:
            messages = to_parsed_messages(seq_result.messages)
            used_sequence_parser = True
        else:
            _log_qianniu_chat_parser_failure(
                cfg,
                customer_name,
                platform_conv_id,
                seq_result,
            )
            if parser_mode == "sequence" and not allow_layout_fallback:
                messages = []

    if not used_sequence_parser and not used_bubble_first:
        fallback_blocks = blocks
        if fallback_ocr is not None and fallback_ocr is not ocr:
            tf = time.perf_counter()
            fallback_blocks = fallback_ocr.recognize(bgra, w, h)
            if _timing is not None:
                _timing["ocr_fallback"] = (time.perf_counter() - tf) * 1000.0
        messages = parse_chat_layout(
            fallback_blocks,
            region_width=float(w),
            platform="qianniu",
            left_threshold=left_threshold,
            right_threshold=right_threshold,
            merge_y_gap=merge_y_gap,
            debug=debug_layout,
        )
    new_messages = detector.filter_new(messages, conv_id=platform_conv_id)
    if _timing is not None:
        _timing["layout"] = (time.perf_counter() - t1) * 1000.0
    if not new_messages:
        return 0

    written_count = 0
    t2 = time.perf_counter()
    conn = open_db(db)
    try:
        for msg in new_messages:
            if msg.side != "in":
                continue
            sender_name = msg.sender_name or ""
            original_ts = msg.original_timestamp or ""
            for chunk in _split_combined_inbox_content(msg.content):
                content = _normalize_inbox_content(chunk)
                if not _is_valid_inbox_content(content):
                    continue
                ch = content_hash(msg.side, content)
                pid = make_platform_msg_id(PLATFORM, platform_conv_id, ch)
                if write_inbox_message(
                    conn,
                    PLATFORM,
                    platform_conv_id,
                    customer_name,
                    content,
                    pid,
                    sender_name=sender_name,
                    original_timestamp=original_ts,
                ):
                    written_count += 1
        if written_count > 0:
            print(f"[千牛-Reader] 写入 {written_count} 条 inbox ({customer_name})")
    finally:
        conn.close()
    if _timing is not None:
        _timing["db"] = (time.perf_counter() - t2) * 1000.0
    return written_count


def _scan_for_unread_rows(
    hwnd: int,
    cfg: dict[str, Any],
    list_bgra: Optional[bytes] = None,
    list_rw: int = 0,
    list_rh: int = 0,
) -> list[tuple[int, float]]:
    unread_cfg = cfg.get("unread_detection") or {}
    if not unread_cfg.get("enabled", False):
        return []
    if list_bgra is not None and list_rw > 0 and list_rh > 0:
        bgra, rw, rh = list_bgra, list_rw, list_rh
    else:
        bgra, rw, rh = capture_conversation_list_bgra(hwnd, cfg)
        if not bgra or rw <= 0 or rh <= 0:
            return []
    dual = bool(unread_cfg.get("dual_band", False))
    rows: list[tuple[int, float]] = []
    scan_x_start = 0.0
    scan_x_end = 0.0
    try:
        # 左带：头像角标；兼容旧配置：未设 scan_x_end_ratio 时，用 scan_x_ratio 作为右边界。
        scan_x_ratio = float(unread_cfg.get("scan_x_ratio", 0.36))
        scan_x_start = float(unread_cfg.get("scan_x_start_ratio", 0.0))
        if "scan_x_end_ratio" in unread_cfg and unread_cfg["scan_x_end_ratio"] is not None:
            scan_x_end = float(unread_cfg["scan_x_end_ratio"])
        else:
            scan_x_end = scan_x_ratio
        if dual:
            rows = detect_unread_rows_dual_band(
                bgra,
                rw,
                rh,
                row_height=int(unread_cfg.get("row_height", 68)),
                left_red_threshold=int(unread_cfg.get("red_threshold", 15)),
                scan_x_start_ratio=scan_x_start,
                scan_x_end_ratio=scan_x_end,
                timer_x_start_ratio=float(unread_cfg.get("timer_x_start_ratio", 0.58)),
                timer_x_end_ratio=float(unread_cfg.get("timer_x_end_ratio", 1.0)),
                timer_red_threshold=int(unread_cfg.get("timer_red_threshold", 8)),
                use_red_dominance=bool(unread_cfg.get("use_red_dominance", True)),
            )
        else:
            rows = detect_unread_rows(
                bgra,
                rw,
                rh,
                row_height=int(unread_cfg.get("row_height", 68)),
                red_threshold=int(unread_cfg.get("red_threshold", 15)),
                scan_x_ratio=scan_x_ratio,
                scan_x_start_ratio=scan_x_start,
                scan_x_end_ratio=scan_x_end,
            )
    except Exception as ex:
        if _reader_event_log_enabled(cfg):
            print(
                "[千牛-Reader] 未读像素扫描异常: "
                f"{type(ex).__name__}: {ex} list={rw}x{rh}"
            )
        return []
    if _reader_event_log_enabled(cfg):
        mode = "dual_band" if dual else "left_band"
        t0 = float(unread_cfg.get("timer_x_start_ratio", 0.58)) if dual else 0.0
        t1 = float(unread_cfg.get("timer_x_end_ratio", 1.0)) if dual else 0.0
        print(
            "[千牛-Reader] 未读像素扫描: "
            f"mode={mode} list={rw}x{rh} left_x=[{scan_x_start:.2f},{scan_x_end:.2f}) "
            + (
                f"timer_x=[{t0:.2f},{t1:.2f}) dom={bool(unread_cfg.get('use_red_dominance', True))} "
                if dual
                else ""
            )
            + f"hits={len(rows)}"
        )
    return rows


def _header_canonical_supersedes_list_row(header: str, list_name: str) -> bool:
    """
    标题区 OCR 得到的买家名是否应覆盖列表行名（典型：列表栏窄导致 tb 号被截断）。

    仅在「标题更长且以列表名为前缀」且二者 `header_matches_target` 为真时返回真；
    且限制为 `tb` + 数字 形式，避免误把「张三」与「张三丰」等普通昵称强行合并。
    """
    if not header or not list_name or header == list_name:
        return False
    if len(header) <= len(list_name):
        return False
    if not header.startswith(list_name):
        return False
    if not header_matches_target(header, list_name):
        return False
    if re.match(r"(?i)^tb\d+$", list_name) and re.match(r"(?i)^tb\d+$", header):
        return True
    return False


def _reconcile_list_entries_with_header_title(
    entries: list[ConversationListEntry],
    header_title: str,
    *,
    trace: bool = False,
) -> list[ConversationListEntry]:
    """列表行名被截断但与顶栏标题一致时，用顶栏标题写回该行 `name`（不可变 dataclass 则重建条目）。"""
    ht = (header_title or "").strip()
    if not entries or not ht:
        return entries
    out: list[ConversationListEntry] = []
    for e in entries:
        if _header_canonical_supersedes_list_row(ht, e.name):
            if trace:
                print(
                    "[千牛-Reader] 会话名(列表修正): "
                    f"列表行名={e.name!r} 与标题={ht!r} 判为截断同源 → 采用标题"
                )
            out.append(
                ConversationListEntry(
                    name=ht,
                    y_center=e.y_center,
                    row_index=e.row_index,
                    confidence=e.confidence,
                )
            )
        else:
            out.append(e)
    return out


def _pick_peer_name_from_list_entries(
    entries: list[ConversationListEntry],
    stabilizer: NameStabilizer,
    scan_cfg: dict[str, Any],
    *,
    trace: bool = False,
) -> Optional[str]:
    """
    用会话列表 OCR 行推断当前接待中的买家名，与标题区一致时可省去标题 OCR。

    匹配对象（谁和谁）：
    - **锚点**：`stabilizer.current`，即上一轮已稳定的「当前会话名」（与顶栏语义一致时才能省 OCR）。
    - **行名**：列表 OCR 每行解析出的 `e.name`（见 `parse_conversation_list_blocks` / `_pick_row_name`）。
    - **判定**：对每一行计算 `header_matches_target(锚点, 行名)`（定义见 `qianniu_session.header_matches_target`：
      二者互相子串包含，或「锚点」前 4 个字符出现在「行名」中）。
    - 多行命中时取 **y_center 最小**（列表最上）的一行。

    若无锚点：仅当 `peer_name_list_top_when_unstable` 为 true 时用列表最上一行；否则返回 None，由调用方做标题 OCR。

    调用方应在解析 entries 后先 `_reconcile_list_entries_with_header_title`，修正列表栏截断的 tb 号。
    """
    if not entries:
        if trace:
            print("[千牛-Reader] 会话名(列表推断): 列表 OCR 无有效行 → 无法用列表匹配，将用标题区 OCR")
        return None
    sorted_by_y = sorted(entries, key=lambda e: e.y_center)
    cur = stabilizer.current
    if trace:
        print(
            "[千牛-Reader] 会话名(列表推断): 规则 header_matches_target(锚点, 行名) — "
            "锚点=stabilizer.current；行名=该行 e.name"
        )
    if cur:
        if trace:
            print(f"[千牛-Reader] 会话名(列表推断): 锚点 stabilizer.current={cur!r}")
        matches: list[ConversationListEntry] = []
        for e in entries:
            ok = header_matches_target(cur, e.name)
            if trace:
                print(
                    "[千牛-Reader] 会话名(列表推断): "
                    f"  行 y≈{e.y_center:.0f} e.name={e.name!r} "
                    f"header_matches_target(锚点,e.name)={ok}"
                )
            if ok:
                matches.append(e)
        if not matches:
            if trace:
                print("[千牛-Reader] 会话名(列表推断): 无命中行 → 将使用标题区 OCR")
            return None
        matches.sort(key=lambda e: e.y_center)
        chosen = matches[0].name
        if trace:
            print(
                "[千牛-Reader] 会话名(列表推断): "
                f"共命中 {len(matches)} 行，取最上 y≈{matches[0].y_center:.0f} → 采用 {chosen!r}（跳过标题区 OCR）"
            )
        return chosen
    if trace:
        print("[千牛-Reader] 会话名(列表推断): stabilizer.current 为空，无法做锚点匹配")
    if bool(scan_cfg.get("peer_name_list_top_when_unstable", False)):
        top = sorted_by_y[0].name
        if trace:
            print(
                "[千牛-Reader] 会话名(列表推断): peer_name_list_top_when_unstable=true "
                f"→ 取列表最上一行 {top!r}"
            )
        return top
    if trace:
        print(
            "[千牛-Reader] 会话名(列表推断): 未开启 peer_name_list_top_when_unstable → "
            "将使用标题区 OCR"
        )
    return None


def _rotate_entries(entries: list[ConversationListEntry], cursor: int) -> list[ConversationListEntry]:
    if not entries:
        return []
    start = cursor % len(entries)
    return entries[start:] + entries[:start]


def _build_scan_plan(
    entries: list[ConversationListEntry],
    unread_rows: list[tuple[int, float]],
    current_name: str,
    scan_cfg: dict[str, Any],
    scan_cursor: int,
) -> tuple[list[ConversationListEntry], int]:
    candidates = [entry for entry in entries if entry.name != current_name]
    if not candidates:
        return [], scan_cursor

    max_switch_per_cycle = max(0, int(scan_cfg.get("max_switch_per_cycle", 4)))
    if max_switch_per_cycle <= 0:
        return [], scan_cursor

    rotated = _rotate_entries(candidates, scan_cursor)
    ordered: list[ConversationListEntry] = []
    scan_mode = str(scan_cfg.get("scan_mode", "visible_round_robin")).lower()
    if scan_mode == "unread_first" and unread_rows:
        row_height_guess = max(36.0, float(scan_cfg.get("row_height_guess", 68)))
        for _row_idx, unread_y in unread_rows:
            nearest = min(rotated, key=lambda item: abs(item.y_center - unread_y), default=None)
            if nearest and abs(nearest.y_center - unread_y) <= row_height_guess * 0.75:
                if all(existing.name != nearest.name for existing in ordered):
                    ordered.append(nearest)

    for entry in rotated:
        if all(existing.name != entry.name for existing in ordered):
            ordered.append(entry)

    plan = ordered[:max_switch_per_cycle]
    next_cursor = (scan_cursor + len(plan)) % max(1, len(candidates))
    return plan, next_cursor


def _entries_matching_unread_rows(
    entries: list[ConversationListEntry],
    unread_rows: list[tuple[int, float]],
    scan_cfg: dict[str, Any],
) -> list[ConversationListEntry]:
    """将红点纵坐标与列表 OCR 行条目对齐，得到带未读红点的会话列表。"""
    if not entries or not unread_rows:
        return []
    row_h = max(36.0, float(scan_cfg.get("row_height_guess", 68)))
    matched: list[ConversationListEntry] = []
    seen: set[str] = set()
    for _ridx, unread_y in unread_rows:
        nearest = min(entries, key=lambda e: abs(e.y_center - unread_y), default=None)
        if nearest and abs(nearest.y_center - unread_y) <= row_h * 0.75:
            if nearest.name not in seen:
                seen.add(nearest.name)
                matched.append(nearest)
    return matched


def _build_unread_only_switch_plan(
    unread_entries: list[ConversationListEntry],
    current_name: str,
    scan_cfg: dict[str, Any],
    scan_cursor: int,
) -> tuple[list[ConversationListEntry], int]:
    """仅切换「带红点且非当前会话」的条目，避免无红点会话的无效 OCR。"""
    max_switch = max(0, int(scan_cfg.get("max_switch_per_cycle", 4)))
    if max_switch <= 0 or not unread_entries:
        return [], scan_cursor
    candidates: list[ConversationListEntry] = []
    for e in unread_entries:
        if header_matches_target(current_name, e.name):
            continue
        candidates.append(e)
    if not candidates:
        return [], scan_cursor
    rotated = _rotate_entries(candidates, scan_cursor)
    plan = rotated[:max_switch]
    next_cursor = (scan_cursor + len(plan)) % max(1, len(candidates))
    return plan, next_cursor


def run_reader(
    config_path: Path = DEFAULT_CONFIG,
    db_path: Optional[Path] = None,
    poll_interval_sec: float = 3,
    debug_dir: Optional[Path] = None,
) -> None:
    if not config_path.exists():
        raise FileNotFoundError(f"Config not found: {config_path}")

    cfg = load_config(config_path)
    production_mode = bool(cfg.get("production_mode", False))
    if production_mode:
        dbg = dict(cfg.get("debug") or {})
        dbg["save_screenshots"] = False
        dbg["save_chat_png"] = False
        dbg["log_parsed_messages"] = False
        cfg["debug"] = dbg

    conv = cfg.get("conversation") or {}
    layout_cfg = cfg.get("layout") or {}
    ocr_cfg = cfg.get("ocr") or {}
    scan_cfg = cfg.get("conversation_scan") or {}

    default_customer_name = str(conv.get("default_customer_name", "未知会话"))
    conv_id_prefix = str(conv.get("conv_id_prefix", "qianniu_"))
    fallback_conv_id = str(conv.get("fallback_platform_conversation_id", f"{conv_id_prefix}default"))

    left_threshold = float(layout_cfg.get("left_threshold", 0.4))
    right_threshold = float(layout_cfg.get("right_threshold", 0.6))
    merge_y_gap = float(layout_cfg.get("merge_y_gap", 40))
    min_confidence = float(ocr_cfg.get("min_confidence", 0.5))
    max_side = int(ocr_cfg.get("max_side", 960))
    invert_for_dark_mode = bool(ocr_cfg.get("invert_for_dark_mode", True))
    det_thresh = float(ocr_cfg.get("det_thresh", 0.2))
    det_box_thresh = float(ocr_cfg.get("det_box_thresh", 0.4))
    chat_engine_name = str(ocr_cfg.get("chat_engine", ocr_cfg.get("engine", "paddleocr"))).lower()
    fallback_engine_name = str(ocr_cfg.get("fallback_engine", "paddleocr")).lower()
    fuzzy_threshold = float(layout_cfg.get("fuzzy_dedup_threshold", 0.85))
    poll_interval = float(cfg.get("poll_interval_sec", poll_interval_sec))

    multi_enabled = bool(scan_cfg.get("enabled", bool(cfg.get("conversation_list_region"))))
    switch_timeout_sec = float(scan_cfg.get("switch_timeout_sec", 2.5))
    lock_cfg = cfg.get("window_lock") or {}
    skip_multi_when_pending = bool(lock_cfg.get("reader_skip_multi_when_pending_send", True))
    reduce_activity_when_pending = bool(lock_cfg.get("reader_reduce_activity_when_pending_send", True))
    _pending_iv_cfg = float(lock_cfg.get("reader_poll_interval_sec_when_pending_send") or 0)
    if _pending_iv_cfg <= 0:
        _pending_iv_cfg = float(poll_interval) * float(lock_cfg.get("pending_send_poll_factor", 1.75))
    pending_poll_interval = max(float(poll_interval), _pending_iv_cfg)

    state_path = STATE_DIR / "qianniu_last_messages.json"
    STATE_DIR.mkdir(parents=True, exist_ok=True)

    rpa_phase(
        "qianniu.reader",
        "ocr_init_start",
        "正在创建 Reader OCR 引擎（标题/列表见 ocr.list_header_engine；聊天区见 ocr.chat_engine）",
    )
    ocr = PaddleOCREngine(
        lang="ch",
        min_confidence=min_confidence,
        max_side=max_side,
        invert_for_dark_mode=invert_for_dark_mode,
        det_thresh=det_thresh,
        det_box_thresh=det_box_thresh,
    )
    # 主 Paddle 实例：聊天区回退、或与 list_header_engine 相同时复用。
    ocr.warmup()
    list_header_engine_name = str(ocr_cfg.get("list_header_engine", "paddleocr")).lower()
    layout_ocr: BaseOCREngine = ocr
    if list_header_engine_name not in ("paddle", "paddleocr"):
        layout_ocr = build_ocr_engine(
            list_header_engine_name,
            lang="ch",
            min_confidence=min_confidence,
            max_side=max_side,
            invert_for_dark_mode=invert_for_dark_mode,
            det_thresh=det_thresh,
            det_box_thresh=det_box_thresh,
        )
        layout_ocr.warmup()
    chat_ocr: BaseOCREngine = ocr
    if chat_engine_name not in ("paddle", "paddleocr"):
        chat_ocr = build_ocr_engine(
            chat_engine_name,
            lang="ch",
            min_confidence=min_confidence,
            max_side=max_side,
            invert_for_dark_mode=invert_for_dark_mode,
            det_thresh=det_thresh,
            det_box_thresh=det_box_thresh,
        )
        chat_ocr.warmup()
    fallback_chat_ocr: Optional[BaseOCREngine] = None
    if fallback_engine_name in ("paddle", "paddleocr"):
        fallback_chat_ocr = ocr
    elif fallback_engine_name == chat_engine_name:
        fallback_chat_ocr = chat_ocr
    else:
        fallback_chat_ocr = build_ocr_engine(
            fallback_engine_name,
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

    db = db_path or resolved_default_db_path()
    dbg = cfg.get("debug") or {}
    if production_mode:
        print(
            "[千牛-Reader] production_mode=已开启：已关闭每轮调试截图与 layout 逐块解析日志；"
            "排障请将 qianniu_config.json 中 production_mode 设为 false"
        )
    if dbg.get("save_screenshots"):
        print(
            f"[千牛-Reader] 调试截图已开启 → {_debug_snapshot_dir(cfg)} "
            f"(每 {max(1, int(dbg.get('every_n_scans', 1)))} 次扫描保存 chat/header/list)"
        )
    print(
        f"[千牛-Reader] DB={db} interval={poll_interval}s "
        f"fuzzy={fuzzy_threshold} coords={coordinate_space(cfg)} multi_scan={multi_enabled} "
        f"yield_multi_on_pending={skip_multi_when_pending} "
        f"pending_poll={pending_poll_interval}s "
        f"list_header_ocr={list_header_engine_name} "
        f"chat_ocr={chat_engine_name} fallback_ocr={fallback_engine_name} "
        f"sleep_after_ingest_cap={float(cfg.get('reader_sleep_after_ingest_sec', 0.5))}s"
    )

    _scan_count = 0
    _last_hb = time.time()
    _last_wait_hwnd_log = 0.0
    scan_cursor = 0
    HB_SEC = 30.0
    rpa_phase("qianniu.reader", "poll_loop_enter", "已进入 while True；若未找到窗口会周期性 heartbeat")

    def _pending_out_count() -> int:
        conn = open_db(db)
        try:
            return count_pending_send(conn, PLATFORM)
        finally:
            conn.close()

    ingest_sleep_sec = float(cfg.get("reader_sleep_after_ingest_sec", 0.5))

    def _sleep_after_scan(written: int = 0) -> None:
        pn = _pending_out_count()
        sec = (
            pending_poll_interval
            if (reduce_activity_when_pending and pn > 0)
            else poll_interval
        )
        if written > 0 and ingest_sleep_sec > 0:
            sec = min(sec, ingest_sleep_sec)
        time.sleep(sec)

    use_lock = bool(lock_cfg.get("enabled", True))

    def _reader_lock_acquire_timeout_sec() -> float:
        base = float(lock_cfg.get("timeout_sec", 15.0))
        cap = float(lock_cfg.get("reader_lock_timeout_sec_when_pending_send", 0.0))
        if cap <= 0:
            return base
        if _pending_out_count() > 0:
            return min(base, cap)
        return base

    def _reader_yield_before_lock_if_pending() -> None:
        if _pending_out_count() <= 0:
            return
        y = float(lock_cfg.get("reader_yield_before_lock_sec_when_pending_send", 0.35))
        if y > 0:
            time.sleep(y)

    unread_cfg_main = cfg.get("unread_detection") or {}
    list_first_active = (
        bool(scan_cfg.get("list_first_chat_ocr", True))
        and bool(unread_cfg_main.get("enabled", False))
        and bool(cfg.get("conversation_list_region"))
    )
    fallback_chat_sec = float(scan_cfg.get("fallback_full_chat_ocr_interval_sec", 0))
    _last_fallback_chat_ts = time.time()

    timing_enabled = _reader_segment_timing_enabled(cfg)
    event_log = _reader_event_log_enabled(cfg)

    def _emit_seg(
        st: dict[str, float],
        round_t0: float,
        scan_idx: int,
        note: str = "",
    ) -> None:
        if not timing_enabled:
            return
        rm = (time.perf_counter() - round_t0) * 1000.0
        _print_reader_segment_timing(scan_idx, dict(st), rm, note)

    if timing_enabled:
        print(
            "[千牛-Reader] 分段耗时日志已开启（debug.reader_segment_timing 或关闭 production_mode）"
        )
    if event_log:
        print(
            "[千牛-Reader] 事件日志已开启（debug.reader_event_log 或关闭 production_mode）："
            "红点检测、列表对齐、切换会话等"
        )

    _list_clicknium_prewarm_done = False
    _chat_hash_skip_state: dict[str, Any] = {
        "prev_digest": "",
        "prev_conv": "",
        "skip_streak": 0,
        "last_full_ocr_ts": 0.0,
    }

    while True:
        _drain_qianniu_incremental_purge_file(detector, STATE_DIR)

        hwnd = find_qianniu_hwnd(cfg)
        if not hwnd:
            now = time.time()
            if now - _last_wait_hwnd_log >= HB_SEC:
                rpa_heartbeat("qianniu.reader", "等待千牛「接待中心」窗口（未找到句柄）；请打开并保持可见")
                _last_wait_hwnd_log = now
            print("[千牛-Reader] 未找到千牛接待中心窗口，等待...")
            time.sleep(poll_interval)
            continue
        if is_window_minimized(hwnd):
            print("[千牛-Reader] 千牛窗口已最小化，请保持窗口可见")
            time.sleep(poll_interval)
            continue

        if not _list_clicknium_prewarm_done:
            lc_pw = cfg.get("list_capture") or {}
            dr_pw = str(lc_pw.get("driver", "win32") or "win32").strip().lower()
            loc_pw = str(lc_pw.get("clicknium_list_locator", "") or "").strip()
            if (
                bool(lc_pw.get("prewarm", True))
                and dr_pw in ("auto", "clicknium")
                and loc_pw
            ):
                t_pw0 = time.perf_counter()
                rpa_phase(
                    "qianniu.reader",
                    "list_clicknium_prewarm",
                    "Clicknium 会话列表预热（首次命中窗口后一次截图）",
                )
                warmed = False
                if use_lock:
                    with hold_platform_window_lock(
                        platform=PLATFORM,
                        owner="reader:list_prewarm",
                        timeout_sec=_reader_lock_acquire_timeout_sec(),
                        retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                    ) as plw:
                        if plw.acquired:
                            capture_conversation_list_bgra(hwnd, cfg)
                            warmed = True
                        elif event_log:
                            print("[千牛-Reader] 列表预热: 等待窗口锁超时，跳过预热（下轮仍正常扫）")
                else:
                    capture_conversation_list_bgra(hwnd, cfg)
                    warmed = True
                if warmed:
                    ms_pw = (time.perf_counter() - t_pw0) * 1000.0
                    print(f"[千牛-Reader] Clicknium 会话列表预热完成 wall={ms_pw:.0f}ms")
            _list_clicknium_prewarm_done = True

        wrote_this_round = 0
        try:
            pending_out = _pending_out_count()
            reader_light_pending = pending_out > 0 and bool(
                lock_cfg.get("reader_light_round_when_pending_send", True)
            )
            st: dict[str, float] = {}
            round_t0 = time.perf_counter()
            lb_bgra: Optional[bytes] = None
            lbw, lbh = 0, 0
            hb_b: Optional[bytes] = None
            hb_w, hb_h = 0, 0
            cb_b: Optional[bytes] = None
            cb_w, cb_h = 0, 0
            unread_rows: list[tuple[int, float]] = []
            force_fallback_chat = (
                list_first_active
                and fallback_chat_sec > 0
                and (time.time() - _last_fallback_chat_ts) >= fallback_chat_sec
            )

            # list_first：同一次持锁内完成「列表+红点判定+标题/聊天截图」，避免两次抢锁间隔
            if list_first_active:
                if use_lock:
                    _reader_yield_before_lock_if_pending()
                    with hold_platform_window_lock(
                        platform=PLATFORM,
                        owner="reader",
                        timeout_sec=_reader_lock_acquire_timeout_sec(),
                        retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                    ) as lock:
                        if not lock.acquired:
                            print("[千牛-Reader] 等待窗口锁超时，本轮跳过扫描")
                            _emit_seg(st, round_t0, _scan_count, "skip_lock_timeout")
                            _sleep_after_scan()
                            continue
                        _scan_count += 1
                        now = time.time()
                        verbose = _scan_count <= 3 or (now - _last_hb >= HB_SEC)
                        _tr = time.perf_counter()
                        lb_bgra, lbw, lbh = capture_conversation_list_bgra(hwnd, cfg)
                        if timing_enabled:
                            st["list_cap"] = (time.perf_counter() - _tr) * 1000.0
                        if not lb_bgra or lbw <= 0 or lbh <= 0:
                            print("[千牛-Reader] 会话列表截图失败，本轮跳过")
                            _emit_seg(st, round_t0, _scan_count, "skip_list_capture")
                            _sleep_after_scan()
                            continue

                        _tr = time.perf_counter()
                        unread_rows = _scan_for_unread_rows(hwnd, cfg, lb_bgra, lbw, lbh)
                        if timing_enabled:
                            st["unread_scan"] = (time.perf_counter() - _tr) * 1000.0

                        if not unread_rows and not force_fallback_chat:
                            if event_log:
                                print(
                                    "[千牛-Reader] 未读/红点: 未检测到红点行（像素扫描）→ list_first 跳过标题/聊天截图"
                                )
                            if verbose:
                                print(
                                    "[千牛-Reader] list_first：列表无未读红点，跳过标题/聊天截图与 OCR"
                                )
                            _emit_seg(st, round_t0, _scan_count, "skip_list_first_no_red")
                            _sleep_after_scan()
                            continue

                        if force_fallback_chat and not unread_rows:
                            _last_fallback_chat_ts = time.time()
                            print(
                                "[千牛-Reader] list_first：兜底全量读（"
                                f"fallback_full_chat_ocr_interval_sec={fallback_chat_sec}s）"
                            )
                        elif event_log and unread_rows:
                            print(
                                "[千牛-Reader] 未读/红点: 检测到红点行 "
                                f"count={len(unread_rows)} 位置={_fmt_unread_rows(unread_rows)} "
                                "→ 列表 OCR 后将判断是否截当前会话标题/聊天区"
                            )

                        if reader_light_pending and not (
                            force_fallback_chat and not unread_rows
                        ):
                            if event_log:
                                print(
                                    "[千牛-Reader] 待发减负: outbound>0，本轮仅列表+红点，"
                                    "跳过标题/聊天截图与 OCR（发送优先）"
                                )
                            _emit_seg(st, round_t0, _scan_count, "reader_light_pending_send")
                            _sleep_after_scan()
                            continue

                        # 有未读时先列表 OCR 对齐，再列表切换+截标题/聊天（见 need_list_first_hdr）
                else:
                    _scan_count += 1
                    now = time.time()
                    verbose = _scan_count <= 3 or (now - _last_hb >= HB_SEC)
                    _tr = time.perf_counter()
                    lb_bgra, lbw, lbh = capture_conversation_list_bgra(hwnd, cfg)
                    if timing_enabled:
                        st["list_cap"] = (time.perf_counter() - _tr) * 1000.0

                    if not lb_bgra or lbw <= 0 or lbh <= 0:
                        print("[千牛-Reader] 会话列表截图失败，本轮跳过")
                        _emit_seg(st, round_t0, _scan_count, "skip_list_capture")
                        _sleep_after_scan()
                        continue

                    _tr = time.perf_counter()
                    unread_rows = _scan_for_unread_rows(hwnd, cfg, lb_bgra, lbw, lbh)
                    if timing_enabled:
                        st["unread_scan"] = (time.perf_counter() - _tr) * 1000.0

                    if not unread_rows and not force_fallback_chat:
                        if event_log:
                            print(
                                "[千牛-Reader] 未读/红点: 未检测到红点行（像素扫描）→ list_first 跳过标题/聊天截图"
                            )
                        if verbose:
                            print(
                                "[千牛-Reader] list_first：列表无未读红点，跳过标题/聊天截图与 OCR"
                            )
                        _emit_seg(st, round_t0, _scan_count, "skip_list_first_no_red")
                        _sleep_after_scan()
                        continue

                    if force_fallback_chat and not unread_rows:
                        _last_fallback_chat_ts = time.time()
                        print(
                            "[千牛-Reader] list_first：兜底全量读（"
                            f"fallback_full_chat_ocr_interval_sec={fallback_chat_sec}s）"
                        )
                    elif event_log and unread_rows:
                        print(
                            "[千牛-Reader] 未读/红点: 检测到红点行 "
                            f"count={len(unread_rows)} 位置={_fmt_unread_rows(unread_rows)} "
                            "→ 列表 OCR 后将判断是否截当前会话标题/聊天区"
                        )

                    if reader_light_pending and not (
                        force_fallback_chat and not unread_rows
                    ):
                        if event_log:
                            print(
                                "[千牛-Reader] 待发减负: outbound>0，本轮仅列表+红点，"
                                "跳过标题/聊天截图与 OCR（发送优先）"
                            )
                        _emit_seg(st, round_t0, _scan_count, "reader_light_pending_send")
                        _sleep_after_scan()
                        continue

            else:
                if reader_light_pending:
                    _scan_count += 1
                    if event_log:
                        print(
                            "[千牛-Reader] 待发减负: outbound>0 且非 list_first，"
                            "本轮跳过读屏与 OCR（发送优先）"
                        )
                    _emit_seg(st, round_t0, _scan_count, "reader_light_pending_non_list_first")
                    _sleep_after_scan()
                    continue
                if use_lock:
                    _reader_yield_before_lock_if_pending()
                    with hold_platform_window_lock(
                        platform=PLATFORM,
                        owner="reader",
                        timeout_sec=_reader_lock_acquire_timeout_sec(),
                        retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                    ) as lock:
                        if not lock.acquired:
                            print("[千牛-Reader] 等待窗口锁超时，本轮跳过扫描")
                            _emit_seg(st, round_t0, _scan_count, "skip_lock_timeout")
                            _sleep_after_scan()
                            continue
                        _scan_count += 1
                        now = time.time()
                        verbose = _scan_count <= 3 or (now - _last_hb >= HB_SEC)

                        _tr = time.perf_counter()
                        hb_b, hb_w, hb_h = capture_contact_header_bgra(hwnd, cfg)
                        cb_b, cb_w, cb_h = _capture_chat_bgra(hwnd, cfg)
                        if multi_enabled and cfg.get("conversation_list_region"):
                            lb_bgra, lbw, lbh = capture_conversation_list_bgra(hwnd, cfg)
                        if timing_enabled:
                            st["snapshot_cap"] = (time.perf_counter() - _tr) * 1000.0
                else:
                    _scan_count += 1
                    now = time.time()
                    verbose = _scan_count <= 3 or (now - _last_hb >= HB_SEC)

                    _tr = time.perf_counter()
                    hb_b, hb_w, hb_h = capture_contact_header_bgra(hwnd, cfg)
                    cb_b, cb_w, cb_h = _capture_chat_bgra(hwnd, cfg)
                    if multi_enabled and cfg.get("conversation_list_region"):
                        lb_bgra, lbw, lbh = capture_conversation_list_bgra(hwnd, cfg)
                    if timing_enabled:
                        st["snapshot_cap"] = (time.perf_counter() - _tr) * 1000.0

                unread_rows = _scan_for_unread_rows(hwnd, cfg, lb_bgra, lbw, lbh)
                if event_log and not list_first_active:
                    print(
                        "[千牛-Reader] 未读/红点: "
                        f"count={len(unread_rows)} 位置={_fmt_unread_rows(unread_rows)} "
                        "（非 list_first，本轮已截标题/聊天/可选列表）"
                    )

            entries: list[ConversationListEntry] = []
            _tr = time.perf_counter()
            if lb_bgra and lbw > 0 and lbh > 0:
                entries = parse_visible_conversation_list_from_bgra(
                    layout_ocr, lb_bgra, lbw, lbh, cfg
                )
            elif multi_enabled and cfg.get("conversation_list_region"):
                entries = parse_visible_conversation_list(layout_ocr, hwnd, cfg)
            if timing_enabled:
                st["list_ocr"] = (time.perf_counter() - _tr) * 1000.0

            unread_entries: list[ConversationListEntry] = []
            if len(unread_rows) > 0 and entries:
                unread_entries = _entries_matching_unread_rows(
                    entries, unread_rows, scan_cfg
                )

            list_first_remaining_unread_plan: Optional[list[ConversationListEntry]] = None

            need_list_first_hdr = (
                list_first_active
                and (len(unread_rows) > 0 or force_fallback_chat)
                and not (
                    reader_light_pending
                    and not (force_fallback_chat and not unread_rows)
                )
            )
            if need_list_first_hdr:

                def _list_first_pre_switch_unread_to_primary() -> None:
                    nonlocal scan_cursor, list_first_remaining_unread_plan
                    if not (list_first_active and unread_entries and len(unread_rows) > 0):
                        return
                    provisional_cur = (stabilizer.current or "").strip() or default_customer_name
                    cands = [
                        e
                        for e in unread_entries
                        if not header_matches_target(provisional_cur, e.name)
                    ]
                    max_sw = max(0, int(scan_cfg.get("max_switch_per_cycle", 4)))
                    if max_sw <= 0:
                        return
                    if cands:
                        rotated_sel = _rotate_entries(cands, scan_cursor)[:max_sw]
                        pre_sw_entry = rotated_sel[0]
                        list_first_remaining_unread_plan = rotated_sel[1:]
                        scan_cursor = (scan_cursor + 1) % max(1, len(cands))
                    else:
                        pre_sw_entry = unread_entries[0]
                        list_first_remaining_unread_plan = []
                    if event_log:
                        rem_n = len(list_first_remaining_unread_plan or [])
                        print(
                            "[千牛-Reader] 未读/先切: "
                            f"list_first 下先列表后台切至 {pre_sw_entry.name!r}，"
                            f"再截标题与聊天区（本轮回合余下切换数={rem_n}）"
                        )
                    sw_nm = switch_to_list_entry(
                        hwnd=hwnd,
                        cfg=cfg,
                        ocr=layout_ocr,
                        entry=pre_sw_entry,
                        timeout_sec=switch_timeout_sec,
                        list_image_height=lbh if lb_bgra and lbh > 0 else None,
                    )
                    if sw_nm:
                        stabilizer.force_set(sw_nm)
                    elif event_log:
                        print(
                            "[千牛-Reader] 未读/先切: 列表点击标题未确认 "
                            f"目标={pre_sw_entry.name!r}，仍截标题区校对"
                        )

                if use_lock:
                    _reader_yield_before_lock_if_pending()
                    with hold_platform_window_lock(
                        platform=PLATFORM,
                        owner="reader:list_after_align",
                        timeout_sec=_reader_lock_acquire_timeout_sec(),
                        retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                    ) as _lk_hdr:
                        if not _lk_hdr.acquired:
                            print("[千牛-Reader] 等待窗口锁超时（标题/聊天截图），本轮跳过")
                            _emit_seg(st, round_t0, _scan_count, "skip_lock_timeout_hdr_chat")
                            _sleep_after_scan()
                            continue
                        _list_first_pre_switch_unread_to_primary()
                        _trc = time.perf_counter()
                        hb_b, hb_w, hb_h = capture_contact_header_bgra(hwnd, cfg)
                        cb_b, cb_w, cb_h = _capture_chat_bgra(hwnd, cfg)
                        if timing_enabled:
                            st["hdr_chat_cap"] = (time.perf_counter() - _trc) * 1000.0
                else:
                    _list_first_pre_switch_unread_to_primary()
                    _trc = time.perf_counter()
                    hb_b, hb_w, hb_h = capture_contact_header_bgra(hwnd, cfg)
                    cb_b, cb_w, cb_h = _capture_chat_bgra(hwnd, cfg)
                    if timing_enabled:
                        st["hdr_chat_cap"] = (time.perf_counter() - _trc) * 1000.0

            if not cb_b or cb_w <= 0 or cb_h <= 0:
                print("[千牛-Reader] 聊天区截图失败，本轮跳过")
                _emit_seg(st, round_t0, _scan_count, "skip_no_chat_region")
                _sleep_after_scan()
                continue

            dbg_parse = cfg.get("debug") or {}
            layout_debug = (not production_mode) and bool(dbg_parse.get("log_parsed_messages", False)) and (
                _scan_count <= 3
            )

            _trt = time.perf_counter()
            header_peer: Optional[str] = None
            if hb_b and hb_w > 0 and hb_h > 0:
                header_peer = peer_name_from_header_bgra(layout_ocr, hb_b, hb_w, hb_h, cfg)
            if entries and (header_peer or "").strip():
                entries = _reconcile_list_entries_with_header_title(
                    entries,
                    header_peer.strip(),
                    trace=bool(event_log),
                )
            if timing_enabled:
                st["title_ocr"] = (time.perf_counter() - _trt) * 1000.0

            if len(unread_rows) > 0 and entries:
                unread_entries = _entries_matching_unread_rows(
                    entries, unread_rows, scan_cfg
                )

            prefer_list_name = bool(scan_cfg.get("prefer_peer_name_from_list", False))
            picked_from_list: Optional[str] = None
            if prefer_list_name:
                picked_from_list = _pick_peer_name_from_list_entries(
                    entries,
                    stabilizer,
                    scan_cfg,
                    trace=bool(event_log),
                )
            if picked_from_list is not None:
                raw_name = picked_from_list
                hp_trim = (header_peer or "").strip()
                # 列表推断依赖 stabilizer 锚点，千牛顶栏才是「当前聊天面板」真源；二者不一致时必须信标题，
                # 否则会出现未读/锚点仍在旧 tb 号、列表命中上一行而聊天已是另一买家 → 消息写入错误会话。
                if hp_trim and not header_matches_target(hp_trim, raw_name):
                    if event_log:
                        print(
                            "[千牛-Reader] 会话名: 列表推断与标题区 OCR 不一致，"
                            f"列表={picked_from_list!r} 标题={hp_trim!r} → 采用标题"
                        )
                    raw_name = hp_trim
            else:
                hp = (header_peer or "").strip()
                if hb_b and hb_w > 0 and hb_h > 0:
                    raw_name = hp or peer_name_from_header_bgra(
                        layout_ocr, hb_b, hb_w, hb_h, cfg
                    )
                else:
                    raw_name = hp or (stabilizer.current or "").strip()

            stabilizer.update(raw_name)
            customer_name = stabilizer.current or default_customer_name
            platform_conv_id = f"{conv_id_prefix}{customer_name}" if stabilizer.current else fallback_conv_id

            if verbose:
                print(
                    f"[千牛-Reader] 扫描#{_scan_count} "
                    f"当前会话={customer_name!r} conv_id={platform_conv_id!r}"
                )
                _last_hb = now

            if verbose and entries:
                print(
                    "[千牛-Reader] 可见会话="
                    + ", ".join(entry.name for entry in entries[:8])
                    + (" ..." if len(entries) > 8 else "")
                )

            use_unread_only_plan = list_first_active and len(unread_rows) > 0
            if use_unread_only_plan and not unread_entries and unread_rows:
                print(
                    "[千牛-Reader] WARN 红点与列表 OCR 行未对齐，本轮回退为当前会话聊天 OCR + 原多会话计划"
                )

            current_has_unread = True
            if list_first_active and len(unread_rows) > 0:
                if unread_entries:
                    current_has_unread = any(
                        header_matches_target(customer_name, e.name) for e in unread_entries
                    )
                else:
                    current_has_unread = True

            if event_log:
                ue_names = [e.name for e in unread_entries[:6]]
                ue_tail = f" 对齐昵称={ue_names}" + (" ..." if len(unread_entries) > 6 else "")
                print(
                    "[千牛-Reader] 未读/对齐: "
                    f"红点行数={len(unread_rows)} 位置={_fmt_unread_rows(unread_rows)} "
                    f"与列表OCR对齐条目数={len(unread_entries)}"
                    f"{ue_tail if unread_entries else ''} "
                    f"当前会话标题={customer_name!r} "
                    f"当前会话是否落在红点行={'是' if current_has_unread else '否'} "
                    f"未读优先计划={'是' if use_unread_only_plan else '否'}"
                )

            sub_cc: dict[str, float] = {}
            if not (list_first_active and len(unread_rows) > 0 and not current_has_unread):
                run_chat_ocr = True
                if (
                    list_first_active
                    and len(unread_rows) > 0
                    and current_has_unread
                    and cb_b
                    and cb_w > 0
                    and cb_h > 0
                    and bool(scan_cfg.get("list_first_same_chat_hash_skip", True))
                ):
                    digest = hashlib.md5(cb_b).hexdigest()
                    force_sec = float(scan_cfg.get("list_first_same_chat_hash_force_sec", 25.0))
                    max_skip = max(1, int(scan_cfg.get("list_first_same_chat_hash_max_skip_rounds", 12)))
                    st_hs = _chat_hash_skip_state
                    now_hs = time.time()
                    force_chat = force_sec > 0 and (now_hs - float(st_hs["last_full_ocr_ts"])) >= force_sec
                    same_conv = str(st_hs["prev_conv"]) == str(platform_conv_id)
                    same_hash = bool(digest) and digest == str(st_hs["prev_digest"])
                    if same_conv and same_hash and not force_chat:
                        st_hs["skip_streak"] = int(st_hs["skip_streak"]) + 1
                        if st_hs["skip_streak"] <= max_skip:
                            run_chat_ocr = False
                            if timing_enabled:
                                st["cur_ocr"] = 0.0
                                st["cur_layout"] = 0.0
                            if event_log:
                                print(
                                    "[千牛-Reader] 聊天 OCR 降频: 红点仍在当前会话且聊天区截图 MD5 未变，"
                                    f"跳过本轮 skip_streak={st_hs['skip_streak']}/{max_skip} "
                                    f"force_sec={force_sec}"
                                )
                            _emit_seg(st, round_t0, _scan_count, "skip_chat_ocr_same_hash")
                        else:
                            st_hs["skip_streak"] = 0
                    else:
                        st_hs["skip_streak"] = 0

                if run_chat_ocr:
                    wrote_this_round += _process_current_chat(
                        ocr=chat_ocr,
                        fallback_ocr=fallback_chat_ocr,
                        hwnd=hwnd,
                        cfg=cfg,
                        detector=detector,
                        customer_name=customer_name,
                        platform_conv_id=platform_conv_id,
                        db=db,
                        left_threshold=left_threshold,
                        right_threshold=right_threshold,
                        merge_y_gap=merge_y_gap,
                        debug_layout=layout_debug,
                        scan_idx=_scan_count,
                        chat_bgra=cb_b,
                        chat_w=cb_w,
                        chat_h=cb_h,
                        header_bgra=hb_b,
                        header_hw=hb_w,
                        header_hh=hb_h,
                        list_bgra=lb_bgra,
                        list_w=lbw,
                        list_h=lbh,
                        _timing=sub_cc if timing_enabled else None,
                    )
                    if (
                        list_first_active
                        and len(unread_rows) > 0
                        and current_has_unread
                        and cb_b
                        and bool(scan_cfg.get("list_first_same_chat_hash_skip", True))
                    ):
                        _chat_hash_skip_state["prev_digest"] = hashlib.md5(cb_b).hexdigest()
                        _chat_hash_skip_state["prev_conv"] = str(platform_conv_id)
                        _chat_hash_skip_state["last_full_ocr_ts"] = time.time()
                    else:
                        _chat_hash_skip_state["prev_digest"] = ""
                        _chat_hash_skip_state["prev_conv"] = ""
                        _chat_hash_skip_state["skip_streak"] = 0
                    if timing_enabled and sub_cc:
                        _merge_chat_timing(st, sub_cc, "cur")
            elif list_first_active and len(unread_rows) > 0 and not current_has_unread:
                _chat_hash_skip_state["prev_digest"] = ""
                _chat_hash_skip_state["prev_conv"] = ""
                _chat_hash_skip_state["skip_streak"] = 0
                if event_log:
                    print(
                        "[千牛-Reader] 未读/跳过: list_first 下存在红点行，但当前会话标题未落在红点行，"
                        f"跳过本会话聊天区 OCR 当前标题={customer_name!r}"
                    )
                if verbose:
                    print(
                        "[千牛-Reader] list_first：当前会话行无红点，跳过当前聊天区 OCR"
                    )

            _multi_this_round = multi_enabled and bool(cfg.get("conversation_list_region"))
            if _multi_this_round and skip_multi_when_pending:
                _pn = _pending_out_count()
                if _pn > 0:
                    if verbose:
                        print(
                            f"[千牛-Reader] 有待发送 outbound={_pn}，"
                            f"本轮跳过多会话扫描（发送优先）"
                        )
                    _multi_this_round = False

            if _multi_this_round:
                if list_first_remaining_unread_plan is not None:
                    plan = list(list_first_remaining_unread_plan)
                elif use_unread_only_plan and unread_entries:
                    plan, scan_cursor = _build_unread_only_switch_plan(
                        unread_entries, customer_name, scan_cfg, scan_cursor
                    )
                else:
                    plan, scan_cursor = _build_scan_plan(
                        entries, unread_rows, customer_name, scan_cfg, scan_cursor
                    )

                if event_log:
                    tgt = [e.name for e in plan[:12]]
                    print(
                        "[千牛-Reader] 多会话计划: "
                        f"本轮回合切换数={len(plan)} "
                        f"目标={tgt}" + (" ..." if len(plan) > 12 else "")
                    )

                if plan:
                    _tr_m = time.perf_counter()
                    if use_lock:
                        _reader_yield_before_lock_if_pending()
                        with hold_platform_window_lock(
                            platform=PLATFORM,
                            owner="reader_multi",
                            timeout_sec=_reader_lock_acquire_timeout_sec(),
                            retry_interval_sec=float(lock_cfg.get("retry_interval_sec", 0.15)),
                        ) as lock2:
                            if not lock2.acquired:
                                print("[千牛-Reader] 多会话切换：等待窗口锁超时，跳过列表轮询")
                            else:
                                for entry in plan:
                                    switch_via = "列表点击"
                                    switched_name = switch_to_list_entry(
                                        hwnd=hwnd,
                                        cfg=cfg,
                                        ocr=layout_ocr,
                                        entry=entry,
                                        timeout_sec=switch_timeout_sec,
                                        list_image_height=lbh if lb_bgra and lbh > 0 else None,
                                    )
                                    if not switched_name:
                                        if event_log:
                                            print(
                                                "[千牛-Reader] 切换会话: "
                                                f"目标={entry.name!r} 列表点击未确认 → 尝试 Ctrl+F"
                                            )
                                        else:
                                            print(
                                                f"[千牛-Reader] 列表点击未确认会话，尝试 Ctrl+F：{entry.name!r}"
                                            )
                                        if not switch_to_conversation_search(
                                            hwnd,
                                            cfg,
                                            layout_ocr,
                                            entry.name,
                                            timeout_sec=switch_timeout_sec,
                                            platform_conv_id="",
                                        ):
                                            print(
                                                f"[千牛-Reader] Ctrl+F 切换仍失败，跳过 {entry.name!r}"
                                            )
                                            if event_log:
                                                print(
                                                    "[千牛-Reader] 切换会话: "
                                                    f"目标={entry.name!r} 结果=失败（Ctrl+F 未找到）"
                                                )
                                            continue
                                        switched_name = entry.name
                                        switch_via = "Ctrl+F"
                                    stabilizer.force_set(switched_name)
                                    switched_conv_id = f"{conv_id_prefix}{switched_name}"
                                    name_ok = header_matches_target(switched_name, entry.name)
                                    print(
                                        "[千牛-Reader] 切换会话: "
                                        f"目标={entry.name!r} 结果=成功 途径={switch_via} "
                                        f"实际标题={switched_name!r} 与目标匹配={'是' if name_ok else '否'}"
                                    )
                                    sub_m: dict[str, float] = {}
                                    wrote_this_round += _process_current_chat(
                                        ocr=chat_ocr,
                                        fallback_ocr=fallback_chat_ocr,
                                        hwnd=hwnd,
                                        cfg=cfg,
                                        detector=detector,
                                        customer_name=switched_name,
                                        platform_conv_id=switched_conv_id,
                                        db=db,
                                        left_threshold=left_threshold,
                                        right_threshold=right_threshold,
                                        merge_y_gap=merge_y_gap,
                                        debug_layout=False,
                                        scan_idx=_scan_count,
                                        _timing=sub_m if timing_enabled else None,
                                    )
                                    if timing_enabled and sub_m:
                                        _merge_chat_timing(st, sub_m, "multi")
                    else:
                        for entry in plan:
                            switch_via = "列表点击"
                            switched_name = switch_to_list_entry(
                                hwnd=hwnd,
                                cfg=cfg,
                                ocr=layout_ocr,
                                entry=entry,
                                timeout_sec=switch_timeout_sec,
                                list_image_height=lbh if lb_bgra and lbh > 0 else None,
                            )
                            if not switched_name:
                                if event_log:
                                    print(
                                        "[千牛-Reader] 切换会话: "
                                        f"目标={entry.name!r} 列表点击未确认 → 尝试 Ctrl+F"
                                    )
                                else:
                                    print(
                                        f"[千牛-Reader] 列表点击未确认会话，尝试 Ctrl+F：{entry.name!r}"
                                    )
                                if not switch_to_conversation_search(
                                    hwnd,
                                    cfg,
                                    layout_ocr,
                                    entry.name,
                                    timeout_sec=switch_timeout_sec,
                                    platform_conv_id="",
                                ):
                                    print(
                                        f"[千牛-Reader] Ctrl+F 切换仍失败，跳过 {entry.name!r}"
                                    )
                                    if event_log:
                                        print(
                                            "[千牛-Reader] 切换会话: "
                                            f"目标={entry.name!r} 结果=失败（Ctrl+F 未找到）"
                                        )
                                    continue
                                switched_name = entry.name
                                switch_via = "Ctrl+F"
                            stabilizer.force_set(switched_name)
                            switched_conv_id = f"{conv_id_prefix}{switched_name}"
                            name_ok = header_matches_target(switched_name, entry.name)
                            print(
                                "[千牛-Reader] 切换会话: "
                                f"目标={entry.name!r} 结果=成功 途径={switch_via} "
                                f"实际标题={switched_name!r} 与目标匹配={'是' if name_ok else '否'}"
                            )
                            sub_m = {}
                            wrote_this_round += _process_current_chat(
                                ocr=chat_ocr,
                                fallback_ocr=fallback_chat_ocr,
                                hwnd=hwnd,
                                cfg=cfg,
                                detector=detector,
                                customer_name=switched_name,
                                platform_conv_id=switched_conv_id,
                                db=db,
                                left_threshold=left_threshold,
                                right_threshold=right_threshold,
                                merge_y_gap=merge_y_gap,
                                debug_layout=False,
                                scan_idx=_scan_count,
                                _timing=sub_m if timing_enabled else None,
                            )
                            if timing_enabled and sub_m:
                                _merge_chat_timing(st, sub_m, "multi")
                    if timing_enabled:
                        st["multi_block"] = (time.perf_counter() - _tr_m) * 1000.0

            if (hb_b and cb_b) or wrote_this_round > 0:
                _last_fallback_chat_ts = time.time()

            _emit_seg(st, round_t0, _scan_count, "")

        except Exception as e:
            print(f"[千牛-Reader] 失败: {e}")

        _tslp = time.perf_counter()
        _sleep_after_scan(wrote_this_round)
        if timing_enabled:
            print(
                f"[千牛-Reader] 轮询后休眠 sleep={(time.perf_counter() - _tslp) * 1000.0:.0f}ms"
            )


if __name__ == "__main__":
    run_reader()
