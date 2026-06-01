"""
OCR engines for chat message recognition.
Return list of (text, bbox, confidence) for layout analysis.
"""
from __future__ import annotations

import os
from abc import ABC, abstractmethod
from typing import Any, List, Optional, Tuple

import numpy as np

# 禁用 oneDNN，避免 PaddlePaddle 3.3+ 的 ConvertPirAttribute2RuntimeAttribute 错误
os.environ.setdefault("FLAGS_use_mkldnn", "0")
os.environ.setdefault("FLAGS_use_dnnl", "0")

OCRBlock = Tuple[str, List[List[float]], float]


def _ensure_pil():
    try:
        from PIL import Image
        return Image
    except ImportError as e:
        raise RuntimeError("OCR 需要 Pillow: pip install pillow") from e


def bgra_to_pil(bgra: bytes, w: int, h: int):
    """Convert BGRA bytes to PIL Image (RGB)."""
    from PIL import Image

    img = Image.frombuffer("RGBA", (w, h), bgra, "raw", "BGRA", 0, 1)
    return img.convert("RGB")


def _rescale_blocks(blocks: List[OCRBlock], scale: float) -> List[OCRBlock]:
    if scale == 1.0 or not blocks:
        return blocks
    return [
        (text, [[p[0] / scale, p[1] / scale] for p in box], conf)
        for text, box, conf in blocks
    ]


def _normalize_box(box: Any, default_top: float = 0.0) -> List[List[float]]:
    if hasattr(box, "tolist"):
        box = box.tolist()
    if isinstance(box, dict):
        pts = box.get("box") or box.get("points") or box.get("bbox")
        if pts is not None:
            box = pts
    if isinstance(box, (list, tuple)) and len(box) == 4 and all(
        isinstance(x, (int, float)) for x in box
    ):
        x1, y1, x2, y2 = [float(x) for x in box]
        return [[x1, y1], [x2, y1], [x2, y2], [x1, y2]]
    if isinstance(box, (list, tuple)) and len(box) >= 4:
        pts: List[List[float]] = []
        for p in box[:4]:
            if isinstance(p, (list, tuple)) and len(p) >= 2:
                pts.append([float(p[0]), float(p[1])])
        if len(pts) == 4:
            return pts
    top = float(default_top)
    return [[0.0, top], [100.0, top], [100.0, top + 20.0], [0.0, top + 20.0]]


def _parse_ocr_line(line1) -> Tuple[str, float]:
    if isinstance(line1, (list, tuple)) and len(line1) >= 2:
        return str(line1[0]), float(line1[1])
    if isinstance(line1, (list, tuple)) and len(line1) == 1:
        return str(line1[0]), 1.0
    if isinstance(line1, dict):
        return str(line1.get("text", line1.get("transcription", ""))), float(
            line1.get("confidence", line1.get("score", 1.0))
        )
    return str(line1), 1.0


def _parse_paddle_result(result, min_confidence: float = 0.6) -> List[OCRBlock]:
    if not result or result[0] is None:
        return []
    data = result[0]

    if hasattr(data, "get") and isinstance(data.get("rec_texts"), (list, tuple)):
        texts = data.get("rec_texts") or []
        polys = data.get("rec_polys")
        if polys is None or (
            hasattr(polys, "size") and polys.size == 0
        ) or (hasattr(polys, "__len__") and len(polys) == 0):
            polys = data.get("rec_boxes")
        if polys is None or (
            hasattr(polys, "size") and polys.size == 0
        ) or (hasattr(polys, "__len__") and len(polys) == 0):
            polys = data.get("dt_polys")
        if polys is None:
            polys = []
        scores = data.get("rec_scores")
        if scores is None or not hasattr(scores, "__len__"):
            scores = [1.0] * len(texts)
        blocks: List[OCRBlock] = []
        for i, text in enumerate(texts):
            if not text or not str(text).strip():
                continue
            conf = float(scores[i]) if i < len(scores) else 1.0
            if conf < min_confidence:
                continue
            box = (
                polys[i]
                if i < len(polys)
                else [[0, 0], [100, 0], [100, 20], [0, 20]]
            )
            if hasattr(box, "tolist"):
                box = box.tolist()
            elif hasattr(box, "__iter__") and not isinstance(box, (list, tuple)):
                box = [list(p) for p in box]
            blocks.append((str(text).strip(), box, conf))
        return blocks

    if isinstance(data, str):
        if data.strip():
            return [(data.strip(), [[0, 0], [100, 0], [100, 20], [0, 20]], 1.0)]
        return []
    if not isinstance(data, (list, tuple)):
        return []

    blocks: List[OCRBlock] = []
    for line in data:
        if isinstance(line, str):
            continue
        if not isinstance(line, (list, tuple)) or len(line) < 2:
            continue
        try:
            box = line[0]
            text, conf = _parse_ocr_line(line[1])
        except (IndexError, TypeError, ValueError):
            continue
        if conf >= min_confidence:
            blocks.append((text, box, float(conf)))
    return blocks


