"""
WeChat UI Automation helpers.

This module wraps the working subset of `poc/pywechat-poc2.0` so the main
RPA pipeline can reuse UIA for session switching, input focusing, send-button
clicking, and environment probing.
"""
from __future__ import annotations

from dataclasses import dataclass
import ctypes
from ctypes import wintypes
import sys
import time
from typing import Any, Callable, Optional

from .wechat_logging import get_logger

try:
    import uiautomation as auto
except ImportError:  # pragma: no cover - depends on local environment
    auto = None  # type: ignore[assignment]


logger = get_logger(__name__)


WECHAT_MAIN_CLASS_CANDIDATES: tuple[str, ...] = (
    "mmui::MainWindow",
    "Qt51514QWindowIcon",
    "WeChatMainWndForPC",
)
WECHAT_TITLE_CANDIDATES: tuple[str, ...] = ("微信", "Weixin")

DEFAULT_MAX_VISITS = 8000
DEFAULT_MAX_DEPTH = 40

TAB_CLASS = "mmui::XTabBarItem"
CHATS_TAB_NAME = "微信"
SESSION_LIST_AUTOMATION_ID = "session_list"
SESSION_LIST_CLASS = "mmui::XTableView"
SESSION_ITEM_CLASS = "mmui::ChatSessionCell"
SESSION_ITEM_AUTOMATION_ID_PREFIX = "session_item_"
CHAT_MESSAGE_LIST_AUTOMATION_ID = "chat_message_list"
CHAT_MESSAGE_LIST_CLASS = "mmui::RecyclerListView"
INPUT_AUTOMATION_ID = "chat_input_field"
INPUT_CLASS = "mmui::ChatInputField"
SEND_CLASS = "mmui::XTextView"
CHAT_TEXT_ITEM_CLASS = "mmui::ChatTextItemView"
CHAT_BUBBLE_REFER_ITEM_CLASS = "mmui::ChatBubbleReferItemView"
CONTACT_HEAD_CLASS = "mmui::ContactHeadView"

SESSION_LIST_MAX_X_RATIO = 0.42
BLOCKED_SESSION_NAMES = frozenset({"服务号", "微信支付", "公众号"})

UIA_ENV_HINT = (
    "\n—— 环境提示（微信 4.x 常见）——\n"
    "若已确认会话存在但仍找不到控件：请完全退出微信（结束进程）→ "
    "先打开 Windows「讲述人」→ 再启动微信并登录 → 再运行本程序。\n"
)


@dataclass(frozen=True)
class WechatUiaProbeResult:
    available: bool
    reason: str
    main_window_found: bool
    session_list_found: bool
    chat_input_found: bool
    message_list_found: bool
    send_button_found: bool
    env_hint: str = UIA_ENV_HINT


@dataclass(frozen=True)
class WechatUiaMessageSample:
    kind: str
    name: str
    class_name: str
    automation_id: str
    control_type: str
    rect: str
    left: float = 0.0
    top: float = 0.0
    right: float = 0.0
    bottom: float = 0.0


@dataclass(frozen=True)
class WechatUiaSessionSample:
    name: str
    class_name: str
    automation_id: str
    rect: str
    left: float = 0.0
    top: float = 0.0
    right: float = 0.0
    bottom: float = 0.0


def _uia_import_error() -> Optional[str]:
    if auto is not None:
        return None
    if sys.platform != "win32":
        return "当前仅支持 Windows UIA"
    return "未安装 uiautomation，请先 pip install -r python/rpa/requirements.txt"


def is_wechat_uia_supported() -> bool:
    return _uia_import_error() is None


def _require_auto() -> Any:
    err = _uia_import_error()
    if err:
        raise RuntimeError(err)
    return auto


def _control_rect_repr(ctrl: Any) -> str:
    try:
        return str(ctrl.BoundingRectangle)
    except Exception:
        return "<rect unavailable>"


def _control_rect_tuple(ctrl: Any) -> tuple[float, float, float, float]:
    try:
        r = ctrl.BoundingRectangle
        return float(r.left), float(r.top), float(r.right), float(r.bottom)
    except Exception:
        return 0.0, 0.0, 0.0, 0.0


