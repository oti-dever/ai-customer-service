from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from rpa.platforms.wechat.media_clipboard import ClipboardFileResult, copy_clipboard_files_to_artifacts

from .navigator import activate_window_for_foreground_rpa, rect_inside
from .qq_logging import get_logger
from .reader import parse_rect
from .uia import safe_prop, safe_rect_tuple, walk_controls


logger = get_logger(__name__)

COPY_MENU_NAMES = ("复制", "Copy", "澶嶅埗")
DIAGNOSTIC_MENU_NAMES = ("复制", "Copy", "另存为", "保存到", "打开文件夹", "澶嶅埗")


@dataclass(frozen=True)
class QQContextMenuCopyResult:
    status: str
    method: str = "qq_context_menu_copy"
    source_paths: list[str] = field(default_factory=list)
    artifact_paths: list[str] = field(default_factory=list)
    error: str = ""
    menu_name: str = ""
    menu_names_seen: list[str] = field(default_factory=list)
    clipboard_method: str = ""


def copy_media_via_context_menu(
    *,
    hwnd: int,
    window_rect: tuple[int, int, int, int],
    media_rect: str,
    root_dir: str | Path,
    content_type: str,
    platform_msg_id: str,
    click_context_menu: Callable[[tuple[int, int]], bool] | None = None,
    find_menu_item: Callable[[tuple[str, ...]], tuple[Any | None, list[str]] | None] | None = None,
    clipboard_copier: Callable[..., ClipboardFileResult] = copy_clipboard_files_to_artifacts,
    settle_seconds: float = 0.25,
) -> QQContextMenuCopyResult:
    total_started_at = time.perf_counter()
    if content_type not in {"image", "emoji", "video", "file"}:
        _log_copy_timing(total_started_at, content_type, platform_msg_id, "skipped", "unsupported_content_type")
        return QQContextMenuCopyResult(status="skipped", error="unsupported_content_type")
    rect = parse_rect(media_rect)
    if rect is None:
        _log_copy_timing(total_started_at, content_type, platform_msg_id, "failed", "invalid_media_rect")
        return QQContextMenuCopyResult(status="failed", error="invalid_media_rect")
    if not rect_inside(rect, window_rect):
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            "media_rect_outside_window",
        )
        return QQContextMenuCopyResult(status="failed", error="media_rect_outside_window")

    stage_started_at = time.perf_counter()
    activation = activate_window_for_foreground_rpa(hwnd)
    activation_ms = _elapsed_ms(stage_started_at)
    if not activation.ok:
        logger.info(
            "qq media foreground activation failed target_hwnd=0x%X foreground_hwnd=0x%X elapsed_ms=%.1f detail=%s",
            activation.target_hwnd,
            activation.foreground_hwnd,
            activation.elapsed_ms or activation_ms,
            activation.detail,
        )
        error = _foreground_error_detail(activation)
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            error,
            activation_ms=activation.elapsed_ms or activation_ms,
        )
        return QQContextMenuCopyResult(status="failed", error=error)

    click_context_menu = click_context_menu or _right_click_point
    point = _context_menu_click_point(rect, content_type=content_type)
    stage_started_at = time.perf_counter()
    if not click_context_menu(point):
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            "right_click_failed",
            activation_ms=activation.elapsed_ms or activation_ms,
            right_click_ms=_elapsed_ms(stage_started_at),
        )
        return QQContextMenuCopyResult(status="failed", error="right_click_failed")
    right_click_ms = _elapsed_ms(stage_started_at)

    stage_started_at = time.perf_counter()
    if find_menu_item is None:
        found = _find_qq_copy_menu_item(COPY_MENU_NAMES, anchor_point=point)
    else:
        found = find_menu_item(COPY_MENU_NAMES)
    menu_find_ms = _elapsed_ms(stage_started_at)
    item, menu_names_seen = found if found is not None else (None, [])
    if item is None:
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            "copy_menu_not_found",
            activation_ms=activation.elapsed_ms or activation_ms,
            right_click_ms=right_click_ms,
            menu_find_ms=menu_find_ms,
            menu_seen=len(menu_names_seen),
        )
        return QQContextMenuCopyResult(
            status="failed",
            error="copy_menu_not_found",
            menu_names_seen=menu_names_seen,
        )

    menu_name = _control_name(item)
    stage_started_at = time.perf_counter()
    if not _invoke_menu_item(item):
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            "copy_menu_invoke_failed",
            activation_ms=activation.elapsed_ms or activation_ms,
            right_click_ms=right_click_ms,
            menu_find_ms=menu_find_ms,
            menu_invoke_ms=_elapsed_ms(stage_started_at),
            menu_seen=len(menu_names_seen),
        )
        return QQContextMenuCopyResult(
            status="failed",
            error="copy_menu_invoke_failed",
            menu_name=menu_name,
            menu_names_seen=menu_names_seen,
        )
    menu_invoke_ms = _elapsed_ms(stage_started_at)

    if settle_seconds > 0:
        time.sleep(settle_seconds)
    stage_started_at = time.perf_counter()
    copied = clipboard_copier(root_dir=root_dir, content_type=content_type, platform_msg_id=platform_msg_id)
    clipboard_ms = _elapsed_ms(stage_started_at)
    if copied.status != "copied":
        error = copied.error or f"clipboard_{copied.status}"
        _log_copy_timing(
            total_started_at,
            content_type,
            platform_msg_id,
            "failed",
            error,
            activation_ms=activation.elapsed_ms or activation_ms,
            right_click_ms=right_click_ms,
            menu_find_ms=menu_find_ms,
            menu_invoke_ms=menu_invoke_ms,
            clipboard_ms=clipboard_ms,
            menu_seen=len(menu_names_seen),
            artifacts=len(copied.artifact_paths or []),
            sources=len(copied.source_paths or []),
        )
        return QQContextMenuCopyResult(
            status="failed",
            source_paths=list(copied.source_paths or []),
            artifact_paths=list(copied.artifact_paths or []),
            error=error,
            menu_name=menu_name,
            menu_names_seen=menu_names_seen,
            clipboard_method=copied.method,
        )
    _log_copy_timing(
        total_started_at,
        content_type,
        platform_msg_id,
        "copied",
        "",
        activation_ms=activation.elapsed_ms or activation_ms,
        right_click_ms=right_click_ms,
        menu_find_ms=menu_find_ms,
        menu_invoke_ms=menu_invoke_ms,
        clipboard_ms=clipboard_ms,
        menu_seen=len(menu_names_seen),
        artifacts=len(copied.artifact_paths or []),
        sources=len(copied.source_paths or []),
    )
    return QQContextMenuCopyResult(
        status="copied",
        source_paths=list(copied.source_paths or []),
        artifact_paths=list(copied.artifact_paths or []),
        menu_name=menu_name,
        menu_names_seen=menu_names_seen,
        clipboard_method=copied.method,
    )


