"""
轻量窗口串行锁。

用途：
- 同进程内：通过 threading.Lock 防止多个线程同时操作同一平台窗口
- 跨进程：通过 Windows 文件锁避免重复启动的 Reader/Writer 同时抢窗口
"""
from __future__ import annotations

import json
import msvcrt
import os
from pathlib import Path
import threading
import time
from typing import Optional

_STATE_DIR = Path(__file__).resolve().parents[1] / "_state"
_LOCK_GUARD = threading.Lock()
_THREAD_LOCKS: dict[str, threading.Lock] = {}


def _get_thread_lock(lock_path: Path) -> threading.Lock:
    key = str(lock_path)
    with _LOCK_GUARD:
        lock = _THREAD_LOCKS.get(key)
        if lock is None:
            lock = threading.Lock()
            _THREAD_LOCKS[key] = lock
        return lock


class PlatformWindowLock:
    def __init__(
        self,
        platform: str,
        owner: str,
        timeout_sec: float = 15.0,
        retry_interval_sec: float = 0.15,
    ):
        _STATE_DIR.mkdir(parents=True, exist_ok=True)
        self.platform = platform
        self.owner = owner
        self.timeout_sec = max(0.1, float(timeout_sec))
        self.retry_interval_sec = max(0.05, float(retry_interval_sec))
        self.lock_path = _STATE_DIR / f"{platform}_window.lock"
        self._thread_lock = _get_thread_lock(self.lock_path)
        self._thread_acquired = False
        self._fh: Optional[object] = None
        self.acquired = False

    def acquire(self) -> bool:
        deadline = time.time() + self.timeout_sec
        while time.time() < deadline:
            remaining = max(0.05, min(self.retry_interval_sec, deadline - time.time()))
            if not self._thread_acquired:
                self._thread_acquired = self._thread_lock.acquire(timeout=remaining)
                if not self._thread_acquired:
                    continue
            if self._try_file_lock():
                self.acquired = True
                self._write_metadata()
                return True
            time.sleep(self.retry_interval_sec)
        self.release()
        return False

    def _try_file_lock(self) -> bool:
        if self._fh is None:
            self._fh = open(self.lock_path, "a+b")
        try:
            self._fh.seek(0, os.SEEK_END)
            if self._fh.tell() == 0:
                self._fh.write(b" ")
                self._fh.flush()
            self._fh.seek(0)
            msvcrt.locking(self._fh.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def _write_metadata(self) -> None:
        if self._fh is None:
            return
        payload = {
            "platform": self.platform,
            "owner": self.owner,
            "pid": os.getpid(),
            "thread": threading.current_thread().name,
            "locked_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        }
        try:
            self._fh.seek(0)
            self._fh.truncate()
            self._fh.write(json.dumps(payload, ensure_ascii=False).encode("utf-8"))
            self._fh.flush()
            self._fh.seek(0)
        except OSError:
            pass

    def release(self) -> None:
        if self._fh is not None:
            try:
                self._fh.seek(0)
                msvcrt.locking(self._fh.fileno(), msvcrt.LK_UNLCK, 1)
            except OSError:
                pass
            try:
                self._fh.close()
            except OSError:
                pass
            self._fh = None
        if self._thread_acquired:
            self._thread_lock.release()
            self._thread_acquired = False
        self.acquired = False

    def __enter__(self) -> "PlatformWindowLock":
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.release()


def hold_platform_window_lock(
    platform: str,
    owner: str,
    timeout_sec: float = 15.0,
    retry_interval_sec: float = 0.15,
) -> PlatformWindowLock:
    return PlatformWindowLock(
        platform=platform,
        owner=owner,
        timeout_sec=timeout_sec,
        retry_interval_sec=retry_interval_sec,
    )