def _parse_rapidocr_result(result: Any, min_confidence: float = 0.6) -> List[OCRBlock]:
    if not result:
        return []

    rows = result[0] if isinstance(result, tuple) and result else result
    if rows is None:
        return []
    if isinstance(rows, str):
        rows = [rows]
    if not isinstance(rows, (list, tuple)):
        return []

    blocks: List[OCRBlock] = []
    synthetic_top = 0.0
    for row in rows:
        text = ""
        conf = 1.0
        box: Any = None

        if isinstance(row, dict):
            text = str(row.get("text", row.get("transcription", ""))).strip()
            conf = float(row.get("score", row.get("confidence", 1.0)))
            box = row.get("box") or row.get("points") or row.get("bbox")
        elif isinstance(row, (list, tuple)):
            if len(row) >= 3 and isinstance(row[1], str):
                box, text, conf = row[0], str(row[1]).strip(), float(row[2] or 1.0)
            elif len(row) >= 2 and isinstance(row[0], str):
                text, conf = str(row[0]).strip(), float(row[1] or 1.0)
            elif len(row) >= 2 and isinstance(row[1], str):
                box, text = row[0], str(row[1]).strip()
                conf = (
                    float(row[2])
                    if len(row) >= 3 and isinstance(row[2], (int, float))
                    else 1.0
                )
            elif len(row) == 1 and isinstance(row[0], str):
                text = str(row[0]).strip()
            else:
                text = str(row).strip()
        else:
            text = str(row).strip()

        if not text or conf < min_confidence:
            synthetic_top += 24.0
            continue

        norm_box = _normalize_box(box, default_top=synthetic_top)
        blocks.append((text, norm_box, conf))
        synthetic_top = max(synthetic_top + 24.0, norm_box[-1][1] + 4.0)

    return blocks


class BaseOCREngine(ABC):
    def __init__(self, min_confidence: float = 0.6, max_side: int = 960):
        self.min_confidence = min_confidence
        self.max_side = max_side
        self._last_raw_result: Optional[object] = None

    def warmup(self) -> None:
        return None

    @abstractmethod
    def recognize(
        self,
        bgra: bytes,
        w: int,
        h: int,
        *,
        skip_dark_invert: bool = False,
    ) -> List[OCRBlock]:
        raise NotImplementedError

    @abstractmethod
    def _recognize_array(self, arr) -> List[OCRBlock]:
        raise NotImplementedError

    def recognize_from_pil(self, img) -> List[OCRBlock]:
        return self._recognize_array(np.array(img))