def _context_menu_click_point(
    rect: tuple[int, int, int, int],
    *,
    content_type: str,
) -> tuple[int, int]:
    left, top, right, bottom = rect
    width = max(1, right - left)
    height = max(1, bottom - top)
    x_ratio = 0.5 if content_type in {"image", "emoji", "video"} else 0.35
    return int(left + width * x_ratio), int(top + height * 0.5)


def _right_click_point(point: tuple[int, int]) -> bool:
    try:
        import win32api
        import win32con
    except Exception:
        return False
    x, y = point
    try:
        old_x, old_y = win32api.GetCursorPos()
    except Exception:
        old_x, old_y = x, y
    try:
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0)
        return True
    except Exception as exc:
        logger.debug("qq media right click failed: %s", exc)
        return False
    finally:
        try:
            win32api.SetCursorPos((old_x, old_y))
        except Exception:
            pass


def _foreground_error_detail(result: Any) -> str:
    return (
        "set_foreground_failed:"
        f"target_hwnd=0x{int(getattr(result, 'target_hwnd', 0) or 0):X},"
        f"foreground_hwnd=0x{int(getattr(result, 'foreground_hwnd', 0) or 0):X},"
        f"elapsed_ms={float(getattr(result, 'elapsed_ms', 0.0) or 0.0):.1f},"
        f"detail={getattr(result, 'detail', '') or '-'}"
    )


def _log_copy_timing(
    started_at: float,
    content_type: str,
    platform_msg_id: str,
    status: str,
    error: str = "",
    **timings: object,
) -> None:
    timing_text = " ".join(f"{key}={value}" for key, value in timings.items() if value not in {"", None})
    logger.info(
        "qq media context copy timing content_type=%s platform_msg_id=%s status=%s error=%s total_ms=%.1f%s%s",
        content_type,
        platform_msg_id,
        status,
        error or "-",
        _elapsed_ms(started_at),
        " " if timing_text else "",
        timing_text,
    )


def _elapsed_ms(started_at: float) -> float:
    return (time.perf_counter() - started_at) * 1000.0


