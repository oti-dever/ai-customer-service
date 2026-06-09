from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

if __package__ in {None, ""}:
    repo_root = Path(__file__).resolve().parents[4]
    python_dir = repo_root / "python"
    if str(python_dir) not in sys.path:
        sys.path.insert(0, str(python_dir))
else:
    repo_root = Path.cwd()

from rpa.platforms.wechat.adapter import WechatSidecarAdapter
from rpa.platforms.wechat.detector import WechatDetector
from rpa.platforms.wechat.media_context_menu import (
    _context_menu_click_point,
    _find_copy_file_menu_item,
    _invoke_menu_item,
    _right_click_sample,
)
from rpa.platforms.wechat.reader import WechatVisibleMessageReader
from rpa.platforms.wechat.uia import find_chat_message_list_control
from rpa.platforms.wechat.uia_scoring import safe_prop, safe_rect_repr, safe_rect_tuple, walk_controls


if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


class MemoryStore:
    def __init__(self) -> None:
        self.events: list[dict[str, Any]] = []

    def append(self, event: dict[str, Any]) -> int:
        self.events.append(event)
        return len(self.events)


def _media_files(root: Path) -> set[str]:
    if not root.exists():
        return set()
    files: set[str] = set()
    for path in root.rglob("*"):
        try:
            if path.is_file():
                files.add(str(path.resolve()))
        except Exception:
            continue
    return files


def _media_summary(messages: list[dict[str, Any]], *, include_all: bool) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for event in messages:
        payload = event.get("payload", {})
        if not isinstance(payload, dict):
            continue
        metadata = payload.get("metadata", {})
        if not isinstance(metadata, dict):
            metadata = {}
        content_type = payload.get("content_type")
        is_media = (
            content_type in {"image", "emoji", "video", "file"}
            or bool(payload.get("content_image_path"))
            or bool(payload.get("evidence_ref"))
        )
        if not include_all and not is_media:
            continue
        rows.append(
            {
                "platform_msg_id": payload.get("platform_msg_id"),
                "display_name": payload.get("display_name"),
                "direction": payload.get("direction"),
                "content_type": content_type,
                "content": payload.get("content"),
                "content_image_path": payload.get("content_image_path"),
                "evidence_ref": payload.get("evidence_ref"),
                "file_copy_status": metadata.get("file_copy_status"),
                "file_copy_error": metadata.get("file_copy_error"),
                "file_copy_menu_name": metadata.get("file_copy_menu_name"),
                "file_source_paths": metadata.get("file_source_paths"),
                "file_artifact_paths": metadata.get("file_artifact_paths"),
                "evidence_status": metadata.get("evidence_status"),
                "evidence_error": metadata.get("evidence_error"),
                "kind": metadata.get("kind"),
                "class_name": metadata.get("class_name"),
                "automation_id": metadata.get("automation_id"),
                "control_type": metadata.get("control_type"),
                "rect": metadata.get("rect"),
            }
        )
    return rows


def _uia_tree(limit: int = 300) -> list[dict[str, Any]]:
    detector = WechatDetector()
    window = detector.find_main_window_control()
    if window is None:
        return []
    root = find_chat_message_list_control(window)
    if root is None:
        return []

    controls: list[dict[str, Any]] = []
    for depth, control in walk_controls(root, max_depth=8, max_nodes=2500):
        if len(controls) >= limit:
            break
        name = safe_prop(control, "Name")
        class_name = safe_prop(control, "ClassName")
        automation_id = safe_prop(control, "AutomationId")
        control_type = safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType")
        if depth > 0 and not any((name, class_name, automation_id, control_type)):
            continue
        controls.append(
            {
                "depth": depth,
                "name": name,
                "class_name": class_name,
                "automation_id": automation_id,
                "control_type": control_type,
                "rect": safe_rect_repr(control),
            }
        )
    return controls


def _desktop_uia_tree(limit: int = 8000) -> list[tuple[int, Any]]:
    try:
        import uiautomation as auto
    except Exception:
        return []
    try:
        return walk_controls(auto.GetRootControl(), max_depth=8, max_nodes=limit)
    except Exception:
        return []


def _control_signature(control: Any) -> tuple[str, str, str, str, str]:
    return (
        safe_prop(control, "Name"),
        safe_prop(control, "ClassName"),
        safe_prop(control, "AutomationId"),
        safe_prop(control, "ControlTypeName") or safe_prop(control, "LocalizedControlType"),
        safe_rect_repr(control),
    )


def _looks_like_menu(control: Any, click_point: tuple[int, int]) -> bool:
    name, class_name, automation_id, control_type, _rect = _control_signature(control)
    text = " ".join((name, class_name, automation_id, control_type)).lower()
    menu_terms = ("menu", "popup", "菜单", "复制", "保存", "另存", "转发", "收藏", "多选", "删除", "撤回")
    if any(term in text for term in menu_terms):
        return True
    rect = safe_rect_tuple(control)
    if rect is None:
        return False
    x, y = click_point
    return rect[0] - 40 <= x <= rect[2] + 40 and rect[1] - 40 <= y <= rect[3] + 40


