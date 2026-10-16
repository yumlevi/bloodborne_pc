"""Builds assets/cursor/arrow.png (the menu cursor's master image) from the source art.

The source (assets/cursor/arrow.webp, decoded with libwebp's dwebp) is lossy: its opaque pixels
carry alpha 251-254 and its background faint noise. Alpha is snapped at both ends, the arrow
is cropped with a small margin and scaled down by area averaging to MasterHeight pixels. The
game scales the master again to the window (bbport_cursor.cpp), and takes the hotspot from its
tip: the leftmost opaque pixel of the topmost opaque row.

    python tools/make_cursor.py [source.webp] [arrow.png]
"""
import os, struct, subprocess, sys, tempfile, zlib

MasterHeight = 256
Margin = 6  # source pixels around the arrow (room for the scaled edge's antialiasing)


def decode(path):
    with tempfile.TemporaryDirectory() as work:
        pam = os.path.join(work, 'cursor.pam')
        subprocess.run(['dwebp', '-quiet', '-pam', path, '-o', pam], check=True)
        data = open(pam, 'rb').read()
    header, pixels = data.split(b'ENDHDR\n', 1)
    fields = dict(line.split(b' ', 1) for line in header.splitlines()[1:])
    width, height = int(fields[b'WIDTH']), int(fields[b'HEIGHT'])
    if fields[b'TUPLTYPE'].strip() != b'RGB_ALPHA':
        raise SystemExit('expected RGBA')
    return width, height, bytearray(pixels)


def weights(source, target):
    """Per target index: [(source index, weight)], the source span it covers."""
    scale = source / target
    result = []
    for t in range(target):
        lo, hi = t * scale, (t + 1) * scale
        row, s = [], int(lo)
        while s < hi and s < source:
            w = min(hi, s + 1) - max(lo, s)
            if w > 0:
                row.append((s, w / scale))
            s += 1
        result.append(row)
    return result


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'assets', 'cursor', 'arrow.webp')
    target = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'assets', 'cursor', 'arrow.png')
    width, height, px = decode(source)
    for i in range(3, len(px), 4):
        px[i] = 0 if px[i] <= 8 else 255 if px[i] >= 245 else px[i]
    opaque = [(i // 4 % width, i // 4 // width) for i in range(3, len(px), 4) if px[i] > 8]
    x0 = max(min(x for x, _ in opaque) - Margin, 0)
    y0 = max(min(y for _, y in opaque) - Margin, 0)
    x1 = min(max(x for x, _ in opaque) + Margin + 1, width)
    y1 = min(max(y for _, y in opaque) + Margin + 1, height)
    cw, ch = x1 - x0, y1 - y0
    tw, th = max(1, round(cw * MasterHeight / ch)), MasterHeight
    # Premultiplied, so the transparent background's colour does not bleed into the edge.
    pre = []
    for y in range(y0, y1):
        row = []
        for x in range(x0, x1):
            r, g, b, a = px[(y * width + x) * 4:(y * width + x) * 4 + 4]
            row.append((r * a / 255, g * a / 255, b * a / 255, a))
        pre.append(row)
    wx, wy = weights(cw, tw), weights(ch, th)
    columns = [[tuple(sum(row[s][c] * w for s, w in wx[t]) for c in range(4)) for t in range(tw)] for row in pre]
    out = bytearray()
    for t in range(th):
        out.append(0)  # PNG filter: none
        for x in range(tw):
            r, g, b, a = (sum(columns[s][x][c] * w for s, w in wy[t]) for c in range(4))
            if a < 0.5:
                out += b'\0\0\0\0'
                continue
            out += bytes(min(255, round(v * 255 / a)) for v in (r, g, b)) + bytes([min(255, round(a))])

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', tw, th, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(out), 9)) + chunk(b'IEND', b'')
    open(target, 'wb').write(png)
    print(f'{target}: {tw}x{th} from {cw}x{ch} at ({x0},{y0}) of {width}x{height}')


main()
