from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .media_context_menu import QQContextMenuCopyResult, copy_media_via_context_menu
from .media_evidence import QQMediaEvidenceResult, QQMediaEvidenceWriter, resolve_media_root
from .qq_logging import get_logger


logger = get_logger(__name__)


@dataclass(frozen=True)
class QQMediaExtractionResult:
    payload_fields: dict[str, Any] = field(default_factory=dict)
    metadata_fields: dict[str, Any] = field(default_factory=dict)


class QQMediaExtractor:
    def __init__(
        self,
        *,
        root_dir: str | Path = "python/rpa/_media/qq",
        evidence_writer: QQMediaEvidenceWriter | None = None,
    ) -> None:
        self.root_dir = resolve_media_root(root_dir)
        self.evidence_writer = evidence_writer or QQMediaEvidenceWriter(root_dir=self.root_dir)

    @classmethod
    def from_config(cls, config: object) -> "QQMediaExtractor":
        root_dir = getattr(config, "media_artifact_dir", "python/rpa/_media/qq")
        return cls(root_dir=root_dir, evidence_writer=QQMediaEvidenceWriter.from_config(config))

    def extract(
        self,
        item: Any,
        *,
        hwnd: int,
        window_rect: tuple[int, int, int, int],
    ) -> QQMediaExtractionResult:
        started_at = time.perf_counter()
        content_type = str(getattr(item, "content_type", "") or "")
        platform_msg_id = str(getattr(item, "platform_msg_id", "") or "")
        rect = str(getattr(item, "media_rect", "") or getattr(item, "rect", "") or "")
        metadata_fields: dict[str, Any] = {}
        payload_fields: dict[str, Any] = {}
        copy_ms = 0.0
        evidence_ms = 0.0

        logger.info(
            "qq media extract start content_type=%s platform_msg_id=%s rect=%s",
            content_type,
            platform_msg_id,
            rect,
        )
        stage_started_at = time.perf_counter()
        copied = self._copy_media_file_if_available(
            content_type=content_type,
            platform_msg_id=platform_msg_id,
            hwnd=hwnd,
            window_rect=window_rect,
            rect=rect,
        )
        copy_ms = _elapsed_ms(stage_started_at)
        if copied is not None:
            metadata_fields.update(_metadata_from_copy(copied))
            if copied.artifact_paths:
                metadata_fields["file_artifact_paths"] = copied.artifact_paths
                first_path = copied.artifact_paths[0]
                payload_fields["evidence_ref"] = first_path
                if content_type in {"image", "emoji"} or _looks_like_image_path(first_path):
                    payload_fields["content_image_path"] = first_path

        evidence: QQMediaEvidenceResult | None = None
        if not payload_fields.get("evidence_ref"):
            stage_started_at = time.perf_counter()
            evidence = self.evidence_writer.capture(
                hwnd=hwnd,
                content_type=content_type,
                platform_msg_id=platform_msg_id,
                rect=rect,
            )
            evidence_ms = _elapsed_ms(stage_started_at)
            metadata_fields.update(_metadata_from_evidence(evidence))
            if evidence.path:
                payload_fields["content_image_path"] = evidence.path
                payload_fields["evidence_ref"] = evidence.path

        logger.info(
            "qq media extract done content_type=%s platform_msg_id=%s evidence_ref=%s copy_status=%s evidence_status=%s total_ms=%.1f copy_ms=%.1f evidence_ms=%.1f",
            content_type,
            platform_msg_id,
            payload_fields.get("evidence_ref", ""),
            getattr(copied, "status", ""),
            getattr(evidence, "status", ""),
            _elapsed_ms(started_at),
            copy_ms,
            evidence_ms,
        )
        return QQMediaExtractionResult(payload_fields=payload_fields, metadata_fields=metadata_fields)

    def _copy_media_file_if_available(
        self,
        *,
        content_type: str,
        platform_msg_id: str,
        hwnd: int,
        window_rect: tuple[int, int, int, int],
        rect: str,
    ) -> QQContextMenuCopyResult | None:
        if content_type not in {"image", "emoji", "video", "file"}:
            return None
        try:
            return copy_media_via_context_menu(
                hwnd=hwnd,
                window_rect=window_rect,
                media_rect=rect,
                root_dir=self.root_dir,
                content_type=content_type,
                platform_msg_id=platform_msg_id,
            )
        except Exception as exc:
            logger.debug("qq media context-menu copy failed: %s", exc)
            return QQContextMenuCopyResult(status="failed", error=type(exc).__name__)


def _metadata_from_copy(result: QQContextMenuCopyResult) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "file_copy_method": result.method,
        "file_copy_status": result.status,
    }
    if result.menu_name:
        metadata["file_copy_menu_name"] = result.menu_name
    if result.menu_names_seen:
        metadata["file_copy_menu_names_seen"] = result.menu_names_seen
    if result.clipboard_method:
        metadata["clipboard_method"] = result.clipboard_method
    if result.source_paths:
        metadata["file_source_paths"] = result.source_paths
    if result.error:
        metadata["file_copy_error"] = result.error
    return metadata


def _metadata_from_evidence(result: QQMediaEvidenceResult) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "evidence_status": result.status,
        "evidence_method": result.method,
    }
    if result.error:
        metadata["evidence_error"] = result.error
    return metadata


def _looks_like_image_path(value: str) -> bool:
    return Path(value).suffix.lower() in {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif"}


def _elapsed_ms(started_at: float) -> float:
    return (time.perf_counter() - started_at) * 1000.0