def _sample_from_control(ctrl: Any, kind: str, *, name: str | None = None) -> WechatUiaMessageSample:
    left, top, right, bottom = _control_rect_tuple(ctrl)
    return WechatUiaMessageSample(
        kind=kind,
        name=_normalize_name(name if name is not None else getattr(ctrl, "Name", "")),
        class_name=(getattr(ctrl, "ClassName", "") or ""),
        automation_id=(getattr(ctrl, "AutomationId", "") or ""),
        control_type=(getattr(ctrl, "ControlTypeName", "") or ""),
        rect=_control_rect_repr(ctrl),
        left=left,
        top=top,
        right=right,
        bottom=bottom,
    )


def _normalize_name(text: str) -> str:
    return " ".join(str(text or "").split()).strip()


def _extract_session_display_name(raw_name: str, automation_id: str) -> str:
    aid = _normalize_name(automation_id)
    if aid.startswith(SESSION_ITEM_AUTOMATION_ID_PREFIX):
        return _normalize_name(aid[len(SESSION_ITEM_AUTOMATION_ID_PREFIX):])
    lines = [_normalize_name(part) for part in str(raw_name or "").splitlines()]
    lines = [line for line in lines if line]
    return lines[0] if lines else _normalize_name(raw_name)


def is_blocked_session_name(name: str) -> bool:
    return _normalize_name(name) in BLOCKED_SESSION_NAMES


