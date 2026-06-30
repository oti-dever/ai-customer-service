from __future__ import annotations

import contextlib
import ctypes
import threading
from ctypes import wintypes
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterator


_AUTOMATION_LOCK = threading.RLock()
user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


@dataclass(frozen=True)
class WindowInfo:
    hwnd: int
    pid: int
    process_name: str
    title: str
    class_name: str
    visible: bool
    rect: tuple[int, int, int, int]
    area: int


@dataclass(frozen=True)
class ControlSummary:
    depth: int
    control_type: str
    class_name: str
    automation_id: str
    name: str
    rect: str
    native_hwnd: int
    is_offscreen: str
    child_count: int

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class DependencyUnavailable(RuntimeError):
    pass


@contextlib.contextmanager
def automation_guard(_label: str = "qq") -> Iterator[None]:
    with _AUTOMATION_LOCK:
        yield


@contextlib.contextmanager
def uia_guard(label: str = "qq") -> Iterator[None]:
    try:
        import comtypes
    except ImportError as exc:
        raise DependencyUnavailable("missing comtypes; cannot initialize UIAutomation thread") from exc

    comtypes.CoInitialize()
    try:
        with automation_guard(label):
            yield
    finally:
        comtypes.CoUninitialize()


def import_uiautomation() -> Any:
    try:
        import comtypes.client
        import comtypes.gen

        cache_dir = Path(__file__).resolve().parents[2] / ".comtypes_cache"
        cache_dir.mkdir(exist_ok=True)
        (cache_dir / "__init__.py").touch(exist_ok=True)

        cache_path = str(cache_dir)
        comtypes.client.gen_dir = cache_path
        gen_paths = list(comtypes.gen.__path__)
        comtypes.gen.__path__ = [cache_path] + [path for path in gen_paths if path != cache_path]

        import uiautomation as auto

        return auto
    except ImportError as exc:
        raise DependencyUnavailable("missing uiautomation/comtypes; cannot inspect UIA tree") from exc


def enum_all_top_level_windows() -> list[WindowInfo]:
    windows: list[WindowInfo] = []

    def enum_callback(hwnd: int, _: object) -> bool:
        try:
            visible = bool(user32.IsWindowVisible(wintypes.HWND(hwnd)))
            title = get_window_text(hwnd)
            class_name = get_class_name(hwnd)
            pid = get_window_pid(hwnd)
            process_name = get_process_name(pid)
            rect = get_window_rect(hwnd)
        except OSError:
            return True
        area = max(0, rect[2] - rect[0]) * max(0, rect[3] - rect[1])
        windows.append(
            WindowInfo(
                hwnd=int(hwnd),
                pid=pid,
                process_name=process_name,
                title=title,
                class_name=class_name,
                visible=visible,
                rect=rect,
                area=area,
            )
        )
        return True

    user32.EnumWindows(EnumWindowsProc(lambda hwnd, lparam: 1 if enum_callback(int(hwnd), lparam) else 0), 0)
    return windows


def find_process_windows(process_name: str, *, visible_only: bool = True) -> list[WindowInfo]:
    target = process_name.lower()
    windows = [
        item
        for item in enum_all_top_level_windows()
        if item.process_name.lower() == target and (item.visible or not visible_only)
    ]
    return sorted(windows, key=lambda item: item.area, reverse=True)


def get_window_text(hwnd: int) -> str:
    length = user32.GetWindowTextLengthW(wintypes.HWND(hwnd))
    if length <= 0:
        return ""
    buffer = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(wintypes.HWND(hwnd), buffer, length + 1)
    return buffer.value


def get_class_name(hwnd: int) -> str:
    buffer = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(wintypes.HWND(hwnd), buffer, len(buffer))
    return buffer.value


def get_window_pid(hwnd: int) -> int:
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), ctypes.byref(pid))
    return int(pid.value or 0)


def get_process_name(pid: int) -> str:
    if pid <= 0:
        return ""
    process_query_limited_information = 0x1000
    handle = kernel32.OpenProcess(process_query_limited_information, False, wintypes.DWORD(pid))
    if not handle:
        return ""
    try:
        buffer = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(len(buffer))
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return ""
        path = buffer.value
        return path.rsplit("\\", 1)[-1]
    finally:
        kernel32.CloseHandle(handle)


def get_window_rect(hwnd: int) -> tuple[int, int, int, int]:
    rect = wintypes.RECT()
    if not user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rect)):
        return (0, 0, 0, 0)
    return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)


def control_from_hwnd(hwnd: int) -> Any | None:
    auto = import_uiautomation()
    try:
        return auto.ControlFromHandle(hwnd)
    except Exception:
        return None


def walk_controls(root: Any, max_depth: int, max_nodes: int) -> list[tuple[int, Any]]:
    found: list[tuple[int, Any]] = []
    queue: list[tuple[int, Any]] = [(0, root)]
    visited = 0
    while queue and visited < max_nodes:
        depth, control = queue.pop(0)
        visited += 1
        found.append((depth, control))
        if depth >= max_depth:
            continue
        try:
            children = list(control.GetChildren())
        except Exception:
            continue
        for child in children:
            queue.append((depth + 1, child))
    return found


def summarize_control(depth: int, control: Any) -> ControlSummary:
    return ControlSummary(
        depth=depth,
        control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
        class_name=safe_prop(control, "ClassName"),
        automation_id=safe_prop(control, "AutomationId"),
        name=safe_prop(control, "Name"),
        rect=safe_rect(control),
        native_hwnd=safe_int_prop(control, "NativeWindowHandle"),
        is_offscreen=safe_prop(control, "IsOffscreen"),
        child_count=child_count(control),
    )


def safe_prop(control: Any, name: str) -> str:
    try:
        value = getattr(control, name, "")
        return "" if value is None else str(value)
    except Exception:
        return ""


def safe_int_prop(control: Any, name: str) -> int:
    try:
        return int(getattr(control, name, 0) or 0)
    except Exception:
        return 0


def safe_rect(control: Any) -> str:
    rect = safe_rect_tuple(control)
    if rect is None:
        return "-"
    return f"({rect[0]},{rect[1]},{rect[2]},{rect[3]})"


def safe_rect_tuple(control: Any) -> tuple[int, int, int, int] | None:
    try:
        rect = control.BoundingRectangle
        return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)
    except Exception:
        return None


def child_count(control: Any) -> int:
    try:
        return len(control.GetChildren())
    except Exception:
        return 0


def trim(value: Any, max_len: int = 120) -> str:
    text = str(value or "").replace("\r", " ").replace("\n", " ").strip()
    return text if len(text) <= max_len else text[: max_len - 3] + "..."
