from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import time
import warnings
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
    from rpa.platforms.qianniu.accounts import (
        AccountTab as ProductionAccountTab,
        detect_account_candidates as detect_production_account_candidates,
    )
    from rpa.platforms.qianniu.config import AppConfig, load_config
    from rpa.platforms.qianniu.detector import QianniuDetector, WindowCandidate
    from rpa.platforms.qianniu.reader import collect_control_texts
    from rpa.platforms.qianniu.sessions import SessionItem, extract_session_items
    from rpa.platforms.qianniu.uia import (
        control_from_hwnd,
        enum_all_top_level_windows,
        safe_prop,
        safe_rect,
        safe_rect_tuple,
        trim,
        uia_guard,
        walk_controls,
    )
else:
    from .accounts import (
        AccountTab as ProductionAccountTab,
        detect_account_candidates as detect_production_account_candidates,
    )
    from .config import AppConfig, load_config
    from .detector import QianniuDetector, WindowCandidate
    from .reader import collect_control_texts
    from .sessions import SessionItem, extract_session_items
    from .uia import (
        control_from_hwnd,
        enum_all_top_level_windows,
        safe_prop,
        safe_rect,
        safe_rect_tuple,
        trim,
        uia_guard,
        walk_controls,
    )


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


DEFAULT_ACCOUNT_ID = "local_qianniu"
DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parents[2] / "logs" / "qianniu" / "account_probe"
ProgressLogger = Callable[[str], None]

BLOCKED_TAB_TEXTS = {
    "",
    "+",
    "x",
    "X",
    "×",
    "客服",
    "设置",
    "展开",
    "操作指南",
    "关闭",
    "最小化",
    "最大化",
    "还原",
    "消息",
    "工作台",
    "进店",
    "工单",
    "打单工具",
    "足迹",
    "推荐",
    "邀请关注",
    "邀请入会",
    "邀请入群",
    "发优惠券",
    "添加备注",
}

BLOCKED_TEXT_FRAGMENTS = {
    "UIWindow.",
    "web_chat-packer",
    "http://",
    "https://",
}


@dataclass(frozen=True)
class AccountCandidate:
    account_id: str
    display_name: str
    selected: bool
    rect: str
    rect_tuple: tuple[int, int, int, int] | None
    source: str
    confidence: float
    raw_texts: list[str]
    automation_id: str
    class_name: str
    control_type: str
    depth: int = 0
    index: int = 0
    visual_blue_ratio: float = 0.0
    has_unread_hint: bool = False
    unread_badge_text: str = ""
    unread_elapsed_text: str = ""
    unread_hint_rect: tuple[int, int, int, int] | None = None
    unread_score: float = 0.0