def _describe_controls(
    controls: list[tuple[int, Any]],
    *,
    before_signatures: set[tuple[str, str, str, str, str]],
    click_point: tuple[int, int],
    limit: int = 300,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, str, str, str, str]] = set()
    for depth, control in controls:
        signature = _control_signature(control)
        if signature in seen:
            continue
        is_new = signature not in before_signatures
        if not is_new and not _looks_like_menu(control, click_point):
            continue
        seen.add(signature)
        rows.append(
            {
                "new_after_right_click": is_new,
                "depth": depth,
                "name": signature[0],
                "class_name": signature[1],
                "automation_id": signature[2],
                "control_type": signature[3],
                "rect": signature[4],
            }
        )
        if len(rows) >= limit:
            break
    return rows


def _press_escape() -> None:
    try:
        import win32api
        import win32con

        win32api.keybd_event(win32con.VK_ESCAPE, 0, 0, 0)
        win32api.keybd_event(win32con.VK_ESCAPE, 0, win32con.KEYEVENTF_KEYUP, 0)
    except Exception:
        pass


def _clipboard_sequence_number() -> int | None:
    try:
        import win32clipboard

        return int(win32clipboard.GetClipboardSequenceNumber())
    except Exception:
        return None


def _clipboard_formats() -> list[dict[str, Any]]:
    try:
        import win32clipboard
    except Exception as exc:
        return [{"error": f"win32clipboard_unavailable:{type(exc).__name__}"}]

    formats: list[dict[str, Any]] = []
    try:
        win32clipboard.OpenClipboard()
        try:
            fmt = 0
            while True:
                fmt = win32clipboard.EnumClipboardFormats(fmt)
                if not fmt:
                    break
                name = _clipboard_format_name(win32clipboard, fmt)
                available = bool(win32clipboard.IsClipboardFormatAvailable(fmt))
                formats.append({"format": fmt, "name": name, "available": available})
        finally:
            win32clipboard.CloseClipboard()
    except Exception as exc:
        formats.append({"error": type(exc).__name__})
    return formats


def _clipboard_format_name(win32clipboard: Any, fmt: int) -> str:
    known = {
        1: "CF_TEXT",
        2: "CF_BITMAP",
        3: "CF_METAFILEPICT",
        4: "CF_SYLK",
        5: "CF_DIF",
        6: "CF_TIFF",
        7: "CF_OEMTEXT",
        8: "CF_DIB",
        9: "CF_PALETTE",
        13: "CF_UNICODETEXT",
        14: "CF_ENHMETAFILE",
        15: "CF_HDROP",
        17: "CF_DIBV5",
    }
    if fmt in known:
        return known[fmt]
    try:
        return str(win32clipboard.GetClipboardFormatName(fmt))
    except Exception:
        return ""


def _imagegrab_clipboard_probe() -> dict[str, Any]:
    try:
        from PIL import Image, ImageGrab
    except Exception as exc:
        return {"status": "error", "error": f"pillow_unavailable:{type(exc).__name__}"}
    try:
        data = ImageGrab.grabclipboard()
    except Exception as exc:
        return {"status": "error", "error": type(exc).__name__}
    if data is None:
        return {"status": "empty", "type": None}
    if isinstance(data, Image.Image):
        return {"status": "image", "type": type(data).__name__, "mode": data.mode, "size": list(data.size)}
    if isinstance(data, list):
        return {"status": "list", "type": type(data).__name__, "items": [str(item) for item in data]}
    return {"status": "unknown", "type": type(data).__name__, "value": str(data)[:300]}


def diagnose_clipboard_after_copy(limit: int) -> dict[str, Any]:
    reader = WechatVisibleMessageReader()
    try:
        batch = reader.read_visible_messages(limit=limit)
    except Exception as exc:
        return {"status": "error", "error": str(exc) or type(exc).__name__}
    sample = next((item for item in batch.samples if getattr(item, "kind", "") == "image"), None)
    if sample is None:
        return {
            "status": "error",
            "error": "visible_image_message_not_found",
            "display_name": batch.display_name,
        }

    click_point = _context_menu_click_point(sample)
    before_sequence = _clipboard_sequence_number()
    if not _right_click_sample(sample):
        return {
            "status": "error",
            "error": "right_click_failed",
            "display_name": batch.display_name,
            "sample": _sample_summary(sample),
            "click_point": click_point,
        }

    try:
        item = _find_copy_file_menu_item(("复制", "Copy"), timeout_seconds=1.2)
        if item is None:
            return {
                "status": "error",
                "error": "copy_menu_not_found",
                "display_name": batch.display_name,
                "sample": _sample_summary(sample),
                "click_point": click_point,
            }
        menu_name = safe_prop(item, "Name")
        if not _invoke_menu_item(item):
            return {
                "status": "error",
                "error": "copy_menu_invoke_failed",
                "display_name": batch.display_name,
                "sample": _sample_summary(sample),
                "click_point": click_point,
                "menu_name": menu_name,
            }
        time.sleep(0.5)
        return {
            "status": "success",
            "display_name": batch.display_name,
            "sample": _sample_summary(sample),
            "click_point": click_point,
            "menu_name": menu_name,
            "clipboard_sequence_before": before_sequence,
            "clipboard_sequence_after": _clipboard_sequence_number(),
            "clipboard_formats": _clipboard_formats(),
            "imagegrab": _imagegrab_clipboard_probe(),
        }
    finally:
        _press_escape()