def _walk_controls(
    root: Any,
    visit: Callable[[Any], None],
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> int:
    ctr = [0]

    def walk(ctrl: Any, depth: int) -> None:
        if depth > max_depth or ctr[0] > max_visits:
            return
        ctr[0] += 1
        try:
            visit(ctrl)
        except Exception:
            pass
        try:
            for ch in ctrl.GetChildren():
                walk(ch, depth + 1)
        except Exception:
            pass

    walk(root, 0)
    return ctr[0]


def _collect_controls(
    root: Any,
    *,
    predicate: Callable[[Any], bool],
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[Any]:
    found: list[Any] = []

    def visit(ctrl: Any) -> None:
        if predicate(ctrl):
            found.append(ctrl)

    _walk_controls(root, visit, max_visits=max_visits, max_depth=max_depth)
    return found


def _collect_by_class(
    root: Any,
    class_name: str,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[Any]:
    return _collect_controls(
        root,
        predicate=lambda ctrl: (getattr(ctrl, "ClassName", "") or "") == class_name,
        max_visits=max_visits,
        max_depth=max_depth,
    )


def _find_first_by_automation_id(
    root: Any,
    automation_id: str,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    found = _collect_controls(
        root,
        predicate=lambda ctrl: (getattr(ctrl, "AutomationId", "") or "") == automation_id,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    return found[0] if found else None


def _find_ancestor_by_class(ctrl: Any, class_name: str, *, max_hops: int = 8) -> Optional[Any]:
    current = ctrl
    for _ in range(max_hops):
        if current is None:
            return None
        if (getattr(current, "ClassName", "") or "") == class_name:
            return current
        try:
            current = current.GetParentControl()
        except Exception:
            return None
    return None


def find_wechat_main_window_uia() -> Optional[Any]:
    _require_auto()
    for title in WECHAT_TITLE_CANDIDATES:
        for cn in WECHAT_MAIN_CLASS_CANDIDATES:
            w = auto.WindowControl(ClassName=cn, Name=title)
            if w.Exists(2, 1):
                return w
    for cn in WECHAT_MAIN_CLASS_CANDIDATES:
        w = auto.WindowControl(ClassName=cn)
        if w.Exists(2, 1):
            return w
    return None


def activate_wechat_main_window(win: Any) -> None:
    try:
        win.SetActive()
    except Exception:
        pass
    time.sleep(0.25)


def find_session_list_control(
    win: Any,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    ctrl = _find_first_by_automation_id(
        win,
        SESSION_LIST_AUTOMATION_ID,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    if ctrl is not None:
        return ctrl
    candidates = _collect_controls(
        win,
        predicate=lambda c: (getattr(c, "ClassName", "") or "") == SESSION_LIST_CLASS
        and _normalize_name(getattr(c, "Name", "")) in ("会话", "聊天"),
        max_visits=max_visits,
        max_depth=max_depth,
    )
    return candidates[0] if candidates else None


def collect_visible_session_samples(
    win: Any,
    *,
    max_items: int = 60,
    max_visits: int = 30000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[WechatUiaSessionSample]:
    list_root = find_session_list_control(win, max_visits=max_visits, max_depth=max_depth)
    if list_root is None:
        return []

    found: list[WechatUiaSessionSample] = []
    seen_names: set[str] = set()

    def maybe_add(ctrl: Any) -> None:
        if len(found) >= max_items:
            return
        class_name = (getattr(ctrl, "ClassName", "") or "")
        automation_id = (getattr(ctrl, "AutomationId", "") or "")
        if class_name != SESSION_ITEM_CLASS and not str(automation_id).startswith(SESSION_ITEM_AUTOMATION_ID_PREFIX):
            return
        name = _extract_session_display_name(getattr(ctrl, "Name", ""), automation_id)
        if not name or is_blocked_session_name(name):
            return
        key = _normalize_name(name)
        if key in seen_names:
            return
        left, top, right, bottom = _control_rect_tuple(ctrl)
        found.append(
            WechatUiaSessionSample(
                name=key,
                class_name=class_name,
                automation_id=automation_id,
                rect=_control_rect_repr(ctrl),
                left=left,
                top=top,
                right=right,
                bottom=bottom,
            )
        )
        seen_names.add(key)

    try:
        direct_children = list_root.GetChildren()
    except Exception:
        direct_children = []

    for child in direct_children:
        maybe_add(child)
        if len(found) >= max_items:
            break
        try:
            grand_children = child.GetChildren()
        except Exception:
            grand_children = []
        for grand in grand_children:
            maybe_add(grand)
            if len(found) >= max_items:
                break

    if not found:
        _walk_controls(list_root, maybe_add, max_visits=max_visits, max_depth=max_depth)
    found.sort(key=lambda item: (item.top, item.left, item.right))
    return found


def find_chat_message_list_control(
    win: Any,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    ctrl = _find_first_by_automation_id(
        win,
        CHAT_MESSAGE_LIST_AUTOMATION_ID,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    if ctrl is not None:
        return ctrl
    candidates = _collect_controls(
        win,
        predicate=lambda c: (getattr(c, "ClassName", "") or "") == CHAT_MESSAGE_LIST_CLASS
        and _normalize_name(getattr(c, "Name", "")) == "消息",
        max_visits=max_visits,
        max_depth=max_depth,
    )
    return candidates[0] if candidates else None


def find_chat_input(
    win: Any,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    ctrl = _find_first_by_automation_id(
        win,
        INPUT_AUTOMATION_ID,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    if ctrl is not None:
        return ctrl
    inputs = _collect_by_class(win, INPUT_CLASS, max_visits=max_visits, max_depth=max_depth)
    return inputs[0] if inputs else None


def get_current_chat_name(
    win: Any,
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> str:
    ctrl = find_chat_input(win, max_visits=max_visits, max_depth=max_depth)
    if ctrl is None:
        return ""
    return _normalize_name(getattr(ctrl, "Name", ""))


def ensure_chats_tab_selected(
    win: Any,
    *,
    max_visits: int = 4000,
    max_depth: int = 25,
) -> bool:
    items = _collect_by_class(win, TAB_CLASS, max_visits=max_visits, max_depth=max_depth)
    for c in items:
        try:
            if _normalize_name(c.Name) == CHATS_TAB_NAME:
                c.Click(simulateMove=False)
                time.sleep(0.28)
                return True
        except Exception:
            continue
    return False


def _session_title_matches(ctrl_name: str, keyword: str, exact: bool) -> bool:
    a = _normalize_name(ctrl_name)
    b = _normalize_name(keyword)
    if not a or not b:
        return False
    if exact:
        return a == b
    return b in a


def _session_automation_id_matches(automation_id: str, keyword: str, exact: bool) -> bool:
    aid = _normalize_name(automation_id)
    key = _normalize_name(keyword)
    if not aid or not key:
        return False
    expected = SESSION_ITEM_AUTOMATION_ID_PREFIX + key
    if exact:
        return aid == expected or expected in aid
    return expected in aid or key in aid


def find_session_candidate(
    win: Any,
    keyword: str,
    *,
    exact: bool = False,
    session_class: Optional[str] = None,
    session_list_max_x_ratio: float = SESSION_LIST_MAX_X_RATIO,
    max_visits: int = 22000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    list_root = find_session_list_control(win, max_visits=max_visits, max_depth=max_depth) or win
    split_x: Optional[float] = None
    try:
        # Keep the same heuristic as the original POC: split against the whole
        # main window, not the session_list control itself. Using the list width
        # makes the threshold too far left and filters out valid session rows.
        wr = win.BoundingRectangle
        split_x = float(wr.left) + float(wr.width()) * session_list_max_x_ratio
    except Exception:
        split_x = None

    scored: list[tuple[tuple[int, float, float], Any]] = []

    def visit(ctrl: Any) -> None:
        cn = (getattr(ctrl, "ClassName", "") or "")
        if cn == INPUT_CLASS:
            return
        name = _normalize_name(getattr(ctrl, "Name", ""))
        automation_id = _normalize_name(getattr(ctrl, "AutomationId", ""))
        name_match = bool(name) and _session_title_matches(name, keyword, exact)
        aid_match = _session_automation_id_matches(automation_id, keyword, exact)
        if not name_match and not aid_match:
            return
        target_ctrl = _find_ancestor_by_class(ctrl, SESSION_ITEM_CLASS, max_hops=6) or ctrl
        target_cn = (getattr(target_ctrl, "ClassName", "") or "")
        target_aid = _normalize_name(getattr(target_ctrl, "AutomationId", "")) or automation_id
        target_name = _extract_session_display_name(getattr(target_ctrl, "Name", ""), target_aid) or name
        if session_class is not None and target_cn != session_class:
            return
        if cn == SEND_CLASS and "发送" in name:
            return
        try:
            r = target_ctrl.BoundingRectangle
            cx = (float(r.left) + float(r.right)) / 2.0
            if split_x is not None and cx > split_x:
                return
            expected_aid = SESSION_ITEM_AUTOMATION_ID_PREFIX + _normalize_name(keyword)
            exact_name = 0 if target_name == _normalize_name(keyword) else 1
            exact_aid = 0 if target_aid == expected_aid else 1
            prefix_aid = 0 if target_aid.startswith(SESSION_ITEM_AUTOMATION_ID_PREFIX) else 1
            class_pri = 0 if target_cn == SESSION_ITEM_CLASS else 1
            scored.append(((class_pri, prefix_aid, exact_aid, exact_name, float(r.top), cx), target_ctrl))
        except Exception:
            return

    _walk_controls(list_root, visit, max_visits=max_visits, max_depth=max_depth)
    if not scored:
        return None
    scored.sort(key=lambda item: item[0])
    return scored[0][1]


def click_session_candidate(ctrl: Any) -> None:
    target = _find_ancestor_by_class(ctrl, SESSION_ITEM_CLASS, max_hops=6) or ctrl
    try:
        logger.info(
            "click_session_candidate src=(%r,%r,%r) dst=(%r,%r,%r)",
            (getattr(ctrl, "ClassName", "") or ""),
            _normalize_name(getattr(ctrl, "Name", "")),
            _normalize_name(getattr(ctrl, "AutomationId", "")),
            (getattr(target, "ClassName", "") or ""),
            _normalize_name(getattr(target, "Name", "")),
            _normalize_name(getattr(target, "AutomationId", "")),
        )
    except Exception:
        pass
    try:
        target.SetFocus()
    except Exception:
        pass
    target.Click(simulateMove=False)
    time.sleep(0.35)


def find_send_button(
    win: Any,
    *,
    max_visits: int = 25000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> Optional[Any]:
    scored: list[tuple[tuple[int, float], Any]] = []

    def visit(ctrl: Any) -> None:
        if (getattr(ctrl, "ClassName", "") or "") != SEND_CLASS:
            return
        name = _normalize_name(getattr(ctrl, "Name", ""))
        if not name:
            return
        if "发送" not in name and name not in ("发送", "Send") and "Send" not in name:
            return
        try:
            bottom = float(ctrl.BoundingRectangle.bottom)
        except Exception:
            bottom = 0.0
        exact_name = 0 if name in ("发送", "Send") else 1
        scored.append(((exact_name, -bottom), ctrl))

    _walk_controls(win, visit, max_visits=max_visits, max_depth=max_depth)
    if not scored:
        return None
    scored.sort(key=lambda item: item[0])
    return scored[0][1]


def focus_chat_input(ctrl: Any) -> bool:
    ok = False
    try:
        ctrl.SetFocus()
        ok = True
    except Exception:
        pass
    try:
        ctrl.Click(simulateMove=False)
        ok = True
    except Exception:
        pass
    time.sleep(0.12)
    return ok


def click_send_button(ctrl: Any) -> bool:
    try:
        ctrl.Click(simulateMove=False)
        time.sleep(0.12)
        return True
    except Exception:
        return False


def _win32_clipboard_get() -> str:
    CF_UNICODETEXT = 13
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    user32.OpenClipboard.argtypes = [wintypes.HWND]
    user32.OpenClipboard.restype = wintypes.BOOL
    user32.CloseClipboard.argtypes = []
    user32.CloseClipboard.restype = wintypes.BOOL
    user32.GetClipboardData.argtypes = [wintypes.UINT]
    user32.GetClipboardData.restype = wintypes.HANDLE
    kernel32.GlobalLock.argtypes = [wintypes.HGLOBAL]
    kernel32.GlobalLock.restype = wintypes.LPVOID
    kernel32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
    kernel32.GlobalUnlock.restype = wintypes.BOOL
    if not user32.OpenClipboard(None):
        return ""
    try:
        handle = user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return ""
        ptr = kernel32.GlobalLock(handle)
        if not ptr:
            return ""
        try:
            return ctypes.wstring_at(ptr)
        finally:
            kernel32.GlobalUnlock(handle)
    finally:
        user32.CloseClipboard()


def _win32_clipboard_set(text: str) -> None:
    CF_UNICODETEXT = 13
    GMEM_MOVEABLE = 0x0002
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    user32.OpenClipboard.argtypes = [wintypes.HWND]
    user32.OpenClipboard.restype = wintypes.BOOL
    user32.CloseClipboard.argtypes = []
    user32.CloseClipboard.restype = wintypes.BOOL
    user32.EmptyClipboard.argtypes = []
    user32.EmptyClipboard.restype = wintypes.BOOL
    user32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
    user32.SetClipboardData.restype = wintypes.HANDLE
    kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
    kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
    kernel32.GlobalFree.argtypes = [wintypes.HGLOBAL]
    kernel32.GlobalFree.restype = wintypes.HGLOBAL
    kernel32.GlobalLock.argtypes = [wintypes.HGLOBAL]
    kernel32.GlobalLock.restype = wintypes.LPVOID
    kernel32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
    kernel32.GlobalUnlock.restype = wintypes.BOOL
    data = text.encode("utf-16-le") + b"\x00\x00"
    size = len(data)
    if not user32.OpenClipboard(None):
        raise OSError("OpenClipboard 失败（可能被其他程序占用）")
    try:
        user32.EmptyClipboard()
        h_global = kernel32.GlobalAlloc(GMEM_MOVEABLE, size)
        if not h_global:
            raise OSError("GlobalAlloc 失败")
        ptr = kernel32.GlobalLock(h_global)
        if not ptr:
            kernel32.GlobalFree(h_global)
            raise OSError("GlobalLock 失败")
        try:
            ctypes.memmove(ptr, data, size)
        finally:
            kernel32.GlobalUnlock(h_global)
        if not user32.SetClipboardData(CF_UNICODETEXT, h_global):
            kernel32.GlobalFree(h_global)
            raise OSError("SetClipboardData 失败")
    finally:
        user32.CloseClipboard()


def _clipboard_get() -> str:
    if sys.platform == "win32":
        try:
            return _win32_clipboard_get()
        except Exception:
            return ""
    return ""


def _clipboard_set(text: str) -> None:
    if sys.platform != "win32":
        raise RuntimeError("当前仅支持 Windows 剪贴板")
    _win32_clipboard_set(text)


def clear_chat_input_via_shortcuts() -> None:
    _require_auto()
    auto.SendKeys("{Ctrl}a", waitTime=0.02)
    auto.SendKeys("{Delete}", waitTime=0.02)
    time.sleep(0.05)


def paste_text_via_clipboard(text: str) -> None:
    _require_auto()
    old = _clipboard_get()
    try:
        _clipboard_set(text)
        time.sleep(0.06)
        auto.SendKeys("{Ctrl}v", waitTime=0.05)
        time.sleep(0.12)
    finally:
        try:
            _clipboard_set(old)
        except Exception:
            pass


def send_enter_key() -> None:
    _require_auto()
    auto.SendKeys("{Enter}", waitTime=0.03)
    time.sleep(0.08)


def probe_wechat_uia_capabilities(
    *,
    max_visits: int = DEFAULT_MAX_VISITS,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> WechatUiaProbeResult:
    err = _uia_import_error()
    if err:
        return WechatUiaProbeResult(
            available=False,
            reason=err,
            main_window_found=False,
            session_list_found=False,
            chat_input_found=False,
            message_list_found=False,
            send_button_found=False,
        )
    win = find_wechat_main_window_uia()
    if win is None:
        return WechatUiaProbeResult(
            available=False,
            reason="未找到微信主窗口",
            main_window_found=False,
            session_list_found=False,
            chat_input_found=False,
            message_list_found=False,
            send_button_found=False,
        )
    session_list = find_session_list_control(win, max_visits=max_visits, max_depth=max_depth)
    chat_input = find_chat_input(win, max_visits=max_visits, max_depth=max_depth)
    message_list = find_chat_message_list_control(win, max_visits=max_visits, max_depth=max_depth)
    send_btn = find_send_button(win, max_visits=max_visits * 3, max_depth=max_depth)
    ok = all((session_list is not None, chat_input is not None, message_list is not None))
    reason = "UIA 可用" if ok else "UIA 部分可用，但核心控件未全部找到"
    return WechatUiaProbeResult(
        available=ok,
        reason=reason,
        main_window_found=True,
        session_list_found=session_list is not None,
        chat_input_found=chat_input is not None,
        message_list_found=message_list is not None,
        send_button_found=send_btn is not None,
    )


def collect_chat_text_samples(
    win: Any,
    *,
    max_items: int = 20,
    max_visits: int = 30000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[WechatUiaMessageSample]:
    samples = collect_chat_layout_samples(
        win,
        max_items=max_items,
        max_visits=max_visits,
        max_depth=max_depth,
    )
    return [item for item in samples if item.kind != "contact_head"]


def collect_chat_layout_samples(
    win: Any,
    *,
    max_items: int = 20,
    max_visits: int = 30000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[WechatUiaMessageSample]:
    message_root = find_chat_message_list_control(win, max_visits=max_visits, max_depth=max_depth)
    if message_root is None:
        return []
    found: list[WechatUiaMessageSample] = []
    matched_message_count = 0

    def visit(ctrl: Any) -> None:
        nonlocal matched_message_count
        if matched_message_count >= max_items:
            return
        class_name = (getattr(ctrl, "ClassName", "") or "")
        if class_name not in (
            CHAT_TEXT_ITEM_CLASS,
            CHAT_BUBBLE_REFER_ITEM_CLASS,
            CONTACT_HEAD_CLASS,
        ):
            return
        name = _normalize_name(getattr(ctrl, "Name", ""))
        if class_name == CONTACT_HEAD_CLASS:
            kind = "contact_head"
        else:
            if not name:
                return
            kind = "text" if class_name == CHAT_TEXT_ITEM_CLASS else "bubble_ref"
            if name == "[图片]":
                kind = "image"
            elif name in ("[动画表情]", "[表情]"):
                kind = "emoji"
            matched_message_count += 1
        found.append(_sample_from_control(ctrl, kind, name=name))

    _walk_controls(message_root, visit, max_visits=max_visits, max_depth=max_depth)
    found.sort(key=lambda item: (item.top, item.left, item.right, item.kind))
    return found


def probe_contact_heads_by_hit_test(
    message: WechatUiaMessageSample,
    *,
    x_offsets: tuple[int, ...] = (8, 20, 36, 56, 80, 108),
    y_ratios: tuple[float, ...] = (0.25, 0.5, 0.75),
    max_hops: int = 8,
) -> list[WechatUiaMessageSample]:
    if auto is None:
        return []
    width = max(1.0, float(message.right) - float(message.left))
    height = max(1.0, float(message.bottom) - float(message.top))
    x_points: list[int] = []
    for offset in x_offsets:
        if offset < width:
            x_points.append(int(float(message.left) + offset))
            x_points.append(int(float(message.right) - offset))
    y_points = [int(float(message.top) + height * ratio) for ratio in y_ratios]
    seen: set[tuple[str, str, str]] = set()
    found: list[WechatUiaMessageSample] = []
    for x in x_points:
        for y in y_points:
            try:
                ctrl = auto.ControlFromPoint(x, y)
            except Exception:
                continue
            head = _find_ancestor_by_class(ctrl, CONTACT_HEAD_CLASS, max_hops=max_hops)
            if head is None:
                continue
            sample = _sample_from_control(head, "contact_head")
            key = (sample.class_name, sample.automation_id, sample.rect)
            if key in seen:
                continue
            seen.add(key)
            found.append(sample)
    found.sort(key=lambda item: (item.top, item.left, item.right))
    return found


def collect_chat_message_samples(
    win: Any,
    *,
    max_items: int = 20,
    max_visits: int = 30000,
    max_depth: int = DEFAULT_MAX_DEPTH,
) -> list[WechatUiaMessageSample]:
    return collect_chat_text_samples(
        win,
        max_items=max_items,
        max_visits=max_visits,
        max_depth=max_depth,
    )