@dataclass(frozen=True)
class SwitchProbeResult:
    account_id: str
    display_name: str
    clicked: bool
    click_method: str
    active_account_id: str
    active_display_name: str
    active_verified: bool
    session_count: int
    session_titles: list[str]
    elapsed_ms: float
    error: str = ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Probe Qianniu multi-shop account tabs without touching main RPA flow.")
    parser.add_argument("--window-index", type=int, default=1, help="1-based Qianniu window index sorted by detector score.")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    parser.add_argument("--save-screenshots", action="store_true", help="Save top-tab screenshot crops for diagnosis.")
    parser.add_argument("--dump-uia", action="store_true", help="Save a JSONL UIA tree summary for the selected window.")
    parser.add_argument("--output-dir", default="", help="Diagnostic output directory. Defaults to python/rpa/logs/qianniu/account_probe.")
    parser.add_argument("--max-depth", type=int, default=10, help="Maximum UIA depth to scan for top account tabs.")
    parser.add_argument("--max-nodes", type=int, default=3000, help="Maximum UIA nodes to scan.")
    parser.add_argument("--min-confidence", type=float, default=0.35, help="Minimum candidate confidence to report.")
    parser.add_argument("--candidate-limit", type=int, default=20, help="Maximum account candidates to return.")
    parser.add_argument("--switch-each", action="store_true", help="Actually click each detected account tab and verify switch + session scan.")
    parser.add_argument("--switch-account", default="", help="Actually click one detected account tab by display name or account_id.")
    parser.add_argument("--switch-unread", action="store_true", help="Actually click the first non-selected account tab with an unread top-tab hint.")
    parser.add_argument("--switch-wait", type=float, default=0.7, help="Seconds to wait after clicking an account tab.")
    parser.add_argument("--no-activate", action="store_true", help="Do not bring Qianniu window to foreground before clicking.")
    parser.add_argument("--no-restore", action="store_true", help="Do not restore original active account after --switch-each.")
    parser.add_argument("--quiet", action="store_true", help="Suppress progress logs written to stderr.")
    parser.add_argument("--windows-only", action="store_true", help="Only list Win32 Qianniu windows; do not enter UIA probing.")
    args = parser.parse_args()
    switch_account_query = normalize_candidate_text(args.switch_account)
    switch_modes = int(bool(args.switch_each)) + int(bool(switch_account_query)) + int(bool(args.switch_unread))
    if switch_modes > 1:
        parser.error("--switch-each, --switch-account, and --switch-unread cannot be used together")

    output_dir = Path(args.output_dir) if args.output_dir else DEFAULT_OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    progress = make_progress_logger(enabled=not args.quiet)
    detector = QianniuDetector(load_config())
    try:
        progress("starting Qianniu account probe; JSON result is written to stdout, progress to stderr")
        if args.windows_only:
            progress("running windows-only mode; UIAutomation will not be initialized")
            report = build_windows_only_report(detector, window_index=max(1, int(args.window_index or 1)), progress=progress)
            if args.json:
                print(json.dumps(report, ensure_ascii=False, indent=2))
            else:
                print_windows_only_report(report)
            return 0 if report.get("ok") else 1

        progress("initializing UIAutomation COM context")
        with uia_guard("qianniu_account_probe"):
            progress("UIAutomation context ready")
            report = build_probe_report(
                detector,
                window_index=max(1, int(args.window_index or 1)),
                output_dir=output_dir,
                save_screenshots=bool(args.save_screenshots),
                dump_uia=bool(args.dump_uia),
                max_depth=max(1, int(args.max_depth or 10)),
                max_nodes=max(1, int(args.max_nodes or 3000)),
                min_confidence=max(0.0, float(args.min_confidence or 0.0)),
                candidate_limit=max(1, int(args.candidate_limit or 20)),
                progress=progress,
            )

            if (args.switch_each or switch_account_query or args.switch_unread) and report.get("ok") and report.get("accounts"):
                accounts = [
                    account_from_payload(item)
                    for item in report.get("accounts", [])
                    if isinstance(item, dict) and item.get("source") != "synthetic_single_account"
                ]
                switch_hwnd = int((report.get("window") or {}).get("hwnd") or 0)
                if switch_hwnd:
                    progress(f"account switch will reuse selected window hwnd=0x{switch_hwnd:X}")
                else:
                    progress("account switch has no selected window hwnd; click verification may fail fast")

                if args.switch_unread:
                    target = first_unread_account(accounts)
                    report["switch_unread"] = True
                    if target is None:
                        report["switch_results"] = []
                        report["switch_note"] = "no non-selected account tab has unread hint"
                        progress("switch-unread requested but no non-selected account tab has unread hint")
                    else:
                        progress(
                            f"switch-unread target matched account_id={target.account_id} "
                            f"name={target.display_name} unread_score={target.unread_score}"
                        )
                        report["switch_target"] = {**account_payload(target), "match_mode": "first_unread_hint"}
                        switch_results = switch_each_account(
                            detector,
                            [target],
                            hwnd=switch_hwnd,
                            wait_seconds=max(0.1, float(args.switch_wait or 0.7)),
                            activate=not args.no_activate,
                            restore_original=False,
                            progress=progress,
                        )
                        report["switch_results"] = [asdict(item) for item in switch_results]
                        if not switch_results:
                            report["switch_error"] = "switch_unread_no_result"
                        elif switch_results[0].error:
                            report["switch_error"] = switch_results[0].error
                        elif not switch_results[0].clicked:
                            report["switch_error"] = "switch_unread_click_failed"
                        elif not switch_results[0].active_verified:
                            report["switch_error"] = "switch_unread_not_verified"
                elif switch_account_query:
                    target, match_mode, match_error = resolve_switch_account(accounts, switch_account_query)
                    report["switch_account_query"] = switch_account_query
                    if target is None:
                        report["switch_results"] = []
                        report["switch_error"] = match_error
                        progress(f"switch-account target unavailable query={switch_account_query} error={match_error}")
                    else:
                        progress(
                            f"switch-account target matched mode={match_mode} "
                            f"account_id={target.account_id} name={target.display_name}"
                        )
                        report["switch_target"] = {**account_payload(target), "match_mode": match_mode}
                        switch_results = switch_each_account(
                            detector,
                            [target],
                            hwnd=switch_hwnd,
                            wait_seconds=max(0.1, float(args.switch_wait or 0.7)),
                            activate=not args.no_activate,
                            restore_original=False,
                            progress=progress,
                        )
                        report["switch_results"] = [asdict(item) for item in switch_results]
                        if not switch_results:
                            report["switch_error"] = "switch_account_no_result"
                        elif switch_results[0].error:
                            report["switch_error"] = switch_results[0].error
                        elif not switch_results[0].clicked:
                            report["switch_error"] = "switch_account_click_failed"
                        elif not switch_results[0].active_verified:
                            report["switch_error"] = "switch_account_not_verified"
                else:
                    report["switch_results"] = [
                        asdict(item)
                        for item in switch_each_account(
                            detector,
                            accounts,
                            hwnd=switch_hwnd,
                            wait_seconds=max(0.1, float(args.switch_wait or 0.7)),
                            activate=not args.no_activate,
                            restore_original=not args.no_restore,
                            progress=progress,
                        )
                    ]
            elif args.switch_each or switch_account_query or args.switch_unread:
                report["switch_results"] = []
                if report.get("ok"):
                    report["switch_note"] = "no real account tab candidates to click"
                    if switch_account_query:
                        report["switch_error"] = "switch_account_no_candidates"
                    if args.switch_unread:
                        report["switch_note"] = "no real account tab candidates to inspect for unread hints"
                progress("account switch requested but no real account tab candidates are available")
    except KeyboardInterrupt:
        progress("interrupted by Ctrl+C")
        report = {
            "ok": False,
            "error": "interrupted",
            "detail": "Stopped by Ctrl+C",
            "process_name": detector.q.process_name,
        }
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print_human_report(report)
        return 130

    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_human_report(report)
    return 0 if report.get("ok") and not report.get("switch_error") else 1