def _find_qq_copy_menu_item(
    menu_names: tuple[str, ...],
    timeout_seconds: float = 1.2,
    *,
    anchor_point: tuple[int, int] | None = None,
) -> tuple[Any | None, list[str]]:
    try:
        import uiautomation as auto
    except Exception:
        return None, []

    wanted = {_normalize_text(name).lower() for name in menu_names if _normalize_text(name)}
    seen: list[str] = []
    deadline = time.perf_counter() + max(0.1, timeout_seconds)
    while time.perf_counter() < deadline:
        items = _iter_candidate_menu_items(auto, anchor_point=anchor_point)
        for item in items:
            name = _control_name(item)
            if name and name not in seen:
                seen.append(name)
        for item in items:
            name = _normalize_text(_control_name(item)).lower()
            if name in wanted:
                return item, seen
        time.sleep(0.08)
    return None, seen


def _iter_candidate_menu_items(auto: Any, *, anchor_point: tuple[int, int] | None = None) -> list[Any]:
    try:
        root = auto.GetRootControl()
        controls = [control for _depth, control in walk_controls(root, max_depth=8, max_nodes=1600)]
    except Exception:
        return []
    if anchor_point is not None:
        controls = [control for control in controls if _control_near_anchor(control, anchor_point)]

    menu_roots = [control for control in controls if _is_probable_menu_root(control)]
    if not menu_roots:
        return [control for control in controls if _is_probable_menu_item(control)]

    items: list[Any] = []
    for menu_root in menu_roots:
        for _depth, control in walk_controls(menu_root, max_depth=5, max_nodes=300):
            if _is_probable_menu_item(control):
                items.append(control)
    return items or menu_roots


def _control_near_anchor(control: Any, anchor_point: tuple[int, int]) -> bool:
    rect = safe_rect_tuple(control)
    if rect is None:
        return False
    return _rect_near_anchor(rect, anchor_point)


def _rect_near_anchor(rect: tuple[int, int, int, int], anchor_point: tuple[int, int]) -> bool:
    left, top, right, bottom = rect
    if right <= left or bottom <= top:
        return False
    x, y = anchor_point
    # Context menus are transient popups near the right-click point. This window
    # excludes persistent menu bars from other foreground apps while still
    # allowing QQ to open the popup slightly above or beside the clicked bubble.
    near_left = x - 420
    near_top = y - 260
    near_right = x + 520
    near_bottom = y + 520
    return not (right < near_left or left > near_right or bottom < near_top or top > near_bottom)


def _is_probable_menu_root(control: Any) -> bool:
    control_type = _control_type(control)
    class_name = _normalize_text(safe_prop(control, "ClassName"))
    name = _control_name(control)
    if "Menu" in control_type:
        return True
    if "menu" in class_name.lower():
        return True
    if name in DIAGNOSTIC_MENU_NAMES and _is_visible_control(control):
        return True
    return False


def _is_probable_menu_item(control: Any) -> bool:
    if not _is_visible_control(control):
        return False
    control_type = _control_type(control)
    class_name = _normalize_text(safe_prop(control, "ClassName"))
    name = _control_name(control)
    if "MenuItem" in control_type:
        return True
    if "menu" in class_name.lower() and name:
        return True
    if name in DIAGNOSTIC_MENU_NAMES:
        return True
    return False


def _is_visible_control(control: Any) -> bool:
    rect = safe_rect_tuple(control)
    if rect is None:
        return False
    left, top, right, bottom = rect
    return right > left and bottom > top


def _invoke_menu_item(item: Any) -> bool:
    for pattern_name in ("GetInvokePattern", "GetLegacyIAccessiblePattern"):
        try:
            getter = getattr(item, pattern_name, None)
            pattern = getter() if getter else None
            if not pattern:
                continue
            if pattern_name == "GetLegacyIAccessiblePattern":
                action = getattr(pattern, "DoDefaultAction", None)
                if action:
                    action()
                    return True
            else:
                pattern.Invoke()
                return True
        except Exception:
            pass
    try:
        item.Click(simulateMove=False, waitTime=0)
        return True
    except Exception:
        return _left_click_control(item)


def _left_click_control(control: Any) -> bool:
    rect = safe_rect_tuple(control)
    if rect is None:
        return False
    left, top, right, bottom = rect
    return _left_click_point((int((left + right) / 2), int((top + bottom) / 2)))


def _left_click_point(point: tuple[int, int]) -> bool:
    try:
        import win32api
        import win32con
    except Exception:
        return False
    x, y = point
    try:
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        return True
    except Exception:
        return False


def _control_name(control: Any) -> str:
    return _normalize_text(safe_prop(control, "Name"))


def _control_type(control: Any) -> str:
    return _normalize_text(safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"))


def _normalize_text(value: Any) -> str:
    return " ".join(str(value or "").split())
