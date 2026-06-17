import sys
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.wechat.adapter import _event_confidence
from rpa.platforms.wechat.click_strategy import click_session_item_detailed
from rpa.platforms.wechat.media_clipboard import (
    copy_clipboard_bitmap_to_artifact,
    copy_clipboard_files_to_artifacts,
)
from rpa.platforms.wechat.media_context_menu import (
    _click_menu_item_with_mouse,
    _context_menu_click_point,
    _find_copy_file_menu_item,
    _iter_candidate_menu_items,
    _menu_item_click_point,
    copy_message_file_via_context_menu,
)
from rpa.platforms.wechat.media_evidence import MediaEvidenceWriter
from rpa.platforms.wechat.reader import _attach_roles_to_samples, _attach_window_hwnd_to_samples, _message_kind
from rpa.platforms.wechat.role_judgement import MessageRoleJudgement, judge_by_position
from rpa.platforms.wechat.screenshot import trim_media_evidence
from rpa.platforms.wechat.uia import collect_chat_layout_samples


class FakeRect:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeControl:
    def __init__(self, rect, *, name="", class_name=""):
        self.BoundingRectangle = rect
        self.Name = name
        self.ClassName = class_name
        self.AutomationId = ""
        self.ControlTypeName = "ListItemControl"

    def Click(self, simulateMove=False, waitTime=0):
        self.clicked = True

    def GetInvokePattern(self):
        return None

    def GetSelectionItemPattern(self):
        return None

    def GetParentControl(self):
        return None


class FakeSample:
    def __init__(self, left=0, top=0, right=0, bottom=0):
        self.left = left
        self.top = top
        self.right = right
        self.bottom = bottom


class FakeImage:
    def __init__(self):
        self.saved = []

    def save(self, path, _format):
        self.saved.append((path, _format))


class FakeClipboardResult:
    def __init__(self, status="copied", source_paths=None, artifact_paths=None, error=""):
        self.status = status
        self.source_paths = source_paths or []
        self.artifact_paths = artifact_paths or []
        self.error = error


class FakeMenuItem:
    Name = "复制"

    def __init__(self):
        self.invoked = False

    def GetInvokePattern(self):
        return self

    def Invoke(self):
        self.invoked = True


class FakeMenuTreeItem:
    def __init__(
        self,
        name="",
        children=None,
        *,
        class_name="",
        automation_id="",
        control_type="",
    ):
        self.Name = name
        self._children = list(children or [])
        self.ClassName = class_name
        self.AutomationId = automation_id
        self.ControlTypeName = control_type

    def GetChildren(self):
        return self._children


class FakeMenuAutomation:
    def __init__(self, root):
        self._root = root

    def GetRootControl(self):
        return self._root


