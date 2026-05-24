"""
控制台可观测性：带时间戳、强制 flush，便于 Qt「控制台输出」窗口判断阶段/是否存活。

约定：
- PHASE：阶段性里程碑（初始化 OCR、进入轮询等）
- heartbeat：周期性存活信号（main 线程与各 reader 内）
"""
from __future__ import annotations

from datetime import datetime


def rpa_log(msg: str) -> None:
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def rpa_phase(component: str, phase: str, detail: str = "") -> None:
    """例如：rpa_phase("qianniu.reader", "ocr_init_start", "首次加载可能较久")"""
    suffix = f" — {detail}" if detail else ""
    rpa_log(f"[RPA][{component}] PHASE {phase}{suffix}")


def rpa_heartbeat(component: str, msg: str) -> None:
    """周期性心跳，与业务 print 区分前缀。"""
    rpa_log(f"[RPA][{component}] heartbeat {msg}")
