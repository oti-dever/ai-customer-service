import hashlib
import json
import sqlite3
import time
import asyncio
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional

import ctypes
from ctypes import wintypes


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = PROJECT_ROOT / "database" / "app.db"
CONFIG_PATH = Path(__file__).resolve().parent / "wechat_config.json"
DEBUG_DIR = Path(__file__).resolve().parent / "_debug"


def open_db():
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn


@dataclass
class WechatConfig:
    poll_interval_sec: int
    chat_x: int
    chat_y: int
    chat_w: int
    chat_h: int
    platform_conversation_id: str
    customer_name: str
    hwnd: Optional[int]
    process_name: str
    title_contains: str


def load_config() -> WechatConfig:
    data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    region = data["chat_region"]
    conv = data["conversation"]
    wm = data.get("window_match", {}) or {}
    hwnd_hex = data.get("hwnd_hex")
    hwnd = None
    if isinstance(hwnd_hex, str) and hwnd_hex.lower().startswith("0x"):
        try:
            hwnd = int(hwnd_hex, 16)
        except ValueError:
            hwnd = None
    return WechatConfig(
        poll_interval_sec=int(data.get("poll_interval_sec", 3)),
        chat_x=int(region["x"]),
        chat_y=int(region["y"]),
        chat_w=int(region["w"]),
        chat_h=int(region["h"]),
        platform_conversation_id=str(conv.get("platform_conversation_id", "demo_wechat_conv_1")),
        customer_name=str(conv.get("customer_name", "演示微信联系人")),
        hwnd=hwnd,
        process_name=str(wm.get("process_name", "")),
        title_contains=str(wm.get("title_contains", "")),
    )


# ----------------- Win32 window locate + capture -----------------

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def _get_window_text(hwnd: wintypes.HWND) -> str:
    length = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buf, length + 1)
    return buf.value


def _get_class_name(hwnd: wintypes.HWND) -> str:
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def find_wechat_hwnd() -> Optional[int]:
    # Heuristic: visible top-level window with title containing 微信
    result = {"hwnd": 0}

    def callback(hwnd, lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        title = _get_window_text(hwnd)
        if not title:
            return True
        if "微信" in title or "WeChat" in title:
            result["hwnd"] = int(hwnd)
            return False
        return True

    user32.EnumWindows(EnumWindowsProc(callback), 0)
    return result["hwnd"] or None


def is_window_valid(hwnd: int) -> bool:
    return bool(hwnd) and bool(user32.IsWindow(wintypes.HWND(hwnd)))

def is_window_minimized(hwnd: int) -> bool:
    return bool(hwnd) and bool(user32.IsIconic(wintypes.HWND(hwnd)))


class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG), ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


def get_window_rect(hwnd: int) -> tuple[int, int, int, int]:
    rc = RECT()
    if not user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rc)):
        raise ctypes.WinError(ctypes.get_last_error())
    return rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def _get_dibits_bgra(hdc: int, hbmp: int, w: int, h: int) -> bytes:
    # Prepare bitmap header for 32bpp BGRA
    BI_RGB = 0
    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = w
    header.biHeight = -h  # top-down
    header.biPlanes = 1
    header.biBitCount = 32
    header.biCompression = BI_RGB
    bmi = BITMAPINFO()
    bmi.bmiHeader = header

    buf_size = w * h * 4
    buf = (ctypes.c_ubyte * buf_size)()
    got = gdi32.GetDIBits(hdc, hbmp, 0, h, ctypes.byref(buf), ctypes.byref(bmi), 0)
    if got == 0:
        raise ctypes.WinError(ctypes.get_last_error())
    return bytes(buf)


def capture_window_printwindow(hwnd: int) -> tuple[bytes, int, int]:
    """
    Attempt off-screen capture via PrintWindow.
    Returns (bgra_bytes, width, height) for the whole window.
    """
    _, _, w, h = get_window_rect(hwnd)
    if w <= 0 or h <= 0:
        raise RuntimeError("window rect invalid")

    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)

    PW_RENDERFULLCONTENT = 0x00000002
    ok = False
    try:
        ok = bool(user32.PrintWindow(wintypes.HWND(hwnd), wintypes.HDC(hdc_mem), PW_RENDERFULLCONTENT))
        if not ok:
            # retry with flags=0
            ok = bool(user32.PrintWindow(wintypes.HWND(hwnd), wintypes.HDC(hdc_mem), 0))
        if not ok:
            raise RuntimeError("PrintWindow failed")
        bgra = _get_dibits_bgra(hdc_mem, hbmp, w, h)
        return bgra, w, h
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)


def crop_bgra(bgra: bytes, src_w: int, src_h: int, x: int, y: int, w: int, h: int) -> bytes:
    if x < 0 or y < 0 or w <= 0 or h <= 0:
        raise ValueError("invalid crop rect")
    if x + w > src_w or y + h > src_h:
        raise ValueError("crop rect out of bounds")
    row_bytes = src_w * 4
    out = bytearray(w * h * 4)
    for row in range(h):
        src_off = (y + row) * row_bytes + x * 4
        dst_off = row * w * 4
        out[dst_off : dst_off + w * 4] = bgra[src_off : src_off + w * 4]
    return bytes(out)


