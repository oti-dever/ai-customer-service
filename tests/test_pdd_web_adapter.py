import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from rpa.platforms.pdd_web.adapter import PddWebSidecarAdapter
from rpa.platforms.pdd_web.image_poc import TINY_RED_PNG_DATA_URL
from rpa.platforms.pdd_web.parser import event_from_page_message


class Store:
    def __init__(self):
        self.events = []

    def append(self, event):
        self.events.append(event)
        return len(self.events)


class PddWebAdapterTests(unittest.TestCase):
    def test_connect_reports_degraded_when_page_agent_offline(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)

        response = adapter.command(
            {
                "request_id": "pdd-1",
                "platform": "pdd_web",
                "command": "connect",
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertTrue(response["result"]["connected"])
        self.assertEqual(response["result"]["health"]["reason"], "page_agent_offline")
        self.assertEqual(store.events[-1]["event_type"], "account_health_changed")
        self.assertEqual(store.events[-1]["platform"], "pdd_web")

    def test_fetch_messages_returns_explicit_page_agent_offline(self):
        adapter = PddWebSidecarAdapter(Store(), start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})

        response = adapter.command(
            {
                "request_id": "pdd-2",
                "platform": "pdd_web",
                "command": "fetch_visible_messages",
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertEqual(response["result"]["error"], "page_agent_offline")
        self.assertFalse(response["result"]["page_agent_connected"])

    def test_send_message_returns_page_agent_offline_when_not_connected(self):
        adapter = PddWebSidecarAdapter(Store(), start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})

        response = adapter.command(
            {
                "request_id": "pdd-3",
                "platform": "pdd_web",
                "command": "send_message",
                "parameters": {"text": "hello", "confirm_token": "manual_confirmed_by_agent"},
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertFalse(response["result"]["sent"])
        self.assertEqual(response["result"]["error"], "page_agent_offline")

    def test_page_text_message_converts_to_unified_event(self):
        event = event_from_page_message(
            {
                "display_name": "buyer-1",
                "sender_role": "customer",
                "content_type": "msg",
                "content": "多久发货",
                "time_text": "12:30",
            },
            account_id="local_pdd_web",
        )

        self.assertEqual(event["event_type"], "message_observed")
        self.assertEqual(event["platform"], "pdd_web")
        self.assertEqual(event["conversation_key"], "pdd_web:local_pdd_web:buyer-1")
        self.assertEqual(event["payload"]["content_type"], "text")
        self.assertEqual(event["payload"]["direction"], "inbound")
        self.assertEqual(event["payload"]["sender_role"], "customer")

    def test_page_agent_snapshots_emit_events(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})

        adapter.handle_page_agent_message({"type": "page_ready", "page_url": "https://mms.pinduoduo.com/"})
        adapter.handle_page_agent_message({"type": "shop_info", "shop_name": "PDD Shop", "kefu_name": "agent-1"})
        conv_ack = adapter.handle_page_agent_message(
            {
                "type": "conversation_snapshot",
                "conversations": [{"display_name": "buyer-1", "unread_count": 1}],
            }
        )
        msg_ack = adapter.handle_page_agent_message(
            {
                "type": "message_snapshot",
                "source": "unread_switch",
                "display_name": "buyer-1",
                "messages": [{"sender_role": "customer", "content_type": "text", "content": "hello"}],
            }
        )

        self.assertEqual(conv_ack["count"], 1)
        self.assertEqual(msg_ack["count"], 1)
        self.assertTrue(any(event["event_type"] == "conversation_observed" for event in store.events))
        self.assertTrue(any(event["event_type"] == "message_observed" for event in store.events))
        self.assertEqual(adapter.health()["health"]["reason"], "page_agent_online")

    def test_page_agent_image_snapshot_saves_media_path(self):
        media_dir = REPO_ROOT / "logs" / "pdd_web_adapter_media_tests"
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False, media_dir=media_dir)
        adapter.command({"request_id": "pdd-1", "command": "connect"})
        adapter.handle_page_agent_message({"type": "page_ready"})

        ack = adapter.handle_page_agent_message(
            {
                "type": "message_snapshot",
                "source": "unread_switch",
                "display_name": "buyer-1",
                "messages": [
                    {
                        "platform_msg_id": "pdd-img-1",
                        "sender_role": "customer",
                        "content_type": "image",
                        "content": "[image]",
                        "asset_url": "blob:null/demo",
                        "asset_data_url": TINY_RED_PNG_DATA_URL,
                    }
                ],
            }
        )

        self.assertEqual(ack["count"], 1)
        event = store.events[-1]
        payload = event["payload"]
        image_path = Path(payload["content_image_path"])
        self.assertTrue(image_path.exists())
        self.assertEqual(payload["evidence_ref"], str(image_path))
        self.assertNotIn("asset_data_url", payload["raw"])
        self.assertEqual(payload["raw"]["asset_capture_method"], "fetch_data_url")

    def test_page_agent_snapshots_are_ignored_until_adapter_connects(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)

        adapter.handle_page_agent_message({"type": "page_ready", "page_url": "https://mms.pinduoduo.com/"})
        conv_ack = adapter.handle_page_agent_message(
            {
                "type": "conversation_snapshot",
                "conversations": [{"display_name": "buyer-1", "unread_count": 1}],
            }
        )
        msg_ack = adapter.handle_page_agent_message(
            {
                "type": "message_snapshot",
                "source": "unread_switch",
                "display_name": "buyer-1",
                "messages": [{"sender_role": "customer", "content_type": "text", "content": "hello"}],
            }
        )

        self.assertEqual(conv_ack["status"], "ignored")
        self.assertEqual(msg_ack["status"], "ignored")
        self.assertFalse(any(event["event_type"] == "conversation_observed" for event in store.events))
        self.assertFalse(any(event["event_type"] == "message_observed" for event in store.events))

        adapter.command({"request_id": "pdd-1", "command": "connect"})
        conv_ack = adapter.handle_page_agent_message(
            {
                "type": "conversation_snapshot",
                "conversations": [{"display_name": "buyer-1", "unread_count": 1}],
            }
        )

        self.assertEqual(conv_ack["status"], "success")
        self.assertTrue(any(event["event_type"] == "conversation_observed" for event in store.events))

    def test_message_snapshots_without_unread_switch_source_are_ignored(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})
        adapter.handle_page_agent_message({"type": "page_ready"})

        ack = adapter.handle_page_agent_message(
            {
                "type": "message_snapshot",
                "display_name": "buyer-1",
                "messages": [{"sender_role": "customer", "content_type": "text", "content": "hello"}],
            }
        )

        self.assertEqual(ack["status"], "ignored")
        self.assertEqual(ack["reason"], "message_snapshot_not_from_unread_switch")
        self.assertFalse(any(event["event_type"] == "message_observed" for event in store.events))

    def test_connect_configures_page_agent_observation_mode(self):
        adapter = PddWebSidecarAdapter(Store(), start_page_agent_server=False)

        class FakeServer:
            def __init__(self):
                self.payloads = []

            def send_json(self, payload):
                self.payloads.append(payload)
                return True

        fake_server = FakeServer()
        adapter._agent_server = fake_server

        adapter.command({"request_id": "pdd-1", "command": "connect"})
        adapter.handle_page_agent_message({"type": "page_ready"})
        adapter.command({"request_id": "pdd-2", "command": "disconnect"})

        self.assertEqual(fake_server.payloads[0]["type"], "configure_observation")
        self.assertTrue(fake_server.payloads[0]["enabled"])
        self.assertEqual(fake_server.payloads[0]["mode"], "unread_then_messages")
        self.assertTrue(fake_server.payloads[1]["enabled"])
        self.assertFalse(fake_server.payloads[2]["enabled"])

    def test_prepare_reply_draft_waits_for_page_agent_result(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})
        adapter.handle_page_agent_message({"type": "page_ready"})

        class FakeServer:
            def __init__(self, owner):
                self.owner = owner
                self.last_payload = None

            def send_json(self, payload):
                self.last_payload = payload
                self.owner.handle_page_agent_message(
                    {
                        "type": "draft_result",
                        "request_id": payload["request_id"],
                        "prepared": True,
                        "status": "success",
                        "display_name": payload["display_name"],
                    }
                )
                return True

        adapter._agent_server = FakeServer(adapter)
        response = adapter.command(
            {
                "request_id": "pdd-2",
                "command": "prepare_reply_draft",
                "parameters": {
                    "conversation_key": "pdd_web:local_pdd_web:buyer-1",
                    "display_name": "buyer-1",
                    "text": "reply draft",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertTrue(response["result"]["prepared"])
        self.assertEqual(adapter._agent_server.last_payload["switch_unread_method"], "click")
        self.assertFalse(adapter._agent_server.last_payload["allow_send_click"])
        self.assertFalse(adapter._agent_server.last_payload["allow_send_enter"])

    def test_prepare_reply_draft_can_request_enter_send(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect"})
        adapter.handle_page_agent_message({"type": "page_ready"})

        class FakeServer:
            def __init__(self, owner):
                self.owner = owner
                self.last_payload = None

            def send_json(self, payload):
                self.last_payload = payload
                self.owner.handle_page_agent_message(
                    {
                        "type": "draft_result",
                        "request_id": payload["request_id"],
                        "prepared": True,
                        "sent": payload["allow_send_enter"],
                        "status": "success",
                        "display_name": payload["display_name"],
                    }
                )
                return True

        adapter._agent_server = FakeServer(adapter)
        response = adapter.command(
            {
                "request_id": "pdd-2",
                "command": "prepare_reply_draft",
                "parameters": {
                    "conversation_key": "pdd_web:local_pdd_web:buyer-1",
                    "display_name": "buyer-1",
                    "text": "reply draft",
                    "allow_send_enter": True,
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertTrue(response["result"]["prepared"])
        self.assertTrue(response["result"]["sent"])
        self.assertTrue(adapter._agent_server.last_payload["allow_send_enter"])

    def test_send_message_emits_message_sent_after_enter_send(self):
        store = Store()
        adapter = PddWebSidecarAdapter(store, start_page_agent_server=False)
        adapter.command({"request_id": "pdd-1", "command": "connect", "account_id": "acct-1"})
        adapter.handle_page_agent_message({"type": "page_ready"})

        class FakeServer:
            def __init__(self, owner):
                self.owner = owner
                self.last_payload = None

            def send_json(self, payload):
                self.last_payload = payload
                self.owner.handle_page_agent_message(
                    {
                        "type": "draft_result",
                        "request_id": payload["request_id"],
                        "prepared": True,
                        "sent": True,
                        "status": "success",
                        "display_name": payload["display_name"],
                        "conversation_key": payload["conversation_key"],
                    }
                )
                return True

        adapter._agent_server = FakeServer(adapter)
        response = adapter.command(
            {
                "request_id": "pdd-send-1",
                "command": "send_message",
                "account_id": "acct-1",
                "parameters": {
                    "conversation_key": "pdd_web:acct-1:buyer-1",
                    "display_name": "buyer-1",
                    "text": "reply sent",
                    "content_type": "text",
                    "client_message_id": "client-1",
                    "confirm_token": "manual_confirmed_by_agent",
                },
            }
        )

        self.assertEqual(response["status"], "success")
        self.assertTrue(response["result"]["sent"])
        self.assertTrue(adapter._agent_server.last_payload["allow_send_enter"])
        self.assertTrue(any(event["event_type"] == "message_sent" for event in store.events))
        sent_event = next(event for event in store.events if event["event_type"] == "message_sent")
        self.assertEqual(sent_event["conversation_key"], "pdd_web:acct-1:buyer-1")
        self.assertEqual(sent_event["client_message_id"], "client-1")
        self.assertEqual(sent_event["payload"]["content"], "reply sent")


if __name__ == "__main__":
    unittest.main()
