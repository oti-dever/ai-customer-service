import sys
import types
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.qianniu import debug_probe_accounts as probe


class QianniuAccountProbeTests(unittest.TestCase):
    def test_stable_account_id_is_normalized(self):
        self.assertEqual(
            probe.stable_account_id(" 有求必应羊羊:王刚 "),
            probe.stable_account_id("有求必应羊羊:王刚"),
        )
        self.assertTrue(probe.stable_account_id("有求必应羊羊:王刚").startswith("qnacct_"))

    def test_account_tab_text_filter_accepts_shop_name_and_rejects_chrome(self):
        self.assertTrue(probe.looks_like_account_tab_text("有求必应羊羊:王刚"))
        self.assertTrue(probe.looks_like_account_tab_text("萌动彼岸岸:机器人"))
        self.assertFalse(probe.looks_like_account_tab_text("+"))
        self.assertFalse(probe.looks_like_account_tab_text("客服"))
        self.assertFalse(probe.looks_like_account_tab_text("UIWindow.centralwidget"))
        self.assertFalse(probe.looks_like_account_tab_text("12:30"))

    def test_normalize_account_display_name_removes_close_suffix(self):
        self.assertEqual(probe.normalize_account_display_name("有求必应羊羊:王刚 ×"), "有求必应羊羊:王刚")
        self.assertEqual(probe.normalize_account_display_name("  萌动彼岸岸:机器人  "), "萌动彼岸岸:机器人")

    def test_top_tab_rect_geometry(self):
        root_rect = (0, 0, 1300, 900)
        self.assertTrue(probe.is_top_tab_rect((58, 4, 214, 36), root_rect))
        self.assertTrue(probe.is_top_tab_rect((220, 4, 388, 36), root_rect))
        self.assertFalse(probe.is_top_tab_rect((1178, 4, 1210, 36), root_rect))
        self.assertFalse(probe.is_top_tab_rect((80, 120, 260, 160), root_rect))
        self.assertFalse(probe.is_top_tab_rect((135, 159, 186, 181), (27, 75, 1337, 958)))

    def test_selected_account_prefers_selected_candidate(self):
        left = probe.AccountCandidate(
            account_id="qnacct_left",
            display_name="left",
            selected=False,
            rect="(58,4,214,36)",
            rect_tuple=(58, 4, 214, 36),
            source="uia",
            confidence=0.9,
            raw_texts=["left"],
            automation_id="",
            class_name="",
            control_type="",
        )
        right = probe.AccountCandidate(
            account_id="qnacct_right",
            display_name="right",
            selected=True,
            rect="(220,4,388,36)",
            rect_tuple=(220, 4, 388, 36),
            source="uia",
            confidence=0.7,
            raw_texts=["right"],
            automation_id="",
            class_name="",
            control_type="",
        )

        self.assertEqual(probe.selected_account([left, right]).account_id, "qnacct_right")

    def test_dedupe_keeps_adjacent_account_tabs(self):
        first = probe.AccountCandidate(
            account_id="qnacct_first",
            display_name="有求必应羊羊:王刚",
            selected=True,
            rect="(813,420,984,458)",
            rect_tuple=(813, 420, 984, 458),
            source="uia",
            confidence=1.0,
            raw_texts=["有求必应羊羊:王刚"],
            automation_id="",
            class_name="",
            control_type="TabItemControl",
        )
        second = probe.AccountCandidate(
            account_id="qnacct_second",
            display_name="萌动彼岸岸:机器人",
            selected=False,
            rect="(984,420,1155,458)",
            rect_tuple=(984, 420, 1155, 458),
            source="uia",
            confidence=0.82,
            raw_texts=["萌动彼岸岸:机器人"],
            automation_id="",
            class_name="",
            control_type="TabItemControl",
        )
        parent = probe.AccountCandidate(
            account_id="qnacct_parent",
            display_name="有求必应羊羊:王刚",
            selected=False,
            rect="(813,420,1155,458)",
            rect_tuple=(813, 420, 1155, 458),
            source="uia",
            confidence=0.7,
            raw_texts=["有求必应羊羊:王刚"],
            automation_id="UIWindow.mutilcentralwidget.topWidget.chat_nc_Widget.MutilAccountTabView.widget1.tabbar",
            class_name="UIMutilpleTabBar",
            control_type="TabControl",
        )

        result = probe.dedupe_account_candidates([parent, first, second])

        self.assertEqual([item.account_id for item in result], ["qnacct_first", "qnacct_second"])

    def test_dedupe_keeps_tab_items_when_parent_tabbar_is_selected(self):
        first = probe.AccountCandidate(
            account_id="qnacct_first",
            display_name="shop left:agent",
            selected=False,
            rect="(77,81,248,119)",
            rect_tuple=(77, 81, 248, 119),
            source="uia",
            confidence=0.82,
            raw_texts=["shop left:agent"],
            automation_id="",
            class_name="",
            control_type="TabItemControl",
        )
        second = probe.AccountCandidate(
            account_id="qnacct_second",
            display_name="shop right:robot",
            selected=True,
            rect="(248,81,419,119)",
            rect_tuple=(248, 81, 419, 119),
            source="uia",
            confidence=1.0,
            raw_texts=["shop right:robot"],
            automation_id="",
            class_name="",
            control_type="TabItemControl",
        )
        selected_parent = probe.AccountCandidate(
            account_id="qnacct_parent",
            display_name="shop right:robot",
            selected=True,
            rect="(77,81,419,119)",
            rect_tuple=(77, 81, 419, 119),
            source="uia",
            confidence=0.96,
            raw_texts=["shop right:robot"],
            automation_id="UIWindow.mutilcentralwidget.topWidget.chat_nc_Widget.MutilAccountTabView.widget1.tabbar",
            class_name="UIMutilpleTabBar",
            control_type="TabControl",
        )

        result = probe.dedupe_account_candidates([selected_parent, first, second])

        self.assertEqual([item.account_id for item in result], ["qnacct_first", "qnacct_second"])

    def test_synthetic_single_account_payload(self):
        account = probe.synthetic_single_account()
        self.assertEqual(account.account_id, "local_qianniu")
        self.assertTrue(account.selected)
        self.assertEqual(account.source, "synthetic_single_account")

    def test_first_unread_account_ignores_selected_and_prefers_high_score(self):
        selected = make_account("qnacct_selected", "shop selected")
        selected = probe.AccountCandidate(**{**selected.__dict__, "selected": True, "has_unread_hint": True, "unread_score": 1.0})
        low = make_account("qnacct_low", "shop low")
        low = probe.AccountCandidate(**{**low.__dict__, "has_unread_hint": True, "unread_score": 0.2})
        high = make_account("qnacct_high", "shop high")
        high = probe.AccountCandidate(**{**high.__dict__, "has_unread_hint": True, "unread_score": 0.8})

        self.assertEqual(probe.first_unread_account([selected, low, high]).account_id, "qnacct_high")

    def test_resolve_switch_account_matches_exact_display_name(self):
        left = make_account("qnacct_left", "shop left:agent")
        right = make_account("qnacct_right", "shop right:robot")

        account, mode, error = probe.resolve_switch_account([left, right], "shop right:robot")

        self.assertEqual(account, right)
        self.assertEqual(mode, "display_name_exact")
        self.assertEqual(error, "")

    def test_resolve_switch_account_matches_account_id(self):
        left = make_account("qnacct_left", "shop left:agent")
        right = make_account("qnacct_right", "shop right:robot")

        account, mode, error = probe.resolve_switch_account([left, right], "qnacct_left")

        self.assertEqual(account, left)
        self.assertEqual(mode, "account_id_exact")
        self.assertEqual(error, "")

    def test_resolve_switch_account_matches_unique_contains(self):
        left = make_account("qnacct_left", "shop left:agent")
        right = make_account("qnacct_right", "shop right:robot")

        account, mode, error = probe.resolve_switch_account([left, right], "right")

        self.assertEqual(account, right)
        self.assertEqual(mode, "contains")
        self.assertEqual(error, "")

    def test_resolve_switch_account_reports_not_found(self):
        account, mode, error = probe.resolve_switch_account([make_account("qnacct_left", "shop left:agent")], "missing")

        self.assertIsNone(account)
        self.assertEqual(mode, "")
        self.assertIn("switch_account_not_found", error)

    def test_resolve_switch_account_reports_ambiguous_contains(self):
        left = make_account("qnacct_left", "shop left:agent")
        right = make_account("qnacct_right", "shop right:robot")

        account, mode, error = probe.resolve_switch_account([left, right], "shop")

        self.assertIsNone(account)
        self.assertEqual(mode, "")
        self.assertIn("switch_account_ambiguous", error)

    def test_detect_current_accounts_uses_supplied_hwnd(self):
        root = object()
        expected = [probe.synthetic_single_account()]

        class Detector:
            def find_best_window(self):
                raise AssertionError("global window search should not be used when hwnd is supplied")

        old_control_from_hwnd = probe.control_from_hwnd
        old_grab_top_image = probe.grab_top_image
        old_detect_account_candidates = probe.detect_account_candidates
        try:
            probe.control_from_hwnd = lambda hwnd: root if hwnd == 1234 else None
            probe.grab_top_image = lambda actual_root: None

            def fake_detect_account_candidates(actual_root, **_kwargs):
                self.assertIs(actual_root, root)
                return expected

            probe.detect_account_candidates = fake_detect_account_candidates
            self.assertEqual(probe.detect_current_accounts(Detector(), hwnd=1234), expected)
        finally:
            probe.control_from_hwnd = old_control_from_hwnd
            probe.grab_top_image = old_grab_top_image
            probe.detect_account_candidates = old_detect_account_candidates

    def test_detect_account_candidates_delegates_to_production_detector(self):
        root = object()
        progress_messages = []
        expected = probe.ProductionAccountTab(
            account_id="qnacct_shared",
            display_name="shop shared:agent",
            selected=False,
            rect="(10,20,30,40)",
            rect_tuple=(10, 20, 30, 40),
            control=object(),
            source="uia",
            confidence=0.9,
            raw_texts=["shop shared:agent"],
            has_unread_hint=True,
            unread_score=0.8,
        )

        old_detect = probe.detect_production_account_candidates
        try:
            def fake_detect(actual_root, **kwargs):
                self.assertIs(actual_root, root)
                self.assertEqual(kwargs["top_image"], "image")
                self.assertEqual(kwargs["max_depth"], 3)
                self.assertEqual(kwargs["max_nodes"], 20)
                self.assertEqual(kwargs["min_confidence"], 0.4)
                self.assertEqual(kwargs["limit"], 2)
                kwargs["progress"]("production-progress")
                return [expected]

            probe.detect_production_account_candidates = fake_detect
            result = probe.detect_account_candidates(
                root,
                top_image="image",
                max_depth=3,
                max_nodes=20,
                min_confidence=0.4,
                limit=2,
                progress=progress_messages.append,
            )
        finally:
            probe.detect_production_account_candidates = old_detect

        self.assertEqual(progress_messages, ["production-progress"])
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].account_id, "qnacct_shared")
        self.assertTrue(result[0].has_unread_hint)

    def test_read_visible_sessions_uses_selected_window_root(self):
        class FakeControl:
            def __init__(self, automation_id="", children=None):
                self.AutomationId = automation_id
                self._children = list(children or [])

            def GetChildren(self):
                return self._children

        session_root = FakeControl("UIWindow.centralwidget.normalList")
        window_root = FakeControl("UIWindow", [session_root])
        expected = ["session"]
        test_case = self

        class Detector:
            q = types.SimpleNamespace(
                max_tree_depth=12,
                max_tree_nodes=3000,
                reception_normal_list_suffix="normalList",
                chat_list_items_suffix="list_widget",
            )

            def find_best_window(self):
                raise AssertionError("global window search should not be used during switch verification")

            def find_current_chat(self):
                raise AssertionError("session reader global chat detection should not be used")

            def find_chat_root_candidates(self, root):
                test_case.assertIs(root, window_root)
                return []

            def find_reception_normal_list(self, chat_root):
                test_case.assertIs(chat_root, window_root)
                return session_root

            def find_chat_list_items_root(self, _chat_root):
                raise AssertionError("fallback should not be reached when reception list is found")

        old_control_from_hwnd = probe.control_from_hwnd
        old_extract_session_items = probe.extract_session_items
        try:
            probe.control_from_hwnd = lambda hwnd: window_root if hwnd == 5678 else None

            def fake_extract_session_items(root, limit):
                self.assertIs(root, session_root)
                self.assertEqual(limit, 20)
                return expected

            probe.extract_session_items = fake_extract_session_items
            self.assertEqual(probe.read_visible_sessions_from_window(Detector(), hwnd=5678, limit=20), expected)
        finally:
            probe.control_from_hwnd = old_control_from_hwnd
            probe.extract_session_items = old_extract_session_items

    def test_click_account_tab_activates_supplied_hwnd(self):
        account = probe.AccountCandidate(
            account_id="qnacct_click",
            display_name="click",
            selected=False,
            rect="(10,20,30,40)",
            rect_tuple=(10, 20, 30, 40),
            source="uia",
            confidence=1.0,
            raw_texts=["click"],
            automation_id="",
            class_name="",
            control_type="TabItemControl",
        )
        activations = []
        cursor_positions = []

        old_activate_window = probe.activate_window
        old_win32api = sys.modules.get("win32api")
        old_win32con = sys.modules.get("win32con")
        try:
            probe.activate_window = lambda hwnd: activations.append(hwnd) or True
            sys.modules["win32api"] = types.SimpleNamespace(
                GetCursorPos=lambda: (1, 2),
                SetCursorPos=lambda pos: cursor_positions.append(pos),
                mouse_event=lambda *_args: None,
            )
            sys.modules["win32con"] = types.SimpleNamespace(MOUSEEVENTF_LEFTDOWN=2, MOUSEEVENTF_LEFTUP=4)

            self.assertEqual(probe.click_account_tab(account, hwnd=2468, activate=True), (True, "rect_click"))
        finally:
            probe.activate_window = old_activate_window
            if old_win32api is None:
                sys.modules.pop("win32api", None)
            else:
                sys.modules["win32api"] = old_win32api
            if old_win32con is None:
                sys.modules.pop("win32con", None)
            else:
                sys.modules["win32con"] = old_win32con

        self.assertEqual(activations, [2468])
        self.assertEqual(cursor_positions[0], (20, 30))
        self.assertEqual(cursor_positions[-1], (1, 2))


def make_account(account_id: str, display_name: str) -> probe.AccountCandidate:
    return probe.AccountCandidate(
        account_id=account_id,
        display_name=display_name,
        selected=False,
        rect="(10,20,30,40)",
        rect_tuple=(10, 20, 30, 40),
        source="uia",
        confidence=1.0,
        raw_texts=[display_name],
        automation_id="",
        class_name="",
        control_type="TabItemControl",
    )


if __name__ == "__main__":
    unittest.main()