def build_probe_report(
    detector: QianniuDetector,
    *,
    window_index: int,
    output_dir: Path,
    save_screenshots: bool,
    dump_uia: bool,
    max_depth: int,
    max_nodes: int,
    min_confidence: float,
    candidate_limit: int,
    progress: ProgressLogger | None = None,
) -> dict[str, Any]:
    started_at = time.perf_counter()
    logs: list[str] = []
    log = progress or noop_progress
    log(f"enumerating Qianniu windows for process={detector.q.process_name} using Win32-only scan")
    windows, logs = find_window_candidates_win32_only(detector, progress=log)
    log(f"window enumeration done; candidates={len(windows)}")
    window = select_window(windows, window_index)
    if window is None:
        log("no usable Qianniu window candidate found")
        return {
            "ok": False,
            "error": "qianniu_window_not_found",
            "process_name": detector.q.process_name,
            "logs": logs,
        }

    log(f"selected window hwnd=0x{window.hwnd:X} pid={window.pid} class={window.class_name or '-'} score={window.score}")
    log("resolving UIA root from selected window handle")
    root = control_from_hwnd(window.hwnd)
    if root is None:
        log("UIA root unavailable for selected window")
        return {
            "ok": False,
            "error": "uia_root_unavailable",
            "process_name": detector.q.process_name,
            "window": window_payload(window),
            "logs": logs,
        }

    run_id = make_run_id()
    screenshot_path = ""
    top_image = None
    if save_screenshots:
        log(f"capturing top account-tab screenshot into {output_dir}")
        screenshot_path, top_image = save_top_screenshot(root, output_dir, run_id)
        log(f"top screenshot {'saved to ' + screenshot_path if screenshot_path else 'not available'}")
    else:
        log("capturing in-memory top image for selected-tab color scoring")
        top_image = grab_top_image(root)
        log(f"in-memory top image {'available' if top_image is not None else 'not available'}")

    uia_path = ""
    rows: list[dict[str, Any]] = []
    if dump_uia:
        log(f"dumping UIA tree max_depth={max_depth} max_nodes={max_nodes}")
        rows = collect_tree_rows(root, max_depth=max_depth, max_nodes=max_nodes, progress=log)
        uia_path = str(write_uia_jsonl(rows, output_dir, run_id))
        log(f"UIA tree written nodes={len(rows)} path={uia_path}")

    log(f"detecting account tab candidates max_depth={max_depth} max_nodes={max_nodes}")
    candidates = detect_account_candidates(
        root,
        top_image=top_image,
        max_depth=max_depth,
        max_nodes=max_nodes,
        min_confidence=min_confidence,
        limit=candidate_limit,
        progress=log,
    )
    log(f"account candidate detection done; candidates={len(candidates)}")
    synthetic = False
    if not candidates:
        log("no account tab candidates found; checking current chat for single-account fallback")
        chat_handle = detector.find_current_chat()
        if chat_handle is not None:
            synthetic = True
            candidates = [synthetic_single_account()]
            log("single-account fallback is available")
        else:
            log("single-account fallback unavailable because current chat was not found")

    active = selected_account(candidates)
    unread_accounts = [item for item in candidates if item.has_unread_hint]
    elapsed_ms = (time.perf_counter() - started_at) * 1000.0
    log(f"probe complete ok={bool(candidates)} accounts={len(candidates)} elapsed_ms={elapsed_ms:.1f}")
    return {
        "ok": bool(candidates),
        "process_name": detector.q.process_name,
        "window": window_payload(window),
        "account_count": len(candidates),
        "accounts": [account_payload(item) for item in candidates],
        "unread_account_count": len(unread_accounts),
        "unread_accounts": [account_payload(item) for item in unread_accounts],
        "active_account_id": active.account_id if active else "",
        "active_display_name": active.display_name if active else "",
        "synthetic_single_account": synthetic,
        "diagnostics": {
            "run_id": run_id,
            "elapsed_ms": round(elapsed_ms, 1),
            "screenshot_path": screenshot_path,
            "uia_jsonl_path": uia_path,
            "uia_node_count": len(rows),
            "logs": logs,
        },
    }


def build_windows_only_report(
    detector: QianniuDetector,
    *,
    window_index: int,
    progress: ProgressLogger | None = None,
) -> dict[str, Any]:
    started_at = time.perf_counter()
    log = progress or noop_progress
    windows, logs = find_window_candidates_win32_only(detector, progress=log)
    selected = select_window(windows, window_index)
    return {
        "ok": bool(windows),
        "process_name": detector.q.process_name,
        "window_count": len(windows),
        "selected_window": window_payload(selected) if selected is not None else {},
        "windows": [window_payload(item) for item in windows],
        "diagnostics": {
            "elapsed_ms": round((time.perf_counter() - started_at) * 1000.0, 1),
            "logs": logs,
        },
        "error": "" if windows else "qianniu_window_not_found",
    }


def find_window_candidates_win32_only(
    detector: QianniuDetector,
    *,
    progress: ProgressLogger | None = None,
) -> tuple[list[WindowCandidate], list[str]]:
    log = progress or noop_progress
    logs: list[str] = []
    log("querying Qianniu process ids")
    process_ids = set(detector.find_process_ids())
    logs.append(f"process ids: {sorted(process_ids) or 'not found'}")
    log(f"process id query done; pids={sorted(process_ids) or 'not found'}")

    log("enumerating top-level windows via Win32 EnumWindows")
    windows = enum_all_top_level_windows()
    logs.append(f"top-level windows enumerated: {len(windows)}")
    log(f"top-level Win32 enumeration done; windows={len(windows)}")

    matches: list[tuple[WindowCandidate, int]] = []
    for item in windows:
        if process_ids and item.pid not in process_ids:
            continue
        if not item.visible:
            continue
        score, reason = detector.score_window_candidate(item.class_name, item.title)
        if score <= 0:
            score = 1
            reason = reason or "pid-match"
        area = window_area(item.rect)
        logs.append(
            f"pid-match hwnd=0x{item.hwnd:X} pid={item.pid} visible={item.visible} "
            f"class={item.class_name} score={score} area={area} title={trim(item.title, 120)}"
        )
        matches.append(
            (
                WindowCandidate(
                    hwnd=item.hwnd,
                    pid=item.pid,
                    class_name=item.class_name,
                    title=item.title,
                    score=score,
                    reason=reason,
                ),
                area,
            )
        )

    matches.sort(key=lambda pair: (pair[0].score, pair[1]), reverse=True)
    log(f"Qianniu pid-matched visible windows={len(matches)}")
    return [item for item, _area in matches], logs


def window_area(rect: tuple[int, int, int, int] | None) -> int:
    if not rect:
        return 0
    left, top, right, bottom = rect
    return max(0, int(right) - int(left)) * max(0, int(bottom) - int(top))


