from __future__ import annotations

import ctypes
import shutil
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from .wechat_logging import get_logger


logger = get_logger(__name__)

CF_HDROP = 15
GMEM_MOVEABLE = 0x0002
GMEM_ZEROINIT = 0x0040


@dataclass(frozen=True)
class ClipboardFileResult:
    status: str
    method: str = "clipboard_file"
    source_paths: list[str] = field(default_factory=list)
    artifact_paths: list[str] = field(default_factory=list)
    error: str = ""


class _DropFiles(ctypes.Structure):
    _fields_ = [
        ("pFiles", ctypes.c_uint32),
        ("pt_x", ctypes.c_long),
        ("pt_y", ctypes.c_long),
        ("fNC", ctypes.c_int),
        ("fWide", ctypes.c_int),
    ]


def set_clipboard_file_paths(paths: Iterable[str | Path]) -> bool:
    normalized = [str(Path(path).resolve()) for path in paths if Path(path).is_file()]
    if not normalized:
        return False

    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        user32 = ctypes.WinDLL("user32", use_last_error=True)
    except Exception:
        return False

    kernel32.GlobalAlloc.argtypes = [ctypes.c_uint, ctypes.c_size_t]
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalFree.argtypes = [ctypes.c_void_p]
    user32.OpenClipboard.argtypes = [ctypes.c_void_p]
    user32.OpenClipboard.restype = ctypes.c_bool
    user32.CloseClipboard.argtypes = []
    user32.CloseClipboard.restype = ctypes.c_bool
    user32.EmptyClipboard.argtypes = []
    user32.EmptyClipboard.restype = ctypes.c_bool
    user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
    user32.SetClipboardData.restype = ctypes.c_void_p

    path_bytes = ("\0".join(normalized) + "\0\0").encode("utf-16le")
    header = _DropFiles(
        pFiles=ctypes.sizeof(_DropFiles),
        pt_x=0,
        pt_y=0,
        fNC=0,
        fWide=1,
    )
    total_size = ctypes.sizeof(header) + len(path_bytes)
    handle = kernel32.GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total_size)
    if not handle:
        return False

    clipboard_open = False
    transferred = False
    try:
        locked = kernel32.GlobalLock(handle)
        if not locked:
            return False
        try:
            address = ctypes.cast(locked, ctypes.c_void_p).value
            if not address:
                return False
            ctypes.memmove(address, ctypes.byref(header), ctypes.sizeof(header))
            ctypes.memmove(address + ctypes.sizeof(header), path_bytes, len(path_bytes))
        finally:
            kernel32.GlobalUnlock(handle)

        for _ in range(10):
            if user32.OpenClipboard(None):
                clipboard_open = True
                break
            time.sleep(0.03)
        if not clipboard_open or not user32.EmptyClipboard():
            return False
        if not user32.SetClipboardData(CF_HDROP, handle):
            return False
        transferred = True
        return True
    except Exception as exc:
        logger.debug("wechat clipboard CF_HDROP write failed: %s", exc)
        return False
    finally:
        if clipboard_open:
            user32.CloseClipboard()
        if not transferred:
            kernel32.GlobalFree(handle)


def press_paste_shortcut() -> bool:
    try:
        import win32api
        import win32con

        win32api.keybd_event(win32con.VK_CONTROL, 0, 0, 0)
        win32api.keybd_event(ord("V"), 0, 0, 0)
        win32api.keybd_event(ord("V"), 0, win32con.KEYEVENTF_KEYUP, 0)
        win32api.keybd_event(win32con.VK_CONTROL, 0, win32con.KEYEVENTF_KEYUP, 0)
        return True
    except Exception as exc:
        logger.debug("wechat clipboard paste shortcut failed: %s", exc)
        return False


def read_clipboard_file_paths() -> list[Path]:
    try:
        import win32clipboard
    except Exception:
        return _read_clipboard_file_paths_win32()

    try:
        win32clipboard.OpenClipboard()
        try:
            if not win32clipboard.IsClipboardFormatAvailable(CF_HDROP):
                return []
            data = win32clipboard.GetClipboardData(CF_HDROP)
        finally:
            win32clipboard.CloseClipboard()
    except Exception as exc:
        logger.debug("wechat clipboard CF_HDROP read failed: %s", exc)
        return []

    paths: list[Path] = []
    for item in data or []:
        try:
            paths.append(Path(str(item)))
        except Exception:
            continue
    return paths


