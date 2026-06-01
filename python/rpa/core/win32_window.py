"""
Win32 window enumeration and lookup.
Platform-specific readers use this to find target windows.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from typing import Callable, Optional

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def get_window_text(hwnd: int) -> str:
    length = user32.GetWindowTextLengthW(wintypes.HWND(hwnd))
    if length == 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(wintypes.HWND(hwnd), buf, length + 1)
    return buf.value


def get_process_name(hwnd: int) -> str:
    """Get process name for window."""
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), ctypes.byref(pid))
    if not pid.value:
        return ""
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    PROCESS_VM_READ = 0x0010
    hproc = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, False, pid)
    if not hproc:
        return ""
    try:
        buf = ctypes.create_unicode_buffer(260)
        size = ctypes.c_ulong(260)
        if kernel32.QueryFullProcessImageNameW(hproc, 0, buf, ctypes.byref(size)):
            path = buf.value
            return path.split("\\")[-1] if "\\" in path else path
    finally:
        kernel32.CloseHandle(hproc)
    return ""


def enum_windows(callback: Callable[[int], bool]) -> None:
    """Enumerate top-level windows. Callback returns False to stop."""
    def proc(hwnd, lparam):
        # Return plain Python int; ctypes BOOL can cause "c_long cannot be interpreted as integer"
        return 1 if callback(int(hwnd)) else 0
    user32.EnumWindows(EnumWindowsProc(proc), 0)


class _RECT(ctypes.Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]


def window_client_area_pixels(hwnd: int) -> int:
    """Approximate window rect area (screen coords), for picking main vs tiny popups."""
    rc = _RECT()
    if not user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rc)):
        return 0
    w = max(0, rc.right - rc.left)
    h = max(0, rc.bottom - rc.top)
    return w * h


def find_all_windows(
    title_contains: str = "",
    process_name: str = "",
    visible_only: bool = True,
) -> list[int]:
    """
    All top-level windows matching criteria (see find_window).
    """
    result: list[int] = []

    def check(hwnd: int) -> bool:
        if visible_only and not user32.IsWindowVisible(wintypes.HWND(hwnd)):
            return True
        if title_contains:
            title = get_window_text(hwnd)
            if title_contains not in title:
                return True
        if process_name:
            pname = get_process_name(hwnd)
            if pname.lower() != process_name.lower():
                return True
        result.append(hwnd)
        return True

    enum_windows(check)
    return result


def find_largest_window(
    title_contains: str = "",
    process_name: str = "",
    visible_only: bool = True,
) -> Optional[int]:
    """Like find_window but returns the match with largest window area (主窗口优先)."""
    hwnds = find_all_windows(title_contains, process_name, visible_only)
    if not hwnds:
        return None
    return max(hwnds, key=window_client_area_pixels)


def find_window_by_title_candidates(
    title_substrings: list[str],
    process_name: str = "",
    visible_only: bool = True,
) -> Optional[int]:
    """
    按顺序尝试每个标题子串，返回第一个有匹配的窗口（同串多条时取面积最大）。
    用于千牛：先「接待中心」独立窗，再「千牛工作台」一体窗。
    """
    for sub in title_substrings:
        s = (sub or "").strip()
        if not s:
            continue
        h = find_largest_window(s, process_name, visible_only)
        if h:
            return h
    return None


def find_window(
    title_contains: str = "",
    process_name: str = "",
    visible_only: bool = True,
) -> Optional[int]:
    """
    Find first top-level window matching criteria.
    title_contains: substring in window title (empty = any)
    process_name: exact process name e.g. Weixin.exe (empty = any)
    """
    result: list[int] = []

    def check(hwnd: int) -> bool:
        if visible_only and not user32.IsWindowVisible(wintypes.HWND(hwnd)):
            return True
        if title_contains:
            title = get_window_text(hwnd)
            if title_contains not in title:
                return True
        if process_name:
            pname = get_process_name(hwnd)
            if pname.lower() != process_name.lower():
                return True
        result.append(hwnd)
        return False

    enum_windows(check)
    return result[0] if result else None


def screen_to_client(hwnd: int, screen_x: int, screen_y: int) -> tuple[int, int]:
    """Convert screen coordinates to window client coordinates."""
    class POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]
    pt = POINT(screen_x, screen_y)
    user32.ScreenToClient(wintypes.HWND(hwnd), ctypes.byref(pt))
    return pt.x, pt.y


def get_foreground_window() -> int:
    """Return the handle of the current foreground window."""
    return int(user32.GetForegroundWindow() or 0)


def is_window_visible(hwnd: int) -> bool:
    return bool(hwnd) and bool(user32.IsWindowVisible(wintypes.HWND(hwnd)))


def bring_to_foreground(hwnd: int) -> bool:
    """
    Bring window to foreground so it receives input.
    Handles minimized/hidden/managed windows (e.g. embedded in another app).
    """
    hwnd = wintypes.HWND(hwnd)

    SW_RESTORE = 9
    SW_SHOW = 5

    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)

    if not user32.IsWindowVisible(hwnd):
        user32.ShowWindow(hwnd, SW_SHOW)

    if not user32.IsWindowVisible(hwnd):
        return False

    fg = user32.GetForegroundWindow()
    if fg == hwnd:
        return True

    my_tid = kernel32.GetCurrentThreadId()
    fg_tid = user32.GetWindowThreadProcessId(wintypes.HWND(fg), None) if fg else 0
    if fg and fg_tid and fg_tid != my_tid:
        user32.AttachThreadInput(my_tid, fg_tid, True)

    user32.SetForegroundWindow(hwnd)

    if fg and fg_tid and fg_tid != my_tid:
        user32.AttachThreadInput(my_tid, fg_tid, False)

    # Fallback: SetWindowPos trick if still not foreground
    if user32.GetForegroundWindow() != hwnd:
        HWND_TOPMOST = wintypes.HWND(-1)
        HWND_NOTOPMOST = wintypes.HWND(-2)
        SWP_NOMOVE = 0x0002
        SWP_NOSIZE = 0x0001
        SWP_SHOWWINDOW = 0x0040
        user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
        user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)

    return user32.GetForegroundWindow() == hwnd
