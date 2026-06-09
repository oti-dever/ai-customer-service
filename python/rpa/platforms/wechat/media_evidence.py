from __future__ import annotations

import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .screenshot import capture_bubble, trim_media_evidence
from .wechat_logging import get_logger


logger = get_logger(__name__)


@dataclass(frozen=True)
class MediaEvidenceResult:
    status: str
    method: str = "bubble_screenshot"
    path: str = ""
    error: str = ""


class MediaEvidenceWriter:
    def __init__(self, enabled: bool = True, root_dir: str | Path = "python/rpa/_media/wechat") -> None:
        self.enabled = enabled
        self.root_dir = Path(root_dir)
        self._lock = threading.Lock()

    @classmethod
    def from_config(cls, config: object) -> "MediaEvidenceWriter":
        enabled = bool(getattr(config, "media_capture_evidence", True))
        root_dir = getattr(config, "media_artifact_dir", "python/rpa/_media/wechat")
        return cls(enabled=enabled, root_dir=root_dir)

    def capture(self, content_type: str, platform_msg_id: str, sample: Any) -> MediaEvidenceResult | None:
        if content_type not in {"image", "emoji", "file", "video"}:
            return None
        if not self.enabled:
            return MediaEvidenceResult(status="disabled")

        hwnd = int(getattr(sample, "window_hwnd", 0) or 0)
        if not hwnd:
            return MediaEvidenceResult(status="failed", error="window_hwnd_unavailable")

        safe_message_id = "".join(char for char in platform_msg_id if char.isalnum() or char in {"-", "_"})
        if not safe_message_id:
            return MediaEvidenceResult(status="failed", error="platform_msg_id_unavailable")

        path = self.root_dir / content_type / f"{safe_message_id}.png"
        with self._lock:
            if path.exists():
                return MediaEvidenceResult(status="existing", path=str(path.resolve()))
            try:
                image = capture_bubble(sample, hwnd=hwnd)
                if image is None:
                    return MediaEvidenceResult(status="failed", error="bubble_capture_failed")
                image = trim_media_evidence(image)
                path.parent.mkdir(parents=True, exist_ok=True)
                image.save(path, "PNG")
                resolved_path = str(path.resolve())
                logger.info(
                    "wechat media evidence saved content_type=%s platform_msg_id=%s path=%s",
                    content_type,
                    platform_msg_id,
                    resolved_path,
                )
                return MediaEvidenceResult(status="saved", path=resolved_path)
            except Exception as exc:
                logger.debug(
                    "wechat media evidence failed content_type=%s platform_msg_id=%s error=%s",
                    content_type,
                    platform_msg_id,
                    exc,
                )
                return MediaEvidenceResult(status="failed", error=type(exc).__name__)
