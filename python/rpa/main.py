"""
RPA main entry: starts Reader and Writer for configured platforms.
Run: python -m rpa.main
Or: python rpa/main.py
"""
from __future__ import annotations

import os

# 必须在 import PaddlePaddle 之前设置，避免 oneDNN ConvertPirAttribute2RuntimeAttribute 错误
# 使用强制赋值，确保覆盖系统已有配置
os.environ["FLAGS_use_mkldnn"] = "0"
os.environ["FLAGS_use_dnnl"] = "0"

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


def run_wechat_reader():
    from rpa.readers.wechat_reader import run_reader
    run_reader()


def run_wechat_writer():
    from rpa.writers.wechat_writer import run_writer
    run_writer()


def main():
    parser = argparse.ArgumentParser(description="RPA Reader + Writer")
    parser.add_argument("--reader-only", action="store_true", help="Only run reader(s)")
    parser.add_argument("--writer-only", action="store_true", help="Only run writer(s)")
    parser.add_argument("--platform", choices=["wechat", "qianniu", "all"], default="wechat",
                        help="Platform to run (default: wechat)")
    args = parser.parse_args()

    run_reader = args.reader_only or (not args.writer_only)
    run_writer = args.writer_only or (not args.reader_only)

    if args.platform in ("wechat", "all") and run_reader:
        t = threading.Thread(target=run_wechat_reader, daemon=True)
        t.start()
        print("[RPA] WeChat reader started")

    if args.platform in ("wechat", "all") and run_writer:
        t = threading.Thread(target=run_wechat_writer, daemon=True)
        t.start()
        print("[RPA] WeChat writer started")

    if args.platform in ("qianniu", "all"):
        if run_reader:
            from rpa.readers.qianniu_reader import run_reader as run_qn_reader
            t = threading.Thread(target=run_qn_reader, daemon=True)
            t.start()
            print("[RPA] 千牛 reader started")
        if run_writer:
            from rpa.writers.qianniu_writer import run_writer as run_qn_writer
            t = threading.Thread(target=run_qn_writer, daemon=True)
            t.start()
            print("[RPA] 千牛 writer started")

    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt:
        print("\n[RPA] 退出")


if __name__ == "__main__":
    main()
