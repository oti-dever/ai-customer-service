import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.common.qianniu_session import ConversationListEntry
from rpa.readers.qianniu_reader import (
    _header_canonical_supersedes_list_row,
    _reconcile_list_entries_with_header_title,
)


class QianniuListHeaderReconcileTests(unittest.TestCase):
    def test_tb_truncated_row_replaced_by_header(self):
        e = ConversationListEntry(name="tb49478945", y_center=28.0, row_index=0, confidence=0.9)
        out = _reconcile_list_entries_with_header_title([e], "tb4947894539")
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0].name, "tb4947894539")
        self.assertEqual(out[0].y_center, 28.0)

    def test_supersedes_only_for_tb_digit_ids(self):
        self.assertTrue(_header_canonical_supersedes_list_row("tb4947894539", "tb49478945"))
        self.assertFalse(_header_canonical_supersedes_list_row("张三丰", "张三"))

    def test_equal_names_unchanged(self):
        e = ConversationListEntry(name="tb810776366", y_center=68.0, row_index=1, confidence=0.88)
        out = _reconcile_list_entries_with_header_title([e], "tb810776366")
        self.assertIs(out[0], e)

    def test_empty_header_no_change(self):
        e = ConversationListEntry(name="tb49478945", y_center=28.0, row_index=0, confidence=0.9)
        out = _reconcile_list_entries_with_header_title([e], "")
        self.assertIs(out[0], e)


if __name__ == "__main__":
    unittest.main()
