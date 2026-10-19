"""Validate the supported Bloodborne executable and detect damaged DCX extraction data.

The runtime and patches target CUSA03173 v1.09. Some extraction tools leave stale bytes in
compressed sectors without reporting an error; validating the first shader bundle turns the
otherwise unexplained loading hang into an actionable message. Set BB_SKIP_GAME_CHECK=1 only
for development. Run ``game_check.py GAME_DIR`` to scan every DCX file.
"""
import hashlib
import os
import struct
import sys
import zlib
from pathlib import Path

SUPPORTED_TITLE = 'CUSA03173'
SUPPORTED_VERSION = '01.09'
SUPPORTED_IMAGE = '071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a'
CHECKED_FOLDER = 'dvdroot_ps4/shader'


def image_sha256(game):
    # Hash the loaded image, not the reconstructed ELF file layout. SELF wrapper/header offsets
    # differ between dump tools, while the mapped executable bytes and patch addresses do not.
    from prepare import parse_self, span
    elf, _header, program_headers, _segments, _missing = parse_self(
        (Path(game) / 'eboot.bin').read_bytes())
    loads = [header for header in program_headers if header['type'] in (1, 0x61000010)]
    image = bytearray(max(header['vaddr'] + header['memsz'] for header in loads))
    for header in loads:
        start = header['vaddr']
        image[start:start + header['filesz']] = span(elf, header['offset'], header['filesz'])
    return hashlib.sha256(image).hexdigest()


def dcx_broken(path):
    """Return true when a DFLT DCX stream fails its checksum or stated unpacked size."""
    data = Path(path).read_bytes()
    if data[:4] != b'DCX\0' or data[0x18:0x1C] != b'DCS\0' or data[0x28:0x2C] != b'DFLT':
        return False
    size, packed = struct.unpack_from('>II', data, 0x1C)
    try:
        return len(zlib.decompress(data[0x4C:0x4C + packed])) != size
    except zlib.error:
        return True


def broken_files(game, folder=CHECKED_FOLDER):
    root = Path(game)
    return [str(path.relative_to(root))
            for path in sorted((root / folder).rglob('*.dcx')) if dcx_broken(path)]


def problem(game, image_hash=None):
    """Return None for a supported intact game, otherwise (kind, title, version)."""
    if os.environ.get('BB_SKIP_GAME_CHECK') == '1':
        return None
    from prepare import sfo
    game = Path(game)
    try:
        info = sfo((game / 'sce_sys/param.sfo').read_bytes())
    except (OSError, ValueError, IndexError, KeyError):
        return 'unreadable', '?', '?'
    title, version = info.get('TITLE_ID', '?'), info.get('APP_VER', '?')
    try:
        if (image_hash or image_sha256(game)) == SUPPORTED_IMAGE:
            return ('damaged_files', title, version) if broken_files(game) else None
    except (OSError, ValueError, IndexError, StopIteration, KeyError):
        return 'unreadable', title, version
    if title != SUPPORTED_TITLE:
        return 'other_title', title, version
    if version != SUPPORTED_VERSION:
        return 'missing_update', title, version
    return 'wrong_eboot', title, version


def explain(kind, title, version):
    found = f'检测到 {title}，版本 {version}。'
    return {
        'missing_update': f'{found}请将游戏本体与 1.09 更新一起正确转储／合并。',
        'wrong_eboot': f'{found}param.sfo 标为 1.09，但 eboot.bin 不是当前核心支持的可执行文件；'
                       '请重新转储游戏本体和更新。',
        'other_title': f'{found}当前核心仅支持 CUSA03173 v1.09；其他区域的运行时补丁地址不兼容。',
        'unreadable': f'{found}无法将 eboot.bin 或 param.sfo 读取为已解密的 PS4 游戏转储。',
        'damaged_files': f'{found}Shader DCX 文件无法正确解压。游戏提取结果已损坏，继续启动会卡在加载；'
                         '请重新提取游戏本体和更新。',
    }[kind]


def main(argv):
    if len(argv) != 2:
        print('Usage: game_check.py GAME_DIR', file=sys.stderr)
        return 2
    game = Path(argv[1])
    files = sorted((game / 'dvdroot_ps4').rglob('*.dcx'))
    broken = []
    for number, path in enumerate(files, 1):
        if dcx_broken(path):
            broken.append(path)
            print(f'damaged: {path.relative_to(game)}')
        if number % 1000 == 0:
            print(f'{number} of {len(files)} checked', file=sys.stderr)
    print(f'{len(broken)} of {len(files)} DCX files do not unpack')
    return 1 if broken else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
