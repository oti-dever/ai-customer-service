from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Callable


@dataclass(frozen=True)
class ClickResult:
    ok: bool
    method: str
    foreground: bool = False
    detail: str = ""


def click_send_button_detailed(control: Any, *, fallback_hwnd: int = 0, allow_foreground: bool = True) -> ClickResult:
    strategies: list[tuple[str, bool, Callable[[Any], bool]]] = [
        ("message_main_window_send", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_post=False)),
        ("message_main_window_post", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_post=True)),
        ("invoke", False, _click_by_invoke),
    ]
    if allow_foreground:
        strategies.extend(
            [
                ("uia_click", True, _click_by_uia),
                ("foreground_mouse", True, _click_by_foreground_mouse),
            ]
        )

    for method, foreground, fn in strategies:
        if fn(control):
            return ClickResult(ok=True, method=method, foreground=foreground)
    return ClickResult(ok=False, method="none", detail="all send button click strategies failed")


def click_session_item_detailed(control: Any, *, fallback_hwnd: int = 0, allow_foreground: bool = True) -> ClickResult:
    strategies: list[tuple[str, bool, Callable[[Any], bool]]] = [
        ("message_main_window_send", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_post=False)),
        ("message_main_window_post", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_post=True)),
        ("message_local_send", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_local_coords=True, use_post=False)),
        ("message_local_post", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_local_coords=True, use_post=True)),
        ("message_double_client_send", False, lambda c: _click_by_window_message(c, fallback_hwnd=fallback_hwnd, use_double_click=True)),
        ("message_parent_chain_send", False, lambda c: _click_by_window_message_parent_chain(c, fallback_hwnd=fallback_hwnd, use_post=False)),
        ("message_parent_chain_post", False, lambda c: _click_by_window_message_parent_chain(c, fallback_hwnd=fallback_hwnd, use_post=True)),
        ("selection_item", False, _click_by_selection_item),
        ("invoke", False, _click_by_invoke),
        ("focus_enter", False, _click_by_focus_enter),
    ]
    if allow_foreground:
        strategies.extend(
            [
                ("uia_click", True, _click_by_uia),
                ("foreground_mouse", True, _click_by_foreground_mouse),
            ]
        )

    for method, foreground, fn in strategies:
        if fn(control):
            return ClickResult(ok=True, method=method, foreground=foreground)
    return ClickResult(ok=False, method="none", detail="all session click strategies failed")


def click_session_item_background_detailed(control: Any, *, fallback_hwnd: int = 0) -> ClickResult:
    return click_session_item_detailed(control, fallback_hwnd=fallback_hwnd, allow_foreground=False)


def click_session_item_foreground_detailed(control: Any) -> ClickResult:
    strategies: list[tuple[str, bool, Callable[[Any], bool]]] = [
        ("uia_click", True, _click_by_uia),
        ("foreground_mouse", True, _click_by_foreground_mouse),
    ]
    for method, foreground, fn in strategies:
        if fn(control):
            return ClickResult(ok=True, method=method, foreground=foreground)
    return ClickResult(ok=False, method="none", detail="all foreground session click strategies failed")


def _click_by_window_message(
    control: Any,
    *,
    fallback_hwnd: int,
    use_local_coords: bool = False,
    use_post: bool = False,
    use_double_click: bool = False,
) -> bool:
    try:
        import win32api
        import win32con
        import win32gui
    except Exception:
        return False

    hwnd = _find_native_window_handle(control, fallback_hwnd=fallback_hwnd)
    if not hwnd:
        return False

    try:
        rect = control.BoundingRectangle
        if use_local_coords and not fallback_hwnd:
            x = max(1, int((rect.right - rect.left) / 2))
            y = max(1, int((rect.bottom - rect.top) / 2))
        else:
            screen_x = int((rect.left + rect.right) / 2)
            screen_y = int((rect.top + rect.bottom) / 2)
            x, y = win32gui.ScreenToClient(hwnd, (screen_x, screen_y))

        lparam = win32api.MAKELONG(x, y)
        send = win32gui.PostMessage if use_post else win32gui.SendMessage

        send(hwnd, win32con.WM_MOUSEMOVE, 0, lparam)
        send(hwnd, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lparam)
        time.sleep(0.05)
        send(hwnd, win32con.WM_LBUTTONUP, 0, lparam)
        if use_double_click:
            time.sleep(0.05)
            send(hwnd, win32con.WM_LBUTTONDBLCLK, win32con.MK_LBUTTON, lparam)
            time.sleep(0.05)
            send(hwnd, win32con.WM_LBUTTONUP, 0, lparam)
        return True
    except Exception:
        return False