def detect_account_candidates(
    root: Any,
    *,
    top_image: Any | None,
    max_depth: int,
    max_nodes: int,
    min_confidence: float,
    limit: int,
    progress: ProgressLogger | None = None,
) -> list[AccountCandidate]:
    detected = detect_production_account_candidates(
        root,
        top_image=top_image,
        max_depth=max_depth,
        max_nodes=max_nodes,
        min_confidence=min_confidence,
        limit=limit,
        progress=progress,
    )
    return [account_candidate_from_production(item) for item in detected]


def account_candidate_from_production(account: ProductionAccountTab) -> AccountCandidate:
    return AccountCandidate(
        account_id=account.account_id,
        display_name=account.display_name,
        selected=account.selected,
        rect=account.rect,
        rect_tuple=account.rect_tuple,
        source=account.source,
        confidence=account.confidence,
        raw_texts=list(account.raw_texts),
        automation_id=account.automation_id,
        class_name=account.class_name,
        control_type=account.control_type,
        depth=account.depth,
        index=account.index,
        visual_blue_ratio=account.visual_blue_ratio,
        has_unread_hint=account.has_unread_hint,
        unread_badge_text=account.unread_badge_text,
        unread_elapsed_text=account.unread_elapsed_text,
        unread_hint_rect=account.unread_hint_rect,
        unread_score=account.unread_score,
    )


def score_account_candidate(
    control: Any,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
    display_name: str,
    depth: int,
    visual_blue_ratio: float,
) -> float:
    left, top, right, bottom = rect
    root_left, root_top, root_right, root_bottom = root_rect
    root_width = max(1, root_right - root_left)
    width = max(1, right - left)
    height = max(1, bottom - top)
    score = 0.0

    if root_top - 4 <= top <= root_top + 54:
        score += 0.18
    if 35 <= width <= min(320, root_width * 0.45):
        score += 0.16
    if 18 <= height <= 46:
        score += 0.14
    if left <= root_left + root_width * 0.55:
        score += 0.12
    if visual_blue_ratio >= 0.12:
        score += 0.18

    control_text = " ".join(
        [
            safe_prop(control, "AutomationId"),
            safe_prop(control, "ClassName"),
            safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
            safe_prop(control, "FrameworkId"),
        ]
    ).lower()
    if "tab" in control_text:
        score += 0.18
    if "button" in control_text:
        score += 0.08
    if ":" in display_name or "：" in display_name:
        score += 0.08
    if re.search(r"[\u4e00-\u9fffA-Za-z0-9]", display_name):
        score += 0.08
    if depth > 8:
        score -= 0.08
    return max(0.0, min(score, 1.0))


def collect_texts_for_candidate(control: Any) -> list[str]:
    values: list[str] = []
    for value in collect_control_texts(control):
        normalized = normalize_candidate_text(value)
        if normalized and normalized not in values:
            values.append(normalized)
    for attr in ("Name", "Value", "LegacyIAccessibleName", "HelpText"):
        normalized = normalize_candidate_text(safe_prop(control, attr))
        if normalized and normalized not in values:
            values.append(normalized)
    return values


def choose_account_display_name(texts: list[str]) -> str:
    for text in texts:
        candidate = normalize_account_display_name(text)
        if looks_like_account_tab_text(candidate):
            return candidate
    return ""


def normalize_candidate_text(value: Any) -> str:
    text = str(value or "").replace("\r", " ").replace("\n", " ")
    text = re.sub(r"\s+", " ", text).strip()
    return text


def normalize_account_display_name(value: Any) -> str:
    text = normalize_candidate_text(value)
    text = text.strip(" \t\r\n")
    text = re.sub(r"\s*[×xX]\s*$", "", text).strip()
    text = re.sub(r"\s+", " ", text).strip()
    return text[:80]


def looks_like_account_tab_text(value: str) -> bool:
    text = normalize_account_display_name(value)
    if not text or text in BLOCKED_TAB_TEXTS:
        return False
    if any(fragment in text for fragment in BLOCKED_TEXT_FRAGMENTS):
        return False
    if len(text) < 2 or len(text) > 80:
        return False
    if text.isdigit():
        return False
    if re.fullmatch(r"[\W_]+", text, flags=re.UNICODE):
        return False
    if re.fullmatch(r"\d{1,2}:\d{2}(:\d{2})?", text):
        return False
    if text in {"true", "false", "None"}:
        return False
    return True


def stable_account_id(display_name: str) -> str:
    normalized = normalize_account_display_name(display_name).lower()
    digest = hashlib.sha1(normalized.encode("utf-8", errors="ignore")).hexdigest()[:12]
    return f"qnacct_{digest}"


def synthetic_single_account() -> AccountCandidate:
    return AccountCandidate(
        account_id=DEFAULT_ACCOUNT_ID,
        display_name=DEFAULT_ACCOUNT_ID,
        selected=True,
        rect="-",
        rect_tuple=None,
        source="synthetic_single_account",
        confidence=0.3,
        raw_texts=[],
        automation_id="",
        class_name="",
        control_type="",
    )


def is_top_tab_rect(rect: tuple[int, int, int, int], root_rect: tuple[int, int, int, int]) -> bool:
    left, top, right, bottom = rect
    root_left, root_top, root_right, root_bottom = root_rect
    if right <= left or bottom <= top:
        return False
    root_width = max(1, root_right - root_left)
    height = max(1, bottom - top)
    width = max(1, right - left)
    top_limit = root_top + 64
    if top < root_top - 12 or top > top_limit:
        return False
    if left < root_left - 4 or right > root_right + 4:
        return False
    if left > root_left + root_width * 0.72:
        return False
    if width < 24 or width > min(420, root_width):
        return False
    if height < 12 or height > 70:
        return False
    return True


