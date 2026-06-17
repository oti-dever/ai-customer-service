import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service import server
from service import rpa_bridge


class FakeBridge:
    def __init__(self):
        self.calls = []

    def health(self, platform):
        self.calls.append(("health", platform))
        return {"status": "success", "platform": platform}

    def events(self, platform, cursor, limit):
        self.calls.append(("events", platform, cursor, limit))
        return {"status": "success", "events": []}

    def replay(self, platform, cursor, limit):
        self.calls.append(("replay", platform, cursor, limit))
        return {"status": "success", "events": []}

    def platforms(self):
        self.calls.append(("platforms",))
        return {
            "status": "success",
            "platforms": [
                {"platform": "wechat", "display_name": "微信", "registered": True, "listening": False}
            ],
        }

    def command(self, payload):
        self.calls.append(("command", payload))
        return {"status": "success", "result": {}}

    def clear_conversation_messages(self, payload):
        self.calls.append(("clear_conversation_messages", payload))
        return {"status": "success", "result": {"mutation_type": "clear_messages"}}

    def delete_conversation(self, payload):
        self.calls.append(("delete_conversation", payload))
        return {"status": "success", "result": {"mutation_type": "delete_conversation"}}


def make_handler(path, payload=None):
    handler = object.__new__(server.AiServiceHandler)
    handler.path = path
    handler.sent = []
    handler._send_json = lambda body, status_code=200: handler.sent.append((body, status_code))
    handler._read_json_body = lambda: payload or {}
    return handler


class ServiceServerPlatformRouteTests(unittest.TestCase):
    def test_platform_get_routes_delegate_to_bridge(self):
        fake = FakeBridge()
        old_get_bridge = server.rpa_bridge.get_bridge
        server.rpa_bridge.get_bridge = lambda: fake
        try:
            make_handler("/api/platform/health?platform=qianniu").do_GET()
            make_handler("/api/platform/events?platform=wechat&cursor=2&limit=3").do_GET()
            make_handler("/api/platform/replay?platform=wechat&cursor=5&limit=7").do_GET()
        finally:
            server.rpa_bridge.get_bridge = old_get_bridge

        self.assertEqual(fake.calls[0], ("health", "qianniu"))
        self.assertEqual(fake.calls[1], ("events", "wechat", "2", 3))
        self.assertEqual(fake.calls[2], ("replay", "wechat", "5", 7))

    def test_platforms_route_returns_registered_platforms(self):
        fake = FakeBridge()
        old_get_bridge = server.rpa_bridge.get_bridge
        server.rpa_bridge.get_bridge = lambda: fake
        try:
            handler = make_handler("/api/platforms")
            handler.do_GET()
        finally:
            server.rpa_bridge.get_bridge = old_get_bridge

        self.assertEqual(fake.calls[0], ("platforms",))
        self.assertEqual(handler.sent[0][0]["platforms"][0]["platform"], "wechat")

    def test_platform_command_route_delegates_to_bridge(self):
        fake = FakeBridge()
        old_get_bridge = server.rpa_bridge.get_bridge
        server.rpa_bridge.get_bridge = lambda: fake
        try:
            make_handler(
                "/api/platform/command",
                {"platform": "wechat", "command": "health_check"},
            ).do_POST()
        finally:
            server.rpa_bridge.get_bridge = old_get_bridge

        self.assertEqual(fake.calls[0][0], "command")
        self.assertEqual(fake.calls[0][1]["platform"], "wechat")

    def test_conversation_mutation_routes_delegate_to_bridge(self):
        fake = FakeBridge()
        old_get_bridge = server.rpa_bridge.get_bridge
        server.rpa_bridge.get_bridge = lambda: fake
        try:
            make_handler(
                "/api/conversations/clear_messages",
                {"platform": "qianniu", "conversation_key": "qianniu:buyer-1"},
            ).do_POST()
            make_handler(
                "/api/conversations/delete",
                {"platform": "qianniu", "conversation_key": "qianniu:buyer-1"},
            ).do_POST()
        finally:
            server.rpa_bridge.get_bridge = old_get_bridge

        self.assertEqual(fake.calls[0][0], "clear_conversation_messages")
        self.assertEqual(fake.calls[0][1]["conversation_key"], "qianniu:buyer-1")
        self.assertEqual(fake.calls[1][0], "delete_conversation")
        self.assertEqual(fake.calls[1][1]["platform"], "qianniu")

    def test_bridge_rejects_mutation_while_observer_active(self):
        bridge = object.__new__(rpa_bridge.RpaBridge)
        adapter = type("Adapter", (), {})()
        adapter._connected = True
        adapter._observer_thread = None
        bridge._adapters = {"qianniu": adapter}
        bridge._command_lock = rpa_bridge.threading.Lock()
        bridge.store = type("Store", (), {"seconds_since_observed_event": lambda self, platform: None})()
        bridge._truth_store = type(
            "Truth",
            (),
            {
                "clear_conversation_messages": lambda self, *args, **kwargs: {
                    "status": "success",
                    "event": {},
                }
            },
        )()

        response = bridge.clear_conversation_messages(
            {"platform": "qianniu", "conversation_key": "qianniu:buyer-1"}
        )

        self.assertEqual(response["status"], "error")
        self.assertEqual(response["error"], "observer_active")

    def test_bridge_rejects_mutation_after_recent_observation(self):
        bridge = object.__new__(rpa_bridge.RpaBridge)
        adapter = type("Adapter", (), {})()
        adapter._connected = False
        adapter._observer_thread = None
        bridge._adapters = {"qianniu": adapter}
        bridge._command_lock = rpa_bridge.threading.Lock()
        bridge.store = type("Store", (), {"seconds_since_observed_event": lambda self, platform: 0.5})()
        bridge._truth_store = type(
            "Truth",
            (),
            {
                "clear_conversation_messages": lambda self, *args, **kwargs: {
                    "status": "success",
                    "event": {},
                }
            },
        )()

        response = bridge.clear_conversation_messages(
            {"platform": "qianniu", "conversation_key": "qianniu:buyer-1"}
        )

        self.assertEqual(response["status"], "error")
        self.assertEqual(response["error"], "recent_observation")


if __name__ == "__main__":
    unittest.main()
