#!/usr/bin/env python3
"""Generate a minimal test GIF for the GIF player project."""
import struct
import os

def make_gif(w, h, colors):
    """Create a simple GIF89a with one frame per color."""
    gif = b'GIF89a'
    gif += struct.pack('<HH', w, h)
    # GCT: 4 colors (2 bits/pixel)
    gif += bytes([0xF2, 0, 0])  # packed=0xF2, bg=0, aspect=0
    pal = b''
    for c in [(0,0,0), (255,0,0), (0,255,0), (0,0,255)]:
        pal += bytes(c)
    gif += pal

    # Netscape loop extension (infinite)
    gif += bytes([0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41,
                  0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00])

    for fill in colors:
        # GCE: delay 50 = 500ms
        gif += bytes([0x21, 0xF9, 0x04, 0x00, 0x32, 0x00, 0x00, 0x00])
        # Image descriptor
        gif += bytes([0x2C])
        gif += struct.pack('<HHHH', 0, 0, w, h)
        gif += bytes([0x00])

        # Build LZW data: min code size=2
        gif += bytes([0x02])
        lzw = bytearray()
        lzw.append(0x80 | (1 << 2))  # clear code
        for y in range(h):
            for x in range(w):
                lzw.append(fill)
        lzw.append(0x80 | ((1 << 2) + 1))  # eoi code

        # Pack into sub-blocks (max 255 bytes each)
        for i in range(0, len(lzw), 255):
            chunk = lzw[i:i+255]
            gif += bytes([len(chunk)]) + bytes(chunk)
        gif += bytes([0x00])

    gif += b'\x3B'  # trailer
    return gif

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    gif_dir = os.path.join(script_dir, '..', 'gifs')
    out_path = os.path.join(gif_dir, 'animation.gif')

    # Create a 16x16 test GIF: flickers between red and green
    gif = make_gif(16, 16, [1, 2])
    with open(out_path, 'wb') as f:
        f.write(gif)
    print(f'Created: {out_path} ({len(gif)} bytes)')