def copy_clipboard_files_to_artifacts(
    *,
    root_dir: str | Path,
    content_type: str,
    platform_msg_id: str,
    source_paths: Iterable[Path] | None = None,
) -> ClipboardFileResult:
    sources = list(source_paths) if source_paths is not None else read_clipboard_file_paths()
    if not sources:
        if source_paths is None and content_type in {"image", "emoji"}:
            return copy_clipboard_bitmap_to_artifact(
                root_dir=root_dir,
                content_type=content_type,
                platform_msg_id=platform_msg_id,
            )
        return ClipboardFileResult(status="empty")

    safe_message_id = "".join(char for char in platform_msg_id if char.isalnum() or char in {"-", "_"})
    if not safe_message_id:
        return ClipboardFileResult(status="failed", error="platform_msg_id_unavailable")

    artifact_dir = Path(root_dir) / content_type / safe_message_id
    copied: list[str] = []
    source_values: list[str] = []
    try:
        artifact_dir.mkdir(parents=True, exist_ok=True)
        for index, source in enumerate(sources, start=1):
            source = Path(source)
            source_values.append(str(source))
            if not source.is_file():
                logger.debug("wechat clipboard file source unavailable: %s", source)
                continue
            target = _unique_target_path(artifact_dir, source.name or f"file_{index}")
            shutil.copy2(source, target)
            copied.append(str(target.resolve()))
    except Exception as exc:
        return ClipboardFileResult(
            status="failed",
            source_paths=source_values,
            artifact_paths=copied,
            error=type(exc).__name__,
        )

    if not copied:
        return ClipboardFileResult(status="failed", source_paths=source_values, error="no_readable_files")
    return ClipboardFileResult(status="copied", source_paths=source_values, artifact_paths=copied)


def copy_clipboard_bitmap_to_artifact(
    *,
    root_dir: str | Path,
    content_type: str,
    platform_msg_id: str,
) -> ClipboardFileResult:
    if content_type not in {"image", "emoji"}:
        return ClipboardFileResult(status="empty", method="clipboard_bitmap")

    safe_message_id = "".join(char for char in platform_msg_id if char.isalnum() or char in {"-", "_"})
    if not safe_message_id:
        return ClipboardFileResult(status="failed", method="clipboard_bitmap", error="platform_msg_id_unavailable")

    try:
        from PIL import Image, ImageGrab
    except Exception:
        return ClipboardFileResult(status="failed", method="clipboard_bitmap", error="pillow_unavailable")

    try:
        data = ImageGrab.grabclipboard()
        if data is None:
            return ClipboardFileResult(status="empty", method="clipboard_bitmap")
        if isinstance(data, list):
            return ClipboardFileResult(status="empty", method="clipboard_bitmap")
        if not isinstance(data, Image.Image):
            return ClipboardFileResult(status="failed", method="clipboard_bitmap", error=type(data).__name__)

        artifact_dir = Path(root_dir) / content_type
        artifact_dir.mkdir(parents=True, exist_ok=True)
        target = _unique_target_path(artifact_dir, f"{safe_message_id}.png")
        data.save(target, "PNG")
        return ClipboardFileResult(status="copied", method="clipboard_bitmap", artifact_paths=[str(target.resolve())])
    except Exception as exc:
        return ClipboardFileResult(status="failed", method="clipboard_bitmap", error=type(exc).__name__)


def _unique_target_path(folder: Path, file_name: str) -> Path:
    target = folder / file_name
    if not target.exists():
        return target
    stem = target.stem or "file"
    suffix = target.suffix
    index = 2
    while True:
        candidate = folder / f"{stem}_{index}{suffix}"
        if not candidate.exists():
            return candidate
        index += 1


def _read_clipboard_file_paths_win32() -> list[Path]:
    try:
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        shell32 = ctypes.WinDLL("shell32", use_last_error=True)
    except Exception:
        return []

    user32.OpenClipboard.argtypes = [ctypes.c_void_p]
    user32.OpenClipboard.restype = ctypes.c_bool
    user32.CloseClipboard.argtypes = []
    user32.CloseClipboard.restype = ctypes.c_bool
    user32.IsClipboardFormatAvailable.argtypes = [ctypes.c_uint]
    user32.IsClipboardFormatAvailable.restype = ctypes.c_bool
    user32.GetClipboardData.argtypes = [ctypes.c_uint]
    user32.GetClipboardData.restype = ctypes.c_void_p
    shell32.DragQueryFileW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_wchar_p, ctypes.c_uint]
    shell32.DragQueryFileW.restype = ctypes.c_uint

    if not user32.OpenClipboard(None):
        return []
    try:
        if not user32.IsClipboardFormatAvailable(CF_HDROP):
            return []
        handle = user32.GetClipboardData(CF_HDROP)
        if not handle:
            return []
        count = shell32.DragQueryFileW(handle, 0xFFFFFFFF, None, 0)
        paths: list[Path] = []
        for index in range(count):
            length = shell32.DragQueryFileW(handle, index, None, 0)
            if length <= 0:
                continue
            buffer = ctypes.create_unicode_buffer(length + 1)
            shell32.DragQueryFileW(handle, index, buffer, length + 1)
            if buffer.value:
                paths.append(Path(buffer.value))
        return paths
    except Exception as exc:
        logger.debug("wechat clipboard CF_HDROP win32 read failed: %s", exc)
        return []
    finally:
        user32.CloseClipboard()
