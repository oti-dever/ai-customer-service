import sys
import sqlite3
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service import server
from service import rpa_bridge
from service import cache_snapshot
from service import truth_store as truth_store_module
from service.truth_store import PythonServiceTruthStore
from rpa.platforms.pdd_web.adapter import PddWebSidecarAdapter


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
    def test_knowledge_base_management_routes_delegate_to_store(self):
        class FakeKnowledgeStore:
            def __init__(self):
                self.calls = []

            def create_base(self, **kwargs):
                self.calls.append(("create_base", kwargs))
                return {"id": "kb-1", **kwargs}

            def update_base(self, base_id, **kwargs):
                self.calls.append(("update_base", base_id, kwargs))
                return {"id": base_id, **kwargs}

            def set_base_enabled(self, base_id, enabled):
                self.calls.append(("set_base_enabled", base_id, enabled))
                return {"id": base_id, "enabled": enabled}

        fake = FakeKnowledgeStore()
        old_get_store = server.get_knowledge_store
        server.get_knowledge_store = lambda: fake
        try:
            create_handler = make_handler(
                "/api/knowledge/bases",
                {
                    "name": "官方旗舰店产品知识库",
                    "source_directory": "D:/kb",
                    "applicable_shops": "官方旗舰店",
                    "enabled": True,
                },
            )
            create_handler.do_POST()

            update_handler = make_handler(
                "/api/knowledge/bases/update",
                {
                    "id": "kb-1",
                    "name": "官方旗舰店产品知识库 V2",
                    "source_directory": "D:/kb2",
                    "applicable_shops": "官方旗舰店、私域",
                    "enabled": False,
                },
            )
            update_handler.do_POST()

            enabled_handler = make_handler(
                "/api/knowledge/bases/set_enabled",
                {"id": "kb-1", "enabled": True},
            )
            enabled_handler.do_POST()
        finally:
            server.get_knowledge_store = old_get_store

        self.assertEqual(create_handler.sent[0][1], 200)
        self.assertEqual(update_handler.sent[0][1], 200)
        self.assertEqual(enabled_handler.sent[0][1], 200)
        self.assertEqual(fake.calls[0][0], "create_base")
        self.assertEqual(fake.calls[0][1]["source_directory"], "D:/kb")
        self.assertEqual(fake.calls[0][1]["applicable_shops"], "官方旗舰店")
        self.assertEqual(fake.calls[1][0], "update_base")
        self.assertEqual(fake.calls[1][1], "kb-1")
        self.assertFalse(fake.calls[1][2]["enabled"])
        self.assertEqual(fake.calls[2], ("set_base_enabled", "kb-1", True))

    def test_knowledge_platform_binding_routes_delegate_to_store(self):
        class FakeKnowledgeStore:
            def __init__(self):
                self.calls = []

            def get_platform_bindings(self, platform):
                self.calls.append(("get_platform_bindings", platform))
                return ["kb-a"]

            def set_platform_bindings(self, platform, base_ids):
                self.calls.append(("set_platform_bindings", platform, base_ids))
                return base_ids

        fake = FakeKnowledgeStore()
        old_get_store = server.get_knowledge_store
        server.get_knowledge_store = lambda: fake
        try:
            get_handler = make_handler("/api/knowledge/platform_bindings?platform=Qianniu")
            get_handler.do_GET()

            post_handler = make_handler(
                "/api/knowledge/platform_bindings",
                {"platform": "Qianniu", "base_ids": ["kb-a", "kb-b"]},
            )
            post_handler.do_POST()
        finally:
            server.get_knowledge_store = old_get_store

        self.assertEqual(get_handler.sent[0][1], 200)
        self.assertEqual(get_handler.sent[0][0]["platform"], "qianniu")
        self.assertEqual(get_handler.sent[0][0]["base_ids"], ["kb-a"])
        self.assertEqual(post_handler.sent[0][1], 200)
        self.assertEqual(post_handler.sent[0][0]["base_ids"], ["kb-a", "kb-b"])
        self.assertEqual(fake.calls[0], ("get_platform_bindings", "qianniu"))
        self.assertEqual(fake.calls[1], ("set_platform_bindings", "qianniu", ["kb-a", "kb-b"]))

    def test_knowledge_search_route_delegates_to_store(self):
        class FakeKnowledgeStore:
            def search(self, **kwargs):
                return {
                    "status": "success",
                    "results": [{"snippet": "ok", "query": kwargs["query"], "mode": kwargs["mode"]}],
                    "metadata": {"mode": kwargs["mode"]},
                }

        old_get_store = server.get_knowledge_store
        server.get_knowledge_store = lambda: FakeKnowledgeStore()
        try:
            handler = make_handler(
                "/api/knowledge/search",
                {"query": "键盘可以退吗", "top_k": 3},
            )
            handler.do_POST()
        finally:
            server.get_knowledge_store = old_get_store

        self.assertEqual(handler.sent[0][1], 200)
        self.assertEqual(handler.sent[0][0]["status"], "success")
        self.assertEqual(handler.sent[0][0]["results"][0]["query"], "键盘可以退吗")
        self.assertEqual(handler.sent[0][0]["results"][0]["mode"], "hybrid")

    def test_knowledge_image_search_route_delegates_to_store(self):
        class FakeKnowledgeStore:
            def search_image_assets(self, **kwargs):
                return {
                    "status": "success",
                    "results": [
                        {
                            "asset_id": "img-1",
                            "query": kwargs["query"],
                            "include_risky": kwargs["include_risky"],
                        }
                    ],
                    "metadata": {"mode": kwargs["mode"]},
                }

        old_get_store = server.get_knowledge_store
        server.get_knowledge_store = lambda: FakeKnowledgeStore()
        try:
            handler = make_handler(
                "/api/knowledge/images/search",
                {"query": "发空格键实拍图", "top_k": 2, "include_risky": True},
            )
            handler.do_POST()
        finally:
            server.get_knowledge_store = old_get_store

        self.assertEqual(handler.sent[0][1], 200)
        self.assertEqual(handler.sent[0][0]["status"], "success")
        self.assertEqual(handler.sent[0][0]["results"][0]["query"], "发空格键实拍图")
        self.assertTrue(handler.sent[0][0]["results"][0]["include_risky"])

    def test_knowledge_images_get_route_delegates_to_store(self):
        class FakeKnowledgeStore:
            def list_image_assets(self, base_id=None):
                return [{"id": "img-1", "base_id": base_id}]

        old_get_store = server.get_knowledge_store
        server.get_knowledge_store = lambda: FakeKnowledgeStore()
        try:
            handler = make_handler("/api/knowledge/images?base_id=kb-1")
            handler.do_GET()
        finally:
            server.get_knowledge_store = old_get_store

        self.assertEqual(handler.sent[0][1], 200)
        self.assertEqual(handler.sent[0][0]["status"], "success")
        self.assertEqual(handler.sent[0][0]["images"][0]["base_id"], "kb-1")

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

    def test_pdd_web_page_events_are_visible_through_main_conversation_routes(self):
        class SharedConnection(sqlite3.Connection):
            def close(self):
                pass

            def real_close(self):
                super().close()

        shared_conn = sqlite3.connect(":memory:", factory=SharedConnection)
        old_truth_open_db = truth_store_module.open_db
        old_cache_open_db = cache_snapshot.open_db
        truth_store_module.open_db = lambda _path=None: shared_conn
        cache_snapshot.open_db = lambda _path=None: shared_conn
        try:
            truth_store = PythonServiceTruthStore(Path(":memory:"))
            truth_store.ensure_schema()
            store = rpa_bridge.RpaEventStore(truth_store=truth_store)
            adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
            adapter.command({"request_id": "pdd-connect", "platform": "pdd_web", "command": "connect"})
            adapter.handle_page_agent_message({"type": "page_ready"})
            adapter.handle_page_agent_message(
                {
                    "type": "conversation_snapshot",
                    "conversations": [{"display_name": "buyer-main", "unread_count": 2}],
                }
            )
            adapter.handle_page_agent_message(
                {
                    "type": "message_snapshot",
                    "source": "unread_switch",
                    "display_name": "buyer-main",
                    "messages": [
                        {
                            "platform_msg_id": "pdd-msg-1",
                            "sender_role": "customer",
                            "content_type": "text",
                            "content": "hello from pdd",
                            "time_text": "10:36",
                        }
                    ],
                }
            )

            list_handler = make_handler("/api/conversations/list?platform=pdd_web&conversation_limit=10")
            list_handler.do_GET()
            list_payload = list_handler.sent[0][0]
            conversation_key = "pdd_web:local_pdd_web:buyer-main"
            self.assertEqual(list_payload["status"], "success")
            self.assertEqual(list_payload["conversation_count"], 1)
            self.assertEqual(list_payload["conversations"][0]["platform_conversation_id"], conversation_key)
            self.assertEqual(list_payload["conversations"][0]["unread_count"], 2)

            messages_handler = make_handler(
                f"/api/conversations/messages?platform=pdd_web&conversation_key={conversation_key}"
            )
            messages_handler.do_GET()
            messages_payload = messages_handler.sent[0][0]
            self.assertEqual(messages_payload["message_count"], 1)
            self.assertEqual(messages_payload["messages"][0]["platform_message_id"], "pdd-msg-1")
            self.assertEqual(messages_payload["messages"][0]["content"], "hello from pdd")
            self.assertEqual(messages_payload["messages"][0]["direction"], "in")

            snapshot_handler = make_handler("/api/cache/snapshot?platform=pdd_web")
            snapshot_handler.do_GET()
            snapshot_payload = snapshot_handler.sent[0][0]
            self.assertEqual(snapshot_payload["conversation_count"], 1)
            self.assertEqual(snapshot_payload["message_count"], 1)
        finally:
            truth_store_module.open_db = old_truth_open_db
            cache_snapshot.open_db = old_cache_open_db
            shared_conn.real_close()


if __name__ == "__main__":
    unittest.main()
