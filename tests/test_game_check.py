from paths import ROOT
import os
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

import game_check
from prepare import parse_self, span
from test_prepare import fixture


def param_sfo(entries):
    """Build a minimal param.sfo containing UTF-8 string entries."""
    keys = b''.join(key.encode() + b'\0' for key in entries)
    values = [value.encode() + b'\0' for value in entries.values()]
    count = len(entries)
    key_table = 20 + 16 * count
    data_table = key_table + len(keys)
    data = bytearray(struct.pack('<4sIIII', b'\0PSF', 0x101, key_table, data_table, count))
    key_offset = data_offset = 0
    for key, value in zip(entries, values):
        data += struct.pack('<HHIII', key_offset, 0x204, len(value), len(value), data_offset)
        key_offset += len(key) + 1
        data_offset += len(value)
    return bytes(data + keys + b''.join(values))


def dcx(payload, declared_size=None):
    packed = zlib.compress(payload)
    data = bytearray(0x4C)
    data[:4] = b'DCX\0'
    data[0x18:0x1C] = b'DCS\0'
    struct.pack_into('>II', data, 0x1C,
                     len(payload) if declared_size is None else declared_size, len(packed))
    data[0x28:0x2C] = b'DFLT'
    return bytes(data) + packed


class GameCheckTests(unittest.TestCase):
    def game(self, title, version, eboot=None):
        path = Path(self.tmp.name)
        (path / 'sce_sys').mkdir(exist_ok=True)
        (path / 'sce_sys/param.sfo').write_bytes(
            param_sfo({'APP_VER': version, 'TITLE_ID': title}))
        (path / 'eboot.bin').write_bytes(fixture() if eboot is None else eboot)
        return path

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT / '.test-tmp')
        os.environ.pop('BB_SKIP_GAME_CHECK', None)

    def tearDown(self):
        self.tmp.cleanup()

    def test_base_game_without_the_update(self):
        self.assertEqual(game_check.problem(self.game('CUSA03173', '01.00')),
                         ('missing_update', 'CUSA03173', '01.00'))

    def test_update_metadata_with_an_old_executable(self):
        self.assertEqual(game_check.problem(self.game('CUSA03173', '01.09'))[0], 'wrong_eboot')

    def test_other_edition(self):
        self.assertEqual(game_check.problem(self.game('CUSA00207', '01.09'))[0], 'other_title')

    def test_unreadable_executable(self):
        self.assertEqual(game_check.problem(self.game('CUSA03173', '01.09', b'junk'))[0],
                         'unreadable')

    def test_supported_image_and_intact_shader_pass(self):
        game = self.game('CUSA03173', '01.09')
        shader = game / game_check.CHECKED_FOLDER / 'test.dcx'
        shader.parent.mkdir(parents=True)
        shader.write_bytes(dcx(b'intact shader'))
        with mock.patch.object(game_check, 'SUPPORTED_IMAGE', game_check.image_sha256(game)):
            self.assertIsNone(game_check.problem(game))

    def test_supported_image_with_other_sfo_title_passes_without_disabling_hash_check(self):
        game = self.game('CUSA00207', '01.09')
        with mock.patch.object(game_check, 'SUPPORTED_IMAGE', game_check.image_sha256(game)):
            self.assertIsNone(game_check.problem(game))

    def test_hash_uses_loaded_virtual_image_not_elf_file_layout(self):
        game = self.game('CUSA03173', '01.09')
        elf, _header, headers, _segments, _missing = parse_self((game / 'eboot.bin').read_bytes())
        loads = [header for header in headers if header['type'] in (1, 0x61000010)]
        image = bytearray(max(header['vaddr'] + header['memsz'] for header in loads))
        for header in loads:
            image[header['vaddr']:header['vaddr'] + header['filesz']] = \
                span(elf, header['offset'], header['filesz'])
        import hashlib
        self.assertEqual(game_check.image_sha256(game), hashlib.sha256(image).hexdigest())

    def test_damaged_shader_is_rejected(self):
        game = self.game('CUSA03173', '01.09')
        shader = game / game_check.CHECKED_FOLDER / 'test.dcx'
        shader.parent.mkdir(parents=True)
        shader.write_bytes(dcx(b'shader', declared_size=99))
        with mock.patch.object(game_check, 'SUPPORTED_IMAGE', game_check.image_sha256(game)):
            self.assertEqual(game_check.problem(game)[0], 'damaged_files')

    def test_skip_switch(self):
        os.environ['BB_SKIP_GAME_CHECK'] = '1'
        self.assertIsNone(game_check.problem(self.game('CUSA03173', '01.00')))

    def test_every_problem_is_explained(self):
        for kind in ('missing_update', 'wrong_eboot', 'other_title', 'unreadable',
                     'damaged_files'):
            self.assertIn('CUSA', game_check.explain(kind, 'CUSA03173', '01.00'))


if __name__ == '__main__':
    unittest.main()
