from __future__ import annotations

import contextlib
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Iterator


_AUTOMATION_LOCK = threading.RLock()


@dataclass(frozen=True)
class WindowInfo:
    hwnd: int
    pid: int
    title: str
    class_name: str
    visible: bool = False
    rect: tuple[int, int, int, int] | None = None


@dataclass(frozen=True)
class ControlInfo:
    depth: int
    control_type: str
    class_name: str
    automation_id: str
    name: str
    rect: str
    native_hwnd: int
    is_offscreen: str


class DependencyUnavailable(RuntimeError):
    pass


@contextlib.contextmanager
def automation_guard(_label: str = "qianniu") -> Iterator[None]:
    with _AUTOMATION_LOCK:
        yield


@contextlib.contextmanager
def uia_guard(label: str = "qianniu") -> Iterator[None]:
    try:
        import comtypes
    except ImportError as exc:
        raise DependencyUnavailable("缺少 comtypes，无法初始化 UIAutomation 线程") from exc

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

        cache_dir = Path(__file__).resolve().parents[1] / ".comtypes_cache"
        cache_dir.mkdir(exist_ok=True)
        (cache_dir / "__init__.py").touch(exist_ok=True)

        cache_path = str(cache_dir)
        comtypes.client.gen_dir = cache_path
        gen_paths = list(comtypes.gen.__path__)
        comtypes.gen.__path__ = [cache_path] + [path for path in gen_paths if path != cache_path]

        import uiautomation as auto

        return auto
    except ImportError as exc:
        raise DependencyUnavailable("缺少 uiautomation/comtypes，无法遍历 UIA 控件树") from exc


def find_process_ids(process_name: str) -> list[int]:
    try:
        import psutil
    except ImportError:
        return []

    target = process_name.lower()
    pids: list[int] = []
    for proc in psutil.process_iter(["name", "pid"]):
        try:
            name = (proc.info.get("name") or "").lower()
            if name == target:
                pids.append(int(proc.info["pid"]))
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return pids


def enum_windows(process_name: str, class_name: str = "", title_keyword: str = "") -> list[WindowInfo]:
    try:
        import win32gui
        import win32process
        import pywintypes
    except ImportError as exc:
        raise DependencyUnavailable("缺少 pywin32，无法枚举窗口") from exc

    process_ids = set(find_process_ids(process_name))
    matches: list[WindowInfo] = []

    def enum_callback(hwnd: int, _: object) -> bool:
        try:
            if not win32gui.IsWindowVisible(hwnd):
                return True

            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            if process_ids and pid not in process_ids:
                return True

            item_class = win32gui.GetClassName(hwnd)
            title = win32gui.GetWindowText(hwnd)
        except pywintypes.error:
            return True

        if class_name and item_class != class_name:
            return True
        if title_keyword and title_keyword not in title:
            return True

        matches.append(WindowInfo(hwnd=hwnd, pid=pid, title=title, class_name=item_class))
        return True

    win32gui.EnumWindows(enum_callback, None)
    return matches


def enum_all_top_level_windows() -> list[WindowInfo]:
    try:
        import win32gui
        import win32process
        import pywintypes
    except ImportError as exc:
        raise DependencyUnavailable("缺少 pywin32，无法枚举窗口") from exc

    windows: list[WindowInfo] = []

    def enum_callback(hwnd: int, _: object) -> bool:
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            rect = win32gui.GetWindowRect(hwnd)
            windows.append(
                WindowInfo(
                    hwnd=hwnd,
                    pid=pid,
                    title=win32gui.GetWindowText(hwnd),
                    class_name=win32gui.GetClassName(hwnd),
                    visible=bool(win32gui.IsWindowVisible(hwnd)),
                    rect=(int(rect[0]), int(rect[1]), int(rect[2]), int(rect[3])),
                )
            )
        except pywintypes.error:
            return True
        return True

    win32gui.EnumWindows(enum_callback, None)
    return windows


