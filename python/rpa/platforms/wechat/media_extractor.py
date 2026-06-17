from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from .media_context_menu import ContextMenuFileResult, copy_message_file_via_context_menu
from .media_evidence import MediaEvidenceResult, MediaEvidenceWriter
from .wechat_logging import get_logger


logger = get_logger(__name__)

MediaFileCopier = Callable[[str, str, Any], ContextMenuFileResult | None]


@dataclass(frozen=True)
class WechatMediaExtractionResult:
    payload_fields: dict[str, Any] = field(default_factory=dict)
    metadata_fields: dict[str, Any] = field(default_factory=dict)


class WechatMediaExtractor:
    def __init__(
        self,
        *,
        root_dir: str | Path = "python/rpa/_media/wechat",
        evidence_writer: MediaEvidenceWriter | None = None,
        file_copier: MediaFileCopier | None = None,
    ) -> None:
        self.root_dir = root_dir
        self.evidence_writer = evidence_writer or MediaEvidenceWriter(root_dir=root_dir)
        self.file_copier = file_copier or self._copy_media_file_if_available

    @classmethod
    def from_config(cls, config: object) -> "WechatMediaExtractor":
        root_dir = getattr(config, "media_artifact_dir", "python/rpa/_media/wechat")
        return cls(root_dir=root_dir, evidence_writer=MediaEvidenceWriter.from_config(config))

    def extract(
        self,
        content_type: str,
        platform_msg_id: str,
        sample: Any,
        *,
        file_copier: MediaFileCopier | None = None,
        evidence_writer: MediaEvidenceWriter | None = None,
    ) -> WechatMediaExtractionResult:
        metadata_fields: dict[str, Any] = {}
        payload_fields: dict[str, Any] = {}

        copier = file_copier or self.file_copier
        writer = evidence_writer or self.evidence_writer

        if content_type in {"image", "emoji", "file", "video"}:
            logger.info(
                "wechat media extract start content_type=%s platform_msg_id=%s sample_name=%s "
                "sample_kind=%s sample_class=%s sample_rect=%s",
                content_type,
                platform_msg_id,
                getattr(sample, "name", ""),
                getattr(sample, "kind", ""),
                getattr(sample, "class_name", ""),
                getattr(sample, "rect", ""),
            )

        file_copy = copier(content_type, platform_msg_id, sample)
        file_copy_paths = list(getattr(file_copy, "artifact_paths", []) or []) if file_copy is not None else []
        evidence = None if file_copy_paths else writer.capture(content_type, platform_msg_id, sample)

        if file_copy is not None:
            metadata_fields.update(_metadata_from_file_copy(file_copy))
            logger.info(
                "wechat media context copy result content_type=%s platform_msg_id=%s status=%s "
                "method=%s artifacts=%s sources=%s menu=%s error=%s",
                content_type,
                platform_msg_id,
                getattr(file_copy, "status", ""),
                getattr(file_copy, "method", ""),
                file_copy_paths,
                list(getattr(file_copy, "source_paths", []) or []),
                getattr(file_copy, "menu_name", ""),
                getattr(file_copy, "error", ""),
            )
            if file_copy.artifact_paths:
                metadata_fields["file_artifact_paths"] = file_copy.artifact_paths
                if content_type == "image":
                    payload_fields["content_image_path"] = file_copy.artifact_paths[0]
                payload_fields["evidence_ref"] = file_copy.artifact_paths[0]

        if evidence is not None:
            metadata_fields.update(_metadata_from_evidence(evidence))
            logger.info(
                "wechat media evidence result content_type=%s platform_msg_id=%s status=%s "
                "method=%s path=%s error=%s",
                content_type,
                platform_msg_id,
                getattr(evidence, "status", ""),
                getattr(evidence, "method", ""),
                getattr(evidence, "path", ""),
                getattr(evidence, "error", ""),
            )
            if evidence.path:
                payload_fields["content_image_path"] = evidence.path
                payload_fields["evidence_ref"] = evidence.path

        if content_type in {"image", "emoji", "file", "video"}:
            logger.info(
                "wechat media extract done content_type=%s platform_msg_id=%s "
                "content_image_path=%s evidence_ref=%s metadata_keys=%s",
                content_type,
                platform_msg_id,
                payload_fields.get("content_image_path", ""),
                payload_fields.get("evidence_ref", ""),
                sorted(metadata_fields.keys()),
            )

        return WechatMediaExtractionResult(payload_fields=payload_fields, metadata_fields=metadata_fields)

    def _copy_media_file_if_available(
        self,
        content_type: str,
        platform_msg_id: str,
        sample: Any,
    ) -> ContextMenuFileResult | None:
        if content_type not in {"image", "file", "video"}:
            return None
        try:
            return copy_message_file_via_context_menu(
                sample,
                root_dir=self.root_dir,
                content_type=content_type,
                platform_msg_id=platform_msg_id,
            )
        except Exception as exc:
            logger.debug("wechat media context-menu copy failed: %s", exc)
            return None


def _metadata_from_file_copy(file_copy: ContextMenuFileResult) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "file_copy_method": file_copy.method,
        "file_copy_status": file_copy.status,
    }
    if file_copy.menu_name:
        metadata["file_copy_menu_name"] = file_copy.menu_name
    if file_copy.error:
        metadata["file_copy_error"] = file_copy.error
    if file_copy.source_paths:
        metadata["file_source_paths"] = file_copy.source_paths
    return metadata


def _metadata_from_evidence(evidence: MediaEvidenceResult) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "evidence_method": evidence.method,
        "evidence_status": evidence.status,
    }
    if evidence.error:
        metadata["evidence_error"] = evidence.error
    return metadata
