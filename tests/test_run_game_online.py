import json
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error
from contextlib import redirect_stdout, redirect_stderr

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import run_game


class OnlineOverridesTests(unittest.TestCase):
    def temporary_directory(self):
        return tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent)

    def test_routes_game_bootstrap_and_summons(self):
        with self.temporary_directory() as directory:
            environment = {
                'BB_COMMUNITY_SERVER': 'https://thehuntersdream.com',
                'BB_SHADNET_WEBAPI': 'http://192.168.1.20:31315',
            }
            response = urllib.error.HTTPError(
                'http://192.168.1.20:31315/summon_messenger/create', 400,
                'expected empty request rejection', {}, None)
            with mock.patch.object(run_game.urllib.request, 'urlopen', side_effect=response):
                run_game.online_overrides(environment, Path(directory))

            path = Path(environment['SHADPS4_HTTP_HOST_OVERRIDES_JSON'])
            routes = json.loads(path.read_text(encoding='utf-8'))
            self.assertEqual(routes[run_game.GAME_SERVER], run_game.COMMUNITY_SERVER)
            for scheme in ('http', 'https'):
                self.assertEqual(
                    routes[f'{scheme}://thehuntersdream.com:18671/summon_messenger'],
                    'http://192.168.1.20:31315')

    def test_custom_file_is_preserved(self):
        with self.temporary_directory() as directory:
            custom = Path(directory) / 'host_overrides.custom.json'
            custom.write_text('{"example.invalid":"http://127.0.0.1"}\n', encoding='utf-8')
            environment = {'BB_SHADNET_WEBAPI': 'http://127.0.0.1:31315'}
            with mock.patch.object(run_game.urllib.request, 'urlopen') as urlopen:
                run_game.online_overrides(environment, Path(directory))
            self.assertEqual(Path(environment['SHADPS4_HTTP_HOST_OVERRIDES_JSON']), custom)
            self.assertEqual(custom.read_text(encoding='utf-8'),
                             '{"example.invalid":"http://127.0.0.1"}\n')
            urlopen.assert_not_called()

    def test_lab_server_with_explicit_port_has_valid_summon_routes(self):
        with self.temporary_directory() as directory:
            environment = {
                'BB_COMMUNITY_SERVER': 'http://192.168.31.226:32443',
                'BB_SHADNET_WEBAPI': 'http://192.168.31.226:32315',
            }
            with mock.patch.object(run_game, 'summon_service', return_value=True):
                run_game.online_overrides(environment, Path(directory))
            routes = json.loads(Path(environment['SHADPS4_HTTP_HOST_OVERRIDES_JSON'])
                                .read_text(encoding='utf-8'))
            self.assertEqual(routes[run_game.GAME_SERVER], environment['BB_COMMUNITY_SERVER'])
            self.assertEqual(routes['http://192.168.31.226:32443/summon_messenger'],
                             environment['BB_SHADNET_WEBAPI'])
            self.assertNotIn('192.168.31.226:32443:18671', json.dumps(routes))

    def test_bbsc_wrapper_is_explicit_and_isolated(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            lab = root / 'bbsc'
            lab.mkdir()
            for name in run_game.BBSC_LAB_HASHES:
                (lab / name).touch()
            command = [Path('C:/game/bb-probe.exe'), 'boot-linked.bin', '--timeout', '0']
            values = {'BBSC_LAB_MOD': '1', 'BB_SEAMLESS_LAB': '1', 'BB_ONLINE': '1',
                      'BB_SHADNET_SERVER': '192.168.31.226:32313',
                      'BB_SHADNET_WEBAPI': 'http://192.168.31.226:32315',
                      'BB_COMMUNITY_SERVER': 'http://192.168.31.226:32443',
                      'BBSC_RELAY': '192.168.31.226:32444',
                      'SHADPS4_BBSC_SHARED_PROGRESS': '1'}
            empty_hash = hashlib.sha256(b'').hexdigest()
            fake_hashes = {name: empty_hash for name in run_game.BBSC_LAB_HASHES}
            with mock.patch.object(run_game, 'WINDOWS', True), mock.patch.dict(os.environ, values), \
                    mock.patch.dict(run_game.BBSC_LAB_HASHES, fake_hashes):
                result = run_game.bbsc_lab_command(command, command[0], root)
                self.assertEqual(result[:7], [lab / 'bbsc-launch.exe', '--emu', command[0],
                                              '--dll', lab / 'bbsc.dll', '--cwd', run_game.ROOT])
                self.assertEqual(result[7:], command[1:])
                self.assertEqual(os.environ['SHADPS4_BLOODBORNE_SEAMLESS_COOP'], '0')
                self.assertEqual(os.environ['SHADPS4_BBSC_SHARED_PROGRESS'], '0')
                self.assertEqual(os.environ['SHADPS4_BBSC_DISCORD'], '0')

    def test_bbsc_wrapper_accepts_managed_overlay_endpoints(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            lab = root / 'bbsc'
            lab.mkdir()
            for name in run_game.BBSC_LAB_HASHES:
                (lab / name).touch()
            command = [Path('C:/game/bb-probe.exe'), 'boot-linked.bin']
            values = {'BBSC_LAB_MOD': '1', 'BB_SEAMLESS_LAB': '1', 'BB_ONLINE': '1',
                      'BB_SHADNET_SERVER': '127.0.0.3:31313',
                      'BB_SHADNET_WEBAPI': 'http://127.0.0.3:31315',
                      'BB_COMMUNITY_SERVER': 'http://127.0.0.3:32443',
                      'BBSC_RELAY': '127.0.0.3:32444'}
            empty_hash = hashlib.sha256(b'').hexdigest()
            fake_hashes = {name: empty_hash for name in run_game.BBSC_LAB_HASHES}
            with mock.patch.object(run_game, 'WINDOWS', True), mock.patch.dict(os.environ, values), \
                    mock.patch.dict(run_game.BBSC_LAB_HASHES, fake_hashes):
                result = run_game.bbsc_lab_command(command, command[0], root)
            self.assertEqual(result[0], lab / 'bbsc-launch.exe')

    def test_bbsc_wrapper_rejects_unmanaged_31313_endpoint(self):
        values = {'BBSC_LAB_MOD': '1', 'BB_SEAMLESS_LAB': '1', 'BB_ONLINE': '1',
                  'BB_SHADNET_SERVER': '192.168.31.226:31313',
                  'BB_SHADNET_WEBAPI': 'http://192.168.31.226:31315',
                  'BB_COMMUNITY_SERVER': 'http://192.168.31.226:32443',
                  'BBSC_RELAY': '192.168.31.226:32444'}
        with mock.patch.object(run_game, 'WINDOWS', True), mock.patch.dict(os.environ, values):
            with self.assertRaisesRegex(ValueError, 'managed 127.0.0.3'):
                run_game.bbsc_lab_command(['probe'], 'probe', 'runtime')

    def test_bbsc_backup_uses_save_title_not_game_title(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            game = root / 'game'
            (game / 'sce_sys').mkdir(parents=True)
            (game / 'sce_sys' / 'param.sfo').write_bytes(b'sfo fixture')
            user = root / 'player'
            expected = user / 'savedata' / '1' / 'CUSA01363' / 'SPRJ0005'
            expected.mkdir(parents=True)
            (expected / 'userdata0000').write_bytes(b'saved before game start')
            with mock.patch('prepare.sfo', return_value={
                    'INSTALL_DIR_SAVEDATA': 'CUSA01363', 'TITLE_ID': 'CUSA03023'}), \
                    mock.patch.dict(os.environ, {}):
                live, snapshot_root = run_game.configure_bbsc_save_dir(user, game)
                self.assertEqual(live, expected)
                self.assertNotEqual(os.environ['BBSC_SAVE_DIR'], str(expected))
                self.assertEqual(Path(os.environ['BBSC_SAVE_DIR']), snapshot_root / 'SPRJ0005')
                self.assertEqual((snapshot_root / 'SPRJ0005' / 'userdata0000').read_bytes(),
                                 b'saved before game start')

    def test_bbsc_backup_path_is_available_before_first_save(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            game = root / 'game'
            (game / 'sce_sys').mkdir(parents=True)
            (game / 'sce_sys' / 'param.sfo').write_bytes(b'sfo fixture')
            user = root / 'player'
            with mock.patch('prepare.sfo', return_value={'TITLE_ID': 'CUSA03173'}), \
                    mock.patch.dict(os.environ, {}):
                result, snapshot_root = run_game.configure_bbsc_save_dir(user, game)
                self.assertEqual(result, user / 'savedata' / '1' / 'CUSA03173' / 'SPRJ0005')
                self.assertIsNone(snapshot_root)
                self.assertFalse(result.exists())  # Never create an empty "backup" source.
                self.assertEqual(os.environ['BBSC_SAVE_DIR'], str(result))

    def test_bbsc_backup_rejects_invalid_title(self):
        with self.temporary_directory() as directory:
            game = Path(directory) / 'game'
            (game / 'sce_sys').mkdir(parents=True)
            (game / 'sce_sys' / 'param.sfo').write_bytes(b'sfo fixture')
            with mock.patch('prepare.sfo', return_value={'INSTALL_DIR_SAVEDATA': '../other'}), \
                    mock.patch.dict(os.environ, {}, clear=True):
                with self.assertRaisesRegex(ValueError, 'no valid Bloodborne save title ID'):
                    run_game.configure_bbsc_save_dir(Path(directory) / 'player', game)
                self.assertNotIn('BBSC_SAVE_DIR', os.environ)

    def test_bbsc_wrapper_rejects_modified_binary(self):
        with self.temporary_directory() as directory:
            lab = Path(directory) / 'bbsc'
            lab.mkdir()
            for name in run_game.BBSC_LAB_HASHES:
                (lab / name).write_bytes(b'changed')
            values = {'BBSC_LAB_MOD': '1', 'BB_SEAMLESS_LAB': '1', 'BB_ONLINE': '1',
                      'BB_SHADNET_SERVER': '127.0.0.1:32313',
                      'BB_SHADNET_WEBAPI': 'http://127.0.0.1:32315',
                      'BB_COMMUNITY_SERVER': 'http://127.0.0.1:32443',
                      'BBSC_RELAY': '127.0.0.1:32444'}
            with mock.patch.object(run_game, 'WINDOWS', True), mock.patch.dict(os.environ, values):
                with self.assertRaisesRegex(ValueError, 'does not match official'):
                    run_game.bbsc_lab_command(['probe'], 'probe', directory)

    def test_bbsc_mod_report_exposes_log_and_missing_ready_marker(self):
        with self.temporary_directory() as directory:
            logs = Path(directory) / 'logs'
            logs.mkdir()
            report = logs / 'bbsc-mod-test.log'
            report.write_text('I Mod: BBSC mod loaded\nE Mod: BBSC is off\n', encoding='utf-8')
            out, err = io.StringIO(), io.StringIO()
            with mock.patch.dict(os.environ, {'BBSC_LAB_COMPONENT_DIR': directory}), \
                    redirect_stdout(out), redirect_stderr(err):
                run_game.bbsc_lab_report(directory, report.stat().st_mtime)
            self.assertIn(str(report), out.getvalue())
            self.assertIn('not confirmed', err.getvalue())

    def test_bbsc_windows_launch_forwards_explicit_output_handles(self):
        child = mock.MagicMock()
        child.__enter__.return_value = child
        child.stdout = io.StringIO('game diagnostic\n')
        child.wait.return_value = 0
        output = io.StringIO()
        with mock.patch.object(run_game, 'WINDOWS', True), \
                mock.patch.dict(os.environ, {'BBSC_LAB_MOD': '1'}), \
                mock.patch.object(run_game.subprocess, 'Popen', return_value=child) as popen, \
                redirect_stdout(output):
            self.assertEqual(run_game.start(['bbsc-launch.exe'], 'bb-probe.exe', None), 0)
        self.assertIn('game diagnostic', output.getvalue())
        self.assertEqual(popen.call_args.kwargs['stdout'], run_game.subprocess.PIPE)
        self.assertEqual(popen.call_args.kwargs['stderr'], run_game.subprocess.STDOUT)

    def test_bbsc_wrapper_rejects_production_or_bypassed_game_check(self):
        with mock.patch.object(run_game, 'WINDOWS', True):
            with mock.patch.dict(os.environ, {'BBSC_LAB_MOD': '1', 'BB_ONLINE': '1',
                                              'BB_SEAMLESS_LAB': ''}):
                with self.assertRaises(ValueError):
                    run_game.bbsc_lab_command(['probe'], 'probe', 'runtime')
            with mock.patch.dict(os.environ, {'BBSC_LAB_MOD': '1', 'BB_ONLINE': '1',
                                              'BB_SEAMLESS_LAB': '1', 'BB_SKIP_GAME_CHECK': '1'}):
                with self.assertRaises(ValueError):
                    run_game.bbsc_lab_command(['probe'], 'probe', 'runtime')

    def test_bbsc_lab_keeps_ordinary_and_custom_routes_untouched(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            custom = root / 'host_overrides.custom.json'
            ordinary = root / 'host_overrides.json'
            custom.write_text('{"custom":true}\n', encoding='utf-8')
            ordinary.write_text('{"ordinary":true}\n', encoding='utf-8')
            environment = {'BBSC_LAB_MOD': '1',
                           'BB_COMMUNITY_SERVER': 'http://192.168.31.226:32443',
                           'BB_SHADNET_WEBAPI': 'http://192.168.31.226:32315'}
            with mock.patch.object(run_game, 'summon_service', return_value=True):
                run_game.online_overrides(environment, root)
            lab = root / 'host_overrides.bbsc.json'
            self.assertEqual(Path(environment['SHADPS4_HTTP_HOST_OVERRIDES_JSON']), lab)
            self.assertEqual(custom.read_text(encoding='utf-8'), '{"custom":true}\n')
            self.assertEqual(ordinary.read_text(encoding='utf-8'), '{"ordinary":true}\n')
            routes = json.loads(lab.read_text(encoding='utf-8'))
            self.assertEqual(routes[run_game.GAME_SERVER], environment['BB_COMMUNITY_SERVER'])


if __name__ == '__main__':
    unittest.main()
