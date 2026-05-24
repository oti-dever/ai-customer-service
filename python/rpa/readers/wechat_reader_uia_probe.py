from __future__ import annotations

import json
from pathlib import Path
import sys


PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.wechat_uia import (
    collect_chat_message_samples,
    find_wechat_main_window_uia,
    probe_wechat_uia_capabilities,
)


def main() -> int:
    probe = probe_wechat_uia_capabilities()
    payload: dict[str, object] = {
        "probe": {
            "available": probe.available,
            "reason": probe.reason,
            "main_window_found": probe.main_window_found,
            "session_list_found": probe.session_list_found,
            "chat_input_found": probe.chat_input_found,
            "message_list_found": probe.message_list_found,
            "send_button_found": probe.send_button_found,
        },
        "samples": [],
        "summary": {
            "text_count": 0,
            "image_count": 0,
            "emoji_count": 0,
            "bubble_ref_count": 0,
        },
    }
    if not probe.main_window_found:
        print("WECHAT_UIA_READER_PROBE_JSON=" + json.dumps(payload, ensure_ascii=False))
        return 1
    win = find_wechat_main_window_uia()
    if win is None:
        print("WECHAT_UIA_READER_PROBE_JSON=" + json.dumps(payload, ensure_ascii=False))
        return 1
    payload["samples"] = [
        {
            "kind": item.kind,
            "name": item.name,
            "class_name": item.class_name,
            "automation_id": item.automation_id,
            "control_type": item.control_type,
            "rect": item.rect,
        }
        for item in collect_chat_message_samples(win)
    ]
    for item in payload["samples"]:
        kind = str(item.get("kind", ""))  # type: ignore[union-attr]
        if kind == "text":
            payload["summary"]["text_count"] += 1  # type: ignore[index]
        elif kind == "image":
            payload["summary"]["image_count"] += 1  # type: ignore[index]
        elif kind == "emoji":
            payload["summary"]["emoji_count"] += 1  # type: ignore[index]
        elif kind == "bubble_ref":
            payload["summary"]["bubble_ref_count"] += 1  # type: ignore[index]
    print("WECHAT_UIA_READER_PROBE_JSON=" + json.dumps(payload, ensure_ascii=False))
    return 0 if probe.available else 1


if __name__ == "__main__":
    raise SystemExit(main())
