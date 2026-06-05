#!/usr/bin/env python3
"""Convert GIF file to C header with embedded byte array."""
import sys, os, glob

def gif2header(gif_path, out_path):
    with open(gif_path, 'rb') as f:
        data = f.read()
    basename = os.path.splitext(os.path.basename(gif_path))[0]
    arr_name = basename.replace('-', '_').replace(' ', '_')
    macro = f'GIF_{arr_name.upper()}_LEN'

    with open(out_path, 'w') as h:
        h.write(f'#ifndef GIF_DATA_H\n#define GIF_DATA_H\n\n')
        h.write(f'#include <stdint.h>\n#include <stddef.h>\n\n')
        h.write(f'#define {macro} {len(data)}UL\n\n')
        h.write(f'const uint8_t gif_{arr_name}[{macro}] = {{\n    ')
        for i, b in enumerate(data):
            h.write(f'0x{b:02X}, ')
            if (i + 1) % 16 == 0:
                h.write('\n    ')
        h.write('\n};\n\n#endif // GIF_DATA_H\n')
    print(f'{gif_path} -> {out_path} ({len(data)} bytes)')

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    gif_dir = os.path.join(script_dir, '..', 'gifs')
    out_dir = os.path.join(script_dir, '..', 'main')
    gifs = glob.glob(os.path.join(gif_dir, '*.gif'))
    if not gifs:
        print('No .gif files found in gifs/')
        sys.exit(1)
    gif2header(gifs[0], os.path.join(out_dir, 'gif_data.h'))
