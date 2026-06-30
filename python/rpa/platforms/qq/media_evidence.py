from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class QQMediaEvidenceResult:
    status: str
    path: str = ""
    method: str = "printwindow_media_rect"
    error: str = ""


class QQMediaEvidenceWriter:
    def __init__(self, root_dir: str | Path = "python/rpa/_media/qq", padding: int = 8) -> None:
        self.root_dir = resolve_media_root(root_dir)
        self.padding = max(0, int(padding))

    @classmethod
    def from_config(cls, config: object) -> "QQMediaEvidenceWriter":
        root_dir = getattr(config, "media_artifact_dir", "python/rpa/_media/qq")
        return cls(root_dir=root_dir)

    def capture(
        self,
        *,
        hwnd: int,
        content_type: str,
        platform_msg_id: str,
        rect: str,
    ) -> QQMediaEvidenceResult:
        if content_type not in {"image", "video", "file"}:
            return QQMediaEvidenceResult(status="skipped", error="unsupported_content_type")
        if not hwnd:
            return QQMediaEvidenceResult(status="failed", error="window_hwnd_unavailable")
        safe_message_id = _safe_file_token(platform_msg_id)
        if not safe_message_id:
            return QQMediaEvidenceResult(status="failed", error="platform_msg_id_unavailable")
        screen_rect = parse_rect(rect)
        if screen_rect is None:
            return QQMediaEvidenceResult(status="failed", error="invalid_rect")

        path = self.root_dir / content_type / f"{safe_message_id}.png"
        if path.exists():
            return QQMediaEvidenceResult(status="existing", path=str(path.resolve()))

        try:
            from rpa.core.screenshot import capture_region, get_window_rect, save_bgra_png

            win_x, win_y, win_w, win_h = get_window_rect(hwnd)
            left, top, right, bottom = screen_rect
            pad = self.padding
            x = left - win_x - pad
            y = top - win_y - pad
            width = max(1, right - left + pad * 2)
            height = max(1, bottom - top + pad * 2)
            if x >= win_w or y >= win_h:
                return QQMediaEvidenceResult(status="failed", error="rect_outside_window")
            bgra, cropped_width, cropped_height, _method = capture_region(hwnd, x, y, width, height)
            save_bgra_png(bgra, cropped_width, cropped_height, path)
            return QQMediaEvidenceResult(status="saved", path=str(path.resolve()))
        except Exception as exc:
            return QQMediaEvidenceResult(status="failed", error=exc.__class__.__name__)


def _safe_file_token(value: str) -> str:
    return "".join(char for char in str(value or "") if char.isalnum() or char in {"-", "_"})


def resolve_media_root(root_dir: str | Path) -> Path:
    path = Path(root_dir)
    if path.is_absolute():
        return path
    repo_root = Path(__file__).resolve().parents[4]
    return repo_root / path


def parse_rect(value: str) -> tuple[int, int, int, int] | None:
    try:
        left, top, right, bottom = [int(part) for part in str(value or "").strip("()").split(",")]
    except ValueError:
        return None
    return left, top, right, bottom