class WechatRoleAndSendTests(unittest.TestCase):
    def test_role_by_position_inbound_left_of_message_list_center(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        bubble = FakeControl(FakeRect(260, 180, 520, 230))

        result = judge_by_position(bubble, message_list)

        self.assertEqual(result.direction, "in")
        self.assertEqual(result.role, "customer")
        self.assertEqual(result.method, "bubble_position")

    def test_role_by_position_outbound_right_of_message_list_center(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        bubble = FakeControl(FakeRect(700, 180, 960, 230))

        result = judge_by_position(bubble, message_list)

        self.assertEqual(result.direction, "out")
        self.assertEqual(result.role, "agent")
        self.assertEqual(result.method, "bubble_position")

    def test_role_by_position_supports_sample_rect(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        sample = FakeSample(700, 180, 960, 230)

        result = judge_by_position(sample, message_list)

        self.assertEqual(result.direction, "out")
        self.assertEqual(result.role, "agent")
        self.assertEqual(result.method, "bubble_position")

    def test_attach_roles_uses_full_role_judgement_for_samples(self):
        message_list = FakeControl(FakeRect(200, 100, 1000, 800))
        sample = FakeSample(260, 180, 520, 230)
        role = MessageRoleJudgement(
            direction="out",
            role="agent",
            method="bubble_variance",
            confidence=0.88,
        )

        with patch("rpa.platforms.wechat.reader.judge_message_role", return_value=role) as mocked:
            result = _attach_roles_to_samples([sample], message_list=message_list, window_hwnd=123)

        mocked.assert_called_once_with(sample, message_list=message_list, window_hwnd=123)
        self.assertIs(result[0], sample)
        self.assertEqual(sample.direction, "out")
        self.assertEqual(sample.sender_role, "agent")
        self.assertEqual(sample.direction_method, "bubble_variance")
        self.assertEqual(sample.role_confidence, 0.88)

    def test_attach_window_hwnd_to_samples(self):
        sample = FakeSample(260, 180, 520, 230)

        result = _attach_window_hwnd_to_samples([sample], 123)

        self.assertIs(result[0], sample)
        self.assertEqual(sample.window_hwnd, 123)

    def test_event_confidence_normalizes_ratio(self):
        self.assertEqual(_event_confidence(0.73, default=65), 73)
        self.assertEqual(_event_confidence(87, default=65), 87)
        self.assertEqual(_event_confidence(None, default=65), 65)

    def test_message_kind_recognizes_media_placeholders(self):
        expected = {
            "[图片]": "image",
            "[动画表情]": "emoji",
            "[视频]": "video",
            "[文件]": "file",
        }
        for name, kind in expected.items():
            with self.subTest(name=name):
                self.assertEqual(_message_kind(FakeControl(FakeRect(), name=name)), kind)

    def test_message_kind_recognizes_bare_media_names_on_bubble_refs(self):
        expected = {
            "图片": "image",
            "动画表情": "emoji",
            "视频": "video",
            "文件": "file",
        }
        for name, kind in expected.items():
            with self.subTest(name=name):
                control = FakeControl(FakeRect(), name=name, class_name="mmui::ChatBubbleReferItemView")
                self.assertEqual(_message_kind(control), kind)

        text_control = FakeControl(FakeRect(), name="图片", class_name="mmui::ChatTextItemView")
        self.assertEqual(_message_kind(text_control), "text")

    def test_message_kind_recognizes_multiline_file_cards(self):
        control = FakeControl(
            FakeRect(),
            name="文件\n支付宝交易明细(20260608-20260608).pdf\n343.4K",
            class_name="mmui::ChatBubbleItemView",
        )

        self.assertEqual(_message_kind(control), "file")

    def test_message_kind_recognizes_video_cards_with_duration(self):
        control = FakeControl(
            FakeRect(),
            name="视频 0:04",
            class_name="mmui::ChatBubbleReferItemView",
        )

        self.assertEqual(_message_kind(control), "video")

    def test_collect_chat_layout_samples_recognizes_bare_media_names_on_bubble_refs(self):
        controls = [
            FakeControl(FakeRect(10, 10, 100, 50), name="动画表情", class_name="mmui::ChatBubbleReferItemView"),
            FakeControl(FakeRect(10, 60, 100, 100), name="图片", class_name="mmui::ChatBubbleReferItemView"),
        ]

        def visit_controls(_root, visit, **_kwargs):
            for control in controls:
                visit(control)

        with (
            patch("rpa.platforms.wechat.uia.find_chat_message_list_control", return_value=object()),
            patch("rpa.platforms.wechat.uia._walk_controls", side_effect=visit_controls),
        ):
            samples = collect_chat_layout_samples(object())

        self.assertEqual([sample.kind for sample in samples], ["emoji", "image"])

    def test_collect_chat_layout_samples_recognizes_multiline_file_cards(self):
        controls = [
            FakeControl(
                FakeRect(10, 10, 100, 80),
                name="文件\n支付宝交易明细(20260608-20260608).pdf\n343.4K",
                class_name="mmui::ChatBubbleItemView",
            ),
        ]

        def visit_controls(_root, visit, **_kwargs):
            for control in controls:
                visit(control)

        with (
            patch("rpa.platforms.wechat.uia.find_chat_message_list_control", return_value=object()),
            patch("rpa.platforms.wechat.uia._walk_controls", side_effect=visit_controls),
        ):
            samples = collect_chat_layout_samples(object())

        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0].kind, "file")
        self.assertEqual(samples[0].name, "文件 支付宝交易明细(20260608-20260608).pdf 343.4K")

    def test_collect_chat_layout_samples_recognizes_video_cards_with_duration(self):
        controls = [
            FakeControl(
                FakeRect(10, 10, 100, 80),
                name="视频 0:04",
                class_name="mmui::ChatBubbleReferItemView",
            ),
        ]

        def visit_controls(_root, visit, **_kwargs):
            for control in controls:
                visit(control)

        with (
            patch("rpa.platforms.wechat.uia.find_chat_message_list_control", return_value=object()),
            patch("rpa.platforms.wechat.uia._walk_controls", side_effect=visit_controls),
        ):
            samples = collect_chat_layout_samples(object())

        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0].kind, "video")
        self.assertEqual(samples[0].name, "视频 0:04")

    def test_collect_chat_layout_samples_recognizes_video_and_file(self):
        controls = [
            FakeControl(FakeRect(10, 10, 100, 50), name="[视频]", class_name="mmui::ChatTextItemView"),
            FakeControl(FakeRect(10, 60, 100, 100), name="[文件]", class_name="mmui::ChatTextItemView"),
        ]

        def visit_controls(_root, visit, **_kwargs):
            for control in controls:
                visit(control)

        with (
            patch("rpa.platforms.wechat.uia.find_chat_message_list_control", return_value=object()),
            patch("rpa.platforms.wechat.uia._walk_controls", side_effect=visit_controls),
        ):
            samples = collect_chat_layout_samples(object())

        self.assertEqual([sample.kind for sample in samples], ["video", "file"])

    def test_media_evidence_requires_window_handle_for_image(self):
        writer = MediaEvidenceWriter()

        result = writer.capture("image", "wechat_msg_1", FakeSample())

        self.assertIsNotNone(result)
        self.assertEqual(result.status, "failed")
        self.assertEqual(result.error, "window_hwnd_unavailable")

    def test_media_evidence_ignores_non_screenshot_types(self):
        writer = MediaEvidenceWriter()

        self.assertIsNone(writer.capture("text", "wechat_msg_1", FakeSample()))

    def test_media_evidence_supports_file_and_video_screenshot_fallback(self):
        writer = MediaEvidenceWriter()

        file_result = writer.capture("file", "wechat_msg_2", FakeSample())
        video_result = writer.capture("video", "wechat_msg_3", FakeSample())

        self.assertIsNotNone(file_result)
        self.assertEqual(file_result.status, "failed")
        self.assertEqual(file_result.error, "window_hwnd_unavailable")
        self.assertIsNotNone(video_result)
        self.assertEqual(video_result.status, "failed")
        self.assertEqual(video_result.error, "window_hwnd_unavailable")

    def test_media_evidence_saves_and_reuses_bubble_screenshot(self):
        sample = FakeSample(10, 10, 100, 100)
        sample.window_hwnd = 123
        image = FakeImage()
        writer = MediaEvidenceWriter(root_dir="python/rpa/_media/test")
        with (
            patch("rpa.platforms.wechat.media_evidence.capture_bubble", return_value=image) as capture,
            patch("rpa.platforms.wechat.media_evidence.Path.exists", side_effect=[False, True]),
            patch("rpa.platforms.wechat.media_evidence.Path.mkdir"),
            patch(
                "rpa.platforms.wechat.media_evidence.Path.resolve",
                return_value=Path("D:/evidence/wechat_msg_1.png"),
            ),
        ):
            saved = writer.capture("image", "wechat_msg_1", sample)
            existing = writer.capture("image", "wechat_msg_1", sample)

        self.assertEqual(saved.status, "saved")
        self.assertEqual(saved.path, "D:\\evidence\\wechat_msg_1.png")
        self.assertEqual(existing.status, "existing")
        self.assertEqual(existing.path, saved.path)
        self.assertEqual(len(image.saved), 1)
        capture.assert_called_once_with(sample, hwnd=123)

    def test_trim_media_evidence_removes_avatar_and_row_background(self):
        from PIL import Image, ImageDraw

        image = Image.new("RGB", (700, 176), (30, 30, 31))
        draw = ImageDraw.Draw(image)
        draw.rectangle((40, 30, 75, 65), fill=(220, 220, 220))
        draw.rectangle((90, 20, 221, 152), fill=(80, 190, 210))

        trimmed = trim_media_evidence(image, padding=6)

        self.assertEqual(trimmed.size, (144, 145))

    def test_copy_clipboard_files_to_artifacts_copies_sources(self):
        source = Path("D:/source/order.pdf")
        with (
            patch("rpa.platforms.wechat.media_clipboard.Path.mkdir"),
            patch("rpa.platforms.wechat.media_clipboard.Path.is_file", return_value=True),
            patch("rpa.platforms.wechat.media_clipboard.Path.exists", return_value=False),
            patch(
                "rpa.platforms.wechat.media_clipboard.Path.resolve",
                return_value=Path("D:/artifact/order.pdf"),
            ),
            patch("rpa.platforms.wechat.media_clipboard.shutil.copy2") as copy2,
        ):
            result = copy_clipboard_files_to_artifacts(
                root_dir="D:/artifact",
                content_type="file",
                platform_msg_id="wechat_msg_clipboard",
                source_paths=[source],
            )

        self.assertEqual(result.status, "copied")
        self.assertEqual(result.source_paths, [str(source)])
        self.assertEqual(result.artifact_paths, ["D:\\artifact\\order.pdf"])
        copy2.assert_called_once()

    def test_copy_clipboard_files_to_artifacts_handles_empty_sources(self):
        result = copy_clipboard_files_to_artifacts(
            root_dir="python/rpa/_media/test",
            content_type="file",
            platform_msg_id="wechat_msg_clipboard",
            source_paths=[],
        )

        self.assertEqual(result.status, "empty")

    def test_copy_clipboard_files_to_artifacts_reports_unreadable_sources(self):
        result = copy_clipboard_files_to_artifacts(
            root_dir="python/rpa/_media/test",
            content_type="file",
            platform_msg_id="wechat_msg_clipboard",
            source_paths=[Path("D:/definitely_missing_source.bin")],
        )

        self.assertEqual(result.status, "failed")
        self.assertEqual(result.error, "no_readable_files")

    def test_copy_clipboard_bitmap_to_artifact_saves_png(self):
        from PIL import Image

        image = Image.new("RGB", (1, 1), "white")
        with (
            patch("rpa.platforms.wechat.media_clipboard.Path.mkdir"),
            patch("rpa.platforms.wechat.media_clipboard.Path.exists", return_value=False),
            patch(
                "rpa.platforms.wechat.media_clipboard.Path.resolve",
                return_value=Path("D:/artifact/wechat_msg_bitmap.png"),
            ),
            patch.object(image, "save") as save,
            patch("PIL.ImageGrab.grabclipboard", return_value=image),
        ):
            result = copy_clipboard_bitmap_to_artifact(
                root_dir="D:/artifact",
                content_type="image",
                platform_msg_id="wechat_msg_bitmap",
            )

        self.assertEqual(result.status, "copied")
        self.assertEqual(result.method, "clipboard_bitmap")
        self.assertEqual(result.artifact_paths, ["D:\\artifact\\wechat_msg_bitmap.png"])
        save.assert_called_once()

    def test_copy_clipboard_files_to_artifacts_falls_back_to_bitmap_for_images(self):
        from PIL import Image

        image = Image.new("RGB", (1, 1), "white")
        with (
            patch("rpa.platforms.wechat.media_clipboard.read_clipboard_file_paths", return_value=[]),
            patch("rpa.platforms.wechat.media_clipboard.Path.mkdir"),
            patch("rpa.platforms.wechat.media_clipboard.Path.exists", return_value=False),
            patch(
                "rpa.platforms.wechat.media_clipboard.Path.resolve",
                return_value=Path("D:/artifact/wechat_msg_bitmap.png"),
            ),
            patch.object(image, "save"),
            patch("PIL.ImageGrab.grabclipboard", return_value=image),
        ):
            result = copy_clipboard_files_to_artifacts(
                root_dir="D:/artifact",
                content_type="image",
                platform_msg_id="wechat_msg_bitmap",
            )

        self.assertEqual(result.status, "copied")
        self.assertEqual(result.method, "clipboard_bitmap")

    def test_context_menu_copy_file_copies_clipboard_file(self):
        item = FakeMenuItem()

        result = copy_message_file_via_context_menu(
            FakeSample(10, 10, 100, 100),
            root_dir="D:/artifact",
            content_type="file",
            platform_msg_id="wechat_msg_1",
            click_context_menu=lambda _sample: True,
            find_menu_item=lambda _names: item,
            clipboard_copier=lambda **_kwargs: FakeClipboardResult(
                source_paths=["D:/source/order.pdf"],
                artifact_paths=["D:/artifact/order.pdf"],
            ),
            settle_seconds=0,
        )

        self.assertEqual(result.status, "copied")
        self.assertTrue(item.invoked)
        self.assertEqual(result.menu_name, "复制")
        self.assertEqual(result.artifact_paths, ["D:/artifact/order.pdf"])

    def test_context_menu_copy_file_reports_missing_menu(self):
        result = copy_message_file_via_context_menu(
            FakeSample(10, 10, 100, 100),
            root_dir="D:/artifact",
            content_type="file",
            platform_msg_id="wechat_msg_1",
            click_context_menu=lambda _sample: True,
            find_menu_item=lambda _names: None,
            settle_seconds=0,
        )

        self.assertEqual(result.status, "failed")
        self.assertEqual(result.error, "copy_file_menu_not_found")

    def test_context_menu_click_point_uses_message_direction(self):
        inbound = FakeSample(100, 20, 500, 120)
        inbound.direction = "in"
        outbound = FakeSample(100, 20, 500, 120)
        outbound.direction = "out"
        unknown = FakeSample(100, 20, 500, 120)

        self.assertEqual(_context_menu_click_point(inbound), (200, 70))
        self.assertEqual(_context_menu_click_point(outbound), (400, 70))
        self.assertEqual(_context_menu_click_point(unknown), (300, 70))

    def test_find_copy_file_menu_item_finds_copy(self):
        copy_item = FakeMenuTreeItem("复制")
        copy_file_item = FakeMenuTreeItem("复制文件")
        root = FakeMenuTreeItem(children=[copy_item, copy_file_item])

        with patch("rpa.platforms.wechat.media_context_menu.time.sleep"), patch.dict(
            "sys.modules", {"uiautomation": FakeMenuAutomation(root)}
        ):
            item = _find_copy_file_menu_item(("复制",), timeout_seconds=0.1)

        self.assertIs(item, copy_item)

    def test_default_copy_menu_candidates_exclude_copy_file(self):
        from rpa.platforms.wechat.media_context_menu import COPY_FILE_MENU_NAMES

        self.assertEqual(COPY_FILE_MENU_NAMES, ("复制", "Copy"))

    def test_iter_candidate_menu_items_scopes_to_wechat_menu(self):
        browser_copy = FakeMenuTreeItem(
            "复制",
            class_name="ContextMenuButton",
            control_type="ButtonControl",
        )
        wechat_copy = FakeMenuTreeItem(
            "复制",
            class_name="mmui::XMenuView",
            automation_id="XMenuItem",
            control_type="MenuItemControl",
        )
        wechat_save_as = FakeMenuTreeItem(
            "另存为...",
            class_name="mmui::XMenuView",
            automation_id="XMenuItem",
            control_type="MenuItemControl",
        )
        wechat_menu = FakeMenuTreeItem(
            "Weixin",
            children=[wechat_copy, wechat_save_as],
            class_name="mmui::XMenu",
            control_type="WindowControl",
        )
        root = FakeMenuTreeItem(children=[browser_copy, wechat_menu])

        items = _iter_candidate_menu_items(FakeMenuAutomation(root))

        self.assertEqual(items, [wechat_copy, wechat_save_as])

    def test_click_menu_item_with_mouse_clicks_center(self):
        item = FakeMenuTreeItem(
            "复制",
            class_name="mmui::XMenuView",
            automation_id="XMenuItem",
            control_type="MenuItemControl",
        )
        item.left = 100
        item.top = 20
        item.right = 180
        item.bottom = 60
        calls = []

        class FakeWin32Api:
            @staticmethod
            def SetCursorPos(pos):
                calls.append(("pos", pos))

            @staticmethod
            def mouse_event(flag, _dx, _dy, _data, _extra):
                calls.append(("mouse", flag))

        class FakeWin32Con:
            MOUSEEVENTF_LEFTDOWN = 2
            MOUSEEVENTF_LEFTUP = 4

        with patch.dict("sys.modules", {"win32api": FakeWin32Api, "win32con": FakeWin32Con}):
            self.assertTrue(_click_menu_item_with_mouse(item))

        self.assertEqual(calls, [("pos", (136, 40)), ("mouse", 2), ("mouse", 4)])

    def test_menu_item_click_point_uses_left_text_area(self):
        self.assertEqual(_menu_item_click_point((2148, 730, 2300, 762)), (2201, 746))
        self.assertEqual(_menu_item_click_point((100, 20, 180, 60)), (136, 40))

    def test_click_strategy_returns_result_object(self):
        result = click_session_item_detailed(FakeControl(FakeRect(0, 0, 10, 10)), allow_foreground=False)
        self.assertIsInstance(result.ok, bool)
        self.assertIsInstance(result.method, str)
        self.assertTrue(result.method)


if __name__ == "__main__":
    unittest.main()