class PaddleOCREngine(BaseOCREngine):
    """
    PaddleOCR engine. Returns blocks: [(text, bbox, confidence), ...]
    bbox: [[x1,y1],[x2,y1],[x3,y3],[x4,y4]] (4 corners)
    """

    def __init__(
        self,
        lang: str = "ch",
        use_angle_cls: bool = True,
        min_confidence: float = 0.6,
        max_side: int = 960,
        invert_for_dark_mode: bool = False,
        det_thresh: float = 0.2,
        det_box_thresh: float = 0.4,
    ):
        super().__init__(min_confidence=min_confidence, max_side=max_side)
        self.lang = lang
        self.use_angle_cls = use_angle_cls
        self.invert_for_dark_mode = invert_for_dark_mode
        self.det_thresh = det_thresh
        self.det_box_thresh = det_box_thresh
        self._ocr: Optional[object] = None

    def warmup(self) -> None:
        self._get_ocr()

    def _get_ocr(self):
        if self._ocr is None:
            os.environ["FLAGS_use_mkldnn"] = "0"
            os.environ["FLAGS_use_dnnl"] = "0"
            try:
                from paddleocr import PaddleOCR
            except ImportError as e:
                raise RuntimeError(
                    "请安装 PaddleOCR: pip install paddlepaddle paddleocr"
                ) from e
            ocr_kw: dict = {
                "lang": self.lang,
                "use_doc_orientation_classify": False,
                "use_doc_unwarping": False,
                "text_det_thresh": self.det_thresh,
                "text_det_box_thresh": self.det_box_thresh,
            }
            try:
                self._ocr = PaddleOCR(**ocr_kw)
            except TypeError:
                ocr_kw = {
                    "lang": self.lang,
                    "text_det_thresh": self.det_thresh,
                    "text_det_box_thresh": self.det_box_thresh,
                }
                self._ocr = PaddleOCR(**ocr_kw)
        return self._ocr

    def recognize(
        self,
        bgra: bytes,
        w: int,
        h: int,
        *,
        skip_dark_invert: bool = False,
    ) -> List[OCRBlock]:
        Image = _ensure_pil()
        img = bgra_to_pil(bgra, w, h)
        if self.invert_for_dark_mode and not skip_dark_invert:
            from PIL import ImageOps

            img = ImageOps.invert(img)
        scale = 1.0
        if self.max_side and (w > self.max_side or h > self.max_side):
            scale = self.max_side / max(w, h)
            new_w, new_h = int(w * scale), int(h * scale)
            resample = getattr(Image, "Resampling", Image).LANCZOS
            img = img.resize((new_w, new_h), resample)
        blocks = self._recognize_array(np.array(img))
        return _rescale_blocks(blocks, scale)

    def _recognize_array(self, arr) -> List[OCRBlock]:
        ocr = self._get_ocr()
        result = ocr.ocr(arr)
        self._last_raw_result = result
        return _parse_paddle_result(result, self.min_confidence)


class RapidOCREngine(BaseOCREngine):
    """RapidOCR engine for lightweight chat OCR."""

    def __init__(self, min_confidence: float = 0.6, max_side: int = 960):
        super().__init__(min_confidence=min_confidence, max_side=max_side)
        self._ocr: Optional[object] = None

    def _get_ocr(self):
        if self._ocr is None:
            try:
                from rapidocr_onnxruntime import RapidOCR
            except ImportError as e:
                raise RuntimeError(
                    "请安装 RapidOCR: pip install rapidocr-onnxruntime"
                ) from e
            self._ocr = RapidOCR()
        return self._ocr

    def warmup(self) -> None:
        Image = _ensure_pil()
        img = Image.new("RGB", (32, 32), "white")
        self._recognize_array(np.array(img))

    def recognize(
        self,
        bgra: bytes,
        w: int,
        h: int,
        *,
        skip_dark_invert: bool = False,
    ) -> List[OCRBlock]:
        Image = _ensure_pil()
        img = bgra_to_pil(bgra, w, h)
        scale = 1.0
        if self.max_side and (w > self.max_side or h > self.max_side):
            scale = self.max_side / max(w, h)
            new_w, new_h = int(w * scale), int(h * scale)
            resample = getattr(Image, "Resampling", Image).LANCZOS
            img = img.resize((new_w, new_h), resample)
        blocks = self._recognize_array(np.array(img))
        return _rescale_blocks(blocks, scale)

    def _recognize_array(self, arr) -> List[OCRBlock]:
        ocr = self._get_ocr()
        result, _ = ocr(arr)
        self._last_raw_result = result
        return _parse_rapidocr_result(result, self.min_confidence)


def build_ocr_engine(
    engine_name: str,
    *,
    lang: str = "ch",
    min_confidence: float = 0.6,
    max_side: int = 960,
    invert_for_dark_mode: bool = False,
    det_thresh: float = 0.2,
    det_box_thresh: float = 0.4,
) -> BaseOCREngine:
    name = (engine_name or "paddleocr").strip().lower()
    if name in ("rapidocr", "rapid_ocr"):
        return RapidOCREngine(
            min_confidence=min_confidence,
            max_side=max_side,
        )
    return PaddleOCREngine(
        lang=lang,
        min_confidence=min_confidence,
        max_side=max_side,
        invert_for_dark_mode=invert_for_dark_mode,
        det_thresh=det_thresh,
        det_box_thresh=det_box_thresh,
    )
