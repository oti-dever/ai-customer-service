"""
千牛接待/工作台顶层窗口查找。

新版千牛常为「千牛工作台」一体化窗口，标题不一定含「接待中心」；
进程名也可能不是 AliIM.exe。由配置 title_fallback_list + 可选 process_name 控制。
"""
from __future__ import annotations

from typing import Any, Optional

from ...core.screenshot import is_window_valid
from ...core.win32_window import find_window_by_title_candidates


def find_qianniu_hwnd(cfg: dict[str, Any]) -> Optional[int]:
    hwnd_hex = cfg.get("hwnd_hex")
    if hwnd_hex:
        try:
            hwnd = int(str(hwnd_hex), 16)
            if is_window_valid(hwnd):
                return hwnd
        except (ValueError, TypeError):
            pass

    wm = cfg.get("window_match") or {}
    process_name = (wm.get("process_name") or "").strip()

    titles: list[str] = []
    tc = wm.get("title_contains")
    if tc:
        titles.append(str(tc))
    for t in wm.get("title_fallback_list") or []:
        s = str(t).strip()
        if s and s not in titles:
            titles.append(s)
    if not titles:
        titles = ["接待中心", "千牛工作台"]

    return find_window_by_title_candidates(titles, process_name)