def capture_region_bitblt(hwnd: int, x: int, y: int, w: int, h: int) -> bytes:
    # Screen capture fallback (may fail when occluded/hidden)
    win_x, win_y, _, _ = get_window_rect(hwnd)
    abs_x = win_x + x
    abs_y = win_y + y

    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)

    SRCCOPY = 0x00CC0020
    try:
        if not gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, abs_x, abs_y, SRCCOPY):
            raise ctypes.WinError(ctypes.get_last_error())
        bgra = _get_dibits_bgra(hdc_mem, hbmp, w, h)
        return bgra
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(0, hdc_screen)


def capture_chat_region(hwnd: int, x: int, y: int, w: int, h: int) -> tuple[bytes, str]:
    """
    Capture chat region.
    Prefer PrintWindow (off-screen). Fallback to BitBlt.
    Returns (bgra_bytes, method).
    """
    try:
        win_bgra, win_w, win_h = capture_window_printwindow(hwnd)
        chat_bgra = crop_bgra(win_bgra, win_w, win_h, x, y, w, h)
        return chat_bgra, "printwindow"
    except Exception:
        return capture_region_bitblt(hwnd, x, y, w, h), "bitblt"

    # (old BitBlt implementation moved to capture_region_bitblt)


# ----------------- Windows OCR -----------------

def windows_ocr_from_bgra32(bgra: bytes, w: int, h: int) -> str:
    try:
        from winsdk.windows.media.ocr import OcrEngine
        from winsdk.windows.graphics.imaging import SoftwareBitmap, BitmapPixelFormat, BitmapAlphaMode
        from winsdk.windows.storage.streams import DataWriter
    except Exception as e:
        raise RuntimeError(
            f"Windows OCR 导入失败：{e!r}。请确认使用同一个 python 安装依赖：python -m pip install winsdk"
        ) from e

    # For OCR, alpha is irrelevant; IGNORE tends to reduce premultiply artifacts.
    sb = SoftwareBitmap(BitmapPixelFormat.BGRA8, w, h, BitmapAlphaMode.IGNORE)
    # winsdk 的 SoftwareBitmap.copy_from_buffer 期望 WinRT IBuffer，而不是 Python bytes。
    # 用 DataWriter 将 bytes 转成 IBuffer，避免 TypeError('convert_to returned null')。
    writer = DataWriter()
    writer.write_bytes(bgra)
    ibuf = writer.detach_buffer()
    sb.copy_from_buffer(ibuf)

    engine = OcrEngine.try_create_from_user_profile_languages()
    if engine is None:
        raise RuntimeError("无法创建 Windows OCR 引擎（请确认系统语言包/组件）。")

    async def _recognize():
        return await engine.recognize_async(sb)

    # winsdk 的 async 返回 IAsyncOperation，需要 await；不同版本不一定支持 .get()
    result = asyncio.run(_recognize())
    return result.text or ""


def normalize_ocr_text(text: str) -> str:
    """
    Minimal readability normalization:
    - Normalize newlines
    - Remove spaces between CJK characters and CJK<->punctuation
    - Compress excessive whitespace
    """
    if not text:
        return ""

    t = text.replace("\r\n", "\n").replace("\r", "\n")
    # Trim each line and drop empties
    lines = [ln.strip() for ln in t.split("\n")]
    lines = [ln for ln in lines if ln]
    t = "\n".join(lines)

    # Remove spaces between CJK chars
    cjk = r"[\u4e00-\u9fff]"
    t = re.sub(rf"({cjk})\s+({cjk})", r"\1\2", t)

    # Remove spaces around common punctuation in Chinese context
    t = re.sub(rf"({cjk})\s+([：:，,。\.！!？?\)])", r"\1\2", t)
    t = re.sub(rf"([（(])\s+({cjk})", r"\1\2", t)
    t = re.sub(rf"([：:，,。\.！!？?\(（])\s+({cjk})", r"\1\2", t)
    t = re.sub(rf"({cjk})\s+([（(])", r"\1\2", t)

    # Collapse spaces (keep single spaces for latin words)
    t = re.sub(r"[ \t]{2,}", " ", t)
    return t.strip()


def stable_text_fingerprint(text: str) -> str:
    # Normalize then hash to detect unchanged OCR output across polling rounds.
    norm = normalize_ocr_text(text)
    return hashlib.sha1(norm.encode("utf-8")).hexdigest()


def make_platform_msg_id(platform: str, conv_id: str, content: str) -> str:
    ts_ms = int(time.time() * 1000)
    digest = hashlib.sha1(content.encode("utf-8")).hexdigest()[:12]
    return f"{platform}:{conv_id}:{ts_ms}:{digest}"


