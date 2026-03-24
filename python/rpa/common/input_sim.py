"""
Win32 input simulation: mouse click, keyboard, clipboard.
For Writer: focus input box, paste text, click send.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from typing import Optional

user32 = ctypes.WinDLL("user32", use_last_error=True)

ULONG_PTR = ctypes.c_size_t

# MOUSEINPUT
class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ULONG_PTR)),
    ]

# KEYBDINPUT
class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ULONG_PTR)),
    ]

class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", wintypes.DWORD),
        ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    ]

class INPUT_UNION(ctypes.Union):
    _fields_ = [
        ("mi", MOUSEINPUT),
        ("ki", KEYBDINPUT),
        ("hi", HARDWAREINPUT),
    ]

class INPUT(ctypes.Structure):
    _fields_ = [
        ("type", wintypes.DWORD),
        ("_union", INPUT_UNION),
    ]

INPUT_MOUSE = 0
INPUT_KEYBOARD = 1
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004

WM_PASTE = 0x0302


def client_to_screen(hwnd: int, client_x: int, client_y: int) -> tuple[int, int]:
    """Convert client coords to screen coords."""
    class POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]
    pt = POINT(client_x, client_y)
    if not user32.ClientToScreen(wintypes.HWND(hwnd), ctypes.byref(pt)):
        raise ctypes.WinError(ctypes.get_last_error())
    return pt.x, pt.y


def set_cursor_pos(x: int, y: int) -> None:
    if not user32.SetCursorPos(x, y):
        raise ctypes.WinError(ctypes.get_last_error())


def simulate_click(screen_x: int, screen_y: int, delay_ms: int = 30) -> None:
    """Mouse click at screen coordinates."""
    set_cursor_pos(screen_x, screen_y)
    if delay_ms > 0:
        import time
        time.sleep(delay_ms / 1000.0)

    def make_mouse_input(flags: int) -> INPUT:
        inp = INPUT()
        inp.type = INPUT_MOUSE
        inp._union.mi = MOUSEINPUT(0, 0, 0, flags, 0, None)
        return inp

    inputs = (INPUT * 2)()
    inputs[0] = make_mouse_input(MOUSEEVENTF_LEFTDOWN)
    inputs[1] = make_mouse_input(MOUSEEVENTF_LEFTUP)

    if user32.SendInput(2, ctypes.byref(inputs), ctypes.sizeof(INPUT)) != 2:
        raise ctypes.WinError(ctypes.get_last_error())


def simulate_double_click(screen_x: int, screen_y: int, delay_ms: int = 30) -> None:
    """Double-click at screen coordinates (some input boxes need this to focus)."""
    simulate_click(screen_x, screen_y, delay_ms)
    import time
    time.sleep(0.08)
    simulate_click(screen_x, screen_y, delay_ms)


def simulate_key(vk: int, key_up: bool = False) -> None:
    """Simulate key press. vk = virtual key code."""
    inp = INPUT()
    inp.type = INPUT_KEYBOARD
    inp._union.ki = KEYBDINPUT(
        wintypes.WORD(vk),
        wintypes.WORD(0),
        wintypes.DWORD(KEYEVENTF_KEYUP if key_up else 0),
        0,
        None,
    )
    if user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT)) != 1:
        raise ctypes.WinError(ctypes.get_last_error())


def simulate_key_combo(modifier_vk: int, key_vk: int) -> None:
    """Simulate Ctrl+V, Ctrl+A, etc."""
    simulate_key(modifier_vk)
    simulate_key(key_vk)
    simulate_key(key_vk, key_up=True)
    simulate_key(modifier_vk, key_up=True)


def _send_unicode_char(code: int) -> None:
    """Send single Unicode char via KEYEVENTF_UNICODE (down + up)."""
    inp_down = INPUT()
    inp_down.type = INPUT_KEYBOARD
    inp_down._union.ki = KEYBDINPUT(0, wintypes.WORD(code), KEYEVENTF_UNICODE, 0, None)
    inp_up = INPUT()
    inp_up.type = INPUT_KEYBOARD
    inp_up._union.ki = KEYBDINPUT(0, wintypes.WORD(code), KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0, None)
    user32.SendInput(1, ctypes.byref(inp_down), ctypes.sizeof(INPUT))
    user32.SendInput(1, ctypes.byref(inp_up), ctypes.sizeof(INPUT))


def simulate_type_unicode(text: str, delay_per_char_ms: float = 12) -> None:
    """
    Type text by sending Unicode characters via SendInput(KEYEVENTF_UNICODE).
    Bypasses clipboard - works when Ctrl+V paste fails (e.g. WeChat custom input).
    """
    import time
    for ch in text:
        code = ord(ch)
        if code > 0xFFFF:
            hi = 0xD800 + ((code - 0x10000) >> 10)
            lo = 0xDC00 + ((code - 0x10000) & 0x3FF)
            _send_unicode_char(hi)
            time.sleep(delay_per_char_ms / 1000.0)
            _send_unicode_char(lo)
        else:
            _send_unicode_char(code)
        time.sleep(delay_per_char_ms / 1000.0)


def send_paste_to_window_at_point(screen_x: int, screen_y: int) -> bool:
    """
    Send WM_PASTE to the window at the given screen coordinates.
    Clipboard must be set before calling. Returns True if a window was found.
    """
    class POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]
    pt = POINT(screen_x, screen_y)
    hwnd = user32.WindowFromPoint(pt)
    if not hwnd:
        return False
    user32.SendMessageW(wintypes.HWND(hwnd), WM_PASTE, 0, 0)
    return True


def set_clipboard_text(text: str) -> bool:
    """Set clipboard to text. Returns True on success."""
    CF_UNICODETEXT = 13
    GMEM_MOVEABLE = 0x0002
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    if not user32.OpenClipboard(None):
        return False
    try:
        user32.EmptyClipboard()
        wstr = text.encode("utf-16-le") + b"\x00\x00"
        size = len(wstr)
        hmem = kernel32.GlobalAlloc(GMEM_MOVEABLE, size)
        if not hmem:
            return False
        ptr = kernel32.GlobalLock(hmem)
        if not ptr:
            kernel32.GlobalFree(hmem)
            return False
        ctypes.memmove(ptr, wstr, size)
        kernel32.GlobalUnlock(hmem)
        user32.SetClipboardData(CF_UNICODETEXT, hmem)
        return True
    finally:
        user32.CloseClipboard()


def get_clipboard_text() -> Optional[str]:
    """Get clipboard text. Returns None if not text."""
    CF_UNICODETEXT = 13
    if not user32.OpenClipboard(None):
        return None
    try:
        h = user32.GetClipboardData(CF_UNICODETEXT)
        if not h:
            return None
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        ptr = kernel32.GlobalLock(h)
        if not ptr:
            return None
        # Read until null
        data = ctypes.string_at(ptr)
        kernel32.GlobalUnlock(h)
        return data.decode("utf-16-le").rstrip("\x00")
    finally:
        user32.CloseClipboard()


class ClipboardGuard:
    """RAII: save clipboard on enter, restore on exit."""

    def __init__(self):
        self._saved: Optional[str] = None

    def __enter__(self):
        self._saved = get_clipboard_text()
        return self

    def __exit__(self, *args):
        if self._saved is not None:
            set_clipboard_text(self._saved)


# ---------------------------------------------------------------------------
# PostMessage-based input (background, no foreground activation needed)
# ---------------------------------------------------------------------------

WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
MK_LBUTTON = 0x0001


def post_click(hwnd: int, client_x: int, client_y: int, delay_ms: int = 20) -> None:
    """
    Send mouse click to a specific window via PostMessage.
    Works in background — the target window does NOT need to be in the foreground.
    Coordinates are relative to the window's client area.
    """
    lParam = ((client_y & 0xFFFF) << 16) | (client_x & 0xFFFF)
    h = wintypes.HWND(hwnd)
    user32.PostMessageW(h, WM_MOUSEMOVE, 0, lParam)
    import time as _t
    _t.sleep(0.01)
    user32.PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, lParam)
    if delay_ms > 0:
        _t.sleep(delay_ms / 1000.0)
    user32.PostMessageW(h, WM_LBUTTONUP, 0, lParam)


def post_click_window_at_point(screen_x: int, screen_y: int, delay_ms: int = 20) -> bool:
    """
    PostMessage click to the concrete child window at screen point.
    Compared with posting to top-level hwnd, this is more reliable for Qt child controls.
    """
    class POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]

    pt = POINT(screen_x, screen_y)
    h = user32.WindowFromPoint(pt)
    if not h:
        return False
    if not user32.ScreenToClient(h, ctypes.byref(pt)):
        return False
    post_click(int(h), int(pt.x), int(pt.y), delay_ms=delay_ms)
    return True


def post_key(hwnd: int, vk: int) -> None:
    """Send key press+release to window via PostMessage (background)."""
    h = wintypes.HWND(hwnd)
    user32.PostMessageW(h, WM_KEYDOWN, vk, 0)
    user32.PostMessageW(h, WM_KEYUP, vk, 0)


def post_key_combo(hwnd: int, modifier_vk: int, key_vk: int) -> None:
    """Send key combo (e.g. Ctrl+V) to window via PostMessage (background).

    WARNING: PostMessage does NOT modify actual keyboard state, so the target
    application's GetKeyState() will NOT see the modifier as pressed.  Qt Quick
    may interpret Ctrl+A as plain 'a'.  Prefer post_clear_text + post_type_text
    for reliable background text operations.
    """
    h = wintypes.HWND(hwnd)
    user32.PostMessageW(h, WM_KEYDOWN, modifier_vk, 0)
    user32.PostMessageW(h, WM_KEYDOWN, key_vk, 0)
    user32.PostMessageW(h, WM_KEYUP, key_vk, 0)
    user32.PostMessageW(h, WM_KEYUP, modifier_vk, 0)


VK_END = 0x23
VK_BACK = 0x08


def post_clear_text(hwnd: int, max_chars: int = 50) -> None:
    """Clear text in a focused control via PostMessage: End key then Backspace × N.

    Avoids key combos (Ctrl+A) which don't work via PostMessage because the
    modifier state is not reflected in GetKeyState().
    """
    h = wintypes.HWND(hwnd)
    user32.PostMessageW(h, WM_KEYDOWN, VK_END, 0)
    user32.PostMessageW(h, WM_KEYUP, VK_END, 0)
    for _ in range(max_chars):
        user32.PostMessageW(h, WM_KEYDOWN, VK_BACK, 0)
        user32.PostMessageW(h, WM_KEYUP, VK_BACK, 0)


def post_type_text(hwnd: int, text: str, delay_per_char_ms: float = 5) -> None:
    """
    Type text by sending WM_CHAR messages to window (background).
    Works for Unicode text including Chinese characters.
    """
    import time as _t
    h = wintypes.HWND(hwnd)
    for ch in text:
        code = ord(ch)
        if code > 0xFFFF:
            hi = 0xD800 + ((code - 0x10000) >> 10)
            lo = 0xDC00 + ((code - 0x10000) & 0x3FF)
            user32.PostMessageW(h, WM_CHAR, hi, 0)
            _t.sleep(delay_per_char_ms / 1000.0)
            user32.PostMessageW(h, WM_CHAR, lo, 0)
        else:
            user32.PostMessageW(h, WM_CHAR, code, 0)
        if delay_per_char_ms > 0:
            _t.sleep(delay_per_char_ms / 1000.0)
