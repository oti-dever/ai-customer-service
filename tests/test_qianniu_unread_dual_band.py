"""千牛未读：左带 + 右带（A+B）像素检测。"""
import sys
import unittest
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.unread_detector import detect_unread_rows_dual_band


def _solid_bgra(w: int, h: int, b: int, g: int, r: int) -> bytes:
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    arr[:, :, 0] = b
    arr[:, :, 1] = g
    arr[:, :, 2] = r
    arr[:, :, 3] = 255
    return arr.tobytes()


class TestQianniuUnreadDualBand(unittest.TestCase):
    def test_dual_band_left_dot_only(self):
        w, h, row = 100, 68, 68
        arr = np.zeros((h, w, 4), dtype=np.uint8)
        arr[:, :, 3] = 255
        arr[4:20, 4:24, 0] = 30
        arr[4:20, 4:24, 1] = 30
        arr[4:20, 4:24, 2] = 250
        bgra = arr.tobytes()
        rows = detect_unread_rows_dual_band(
            bgra,
            w,
            h,
            row_height=row,
            left_red_threshold=10,
            scan_x_start_ratio=0.0,
            scan_x_end_ratio=0.35,
            timer_x_start_ratio=0.75,
            timer_x_end_ratio=1.0,
            timer_red_threshold=50,
            use_red_dominance=False,
        )
        self.assertTrue(rows)
        self.assertEqual(rows[0][0], 0)

    def test_dual_band_right_timer_only(self):
        w, h, row = 120, 68, 68
        arr = np.zeros((h, w, 4), dtype=np.uint8)
        arr[:, :, 3] = 255
        arr[10:40, 85:115, 0] = 40
        arr[10:40, 85:115, 1] = 40
        arr[10:40, 85:115, 2] = 250
        bgra = arr.tobytes()
        rows = detect_unread_rows_dual_band(
            bgra,
            w,
            h,
            row_height=row,
            left_red_threshold=500,
            scan_x_start_ratio=0.0,
            scan_x_end_ratio=0.2,
            timer_x_start_ratio=0.65,
            timer_x_end_ratio=1.0,
            timer_red_threshold=80,
            use_red_dominance=False,
        )
        self.assertTrue(rows)
        self.assertEqual(rows[0][0], 0)

    def test_dual_band_no_signal(self):
        w, h = 80, 68
        bgra = _solid_bgra(w, h, 200, 200, 200)
        rows = detect_unread_rows_dual_band(
            bgra,
            w,
            h,
            row_height=68,
            left_red_threshold=20,
            scan_x_start_ratio=0.0,
            scan_x_end_ratio=0.4,
            timer_x_start_ratio=0.55,
            timer_x_end_ratio=1.0,
            timer_red_threshold=20,
            use_red_dominance=False,
        )
        self.assertEqual(rows, [])


if __name__ == "__main__":
    unittest.main()