def is_selected_control(control: Any) -> bool:
    try:
        getter = getattr(control, "GetSelectionItemPattern", None)
        if callable(getter):
            pattern = getter()
            if pattern:
                selected = getattr(pattern, "IsSelected", None)
                if callable(selected):
                    selected = selected()
                if selected is not None:
                    return bool(selected)
    except Exception:
        pass
    value = safe_prop(control, "IsSelected").strip().lower()
    return value in {"1", "true", "yes", "selected"}


def selected_account(candidates: list[AccountCandidate]) -> AccountCandidate | None:
    selected = [item for item in candidates if item.selected]
    if selected:
        return sorted(selected, key=lambda item: (-item.confidence, rect_sort_key(item.rect_tuple)))[0]
    return candidates[0] if candidates else None


def first_unread_account(candidates: list[AccountCandidate]) -> AccountCandidate | None:
    unread = [item for item in candidates if item.has_unread_hint and not item.selected]
    if not unread:
        return None
    return sorted(unread, key=lambda item: (-item.unread_score, rect_sort_key(item.rect_tuple)))[0]


def resolve_switch_account(accounts: list[AccountCandidate], query: str) -> tuple[AccountCandidate | None, str, str]:
    normalized_query = normalize_candidate_text(query)
    if not normalized_query:
        return None, "", "switch_account_empty_query"
    query_display = normalize_account_display_name(normalized_query)
    query_key = normalized_query.casefold()
    query_display_key = query_display.casefold()

    for account in accounts:
        if account.account_id.casefold() == query_key:
            return account, "account_id_exact", ""

    exact_display_matches = [
        account
        for account in accounts
        if normalize_account_display_name(account.display_name).casefold() == query_display_key
    ]
    if len(exact_display_matches) == 1:
        return exact_display_matches[0], "display_name_exact", ""
    if len(exact_display_matches) > 1:
        return None, "", switch_account_ambiguous_error(normalized_query, exact_display_matches)

    contains_matches = [
        account
        for account in accounts
        if account_query_matches(account, query_key, query_display_key)
    ]
    if len(contains_matches) == 1:
        return contains_matches[0], "contains", ""
    if len(contains_matches) > 1:
        return None, "", switch_account_ambiguous_error(normalized_query, contains_matches)

    available = ", ".join(account.display_name for account in accounts[:10])
    return None, "", f"switch_account_not_found query={normalized_query} available=[{available}]"


def account_query_matches(account: AccountCandidate, query_key: str, query_display_key: str) -> bool:
    if query_key and query_key in account.account_id.casefold():
        return True
    text_values = [account.display_name, *account.raw_texts]
    for value in text_values:
        normalized = normalize_account_display_name(value).casefold()
        if query_display_key and query_display_key in normalized:
            return True
    return False


def switch_account_ambiguous_error(query: str, accounts: list[AccountCandidate]) -> str:
    names = ", ".join(account.display_name for account in accounts[:10])
    return f"switch_account_ambiguous query={query} matches=[{names}]"


def dedupe_account_candidates(candidates: list[AccountCandidate]) -> list[AccountCandidate]:
    ordered = sorted(
        candidates,
        key=lambda item: (
            -account_candidate_priority(item),
            -item.selected,
            rect_sort_key(item.rect_tuple),
            -item.confidence,
        ),
    )
    result: list[AccountCandidate] = []
    for item in ordered:
        if any(item.account_id == existing.account_id or rects_overlap(item.rect_tuple, existing.rect_tuple) for existing in result):
            continue
        result.append(item)
    return sorted(result, key=lambda item: rect_sort_key(item.rect_tuple))


def account_candidate_priority(item: AccountCandidate) -> int:
    control_text = f"{item.control_type} {item.class_name} {item.automation_id}".lower()
    if "tabitem" in control_text:
        return 30
    if "tabbar" in control_text or "tabcontrol" in control_text or "tab" in control_text:
        return 20
    return 10


