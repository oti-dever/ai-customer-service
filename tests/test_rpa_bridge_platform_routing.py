import sys
import threading
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service import rpa_bridge


class FakeAdapter:
    def __init__(self, platform):
        self.platform = platform
        self.commands = []

    def command(self, payload):
        self.commands.append(payload)
        return {"status": "success", "platform": self.platform}

    def health(self):
        return {"status": "success", "platform": self.platform}


class RpaBridgePlatformRoutingTests(unittest.TestCase):
    def _make_bridge(self, mode="debug"):
        bridge = object.__new__(rpa_bridge.RpaBridge)
        bridge._command_lock = threading.Lock()
        bridge._mode = mode
        bridge._wechat = FakeAdapter("wechat")
        bridge._qianniu = FakeAdapter("qianniu")
        bridge._adapters = {
            "wechat": bridge._wechat,
            "qianniu": bridge._qianniu,
        }
        return bridge

    def test_command_routes_to_qianniu_adapter(self):
        bridge = self._make_bridge()
        response = rpa_bridge.RpaBridge.command(
            bridge,
            {
                "request_id": "1",
                "platform": "qianniu",
                "command": "health_check",
            },
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["platform"], "qianniu")
        self.assertEqual(len(bridge._qianniu.commands), 1)

    def test_formal_mode_blocks_send_for_all_platforms(self):
        bridge = self._make_bridge(mode="formal")
        response = rpa_bridge.RpaBridge.command(
            bridge,
            {
                "request_id": "2",
                "platform": "qianniu",
                "command": "send_message",
            },
        )

        self.assertEqual(response["status"], "error")
        self.assertEqual(response["error"], "send_disabled_in_formal_mode")
        self.assertEqual(len(bridge._qianniu.commands), 0)

    def test_health_routes_to_qianniu_adapter(self):
        bridge = self._make_bridge()
        response = rpa_bridge.RpaBridge.health(bridge, "qianniu")

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["platform"], "qianniu")
        self.assertEqual(response["mode"], "debug")


if __name__ == "__main__":
    unittest.main()
