import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu import accounts
from rpa.platforms.qianniu.adapter import QianniuSidecarAdapter
from rpa.platforms.qianniu.config import AppConfig
from rpa.platforms.qianniu.detector import WindowCandidate


class FakeRect:
    def __init__(self, left, top, right, bottom):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeControl:
    def __init__(
        self,
        *,
        name="",
        rect=(0, 0, 0, 0),
        control_type="GroupControl",
        automation_id="",
        class_name="",
        selected=False,
        children=None,
    ):
        self.Name = name
        self.BoundingRectangle = FakeRect(*rect)
        self.ControlTypeName = control_type
        self.AutomationId = automation_id
        self.ClassName = class_name
        self.IsSelected = "true" if selected else "false"
        self._children = list(children or [])

    def GetChildren(self):
        return list(self._children)


class FakeDetector:
    config = AppConfig()

    def find_process_ids(self):
        return [1234]

    def score_window_candidate(self, class_name, title=""):
        return 60, "class-hint"


class FakeStore:
    def append(self, event):
        return 1


class CacheComponent:
    def __init__(self):
        self.invalidate_calls = 0

    def invalidate_cache(self):
        self.invalidate_calls += 1


class QianniuAccountsTests(unittest.TestCase):
    def test_stable_account_id_uses_display_name(self):
        self.assertEqual(
            accounts.stable_account_id(" 有求必应羊羊:王刚 "),
            accounts.stable_account_id("有求必应羊羊:王刚"),
        )
        self.assertTrue(accounts.stable_account_id("萌动彼岸岸:机器人").startswith("qnacct_"))

    def test_list_accounts_reads_top_tab_items_without_ocr(self):
        root = make_root(
            FakeControl(name="有求必应羊羊:王刚", rect=(77, 81, 248, 119), control_type="TabItemControl"),
            FakeControl(
                name="萌动彼岸岸:机器人",
                rect=(248, 81, 419, 119),
                control_type="TabItemControl",
                selected=True,
            ),
        )
        reader = make_reader(self, root)

        listed = reader.list_accounts(fresh=True)

        self.assertEqual([item.display_name for item in listed], ["有求必应羊羊:王刚", "萌动彼岸岸:机器人"])
        self.assertEqual(reader.selected_account().display_name, "萌动彼岸岸:机器人")

    def test_list_accounts_can_activate_window_before_visual_scan(self):
        root = make_root(FakeControl(name="shop shared:agent", rect=(77, 81, 248, 119), control_type="TabItemControl"))
        calls = []
        old_find_windows = accounts.find_window_candidates_win32_only
        old_control_from_hwnd = accounts.control_from_hwnd
        old_grab_top_image = accounts.grab_top_image
        old_activate_window = accounts.activate_window
        try:
            accounts.find_window_candidates_win32_only = lambda _detector: [
                WindowCandidate(
                    hwnd=1001,
                    pid=1234,
                    class_name="Qt5152QWindowIcon",
                    title="Qianniu",
                    score=60,
                    reason="class-hint",
                )
            ]
            accounts.control_from_hwnd = lambda hwnd: root if hwnd == 1001 else None
            accounts.activate_window = lambda hwnd: calls.append(("activate", hwnd)) or True
            accounts.grab_top_image = lambda actual_root: calls.append(("grab", actual_root)) or None
            reader = accounts.QianniuAccountReader(
                AppConfig(),
                detector=FakeDetector(),
                activate_before_scan=True,
            )

            listed = reader.list_accounts(fresh=True)
        finally:
            accounts.find_window_candidates_win32_only = old_find_windows
            accounts.control_from_hwnd = old_control_from_hwnd
            accounts.grab_top_image = old_grab_top_image
            accounts.activate_window = old_activate_window

        self.assertEqual(len(listed), 1)
        self.assertEqual(calls, [("activate", 1001), ("grab", root)])

    def test_list_accounts_uses_single_account_fallback_when_no_tabs(self):
        root = make_root()
        reader = make_reader(self, root)

        listed = reader.list_accounts(fresh=True)

        self.assertEqual(len(listed), 1)
        self.assertEqual(listed[0].account_id, accounts.DEFAULT_ACCOUNT_ID)
        self.assertEqual(listed[0].source, "synthetic_single_account")

    def test_list_accounts_does_not_fallback_when_multi_account_container_exists(self):
        root = make_root(
            FakeControl(
                rect=(77, 81, 1241, 119),
                control_type="GroupControl",
                class_name="MutilAccountTabView",
                automation_id="UIWindow.mutilcentralwidget.topWidget.chat_nc_Widget.MutilAccountTabView",
            )
        )
        reader = make_reader(self, root)

        self.assertEqual(reader.list_accounts(fresh=True), [])

    def test_unread_hint_ignores_left_shop_icon(self):
        root = make_top_root(FakeControl(name="shop left:agent", rect=(10, 6, 180, 44), control_type="TabItemControl"))
        image = make_top_image()
        draw_orange_shop_icon(image, x=16, y=16)

        listed = accounts.detect_account_tabs(
            root,
            top_image=image,
            max_depth=4,
            max_nodes=100,
            min_confidence=0.35,
            limit=10,
        )

        self.assertEqual(len(listed), 1)
        self.assertFalse(listed[0].has_unread_hint)
        self.assertEqual(listed[0].unread_score, 0.0)

    def test_unread_hint_detects_red_badge_inside_account_tab(self):
        root = make_top_root(FakeControl(name="shop left:agent", rect=(10, 6, 180, 44), control_type="TabItemControl"))
        image = make_top_image()
        draw_orange_shop_icon(image, x=16, y=16)
        draw_red_badge(image, x=112, y=17)

        listed = accounts.detect_account_tabs(
            root,
            top_image=image,
            max_depth=4,
            max_nodes=100,
            min_confidence=0.35,
            limit=10,
        )

        self.assertEqual(len(listed), 1)
        self.assertTrue(listed[0].has_unread_hint)
        self.assertGreaterEqual(listed[0].unread_score, 0.18)
        self.assertIsNotNone(listed[0].unread_hint_rect)

    def test_unread_hint_reads_badge_and_elapsed_text_from_child_controls(self):
        root = make_top_root(
            FakeControl(
                name="shop left:agent",
                rect=(10, 6, 180, 44),
                control_type="TabItemControl",
                children=[
                    FakeControl(name="1", rect=(112, 17, 125, 30), control_type="TextControl"),
                    FakeControl(name="3秒", rect=(130, 17, 156, 30), control_type="TextControl"),
                ],
            )
        )

        listed = accounts.detect_account_tabs(
            root,
            top_image=None,
            max_depth=4,
            max_nodes=100,
            min_confidence=0.35,
            limit=10,
        )

        self.assertEqual(len(listed), 1)
        self.assertTrue(listed[0].has_unread_hint)
        self.assertEqual(listed[0].unread_badge_text, "1")
        self.assertEqual(listed[0].unread_elapsed_text, "3秒")

    def test_switch_account_by_display_name_clicks_and_invalidates(self):
        left = FakeControl(name="有求必应羊羊:王刚", rect=(77, 81, 248, 119), control_type="TabItemControl")
        right = FakeControl(
            name="萌动彼岸岸:机器人",
            rect=(248, 81, 419, 119),
            control_type="TabItemControl",
            selected=True,
        )
        root = make_root(left, right)
        switched = []

        def fake_click(account, *, hwnd, activate):
            self.assertEqual(account.display_name, "有求必应羊羊:王刚")
            self.assertEqual(hwnd, 1001)
            left.IsSelected = "true"
            right.IsSelected = "false"
            return True, "rect_click"

        reader = make_reader(self, root, click=fake_click, on_switched=lambda: switched.append(True))

        ok, method = reader.switch_account("有求必应羊羊:王刚")

        self.assertTrue(ok)
        self.assertEqual(method, "rect_click")
        self.assertEqual(switched, [True])
        self.assertEqual(reader.selected_account().display_name, "有求必应羊羊:王刚")

    def test_switch_account_returns_not_found_without_clicking(self):
        root = make_root(FakeControl(name="有求必应羊羊:王刚", rect=(77, 81, 248, 119), control_type="TabItemControl"))
        click_calls = []
        reader = make_reader(self, root, click=lambda *_args, **_kwargs: click_calls.append(True) or (True, "rect_click"))

        ok, detail = reader.switch_account("missing-shop")

        self.assertFalse(ok)
        self.assertIn("account_not_found", detail)
        self.assertEqual(click_calls, [])

    def test_adapter_invalidates_account_scoped_caches(self):
        adapter = QianniuSidecarAdapter(FakeStore())
        sessions = CacheComponent()
        reader = CacheComponent()
        sender = CacheComponent()
        account_reader = CacheComponent()
        adapter._accounts = account_reader
        adapter._sessions = sessions
        adapter._reader = reader
        adapter._sender = sender
        adapter._handled_unread_session_keys = {"local_qianniu:Alice"}

        adapter._invalidate_account_scoped_caches()

        self.assertEqual(account_reader.invalidate_calls, 1)
        self.assertEqual(sessions.invalidate_calls, 0)
        self.assertEqual(reader.invalidate_calls, 1)
        self.assertEqual(sender.invalidate_calls, 1)
        self.assertEqual(adapter._handled_unread_session_keys, set())


