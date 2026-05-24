from __future__ import annotations

import json
from pathlib import Path
import sys


PYTHON_DIR = Path(__file__).resolve().parents[2]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.wechat_uia import (
    CHAT_BUBBLE_REFER_ITEM_CLASS,
    CHAT_MESSAGE_LIST_AUTOMATION_ID,
    CHAT_TEXT_ITEM_CLASS,
    INPUT_AUTOMATION_ID,
    SESSION_ITEM_AUTOMATION_ID_PREFIX,
    SESSION_ITEM_CLASS,
    SESSION_LIST_AUTOMATION_ID,
    probe_wechat_uia_capabilities,
)


def main() -> int:
    result = probe_wechat_uia_capabilities()
    payload = {
        "available": result.available,
        "reason": result.reason,
        "main_window_found": result.main_window_found,
        "session_list_found": result.session_list_found,
        "chat_input_found": result.chat_input_found,
        "message_list_found": result.message_list_found,
        "send_button_found": result.send_button_found,
        "env_hint": result.env_hint if not result.available else "",
        "selectors": {
            "session_list_automation_id": SESSION_LIST_AUTOMATION_ID,
            "session_item_class": SESSION_ITEM_CLASS,
            "session_item_automation_id_prefix": SESSION_ITEM_AUTOMATION_ID_PREFIX,
            "chat_input_automation_id": INPUT_AUTOMATION_ID,
            "chat_message_list_automation_id": CHAT_MESSAGE_LIST_AUTOMATION_ID,
            "chat_text_item_class": CHAT_TEXT_ITEM_CLASS,
            "chat_bubble_refer_item_class": CHAT_BUBBLE_REFER_ITEM_CLASS,
        },
    }
    print("WECHAT_UIA_PROBE_JSON=" + json.dumps(payload, ensure_ascii=False))
    return 0 if result.available else 1


if __name__ == "__main__":
    raise SystemExit(main())
