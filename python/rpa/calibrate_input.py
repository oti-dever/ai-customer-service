"""
校准微信输入框坐标。
运行后会将鼠标移动到当前估算的输入框中心，3 秒后退出。
若位置不对，请根据终端输出的窗口尺寸，调整 wechat_config.json 中 input_box 的 x,y,w,h。
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

# Ensure python/ is on path
PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.screenshot import get_window_rect
from rpa.common.wechat_capture import rect_input_region_resolved
from rpa.common.win32_window import find_window
from rpa.common.input_sim import set_cursor_pos


def main():
    config_path = Path(__file__).resolve().parents[1] / "config" / "wechat_config.json"
    if not config_path.exists():
        print(f"配置不存在: {config_path}")
        return 1

    with open(config_path, encoding="utf-8") as f:
        cfg = json.load(f)

    wm = cfg.get("window_match") or {}
    hwnd = find_window(
        title_contains=wm.get("title_contains", "微信"),
        process_name=wm.get("process_name", ""),
    )
    if not hwnd:
        print("未找到微信窗口，请确保微信已打开且未最小化")
        return 1

    win_x, win_y, win_w, win_h = get_window_rect(hwnd)
    print(f"微信窗口: 左上=({win_x},{win_y}) 尺寸={win_w}x{win_h}")

    ix, iy, iw, ih = rect_input_region_resolved(cfg, win_h)
    ib = cfg.get("input_box") or {}
    ox = int(ib.get("click_offset_x", 80))
    oy = int(ib.get("click_offset_y", ih // 2))
    cx, cy = ix + ox, iy + oy
    if ib.get("x") is not None and ib.get("y") is not None:
        print(f"使用配置 input_box: 区域({ix},{iy},{iw}x{ih}) 点击点=({cx},{cy}) [offset={ox},{oy}]")
    else:
        print(
            f"由 chat_region 推算输入区（与 Writer 一致）: 区域({ix},{iy},{iw}x{ih}) "
            f"点击点=({cx},{cy}) [offset={ox},{oy}]"
        )

    screen_x = win_x + cx
    screen_y = win_y + cy
    print(f"屏幕坐标: ({screen_x},{screen_y})")
    print("3 秒内将鼠标移至此位置，请观察是否对准输入框...")

    set_cursor_pos(screen_x, screen_y)
    time.sleep(3)

    print("若位置不准，请编辑 wechat_config.json 的 input_box，调整 x,y,w,h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
