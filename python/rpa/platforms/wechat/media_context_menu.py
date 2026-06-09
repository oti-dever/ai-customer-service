from __future__ import annotations

import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from .media_clipboard import ClipboardFileResult, copy_clipboard_files_to_artifacts
from .uia_scoring import normalize_text, safe_rect_tuple
from .wechat_logging import get_logger


logger = get_logger(__name__)

COPY_FILE_MENU_NAMES = ("复制", "Copy")


@dataclass(frozen=True)
class ContextMenuFileResult:
    status: str
    method: str = "context_menu_copy_file"
    source_paths: list[str] | None = None
    artifact_paths: list[str] | None = None
    error: str = ""
    menu_name: str = ""


def copy_message_file_via_context_menu(
    sample: Any,
    *,
    root_dir: str | Path,
    content_type: str,
    platform_msg_id: str,
    menu_names: tuple[str, ...] = COPY_FILE_MENU_NAMES,
    click_context_menu: Callable[[Any], bool] | None = None,
    find_menu_item: Callable[[tuple[str, ...]], Any | None] | None = None,
    clipboard_copier: Callable[..., ClipboardFileResult] = copy_clipboard_files_to_artifacts,
    settle_seconds: float = 0.2,
) -> ContextMenuFileResult:
    if content_type not in {"image", "file", "video"}:
        return ContextMenuFileResult(status="skipped", error="unsupported_content_type")

    click_context_menu = click_context_menu or _right_click_sample
    find_menu_item = find_menu_item or _find_copy_file_menu_item

    if not click_context_menu(sample):
        return ContextMenuFileResult(status="failed", error="right_click_failed")

    item = find_menu_item(menu_names)
    if item is None:
        return ContextMenuFileResult(status="failed", error="copy_file_menu_not_found")

    menu_name = normalize_text(getattr(item, "Name", ""))
    if not _invoke_menu_item(item):
        return ContextMenuFileResult(status="failed", error="copy_file_menu_invoke_failed", menu_name=menu_name)

    if settle_seconds > 0:
        time.sleep(settle_seconds)

    copied = clipboard_copier(root_dir=root_dir, content_type=content_type, platform_msg_id=platform_msg_id)
    if copied.status != "copied":
        copied_method = normalize_text(getattr(copied, "method", ""))
        error = copied.error
        if not error:
            error = f"{copied_method}_{copied.status}" if copied_method and copied_method != "clipboard_file" else f"clipboard_{copied.status}"
        return ContextMenuFileResult(
            status="failed",
            source_paths=copied.source_paths,
            artifact_paths=copied.artifact_paths,
            error=error,
            menu_name=menu_name,
        )
    return ContextMenuFileResult(
        status="copied",
        source_paths=copied.source_paths,
        artifact_paths=copied.artifact_paths,
        menu_name=menu_name,
    )


def _right_click_sample(sample: Any) -> bool:
    rect = safe_rect_tuple(sample)
    if rect is None:
        return False
    x, y = _context_menu_click_point(sample, rect)
    try:
        import win32api
        import win32con
    except Exception:
        return False
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
        logger.debug("wechat media right click failed: %s", exc)
        return False
    finally:
        try:
            win32api.SetCursorPos((old_x, old_y))
        except Exception:
            pass


def _context_menu_click_point(sample: Any, rect: tuple[int, int, int, int] | None = None) -> tuple[int, int]:
    rect = rect or safe_rect_tuple(sample)
    if rect is None:
        return 0, 0
    left, top, right, bottom = rect
    direction = normalize_text(getattr(sample, "direction", "")).lower()
    x_ratio = 0.25 if direction in {"in", "inbound", "customer"} else 0.75 if direction in {
        "out",
        "outbound",
        "agent",
    } else 0.5
    return int(left + (right - left) * x_ratio), int((top + bottom) / 2)


def _find_copy_file_menu_item(menu_names: tuple[str, ...], timeout_seconds: float = 1.2) -> Any | None:
    try:
        import uiautomation as auto
    except Exception:
        return None

    normalized_names = [normalize_text(name).lower() for name in menu_names if normalize_text(name)]
    deadline = time.perf_counter() + max(0.1, timeout_seconds)
    while time.perf_counter() < deadline:
        candidates: dict[str, Any] = {}
        for item in _iter_candidate_menu_items(auto):
            name = normalize_text(getattr(item, "Name", "")).lower()
            if name in normalized_names and name not in candidates:
                candidates[name] = item
        for name in normalized_names:
            if name in candidates:
                return candidates[name]
        time.sleep(0.08)
    return None


def _iter_candidate_menu_items(auto: Any) -> list[Any]:
    try:
        root = auto.GetRootControl()
        controls = _walk_uia(root, max_depth=8, max_nodes=1200)
    except Exception:
        return []
    menu_roots = [control for control in controls if _is_wechat_menu_root(control)]
    if not menu_roots:
        return controls

    menu_items: list[Any] = []
    for menu_root in menu_roots:
        for control in _walk_uia(menu_root, max_depth=4, max_nodes=200):
            if _is_wechat_menu_item(control):
                menu_items.append(control)
    return menu_items or menu_roots


def _walk_uia(root: Any, *, max_depth: int, max_nodes: int) -> list[Any]:
    found: list[Any] = []
    queue: list[tuple[int, Any]] = [(0, root)]
    while queue and len(found) < max_nodes:
        depth, control = queue.pop(0)
        found.append(control)
        if depth >= max_depth:
            continue
        try:
            children = control.GetChildren()
        except Exception:
            continue
        for child in children:
            queue.append((depth + 1, child))
    return found


def _is_wechat_menu_root(control: Any) -> bool:
    class_name = normalize_text(getattr(control, "ClassName", ""))
    control_type = normalize_text(
        getattr(control, "ControlTypeName", "") or getattr(control, "LocalizedControlType", "")
    )
    return class_name == "mmui::XMenu" and "Window" in control_type


def _is_wechat_menu_item(control: Any) -> bool:
    class_name = normalize_text(getattr(control, "ClassName", ""))
    automation_id = normalize_text(getattr(control, "AutomationId", ""))
    control_type = normalize_text(
        getattr(control, "ControlTypeName", "") or getattr(control, "LocalizedControlType", "")
    )
    return class_name == "mmui::XMenuView" or automation_id == "XMenuItem" or "MenuItem" in control_type


def _invoke_menu_item(item: Any) -> bool:
    if _is_wechat_menu_item(item) and _click_menu_item_with_mouse(item):
        return True
    try:
        pattern_getter = getattr(item, "GetInvokePattern", None)
        if pattern_getter:
            pattern = pattern_getter()
            if pattern:
                pattern.Invoke()
                return True
    except Exception:
        pass
    try:
        item.Click(simulateMove=False, waitTime=0)
        return True
    except Exception as exc:
        logger.debug("wechat copy file menu click failed: %s", exc)
        return False


def _click_menu_item_with_mouse(item: Any) -> bool:
    rect = safe_rect_tuple(item)
    if rect is None:
        return False
    x, y = _menu_item_click_point(rect)
    try:
        import win32api
        import win32con
    except Exception:
        return False
    try:
        win32api.SetCursorPos((x, y))
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        return True
    except Exception as exc:
        logger.debug("wechat menu item mouse click failed: %s", exc)
        return False


def _menu_item_click_point(rect: tuple[int, int, int, int]) -> tuple[int, int]:
    left, top, right, bottom = rect
    width = max(1, right - left)
    x_offset = min(max(36, int(width * 0.35)), max(1, width - 8))
    return left + x_offset, int((top + bottom) / 2)