def _click_by_window_message_parent_chain(control: Any, *, fallback_hwnd: int, use_post: bool) -> bool:
    try:
        import win32api
        import win32con
        import win32gui
    except Exception:
        return False

    handles = _native_window_handle_chain(control)
    if fallback_hwnd and int(fallback_hwnd) not in handles:
        handles.insert(0, int(fallback_hwnd))
    if not handles:
        return False

    try:
        rect = control.BoundingRectangle
        screen_x = int((rect.left + rect.right) / 2)
        screen_y = int((rect.top + rect.bottom) / 2)
        send = win32gui.PostMessage if use_post else win32gui.SendMessage

        for hwnd in handles:
            x, y = win32gui.ScreenToClient(hwnd, (screen_x, screen_y))
            lparam = win32api.MAKELONG(x, y)
            send(hwnd, win32con.WM_MOUSEMOVE, 0, lparam)
            send(hwnd, win32con.WM_LBUTTONDOWN, win32con.MK_LBUTTON, lparam)
            time.sleep(0.03)
            send(hwnd, win32con.WM_LBUTTONUP, 0, lparam)
            time.sleep(0.05)
        return True
    except Exception:
        return False


def _click_by_selection_item(control: Any) -> bool:
    try:
        get_selection_item_pattern = getattr(control, "GetSelectionItemPattern", None)
        if not get_selection_item_pattern:
            return False
        pattern = get_selection_item_pattern()
        if not pattern:
            return False
        pattern.Select()
        return True
    except Exception:
        return False


def _click_by_invoke(control: Any) -> bool:
    try:
        get_invoke_pattern = getattr(control, "GetInvokePattern", None)
        if not get_invoke_pattern:
            return False
        pattern = get_invoke_pattern()
        if not pattern:
            return False
        pattern.Invoke()
        return True
    except Exception:
        return False


def _click_by_uia(control: Any) -> bool:
    try:
        set_focus = getattr(control, "SetFocus", None)
        if set_focus:
            try:
                set_focus()
            except Exception:
                pass
        control.Click(simulateMove=False, waitTime=0)
        time.sleep(0.12)
        return True
    except Exception:
        return False


def _click_by_focus_enter(control: Any) -> bool:
    try:
        import win32api
        import win32con
    except Exception:
        return False

    try:
        set_focus = getattr(control, "SetFocus", None)
        if set_focus:
            set_focus()
        time.sleep(0.05)
        win32api.keybd_event(win32con.VK_RETURN, 0, 0, 0)
        time.sleep(0.03)
        win32api.keybd_event(win32con.VK_RETURN, 0, win32con.KEYEVENTF_KEYUP, 0)
        return True
    except Exception:
        return False


def _click_by_foreground_mouse(control: Any) -> bool:
    try:
        import win32api
        import win32con
        import win32gui
    except Exception:
        return False

    hwnd = _find_native_window_handle(control)
    if not hwnd:
        return False

    try:
        rect = control.BoundingRectangle
        screen_x = int((rect.left + rect.right) / 2)
        screen_y = int((rect.top + rect.bottom) / 2)
        try:
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
            win32gui.SetForegroundWindow(hwnd)
        except Exception:
            pass
        old_x, old_y = win32api.GetCursorPos()
        win32api.SetCursorPos((screen_x, screen_y))
        time.sleep(0.04)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.04)
        win32api.mouse_event(win32con.MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        win32api.SetCursorPos((old_x, old_y))
        return True
    except Exception:
        return False


def _find_native_window_handle(control: Any, *, fallback_hwnd: int = 0) -> int:
    current = control
    for _ in range(10):
        hwnd = getattr(current, "NativeWindowHandle", 0) or 0
        if hwnd:
            return int(hwnd)
        try:
            current = current.GetParentControl()
        except Exception:
            break
        if not current:
            break
    return int(fallback_hwnd or 0)


def _native_window_handle_chain(control: Any, max_depth: int = 10) -> list[int]:
    handles: list[int] = []
    current = control
    for _ in range(max_depth):
        if not current:
            break
        hwnd = getattr(current, "NativeWindowHandle", 0) or 0
        if hwnd and int(hwnd) not in handles:
            handles.append(int(hwnd))
        try:
            current = current.GetParentControl()
        except Exception:
            break
    return handles
