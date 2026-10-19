"""Offline tests for portal policy and protocol framing; no Docker required."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import server


class PortalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).parent)
        self.addCleanup(self.temp.cleanup)
        self.portal = server.Portal(Path(self.temp.name))
        self.addCleanup(self.portal.db.close)

    def application(self, npid="Hunter01", listed=True):
        return self.portal.apply({"qq": "123456789", "nickname": "猎人甲",
                                  "npid": npid, "show_in_list": listed}, "100.64.0.1")

    def test_pending_approval_and_one_time_registration(self):
        with patch.object(server, "AUTO_APPROVE", False):
            applied = self.application()
        self.assertEqual(applied["state"], "pending")
        self.assertIsNone(applied["registration_key"])
        with self.assertRaises(PermissionError):
            self.portal.register({"application_id": applied["application_id"],
                                  "application_token": applied["application_token"],
                                  "registration_key": "wrong", "password": "longpassword"})
        self.portal.approve(applied["application_id"])
        approved = self.portal.application(applied["application_id"], applied["application_token"])
        self.assertEqual(approved["state"], "approved")
        with patch.object(server, "register_shadnet", return_value=0) as register, patch.object(
                server, "SHADNET_SECRET", Path(self.temp.name) / "shadnet-secret"):
            (Path(self.temp.name) / "shadnet-secret").write_text("server-only-secret")
            request = {"application_id": applied["application_id"],
                       "application_token": applied["application_token"],
                       "registration_key": approved["registration_key"],
                       "password": "longpassword"}
            self.assertTrue(self.portal.register(request)["ok"])
            register.assert_called_once_with("Hunter01", "longpassword", "server-only-secret")
            with self.assertRaises(PermissionError):
                self.portal.register(request)
        self.assertEqual(self.portal.application(applied["application_id"],
                                                 applied["application_token"])["state"], "registered")

    def test_qq_is_hashed_rate_limited_and_npid_unique(self):
        self.application()
        with self.assertRaises(RuntimeError):
            self.application("Hunter02")
        with self.assertRaises(ValueError):
            self.portal.apply({"qq": "987654321", "nickname": "乙", "npid": "hunter01"}, "100.64.0.2")
        raw = (Path(self.temp.name) / "portal.db").read_bytes()
        self.assertNotIn(b"123456789", raw)
        self.assertEqual(self.portal.db.execute("SELECT qq_last4 FROM applications").fetchone()[0], "6789")

    def test_device_key_requires_registered_account_and_limits_issuance(self):
        with patch.object(server, "AUTO_APPROVE", True):
            applied = self.application()
        request = {"application_id": applied["application_id"],
                   "application_token": applied["application_token"]}
        with patch.object(server, "create_headscale_preauth", return_value="hskey-auth-test") as mint:
            with self.assertRaises(PermissionError):
                self.portal.device_key(request)
            self.portal.db.execute("UPDATE applications SET state='registered' WHERE id=?",
                                   (applied["application_id"],))
            self.portal.db.commit()
            self.portal.db.execute("UPDATE applications SET expires_at=0 WHERE id=?",
                                   (applied["application_id"],))
            self.portal.db.commit()
            self.assertEqual(self.portal.application(applied["application_id"],
                                                     applied["application_token"])["state"], "registered")
            with self.assertRaises(PermissionError):
                self.portal.device_key({"application_id": applied["application_id"],
                                        "application_token": "wrong"})
            for _ in range(server.DEVICE_KEYS_PER_DAY):
                self.assertEqual(self.portal.device_key(request)["auth_key"], "hskey-auth-test")
            with self.assertRaises(RuntimeError):
                self.portal.device_key(request)
            self.assertEqual(mint.call_count, server.DEVICE_KEYS_PER_DAY)

    def test_headscale_request_is_one_use_and_short_lived(self):
        secret = Path(self.temp.name) / "api-key"
        secret.write_text("hskey-api-secret")
        class Response:
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def read(self): return b'{"preAuthKey":{"key":"hskey-auth-once"}}'
        with patch.object(server, "HEADSCALE_API_KEY_FILE", secret), patch.object(
                server, "HEADSCALE_USER_ID", "1"), patch.object(server, "urlopen", return_value=Response()) as urlopen:
            self.assertEqual(server.create_headscale_preauth(), "hskey-auth-once")
            request = urlopen.call_args.args[0]
            payload = json.loads(request.data)
            self.assertEqual(request.get_header("Authorization"), "Bearer hskey-api-secret")
            self.assertFalse(payload["reusable"])
            self.assertFalse(payload["ephemeral"])
            self.assertEqual(payload["user"], "1")
            self.assertIn("expiration", payload)

    def test_status_requires_registration_and_consent(self):
        with patch.object(server, "AUTO_APPROVE", True):
            visible = self.application()
            self.portal.db.execute("UPDATE applications SET state='registered' WHERE id=?",
                                   (visible["application_id"],))
            self.portal.db.commit()
        usage = {"total_users": 3}
        online = {"users": [
            {"npid": "Hunter01", "title_id": "CUSA03173", "title_name": "Bloodborne"},
            {"npid": "Unregistered", "title_name": "Bloodborne"},
            {"npid": "Invisible", "appear_offline": True}]}
        with patch.object(server, "stats", side_effect=[usage, online, usage, online]):
            first = self.portal.snapshot()
            second = self.portal.snapshot()
        self.assertEqual(first["online_players"], 3)
        self.assertEqual(first["listed_players"], 1)
        self.assertEqual(first["users"][0]["nickname"], "猎人甲")
        self.assertEqual(second["users"], first["users"])
        self.assertEqual(self.portal.db.execute(
            "SELECT login_count FROM activity WHERE npid='Hunter01'").fetchone()[0], 1)
        self.assertNotIn("qq", json.dumps(first))

    def test_seamless_presence_requires_registered_application_and_expires(self):
        with patch.object(server, "AUTO_APPROVE", True):
            applied = self.application()
        body = {"application_id": applied["application_id"],
                "application_token": applied["application_token"], "state": "online"}
        with self.assertRaises(PermissionError):
            self.portal.presence(body)
        self.portal.db.execute("UPDATE applications SET state='registered' WHERE id=?",
                               (applied["application_id"],))
        self.portal.db.commit()
        with self.assertRaises(PermissionError):
            self.portal.presence(dict(body, application_token="wrong"))
        self.assertTrue(self.portal.presence(body)["ok"])
        with patch.object(server, "stats", side_effect=[{"total_users": 0}, {"users": []}]):
            result = self.portal.snapshot()
        self.assertEqual(result["online_players"], 1)
        self.assertEqual(result["seamless_online_players"], 1)
        self.assertEqual(result["users"][0]["title_name"], "Bloodborne（无缝）")
        self.assertEqual(result["users"][0]["npid"], "Hunter01")
        self.portal.seamless_presence["hunter01"] = 0
        with patch.object(server, "stats", side_effect=[{"total_users": 0}, {"users": []}]):
            self.assertEqual(self.portal.snapshot()["online_players"], 0)

    def test_seamless_presence_deduplicates_traditional_login_and_respects_opt_out(self):
        with patch.object(server, "AUTO_APPROVE", True):
            applied = self.application(listed=False)
        self.portal.db.execute("UPDATE applications SET state='registered' WHERE id=?",
                               (applied["application_id"],))
        self.portal.db.commit()
        body = {"application_id": applied["application_id"],
                "application_token": applied["application_token"], "state": "online"}
        self.portal.presence(body)
        with patch.object(server, "stats", side_effect=[{"total_users": 1},
                {"users": [{"npid": "Hunter01", "title_name": "Bloodborne"}]}]):
            result = self.portal.snapshot()
        self.assertEqual(result["online_players"], 1)
        self.assertEqual(result["seamless_online_players"], 1)
        self.assertEqual(result["users"], [])
        self.portal.presence(dict(body, state="offline"))
        self.assertFalse(self.portal.seamless_presence)

    def test_same_player_in_both_modes_is_labeled_once(self):
        with patch.object(server, "AUTO_APPROVE", True):
            applied = self.application()
        self.portal.db.execute("UPDATE applications SET state='registered' WHERE id=?",
                               (applied["application_id"],))
        self.portal.db.commit()
        self.portal.presence({"application_id": applied["application_id"],
                              "application_token": applied["application_token"], "state": "online"})
        with patch.object(server, "stats", side_effect=[{"total_users": 1},
                {"users": [{"npid": "Hunter01", "title_name": "Bloodborne"}]}]):
            result = self.portal.snapshot()
        self.assertEqual(result["online_players"], 1)
        self.assertEqual(result["listed_players"], 1)
        self.assertEqual(result["users"][0]["mode"], "both")


if __name__ == "__main__":
    unittest.main()