def rect_sort_key(rect: tuple[int, int, int, int] | None) -> tuple[int, int, int]:
    if rect is None:
        return (10**9, 10**9, 10**9)
    left, top, right, bottom = rect
    return (top // 8, left, right - left + bottom - top)


def rects_overlap(left: tuple[int, int, int, int] | None, right: tuple[int, int, int, int] | None) -> bool:
    if left is None or right is None:
        return False
    overlap_width = min(left[2], right[2]) - max(left[0], right[0])
    overlap_height = min(left[3], right[3]) - max(left[1], right[1])
    return overlap_width > 0 and overlap_height > 0


def account_rect_blue_ratio(
    top_image: Any | None,
    rect: tuple[int, int, int, int],
    root_rect: tuple[int, int, int, int],
) -> float:
    if top_image is None:
        return 0.0
    try:
        left, top, right, bottom = rect
        root_left, root_top, _root_right, _root_bottom = root_rect
        local_box = (
            max(0, left - root_left),
            max(0, top - root_top),
            max(0, right - root_left),
            max(0, bottom - root_top),
        )
        if local_box[2] <= local_box[0] or local_box[3] <= local_box[1]:
            return 0.0
        image = top_image.crop(local_box).convert("RGB")
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            pixels = list(image.getdata())
        if not pixels:
            return 0.0
        blue = 0
        for r, g, b in pixels:
            if b >= 150 and g >= 80 and r <= 110 and b >= r * 1.5 and b >= g * 1.05:
                blue += 1
        return blue / max(1, len(pixels))
    except Exception:
        return 0.0


def grab_top_image(root: Any) -> Any | None:
    rect = safe_rect_tuple(root)
    if not rect:
        return None
    try:
        from PIL import ImageGrab

        left, top, right, bottom = rect
        top_bottom = min(bottom, top + max(56, min(100, int((bottom - top) * 0.12))))
        return ImageGrab.grab(bbox=(left, top, right, top_bottom)).convert("RGB")
    except Exception:
        return None


def save_top_screenshot(root: Any, output_dir: Path, run_id: str) -> tuple[str, Any | None]:
    image = grab_top_image(root)
    if image is None:
        return "", None
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / f"{run_id}_top.png"
    try:
        image.save(path)
        return str(path), image
    except Exception:
        return "", image


def switch_each_account(
    detector: QianniuDetector,
    accounts: list[AccountCandidate],
    *,
    hwnd: int,
    wait_seconds: float,
    activate: bool,
    restore_original: bool,
    progress: ProgressLogger | None = None,
) -> list[SwitchProbeResult]:
    if not accounts:
        return []
    log = progress or noop_progress
    original = selected_account(accounts)
    results: list[SwitchProbeResult] = []
    for index, account in enumerate(accounts, start=1):
        started_at = time.perf_counter()
        clicked = False
        method = ""
        error = ""
        try:
            log(f"switching account {index}/{len(accounts)} account_id={account.account_id} name={account.display_name}")
            clicked, method = click_account_tab(account, hwnd=hwnd, activate=activate, progress=log)
            log(f"clicked account tab method={method}; waiting {wait_seconds:.2f}s for UI switch")
            time.sleep(wait_seconds)
            log("verifying active account after switch")
            fresh = detect_current_accounts(detector, hwnd=hwnd, progress=log)
            active = selected_account(fresh)
            log("reading visible sessions after account switch")
            sessions = read_visible_sessions_from_window(detector, hwnd=hwnd, limit=20, progress=log)
            log(
                f"switch result verified={bool(active and active.account_id == account.account_id)} "
                f"active={active.display_name if active else '-'} sessions={len(sessions)}"
            )
            results.append(
                SwitchProbeResult(
                    account_id=account.account_id,
                    display_name=account.display_name,
                    clicked=clicked,
                    click_method=method,
                    active_account_id=active.account_id if active else "",
                    active_display_name=active.display_name if active else "",
                    active_verified=bool(active and active.account_id == account.account_id),
                    session_count=len(sessions),
                    session_titles=[item.title for item in sessions[:10]],
                    elapsed_ms=round((time.perf_counter() - started_at) * 1000.0, 1),
                )
            )
        except Exception as exc:
            error = str(exc)
            log(f"switch account failed account_id={account.account_id} error={error}")
            results.append(
                SwitchProbeResult(
                    account_id=account.account_id,
                    display_name=account.display_name,
                    clicked=clicked,
                    click_method=method,
                    active_account_id="",
                    active_display_name="",
                    active_verified=False,
                    session_count=0,
                    session_titles=[],
                    elapsed_ms=round((time.perf_counter() - started_at) * 1000.0, 1),
                    error=error,
                )
            )
    if restore_original and original is not None:
        try:
            log(f"restoring original active account account_id={original.account_id} name={original.display_name}")
            fresh = detect_current_accounts(detector, hwnd=hwnd, progress=log)
            target = next((item for item in fresh if item.account_id == original.account_id), None)
            if target is not None:
                click_account_tab(target, hwnd=hwnd, activate=activate, progress=log)
                time.sleep(wait_seconds)
                log("original account restore attempted")
            else:
                log("original account restore skipped because target is not visible")
        except Exception:
            log("original account restore failed")
            pass
    return results


def detect_current_accounts(
    detector: QianniuDetector,
    *,
    hwnd: int = 0,
    progress: ProgressLogger | None = None,
) -> list[AccountCandidate]:
    log = progress or noop_progress
    if hwnd:
        log(f"resolving UIA root from selected hwnd=0x{hwnd:X} for account verification")
        root = control_from_hwnd(hwnd)
    else:
        log("selected hwnd is missing; falling back to global best-window search")
        window = detector.find_best_window()
        if window is None:
            return []
        root = control_from_hwnd(window.hwnd)
    if root is None:
        log("account verification skipped because UIA root is unavailable")
        return []
    log("capturing top image for account verification")
    top_image = grab_top_image(root)
    accounts = detect_account_candidates(
        root,
        top_image=top_image,
        max_depth=10,
        max_nodes=3000,
        min_confidence=0.35,
        limit=20,
        progress=log,
    )
    log(f"account verification scan done; accounts={len(accounts)}")
    return accounts


def read_visible_sessions_from_window(
    detector: QianniuDetector,
    *,
    hwnd: int,
    limit: int,
    progress: ProgressLogger | None = None,
) -> list[SessionItem]:
    log = progress or noop_progress
    if not hwnd:
        log("session scan skipped because selected hwnd is missing")
        return []

    log(f"resolving UIA root from selected hwnd=0x{hwnd:X} for session scan")
    root = control_from_hwnd(hwnd)
    if root is None:
        log("session scan skipped because UIA root is unavailable")
        return []

    log("locating chat root inside selected window")
    chat_root = find_chat_root_from_window(detector, root, progress=log)
    if chat_root is None:
        log("session scan skipped because chat root was not found")
        return []

    log("locating session list root inside selected chat root")
    session_root, root_source = find_session_root_from_chat(detector, chat_root, progress=log)
    if session_root is None:
        log("session scan skipped because session list root was not found")
        return []

    started_at = time.perf_counter()
    log(f"extracting visible sessions from {root_source}")
    sessions = extract_session_items(session_root, limit=limit)
    elapsed_ms = (time.perf_counter() - started_at) * 1000.0
    log(f"visible session extraction done; sessions={len(sessions)} elapsed_ms={elapsed_ms:.1f}")
    return sessions


def find_chat_root_from_window(
    detector: QianniuDetector,
    root: Any,
    *,
    progress: ProgressLogger | None = None,
) -> Any | None:
    log = progress or noop_progress
    best: tuple[int, str, int, Any] | None = None
    for depth, control in iter_controls_progressive(
        root,
        max_depth=detector.q.max_tree_depth,
        max_nodes=detector.q.max_tree_nodes,
        progress=log,
        label="chat root scan",
    ):
        try:
            score, reason = detector._score_chat_root_candidate(control, depth)
            if detector._is_definitive_chat_root(control, score):
                log(f"definitive chat root found score={score} reason={reason or '-'} depth={depth}")
                return control
            if score > 0 and (best is None or score > best[0]):
                best = (score, reason, depth, control)
        except Exception:
            continue

    if best is not None:
        score, reason, depth, control = best
        log(f"chat root candidate found score={score} reason={reason or '-'} depth={depth}")
        return control

    log("no scored chat root candidate found; using selected window root as session-scan fallback")
    return root


def find_session_root_from_chat(
    detector: QianniuDetector,
    chat_root: Any,
    *,
    progress: ProgressLogger | None = None,
) -> tuple[Any | None, str]:
    log = progress or noop_progress
    fallback: Any | None = None
    for depth, control in iter_controls_progressive(
        chat_root,
        max_depth=detector.q.max_tree_depth,
        max_nodes=detector.q.max_tree_nodes,
        progress=log,
        label="session root scan",
    ):
        aid = safe_prop(control, "AutomationId")
        if aid == detector.q.reception_normal_list_suffix or aid.endswith(detector.q.reception_normal_list_suffix):
            log(f"session root found via reception_normal_list suffix depth={depth}")
            return control, "reception_normal_list"
        if fallback is None and (aid == detector.q.chat_list_items_suffix or aid.endswith(detector.q.chat_list_items_suffix)):
            fallback = control

    if fallback is not None:
        log("session root found via chat_list_items suffix")
        return fallback, "chat_list_items"
    return None, ""


def click_account_tab(
    account: AccountCandidate,
    *,
    hwnd: int,
    activate: bool,
    progress: ProgressLogger | None = None,
) -> tuple[bool, str]:
    log = progress or noop_progress
    if not account.rect_tuple:
        return False, "missing_rect"
    if activate:
        if hwnd:
            log(f"activating selected window hwnd=0x{hwnd:X}")
            if not activate_window(hwnd):
                log("selected window activation failed; continuing with rect click")
        else:
            log("window activation skipped because selected hwnd is missing")
    left, top, right, bottom = account.rect_tuple
    x = int((left + right) / 2)
    y = int((top + bottom) / 2)
    log(f"clicking account tab center x={x} y={y}")
    import win32api
    import win32con

    old = win32api.GetCursorPos()
    win32api.SetCursorPos((x, y))
    time.sleep(0.04)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.04)
    win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    win32api.SetCursorPos(old)
    return True, "rect_click"


