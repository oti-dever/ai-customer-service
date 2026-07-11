import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.pdd_web.image_poc import (
    TINY_RED_PNG_DATA_URL,
    build_fixture_html,
    latest_customer_image_candidate,
    save_data_url,
)


class PddWebImagePocTests(unittest.TestCase):
    def test_save_data_url_writes_png(self):
        temp_root = REPO_ROOT / "logs" / "pdd_web_image_poc_tests"
        temp_root.mkdir(parents=True, exist_ok=True)
        saved = save_data_url(TINY_RED_PNG_DATA_URL, temp_root, prefix="sample")

        path = Path(saved["path"])
        self.assertTrue(path.exists())
        self.assertEqual(path.suffix, ".png")
        self.assertEqual(saved["mime_type"], "image/png")
        self.assertGreater(saved["bytes"], 0)

    def test_latest_customer_image_prefers_last_customer_candidate(self):
        selected = latest_customer_image_candidate(
            [
                {"sender_role": "customer", "candidate_id": "old"},
                {"sender_role": "agent", "candidate_id": "agent"},
                {"sender_role": "customer", "candidate_id": "latest"},
            ]
        )

        self.assertEqual(selected["candidate_id"], "latest")

    def test_fixture_contains_pdd_message_shape(self):
        html = build_fixture_html("data")

        self.assertIn("middlePanel_List_customer_2", html)
        self.assertIn("image-msg", html)
        self.assertIn("data:image/png;base64", html)


if __name__ == "__main__":
    unittest.main()
