import base64
import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "deploy/seamless_lab/lab_server.py"
SPEC = importlib.util.spec_from_file_location("lab_server", SCRIPT)
lab_server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lab_server)
REGION_SCRIPT = SCRIPT.with_name("lab_region_info.py")
REGION_SPEC = importlib.util.spec_from_file_location("lab_region_info", REGION_SCRIPT)
lab_region_info = importlib.util.module_from_spec(REGION_SPEC)
REGION_SPEC.loader.exec_module(lab_region_info)


class SeamlessLabServerTests(unittest.TestCase):
    def test_template_rewrites_only_four_shadnet_routes_and_restart_is_safe(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            template = Path(directory) / "ss_info_template.xml"
            template.write_text("http://{YOUR SHADNET SERVER}:31315\n" * 4 +
                                "http://127.0.0.1:20443\n", encoding="utf-8")
            lab_server.configure(template, "192.168.31.226")
            lab_server.configure(template, "192.168.31.226")
            content = template.read_text(encoding="utf-8")
            self.assertEqual(content.count("http://192.168.31.226:32315"), 4)
            self.assertIn("http://127.0.0.1:20443", content)

    def test_template_rejects_unexpected_source_or_invalid_address(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            template = Path(directory) / "ss_info_template.xml"
            template.write_text("unexpected", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                lab_server.configure(template, "192.168.31.226")
            with self.assertRaises(ValueError):
                lab_server.configure(template, "not-an-ip")

    def test_game_server_region_patch_is_idempotent_and_pinned(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            server = Path(directory) / "bbsc_gameserver.py"
            server.write_text('import bbhost_service\n'
                              '            self._send(info, "text/plain; charset=utf-8")\n',
                              encoding="utf-8")
            lab_server.patch_region_response(server)
            first = server.read_text(encoding="utf-8")
            lab_server.patch_region_response(server)
            self.assertEqual(server.read_text(encoding="utf-8"), first)
            self.assertIn("adapt_region_ss_info(path, info)", first)
            server.write_text("upstream changed", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                lab_server.patch_region_response(server)

    def test_region_specific_status_fields_and_languages(self):
        xml = ("<ss>0</ss>\n<msg2><lang3></lang3><lang19></lang19></msg2>\n"
               "<BetaTestWebUrl2><lang3>https://example.invalid/</lang3>"
               "<lang19>https://example.invalid/</lang19></BetaTestWebUrl2>\n"
               "<gameurl2><api_Login>http://192.168.31.226:32443</api_Login></gameurl2>\n"
               "<ReloadServerStatusInfoInterval2>300</ReloadServerStatusInfoInterval2>\n"
               "<Playlog_Test2>0</Playlog_Test2>")
        encoded = base64.b64encode(xml.encode("utf-8"))
        self.assertEqual(lab_region_info.adapt_region_ss_info("/bb-eu/ss.info", encoded), encoded)
        self.assertEqual(lab_region_info.adapt_region_ss_info("/unknown/ss.info", encoded), encoded)
        for region, number, languages in (("bb-us", "1", (2, 3, 18, 21)),
                                           ("bb-asia", "4", (10, 11, 12, 19))):
            result = base64.b64decode(lab_region_info.adapt_region_ss_info(
                f"/{region}/ss.info", encoded)).decode("utf-8")
            self.assertIn(f"<gameurl{number}>", result)
            self.assertIn(f"<ReloadServerStatusInfoInterval{number}>300", result)
            self.assertIn(f"<Playlog_Test{number}>0", result)
            self.assertNotIn("<gameurl2>", result)
            self.assertEqual(result.count("<lang"), 2 * len(languages))
            for language in languages:
                self.assertIn(f"<lang{language}>", result)

    def test_region_adapter_rejects_changed_template(self):
        encoded = base64.b64encode(b"<ss>0</ss>")
        with self.assertRaises(ValueError):
            lab_region_info.adapt_region_ss_info("/bb-asia/ss.info", encoded)


if __name__ == "__main__":
    unittest.main()