def diagnose_context_menu(limit: int) -> dict[str, Any]:
    reader = WechatVisibleMessageReader()
    try:
        batch = reader.read_visible_messages(limit=limit)
    except Exception as exc:
        return {"status": "error", "error": str(exc) or type(exc).__name__}
    sample = next((item for item in batch.samples if getattr(item, "kind", "") == "image"), None)
    if sample is None:
        return {
            "status": "error",
            "error": "visible_image_message_not_found",
            "display_name": batch.display_name,
        }

    click_point = _context_menu_click_point(sample)
    before = _desktop_uia_tree()
    before_signatures = {_control_signature(control) for _depth, control in before}
    if not _right_click_sample(sample):
        return {
            "status": "error",
            "error": "right_click_failed",
            "display_name": batch.display_name,
            "sample": _sample_summary(sample),
            "click_point": click_point,
        }

    try:
        time.sleep(0.5)
        after = _desktop_uia_tree()
        return {
            "status": "success",
            "display_name": batch.display_name,
            "sample": _sample_summary(sample),
            "click_point": click_point,
            "desktop_controls_before": len(before),
            "desktop_controls_after": len(after),
            "context_menu_candidates": _describe_controls(
                after,
                before_signatures=before_signatures,
                click_point=click_point,
            ),
        }
    finally:
        _press_escape()


def _sample_summary(sample: Any) -> dict[str, Any]:
    return {
        "kind": getattr(sample, "kind", ""),
        "name": getattr(sample, "name", ""),
        "direction": getattr(sample, "direction", ""),
        "class_name": getattr(sample, "class_name", ""),
        "automation_id": getattr(sample, "automation_id", ""),
        "rect": getattr(sample, "rect", ""),
    }


def run(limit: int, *, include_all: bool, include_uia_tree: bool) -> dict[str, Any]:
    media_root = repo_root / "python" / "rpa" / "_media" / "wechat"
    before = _media_files(media_root)
    store = MemoryStore()
    adapter = WechatSidecarAdapter(store)
    response = adapter.command(
        {
            "request_id": "manual-media-fetch",
            "platform": "wechat",
            "command": "fetch_visible_messages",
            "parameters": {"limit": limit},
        }
    )
    after = _media_files(media_root)

    result = response.get("result", {}) if isinstance(response, dict) else {}
    if not isinstance(result, dict):
        result = {}
    messages = result.get("messages", [])
    if not isinstance(messages, list):
        messages = []

    summary = {
        "response_status": response.get("status") if isinstance(response, dict) else None,
        "response_error": response.get("error") if isinstance(response, dict) else None,
        "message_count": result.get("count"),
        "display_name": result.get("display_name"),
        "context": result.get("context"),
        "media_count": len(_media_summary(messages, include_all=False)),
        "media_messages": _media_summary(messages, include_all=include_all),
        "new_artifacts": sorted(after - before),
        "event_types": [event.get("event_type") for event in store.events],
    }
    if include_uia_tree:
        summary["uia_tree"] = _uia_tree()
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="微信当前可见会话媒体消息手动验证")
    parser.add_argument("--limit", type=int, default=40, help="读取当前可见消息数量上限，默认 40")
    parser.add_argument("--all", action="store_true", help="同时输出文本消息摘要；默认只输出媒体消息")
    parser.add_argument("--uia-tree", action="store_true", help="输出聊天消息列表下的原始 UIA 控件树摘要")
    parser.add_argument("--context-menu-tree", action="store_true", help="右键第一条可见图片并输出菜单 UIA 控件")
    parser.add_argument("--clipboard-after-copy", action="store_true", help="右键第一条可见图片并点击复制后输出剪贴板格式")
    args = parser.parse_args()

    limit = max(1, min(args.limit, 100))
    if args.context_menu_tree:
        result = diagnose_context_menu(limit)
        print(json.dumps(result, ensure_ascii=False, indent=2, default=str))
        return 0 if result.get("status") == "success" else 1
    if args.clipboard_after_copy:
        result = diagnose_clipboard_after_copy(limit)
        print(json.dumps(result, ensure_ascii=False, indent=2, default=str))
        return 0 if result.get("status") == "success" else 1
    summary = run(limit, include_all=args.all, include_uia_tree=args.uia_tree)
    print(json.dumps(summary, ensure_ascii=False, indent=2, default=str))
    return 0 if summary.get("response_status") == "success" else 1


if __name__ == "__main__":
    raise SystemExit(main())