def make_root(*tabs):
    return FakeControl(
        name="千牛接待台",
        rect=(27, 75, 1337, 958),
        control_type="WindowControl",
        class_name="MutilChatView",
        automation_id="UIWindow",
        children=list(tabs),
    )


def make_top_root(*tabs):
    return FakeControl(
        name="Qianniu",
        rect=(0, 0, 600, 80),
        control_type="WindowControl",
        class_name="MutilChatView",
        automation_id="UIWindow",
        children=list(tabs),
    )


def make_top_image():
    from PIL import Image

    return Image.new("RGB", (600, 80), (245, 247, 250))


def draw_orange_shop_icon(image, *, x, y):
    from PIL import ImageDraw

    draw = ImageDraw.Draw(image)
    draw.ellipse((x, y, x + 13, y + 13), fill=(238, 85, 24))


def draw_red_badge(image, *, x, y):
    from PIL import ImageDraw

    draw = ImageDraw.Draw(image)
    draw.ellipse((x, y, x + 12, y + 12), fill=(235, 0, 0))


def make_reader(test_case, root, *, click=None, on_switched=None):
    old_find_windows = accounts.find_window_candidates_win32_only
    old_control_from_hwnd = accounts.control_from_hwnd
    old_grab_top_image = accounts.grab_top_image
    old_click = accounts.click_account_tab
    accounts.find_window_candidates_win32_only = lambda _detector: [
        WindowCandidate(
            hwnd=1001,
            pid=1234,
            class_name="Qt5152QWindowIcon",
            title="千牛接待台",
            score=60,
            reason="class-hint",
        )
    ]
    accounts.control_from_hwnd = lambda hwnd: root if hwnd == 1001 else None
    accounts.grab_top_image = lambda _root: None
    if click is not None:
        accounts.click_account_tab = click
    test_case.addCleanup(
        restore_accounts_module,
        old_find_windows,
        old_control_from_hwnd,
        old_grab_top_image,
        old_click,
    )
    detector = FakeDetector()
    return accounts.QianniuAccountReader(
        detector.config,
        detector=detector,
        on_switched=on_switched,
        wait_seconds=0.1,
    )


def restore_accounts_module(old_find_windows, old_control_from_hwnd, old_grab_top_image, old_click):
    accounts.find_window_candidates_win32_only = old_find_windows
    accounts.control_from_hwnd = old_control_from_hwnd
    accounts.grab_top_image = old_grab_top_image
    accounts.click_account_tab = old_click


if __name__ == "__main__":
    unittest.main()