def activate_window(hwnd: int) -> bool:
    try:
        import win32con
        import win32gui

        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetForegroundWindow(hwnd)
        time.sleep(0.08)
        return True
    except Exception:
        return False


def collect_tree_rows(
    root: Any,
    *,
    max_depth: int,
    max_nodes: int,
    progress: ProgressLogger | None = None,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, (depth, control) in enumerate(iter_controls(root, max_depth=max_depth, max_nodes=max_nodes, progress=progress, label="UIA dump")):
        try:
            child_count = len(control.GetChildren())
        except Exception:
            child_count = 0
        rows.append(
            {
                "index": index,
                "depth": depth,
                "control_type": safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
                "class_name": safe_prop(control, "ClassName"),
                "automation_id": safe_prop(control, "AutomationId"),
                "name": safe_prop(control, "Name"),
                "rect": safe_rect(control),
                "native_hwnd": safe_prop(control, "NativeWindowHandle"),
                "is_offscreen": safe_prop(control, "IsOffscreen"),
                "child_count": child_count,
            }
        )
    return rows


def iter_controls(
    root: Any,
    *,
    max_depth: int,
    max_nodes: int,
    progress: ProgressLogger | None = None,
    label: str = "UIA scan",
) -> list[tuple[int, Any]]:
    return list(
        iter_controls_progressive(
            root,
            max_depth=max_depth,
            max_nodes=max_nodes,
            progress=progress,
            label=label,
        )
    )


def iter_controls_progressive(
    root: Any,
    *,
    max_depth: int,
    max_nodes: int,
    progress: ProgressLogger | None = None,
    label: str = "UIA scan",
) -> Any:
    log = progress or noop_progress
    queue: list[tuple[int, Any]] = [(0, root)]
    visited = 0
    last_log_at = 0
    returned = 0

    try:
        while queue and visited < max_nodes:
            depth, control = queue.pop(0)
            visited += 1
            returned += 1
            if visited - last_log_at >= 500:
                last_log_at = visited
                log(f"{label}: visited={visited} queued={len(queue)} depth={depth}")
            yield depth, control
            if depth >= max_depth:
                continue
            try:
                children = control.GetChildren()
            except Exception:
                continue
            for child in children:
                queue.append((depth + 1, child))
    finally:
        log(f"{label}: complete visited={visited} returned={returned}")


def write_uia_jsonl(rows: list[dict[str, Any]], output_dir: Path, run_id: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / f"{run_id}_uia.jsonl"
    with path.open("w", encoding="utf-8") as file:
        for row in rows:
            file.write(json.dumps(row, ensure_ascii=False) + "\n")
    return path


def select_window(windows: list[WindowCandidate], index: int) -> WindowCandidate | None:
    if not windows:
        return None
    safe_index = max(1, min(int(index or 1), len(windows)))
    return windows[safe_index - 1]


def window_payload(window: WindowCandidate) -> dict[str, Any]:
    return {
        "hwnd": window.hwnd,
        "hwnd_hex": f"0x{window.hwnd:X}",
        "pid": window.pid,
        "class_name": window.class_name,
        "title": window.title,
        "score": window.score,
        "reason": window.reason,
    }


def account_payload(account: AccountCandidate) -> dict[str, Any]:
    data = asdict(account)
    if account.rect_tuple is not None:
        data["rect_tuple"] = list(account.rect_tuple)
    if account.unread_hint_rect is not None:
        data["unread_hint_rect"] = list(account.unread_hint_rect)
    return data


def account_from_payload(value: dict[str, Any]) -> AccountCandidate:
    rect_value = value.get("rect_tuple")
    rect_tuple = tuple(int(item) for item in rect_value) if isinstance(rect_value, list) and len(rect_value) == 4 else None
    unread_rect_value = value.get("unread_hint_rect")
    unread_hint_rect = (
        tuple(int(item) for item in unread_rect_value)
        if isinstance(unread_rect_value, list) and len(unread_rect_value) == 4
        else None
    )
    return AccountCandidate(
        account_id=str(value.get("account_id") or ""),
        display_name=str(value.get("display_name") or ""),
        selected=bool(value.get("selected")),
        rect=str(value.get("rect") or "-"),
        rect_tuple=rect_tuple,
        source=str(value.get("source") or ""),
        confidence=float(value.get("confidence") or 0.0),
        raw_texts=[str(item) for item in value.get("raw_texts", []) if str(item)],
        automation_id=str(value.get("automation_id") or ""),
        class_name=str(value.get("class_name") or ""),
        control_type=str(value.get("control_type") or ""),
        depth=int(value.get("depth") or 0),
        index=int(value.get("index") or 0),
        visual_blue_ratio=float(value.get("visual_blue_ratio") or 0.0),
        has_unread_hint=bool(value.get("has_unread_hint")),
        unread_badge_text=str(value.get("unread_badge_text") or ""),
        unread_elapsed_text=str(value.get("unread_elapsed_text") or ""),
        unread_hint_rect=unread_hint_rect,
        unread_score=float(value.get("unread_score") or 0.0),
    )


def make_run_id() -> str:
    return datetime.now().strftime("%Y%m%dT%H%M%S.%f")[:-3]


def make_progress_logger(*, enabled: bool) -> ProgressLogger:
    started_at = time.perf_counter()

    def log(message: str) -> None:
        if not enabled:
            return
        elapsed = time.perf_counter() - started_at
        print(f"[qianniu-account-probe +{elapsed:6.1f}s] {message}", file=sys.stderr, flush=True)

    return log


def noop_progress(_message: str) -> None:
    return None


def print_human_report(report: dict[str, Any]) -> None:
    if not report.get("ok"):
        print(f"ok=False error={report.get('error') or '-'} process={report.get('process_name') or '-'}")
        for line in report.get("logs") or []:
            print(f"log: {line}")
        return
    window = report.get("window") or {}
    diagnostics = report.get("diagnostics") or {}
    print(
        f"ok=True process={report.get('process_name')} hwnd={window.get('hwnd_hex') or '-'} "
        f"title={trim(window.get('title') or '', 120) or '-'} accounts={report.get('account_count')} "
        f"unread_accounts={report.get('unread_account_count', 0)} "
        f"active={report.get('active_display_name') or report.get('active_account_id') or '-'} "
        f"elapsed_ms={diagnostics.get('elapsed_ms')}"
    )
    if diagnostics.get("screenshot_path"):
        print(f"screenshot={diagnostics.get('screenshot_path')}")
    if diagnostics.get("uia_jsonl_path"):
        print(f"uia_jsonl={diagnostics.get('uia_jsonl_path')}")
    for index, account in enumerate(report.get("accounts") or [], start=1):
        selected = "*" if account.get("selected") else " "
        print(
            f"{index:02d}{selected} account_id={account.get('account_id')} "
            f"name={account.get('display_name')} source={account.get('source')} "
            f"confidence={account.get('confidence')} blue={account.get('visual_blue_ratio')} "
            f"unread={account.get('has_unread_hint')} unread_score={account.get('unread_score')} "
            f"badge={account.get('unread_badge_text') or '-'} elapsed={account.get('unread_elapsed_text') or '-'} "
            f"rect={account.get('rect')}"
        )
    if report.get("switch_account_query"):
        target = report.get("switch_target") or {}
        print(
            f"switch_query={report.get('switch_account_query')} "
            f"target={target.get('display_name') or target.get('account_id') or '-'} "
            f"match={target.get('match_mode') or '-'}"
        )
    if report.get("switch_unread"):
        target = report.get("switch_target") or {}
        print(
            f"switch_unread=True "
            f"target={target.get('display_name') or target.get('account_id') or '-'} "
            f"match={target.get('match_mode') or '-'}"
        )
    if report.get("switch_note"):
        print(f"switch_note={report.get('switch_note')}")
    if report.get("switch_error"):
        print(f"switch_error={report.get('switch_error')}")
    for item in report.get("switch_results") or []:
        print(
            f"switch account={item.get('display_name') or item.get('account_id')} "
            f"clicked={item.get('clicked')} verified={item.get('active_verified')} "
            f"active={item.get('active_display_name') or item.get('active_account_id') or '-'} "
            f"sessions={item.get('session_count')} elapsed_ms={item.get('elapsed_ms')} "
            f"error={item.get('error') or '-'}"
        )


def print_windows_only_report(report: dict[str, Any]) -> None:
    if not report.get("ok"):
        print(f"ok=False error={report.get('error') or '-'} process={report.get('process_name') or '-'}")
        for line in (report.get("diagnostics") or {}).get("logs") or []:
            print(f"log: {line}")
        return
    print(
        f"ok=True process={report.get('process_name')} windows={report.get('window_count')} "
        f"elapsed_ms={(report.get('diagnostics') or {}).get('elapsed_ms')}"
    )
    for index, window in enumerate(report.get("windows") or [], start=1):
        print(
            f"{index:02d} hwnd={window.get('hwnd_hex') or '-'} pid={window.get('pid')} "
            f"class={window.get('class_name') or '-'} score={window.get('score')} "
            f"reason={window.get('reason') or '-'} title={trim(window.get('title') or '', 160) or '-'}"
        )


if __name__ == "__main__":
    raise SystemExit(main())