def get_foreground_window() -> WindowInfo | None:
    try:
        import win32gui
        import win32process
    except ImportError as exc:
        raise DependencyUnavailable("缺少 pywin32，无法获取前台窗口") from exc

    hwnd = win32gui.GetForegroundWindow()
    if not hwnd:
        return None
    _, pid = win32process.GetWindowThreadProcessId(hwnd)
    rect = win32gui.GetWindowRect(hwnd)
    return WindowInfo(
        hwnd=int(hwnd),
        pid=int(pid),
        title=win32gui.GetWindowText(hwnd),
        class_name=win32gui.GetClassName(hwnd),
        visible=bool(win32gui.IsWindowVisible(hwnd)),
        rect=(int(rect[0]), int(rect[1]), int(rect[2]), int(rect[3])),
    )


def enum_child_windows(parent_hwnd: int) -> list[WindowInfo]:
    try:
        import win32gui
        import win32process
        import pywintypes
    except ImportError as exc:
        raise DependencyUnavailable("缺少 pywin32，无法枚举子窗口") from exc

    windows: list[WindowInfo] = []

    def enum_callback(hwnd: int, _: object) -> bool:
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            rect = win32gui.GetWindowRect(hwnd)
            windows.append(
                WindowInfo(
                    hwnd=hwnd,
                    pid=pid,
                    title=win32gui.GetWindowText(hwnd),
                    class_name=win32gui.GetClassName(hwnd),
                    visible=bool(win32gui.IsWindowVisible(hwnd)),
                    rect=(int(rect[0]), int(rect[1]), int(rect[2]), int(rect[3])),
                )
            )
        except pywintypes.error:
            return True
        return True

    win32gui.EnumChildWindows(parent_hwnd, enum_callback, None)
    return windows


def get_root_control() -> Any:
    auto = import_uiautomation()
    return auto.GetRootControl()


def control_from_hwnd(hwnd: int) -> Any | None:
    auto = import_uiautomation()
    try:
        return auto.ControlFromHandle(hwnd)
    except Exception:
        return None


def control_from_point(x: int, y: int) -> Any | None:
    auto = import_uiautomation()
    try:
        return auto.ControlFromPoint(x, y)
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
            children = control.GetChildren()
        except Exception:
            continue
        for child in children:
            queue.append((depth + 1, child))

    return found


def describe_control(depth: int, control: Any) -> ControlInfo:
    return ControlInfo(
        depth=depth,
        control_type=safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
        class_name=safe_prop(control, "ClassName"),
        automation_id=safe_prop(control, "AutomationId"),
        name=safe_prop(control, "Name"),
        rect=safe_rect(control),
        native_hwnd=safe_int_prop(control, "NativeWindowHandle"),
        is_offscreen=safe_prop(control, "IsOffscreen"),
    )


def iter_descriptions(root: Any, max_depth: int, max_nodes: int) -> Iterable[ControlInfo]:
    for depth, control in walk_controls(root, max_depth=max_depth, max_nodes=max_nodes):
        yield describe_control(depth, control)


def safe_prop(control: Any, name: str) -> str:
    try:
        value = getattr(control, name, "")
        return "" if value is None else str(value)
    except Exception:
        return ""


def safe_int_prop(control: Any, name: str) -> int:
    try:
        value = getattr(control, name, 0) or 0
        return int(value)
    except Exception:
        return 0


def safe_rect(control: Any) -> str:
    try:
        rect = control.BoundingRectangle
        return f"({rect.left},{rect.top},{rect.right},{rect.bottom})"
    except Exception:
        return "-"


def safe_rect_tuple(control: Any) -> tuple[int, int, int, int] | None:
    try:
        rect = control.BoundingRectangle
        return int(rect.left), int(rect.top), int(rect.right), int(rect.bottom)
    except Exception:
        return None


def is_control_available(control: Any | None) -> bool:
    rect = safe_rect_tuple(control) if control is not None else None
    if not rect:
        return False
    left, top, right, bottom = rect
    return right > left and bottom > top


def trim(value: str, max_len: int = 120) -> str:
    value = value.replace("\r", " ").replace("\n", " ")
    return value if len(value) <= max_len else value[: max_len - 3] + "..."
