"""
千牛 Clicknium 公共：工程根解析与 Writer 输入/发送按钮定位器操作。

与 Reader 的 chat_capture 共用仓库根 `.locator` 策略；Writer 可在 writer_clicknium 中覆盖工程根。
"""
from __future__ import annotations

import os
import sys
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, Optional, Tuple

# python/rpa/common/qianniu_clicknium.py -> parents[3] = 仓库根
PROJECT_ROOT = Path(__file__).resolve().parents[3]


@contextmanager
def clicknium_sys_path_first(project_root: Path) -> Iterator[None]:
    """将 Clicknium 工程根置于 sys.path[0]，与 Reader 中 find_element 用法一致。"""
    root = project_root.resolve()
    old = sys.path[0] if sys.path else ""
    try:
        if sys.path:
            sys.path[0] = str(root)
        else:
            sys.path.insert(0, str(root))
        yield
    finally:
        if sys.path:
            sys.path[0] = old


def resolve_clicknium_project_root(cfg: dict[str, Any]) -> Optional[Path]:
    """
    解析含 `.locator` 的 Clicknium 工程根：优先 list_capture、writer_clicknium、chat_capture，
    再环境变量，最后仓库根下存在 `.locator` 则返回仓库根。
    """
    lc = cfg.get("list_capture") or {}
    wc = cfg.get("writer_clicknium") or {}
    cc = cfg.get("chat_capture") or {}
    env_name = str(
        lc.get("clicknium_project_root_env")
        or wc.get("clicknium_project_root_env")
        or cc.get("clicknium_project_root_env")
        or "QN_CLICKNIUM_PROJECT"
    ).strip()
    raw = ""
    if env_name:
        raw = str(os.environ.get(env_name, "") or "").strip()
    if not raw:
        raw = str(
            lc.get("clicknium_project_root", "")
            or wc.get("clicknium_project_root", "")
            or cc.get("clicknium_project_root", "")
            or ""
        ).strip()
    if raw:
        path = Path(raw).expanduser()
        if not path.is_absolute():
            path = PROJECT_ROOT / path
        return path
    if (PROJECT_ROOT / ".locator").is_dir():
        return PROJECT_ROOT
    return None


def writer_fill_input_clicknium(project_root: Path, input_locator: str, text: str) -> bool:
    """
    点击输入控件、清空、写入文本。失败返回 False。
    """
    locator = (input_locator or "").strip()
    if not locator:
        return False
    try:
        with clicknium_sys_path_first(project_root):
            from clicknium import find_element

            el = find_element(locator)
            if el is None:
                return False
            el.click(timeout=15)
            time.sleep(0.05)
            try:
                el.clear_text("send-hotkey", timeout=15)
            except Exception:
                try:
                    el.clear_text("set-text", timeout=15)
                except Exception:
                    pass
            time.sleep(0.03)
            el.set_text(text, overwrite=True, timeout=30)
        return True
    except Exception:
        return False


def _load_png_as_bgra(path: Path) -> Tuple[Optional[bytes], int, int]:
    try:
        from PIL import Image
    except ImportError:
        return None, 0, 0
    try:
        with Image.open(path) as img:
            rgba = img.convert("RGBA")
            w, h = rgba.size
            return rgba.tobytes("raw", "BGRA"), w, h
    except Exception:
        return None, 0, 0


def capture_locator_bgra(
    project_root: Path,
    locator: str,
    tmp_png: Path,
) -> Tuple[Optional[bytes], int, int]:
    """
    Clicknium find_element + save_to_image，再读为 BGRA bytes（与 Reader 聊天区截图一致）。
    失败返回 (None, 0, 0)。
    """
    loc = (locator or "").strip()
    if not loc:
        return None, 0, 0
    try:
        tmp_png.parent.mkdir(parents=True, exist_ok=True)
        with clicknium_sys_path_first(project_root):
            from clicknium import find_element

            el = find_element(loc)
            if el is None:
                return None, 0, 0
            el.save_to_image(str(tmp_png))
        return _load_png_as_bgra(tmp_png)
    except Exception:
        return None, 0, 0


def writer_click_send_clicknium(project_root: Path, send_locator: str) -> bool:
    """点击发送按钮定位器。未配置或找不到元素返回 False。"""
    locator = (send_locator or "").strip()
    if not locator:
        return False
    try:
        with clicknium_sys_path_first(project_root):
            from clicknium import find_element

            el = find_element(locator)
            if el is None:
                return False
            el.click(timeout=15)
        return True
    except Exception:
        return False
