# Common RPA utilities shared across platforms
#
# 注意：通用基础设施模块已迁移到 rpa.core，此处保留重导出以保持旧 import 路径兼容。
# 新代码建议直接从 rpa.core 导入。

try:
    from ..core.input_sim import (
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
    from ..core.screenshot import (
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
    from ..core.win32_window import (
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
    from ..core.ocr_engine import (
        BaseOCREngine,
        PaddleOCREngine,
        RapidOCREngine,
        build_ocr_engine,
        bgra_to_pil,
        OCRBlock,
    )
    from ..core.window_lock import (
        PlatformWindowLock,
        hold_platform_window_lock,
    )
    from ..core.incremental import (
        IncrementalDetector,
        content_hash,
        make_platform_msg_id,
        MessageLike,
    )
    from ..core.rpa_console_log import (
        rpa_log,
        rpa_phase,
        rpa_heartbeat,
    )
    from ..core.name_stabilizer import NameStabilizer
except ImportError:
    # 降级：core/ 尚未创建时使用本地模块
    pass

# 平台专用模块已迁移到 rpa.platforms，此处保留重导出以保持旧 import 路径兼容。
# 新代码建议直接从 rpa.platforms.{platform} 导入。

# 千牛平台模块兼容重导出
try:
    from ..platforms.qianniu import window as qianniu_window
    from ..platforms.qianniu import session as qianniu_session
    from ..platforms.qianniu import coords as qianniu_coords
    from ..platforms.qianniu import header as qianniu_header
    from ..platforms.qianniu import chat_parser as qianniu_chat_parser
    from ..platforms.qianniu import bubble_parser as qianniu_bubble_parser
    from ..platforms.qianniu import clicknium as qianniu_clicknium
except ImportError:
    pass

# 微信平台模块兼容重导出
try:
    from ..platforms.wechat import uia as wechat_uia
except ImportError:
    pass
