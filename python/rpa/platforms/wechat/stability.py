from __future__ import annotations

import contextlib
import logging
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import comtypes
from PIL import Image, ImageDraw

from .screenshot import capture_bubble
from rpa.core.screenshot import capture_window_printwindow
from .wechat_logging import get_logger


_AUTOMATION_LOCK = threading.RLock()
logger = get_logger(__name__)


@dataclass
class FailureTracker:
    max_consecutive_failures: int = 8
    cooldown_seconds: float = 30.0
    failure_count: int = 0
    last_stage: str = ""
    same_stage_count: int = 0

    def record(self, ok: bool, stage: str = "") -> int:
        if ok:
            self.reset()
            return self.failure_count

        self.failure_count += 1
        if stage and stage == self.last_stage:
            self.same_stage_count += 1
        else:
            self.same_stage_count = 1
            self.last_stage = stage
        return self.failure_count

    def reset(self) -> None:
        self.failure_count = 0
        self.last_stage = ""
        self.same_stage_count = 0

    def should_cooldown(self) -> bool:
        return self.failure_count >= self.max_consecutive_failures or self.same_stage_count >= 3

    def next_sleep(self, base_interval: float, max_interval_steps: int) -> float:
        if self.failure_count <= 0:
            return base_interval
        step = min(self.failure_count, max(1, max_interval_steps))
        return max(0.2, base_interval * step)


@dataclass(frozen=True)
class SnapshotPaths:
    window: Path | None = None
    bubble: Path | None = None
    compare: Path | None = None


class DebugArtifactWriter:
    def __init__(self, enabled: bool = False, root_dir: str | Path = "python/rpa/_debug/wechat") -> None:
        self.enabled = enabled
        self.root_dir = Path(root_dir)

    @classmethod
    def from_config(cls, config: object) -> "DebugArtifactWriter":
        enabled = bool(getattr(config, "debug_save_artifacts", False))
        root_dir = getattr(config, "debug_artifact_dir", "python/rpa/_debug/wechat")
        return cls(enabled=enabled, root_dir=root_dir)

    def save_snapshot(
        self,
        label: str,
        *,
        stage: str = "",
        detail: str = "",
        hwnd: int = 0,
        bubble_control: object | None = None,
        window_title: str = "",
        chat_title: str = "",
    ) -> SnapshotPaths:
        if not self.enabled or not hwnd:
            return SnapshotPaths()

        timestamp = threading.get_ident()
        folder = self.root_dir / f"{timestamp}_{_slugify(label)}"
        folder.mkdir(parents=True, exist_ok=True)

        window_path = None
        bubble_path = None
        compare_path = None

        window_image = self._capture_window_png(hwnd)
        if window_image is not None:
            window_path = folder / "window.png"
            self._save_image(window_image, window_path, f"window: {window_title}")

        if bubble_control is not None and window_image is not None:
            bubble_image = self._capture_bubble_png(bubble_control, hwnd)
            if bubble_image is not None:
                bubble_path = folder / "bubble.png"
                self._save_image(bubble_image, bubble_path, f"bubble: {chat_title}")
                compare_path = folder / "compare.png"
                self._save_image(
                    self._build_compare_image(window_image, bubble_image, window_title, chat_title, stage, detail),
                    compare_path,
                    "compare",
                )

        return SnapshotPaths(window=window_path, bubble=bubble_path, compare=compare_path)

    def _capture_window_png(self, hwnd: int) -> Image.Image | None:
        try:
            bgra, width, height = capture_window_printwindow(hwnd)
            return Image.frombuffer("RGBA", (width, height), bgra, "raw", "BGRA", 0, 1).convert("RGB")
        except Exception as exc:
            logger.debug("failed to capture window snapshot: %s", exc)
            return None

    def _capture_bubble_png(self, control: object, hwnd: int) -> Image.Image | None:
        try:
            return capture_bubble(control, hwnd=hwnd)
        except Exception as exc:
            logger.debug("failed to capture bubble snapshot: %s", exc)
            return None

    def _save_image(self, image: Image.Image, path: Path, label: str) -> None:
        try:
            image.save(path)
            logger.info("wechat debug snapshot saved: %s (%s)", path, label)
        except Exception as exc:
            logger.debug("failed to save snapshot %s: %s", path, exc)

    def _build_compare_image(
        self,
        window_image: Image.Image,
        bubble_image: Image.Image,
        window_title: str,
        chat_title: str,
        stage: str,
        detail: str,
    ) -> Image.Image:
        left = window_image.convert("RGB")
        right = bubble_image.convert("RGB")
        padding = 16
        header = 32
        width = left.width + right.width + padding * 3
        height = max(left.height, right.height) + header + padding * 2
        canvas = Image.new("RGB", (width, height), "white")
        canvas.paste(left, (padding, padding + header))
        canvas.paste(right, (left.width + padding * 2, padding + header))
        draw = ImageDraw.Draw(canvas)
        draw.multiline_text(
            (padding, 8),
            "\n".join([line for line in [f"window: {window_title}", f"stage: {stage}", f"detail: {detail}"] if line]),
            fill="black",
            spacing=4,
        )
        draw.multiline_text((left.width + padding * 2, 8), f"bubble: {chat_title}", fill="black", spacing=4)
        return canvas


def _slugify(value: str) -> str:
    text = "_".join(str(value or "snapshot").split())
    text = "".join(ch if ch.isalnum() or ch in "_-." else "_" for ch in text)
    return text[:48] or "snapshot"


@contextlib.contextmanager
def automation_guard(_label: str = "wechat") -> Iterator[None]:
    with _AUTOMATION_LOCK:
        yield


@contextlib.contextmanager
def uia_guard(_label: str = "wechat") -> Iterator[None]:
    comtypes.CoInitialize()
    try:
        with automation_guard(_label):
            yield
    finally:
        comtypes.CoUninitialize()
