"""Recover only the Wi-Fi JSON from the owner's full UF2 backup; never print it.

Output and resulting personalized firmware must stay in ignored generated/.
"""
import argparse
import json
import struct
from pathlib import Path


def recover(path):
    raw = Path(path).read_bytes()
    flash = bytearray(b'\xff' * (2 * 1024 * 1024))
    for at in range(0, len(raw), 512):
        block = raw[at:at + 512]
        if len(block) != 512:
            raise ValueError('Truncated UF2')
        magic, magic2, flags, addr, size = struct.unpack_from('<5I', block)
        if (magic, magic2) != (0x0a324655, 0x9e5d5157) or size > 476:
            raise ValueError('Not UF2')
        offset = addr - 0x10000000
        if 0 <= offset <= len(flash) - size:
            flash[offset:offset + size] = block[32:32 + size]
    candidates = []
    for at, b in enumerate(flash):
        if b != ord('{'):
            continue
        try:
            value, _ = json.JSONDecoder().raw_decode(flash[at:at + 2048].decode('utf8', errors='replace'))
            if (isinstance(value, dict) and isinstance(value.get('ssid'), str)
                    and isinstance(value.get('password'), str) and value['ssid'] and value['password']):
                if value not in candidates:
                    candidates.append(value)
        except ValueError:
            pass
    if len(candidates) != 1:
        raise ValueError('Expected exactly one intact Wi-Fi configuration; use explicit config JSON instead')
    return candidates[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--backup', type=Path)
    src.add_argument('--config', type=Path)
    args = ap.parse_args()
    c = recover(args.backup) if args.backup else json.loads(args.config.read_text(encoding='utf8'))
    target = Path(__file__).parent / 'generated' / 'solar_wifi.h'
    target.parent.mkdir(exist_ok=True)
    lines = ['// PRIVATE: generated local credentials. Never publish this header or personalized UF2.', '#pragma once']
    for key, macro in [('ssid', 'SOLAR_SSID'), ('password', 'SOLAR_PASSWORD'), ('router', 'SOLAR_ROUTER')]:
        value = c.get(key, '192.168.1.1' if key == 'router' else '')
        if not isinstance(value, str) or not value or '\0' in value:
            raise ValueError('Invalid configuration field: ' + key)
        # Octal escapes preserve UTF-8 without C hex-escape ambiguity.
        literal = '"' + ''.join('\\%03o' % b for b in value.encode('utf8')) + '"'
        lines.append('#define ' + macro + ' ' + literal)
    target.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print('Private Wi-Fi build configuration prepared; credentials omitted.')


if __name__ == '__main__':
    main()
