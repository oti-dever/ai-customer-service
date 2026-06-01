"""
RPA 通用基础设施模块。

提供与平台无关的 Win32 输入模拟、窗口截图、OCR 引擎、窗口锁、增量去重等能力。
平台特化代码应放在 platforms/ 子目录下。
"""
from .input_sim import (
    simulate_click,
    simulate_double_click,
    simulate_key,
    simulate_key_combo,
    simulate_type_unicode,
    set_clipboard_text,
    get_clipboard_text,
    ClipboardGuard,
    client_to_screen,
    post_click,
    post_click_window_at_point,
    post_key,
    post_key_combo,
    post_clear_text,
    post_type_text,
    get_window_at_point,
)
from .screenshot import (
    capture_region,
    capture_window_printwindow,
    crop_bgra,
    save_bgra_png,
    get_window_rect,
    get_client_area_in_window_bitmap,
    is_window_valid,
    is_window_minimized,
    hwnd_screen_rect_unobstructed,
    hwnd_capture_subrect_unobstructed,
    hwnd_screen_root_unobstructed,
)
from .win32_window import (
    find_window,
    find_all_windows,
    find_largest_window,
    find_window_by_title_candidates,
    get_window_text,
    get_process_name,
    enum_windows,
    screen_to_client,
    get_foreground_window,
    is_window_visible,
    bring_to_foreground,
    window_client_area_pixels,
)
from .ocr_engine import (
    BaseOCREngine,
    PaddleOCREngine,
    RapidOCREngine,
    build_ocr_engine,
    bgra_to_pil,
    OCRBlock,
)
from .window_lock import (
    PlatformWindowLock,
    hold_platform_window_lock,
)
from .incremental import (
    IncrementalDetector,
    content_hash,
    make_platform_msg_id,
    MessageLike,
)
from .rpa_console_log import (
    rpa_log,
    rpa_phase,
    rpa_heartbeat,
)
from .name_stabilizer import NameStabilizer
from .layout_parser import (
    LayoutParserConfig,
    ParsedMessage,
    default_layout_config,
    parse_chat_layout,
)

__all__ = [
    # input_sim
    "simulate_click",
    "simulate_double_click",
    "simulate_key",
    "simulate_key_combo",
    "simulate_type_unicode",
    "set_clipboard_text",
    "get_clipboard_text",
    "ClipboardGuard",
    "client_to_screen",
    "post_click",
    "post_click_window_at_point",
    "post_key",
    "post_key_combo",
    "post_clear_text",
    "post_type_text",
    "get_window_at_point",
    # screenshot
    "capture_region",
    "capture_window_printwindow",
    "crop_bgra",
    "save_bgra_png",
    "get_window_rect",
    "get_client_area_in_window_bitmap",
    "is_window_valid",
    "is_window_minimized",
    "hwnd_screen_rect_unobstructed",
    "hwnd_capture_subrect_unobstructed",
    "hwnd_screen_root_unobstructed",
    # win32_window
    "find_window",
    "find_all_windows",
    "find_largest_window",
    "find_window_by_title_candidates",
    "get_window_text",
    "get_process_name",
    "enum_windows",
    "screen_to_client",
    "get_foreground_window",
    "is_window_visible",
    "bring_to_foreground",
    "window_client_area_pixels",
    # ocr_engine
    "BaseOCREngine",
    "PaddleOCREngine",
    "RapidOCREngine",
    "build_ocr_engine",
    "bgra_to_pil",
    "OCRBlock",
    # window_lock
    "PlatformWindowLock",
    "hold_platform_window_lock",
    # incremental
    "IncrementalDetector",
    "content_hash",
    "make_platform_msg_id",
    "MessageLike",
    # rpa_console_log
    "rpa_log",
    "rpa_phase",
    "rpa_heartbeat",
    # name_stabilizer
    "NameStabilizer",
    # layout_parser
    "LayoutParserConfig",
    "ParsedMessage",
    "default_layout_config",
    "parse_chat_layout",
]
