"""
RPA main entry: starts Reader and Writer for configured platforms.

运行方式（任选其一，注意工作目录）：

1) 在仓库的 python/ 目录下：
   cd <项目根>/python
   python -m rpa.main --platform qianniu

2) 在项目根目录下，直接指定脚本路径（main 会把 python/ 加入 sys.path）：
   python python/rpa/main.py --platform qianniu

3) 仅读链路（不启动 Writer，避免与 Reader 争用千牛窗口锁；用于测 OCR→入库 延迟）：
   python -m rpa.main --platform qianniu --reader-only

4) 仅写链路：
   python -m rpa.main --platform qianniu --writer-only

不要只在项目根执行 ``python -m rpa.main``，此时找不到 rpa 包。
"""
from __future__ import annotations

import os

# 必须在 import PaddlePaddle 之前设置，避免 oneDNN ConvertPirAttribute2RuntimeAttribute 错误
# 使用强制赋值，确保覆盖系统已有配置
os.environ["FLAGS_use_mkldnn"] = "0"
os.environ["FLAGS_use_dnnl"] = "0"
# 跳过 PaddleX 启动时连网检查模型源，减少控制台「Checking connectivity...」与延迟
os.environ.setdefault("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK", "1")

import argparse
import sys
import threading
import time
from pathlib import Path

# Ensure python/ is on path for "from rpa.xxx import"
# python/rpa/main.py -> parents[1] = python/
PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.rpa_console_log import rpa_log, rpa_phase


def run_wechat_reader():
    from rpa.readers.wechat_reader import run_reader
    run_reader()


def run_wechat_writer():
    from rpa.writers.wechat_writer import run_writer
    run_writer()

def run_pdd_reader():
    from rpa.readers.pdd_reader import run_reader
    run_reader()


def run_pdd_writer():
    from rpa.writers.pdd_writer import run_writer
    run_writer()


def main():
    parser = argparse.ArgumentParser(description="RPA Reader + Writer")
    parser.add_argument("--reader-only", action="store_true", help="Only run reader(s)")
    parser.add_argument("--writer-only", action="store_true", help="Only run writer(s)")
    parser.add_argument("--platform", choices=["wechat", "qianniu", "pdd", "all"], default="wechat",
                        help="Platform to run (default: wechat)")
    args = parser.parse_args()

    run_reader = args.reader_only or (not args.writer_only)
    run_writer = args.writer_only or (not args.reader_only)

    rpa_phase("main", "process_start", f"platform={args.platform} reader={run_reader} writer={run_writer}")

    from rpa.common.db_helper import resolved_default_db_path

    rpa_log(f"[RPA] SQLite 路径={resolved_default_db_path()}（未设置 AI_CUSTOMER_SERVICE_DB 时与 Qt AppData 下 app.db 自动对齐）")

    if args.platform in ("wechat", "all") and run_reader:
        t = threading.Thread(target=run_wechat_reader, daemon=True, name="wechat_reader")
        t.start()
        rpa_log("[RPA] WeChat reader thread started")

    if args.platform in ("wechat", "all") and run_writer:
        t = threading.Thread(target=run_wechat_writer, daemon=True, name="wechat_writer")
        t.start()
        rpa_log("[RPA] WeChat writer thread started")

    if args.platform in ("qianniu", "all"):
        if run_reader:
            from rpa.readers.qianniu_reader import run_reader as run_qn_reader
            t = threading.Thread(target=run_qn_reader, daemon=True, name="qianniu_reader")
            t.start()
            rpa_log("[RPA] 千牛 reader thread started（OCR 首次加载可能较慢，请看 reader PHASE 日志）")
        if run_writer:
            from rpa.writers.qianniu_writer import run_writer as run_qn_writer
            t = threading.Thread(target=run_qn_writer, daemon=True, name="qianniu_writer")
            t.start()
            rpa_log("[RPA] 千牛 writer thread started")

    if args.platform in ("pdd", "all"):
        if run_reader:
            t = threading.Thread(target=run_pdd_reader, daemon=True, name="pdd_reader")
            t.start()
            rpa_log("[RPA] 拼多多 reader thread started")
        if run_writer:
            t = threading.Thread(target=run_pdd_writer, daemon=True, name="pdd_writer")
            t.start()
            rpa_log("[RPA] 拼多多 writer thread started")

    rpa_phase("main", "threads_spawned", "主线程进入保活循环；若长时间无 reader PHASE=ocr_init_done，多半卡在模型加载")

    HEARTBEAT_SEC = 30.0
    try:
        while True:
            time.sleep(HEARTBEAT_SEC)
            alive = [t.name for t in threading.enumerate() if t.is_alive() and t != threading.main_thread()]
            rpa_log(
                f"[RPA] heartbeat: main alive, daemon_threads={threading.active_count() - 1}, "
                f"named={[n for n in alive if n]}"
            )
    except KeyboardInterrupt:
        rpa_log("\n[RPA] 收到 Ctrl+C，准备退出")


if __name__ == "__main__":
    main()