def write_inbox(platform: str, conv_id: str, customer_name: str, content: str):
    created_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    platform_msg_id = make_platform_msg_id(platform, conv_id, content)

    conn = open_db()
    try:
        cur = conn.cursor()
        cur.execute("BEGIN")
        cur.execute(
            """
            INSERT OR IGNORE INTO rpa_inbox_messages
            (platform, platform_conversation_id, customer_name, content, created_at, platform_msg_id, consume_status, error_reason)
            VALUES (?, ?, ?, ?, ?, ?, 0, '')
            """,
            (platform, conv_id, customer_name, content, created_at, platform_msg_id),
        )
        conn.commit()
        print(f"[WeChat-Reader] inbox wrote: {platform_msg_id} len={len(content)}")
    finally:
        conn.close()


def main():
    if not CONFIG_PATH.exists():
        raise SystemExit(f"未找到配置文件: {CONFIG_PATH}")

    cfg = load_config()
    print(f"[WeChat-Reader] DB={DB_PATH}")
    print(f"[WeChat-Reader] config chat_region=({cfg.chat_x},{cfg.chat_y},{cfg.chat_w}x{cfg.chat_h}) interval={cfg.poll_interval_sec}s")
    if cfg.hwnd:
        print(f"[WeChat-Reader] config hwnd_hex=0x{cfg.hwnd:x}")
    DEBUG_DIR.mkdir(parents=True, exist_ok=True)

    platform = "wechat_pc"
    last_fp: Optional[str] = None
    while True:
        hwnd = cfg.hwnd if (cfg.hwnd and is_window_valid(cfg.hwnd)) else None
        if not hwnd:
            hwnd = find_wechat_hwnd()
        if not hwnd:
            print("[WeChat-Reader] 未找到微信窗口（请先在主程序做一次“微信RPA校准”以写入 hwnd_hex），等待...")
            time.sleep(cfg.poll_interval_sec)
            continue
        if is_window_minimized(hwnd):
            print("[WeChat-Reader] 微信窗口已最小化（任务栏），PrintWindow/OCR 可能为空；请保持窗口非最小化（可移到屏幕外）")
            time.sleep(cfg.poll_interval_sec)
            continue

        try:
            bgra, cap_method = capture_chat_region(hwnd, cfg.chat_x, cfg.chat_y, cfg.chat_w, cfg.chat_h)
            # Debug dump: screenshot + raw OCR text
            ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
            png_path = DEBUG_DIR / f"wechat_capture_{ts}_{cap_method}_{cfg.chat_w}x{cfg.chat_h}.png"
            txt_raw_path = DEBUG_DIR / f"wechat_ocr_{ts}.raw.txt"
            txt_norm_path = DEBUG_DIR / f"wechat_ocr_{ts}.norm.txt"

            # Write PNG + preprocess image (requires Pillow). If Pillow isn't available, keep bgra as-is.
            pre_bgra = bgra
            try:
                from PIL import Image  # type: ignore
                from PIL import ImageEnhance  # type: ignore

                img = Image.frombuffer(
                    "RGBA",
                    (cfg.chat_w, cfg.chat_h),
                    bgra,
                    "raw",
                    "BGRA",
                    0,
                    1,
                )
                img.save(png_path)

                # Light OCR-oriented preprocess: grayscale -> upscale -> contrast/sharpness
                gray = img.convert("L")
                up = gray.resize((cfg.chat_w * 2, cfg.chat_h * 2), resample=Image.BICUBIC)
                up = ImageEnhance.Contrast(up).enhance(1.8)
                up = ImageEnhance.Sharpness(up).enhance(1.6)
                # Convert back to BGRA bytes for WinRT OCR
                rgba = up.convert("RGBA")
                pre_bgra = rgba.tobytes("raw", "BGRA")
            except Exception as e:
                print("[WeChat-Reader] Debug截图保存失败(需要 Pillow 才能保存PNG):", repr(e))

            # OCR (prefer preprocessed if available)
            ocr_w = cfg.chat_w * 2 if len(pre_bgra) == (cfg.chat_w * 2) * (cfg.chat_h * 2) * 4 else cfg.chat_w
            ocr_h = cfg.chat_h * 2 if len(pre_bgra) == (cfg.chat_w * 2) * (cfg.chat_h * 2) * 4 else cfg.chat_h
            text_raw = windows_ocr_from_bgra32(pre_bgra, ocr_w, ocr_h)
            txt_raw_path.write_text(text_raw, encoding="utf-8", errors="replace")
            norm = normalize_ocr_text(text_raw)
            txt_norm_path.write_text(norm, encoding="utf-8", errors="replace")
            if norm:
                fp = hashlib.sha1(norm.encode("utf-8")).hexdigest()
                if last_fp == fp:
                    print("[WeChat-Reader] OCR未变化，跳过写入")
                else:
                    last_fp = fp
                # MVP: 写整段文本为一条入站消息
                    write_inbox(platform, cfg.platform_conversation_id, cfg.customer_name, norm)
            else:
                print("[WeChat-Reader] OCR为空")
        except Exception as e:
            print("[WeChat-Reader] 失败:", repr(e))

        time.sleep(cfg.poll_interval_sec)


if __name__ == "__main__":
    main()

