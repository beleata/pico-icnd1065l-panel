"""Private, device-specific firmware packaging. Never publish generated secrets/images."""
import argparse
import hashlib
import hmac
import json
import secrets
import struct
from pathlib import Path

ROOT = Path(__file__).parent
PRIVATE = ROOT / "generated" / "panel-admin.json"
SLOT = 984 * 1024

def prepare():
    if PRIVATE.exists():
        data = json.loads(PRIVATE.read_text())
    else:
        data = {"token": secrets.token_hex(24), "signing_key": secrets.token_hex(32)}
        PRIVATE.parent.mkdir(parents=True, exist_ok=True)
        PRIVATE.write_text(json.dumps(data, indent=2) + "\n")
    key = bytes.fromhex(data["signing_key"])
    header = '#pragma once\n#define PANEL_ADMIN_TOKEN "' + data["token"] + '"\n'
    header += 'static const unsigned char PANEL_SIGNING_KEY[32]={' + ','.join(map(str,key)) + '};\n'
    (ROOT / "generated" / "panel_admin.h").write_text(header)
    (ROOT / 'generated' / 'panel-access.txt').write_text(
        'Solar panel: http://192.168.1.172/\nManagement key (token):\n'+data['token']+
        '\n\nPRIVATE: do not publish this file.\n',encoding='utf-8')
    html=(ROOT / 'panel_web.html').read_text(encoding='utf-8')
    (ROOT / 'generated' / 'panel_web_page.h').write_text(
        '#pragma once\nstatic const char PANEL_WEB_PAGE[]=R"WEB('+html+')WEB";\n', encoding='utf-8')
    return data

def package(binary, revision, data):
    binary += b'\xff' * (-len(binary) % 256)
    if not 256 <= len(binary) <= SLOT:
        raise ValueError("Image exceeds application slot")
    sp, pc = struct.unpack_from('<II', binary)
    if not (0x20000000 <= sp <= 0x20042000 and pc & 1 and 0x1000a000 <= pc < 0x10100000):
        raise ValueError("Not an OTA-linked RP2040 application")
    header = bytearray(128)
    header[:8] = b'PNLOTA1\0'
    struct.pack_into('<II', header, 8, len(binary), 0xa000)
    header[16:48] = hashlib.sha256(binary).digest()
    rev = revision.encode('ascii')
    if len(rev) > 31: raise ValueError("Revision too long")
    header[80:80+len(rev)] = rev
    header[48:80] = hmac.digest(bytes.fromhex(data['signing_key']), header[:48]+header[80:], 'sha256')
    return bytes(header)+binary

def bootstrap(directory):
    blocks=[]
    for name in ('panel_bootloader.uf2','pico_panel.uf2'):
        data=(directory/name).read_bytes()
        if len(data)%512:raise ValueError('Invalid UF2 length')
        blocks.extend(bytearray(data[i:i+512]) for i in range(0,len(data),512))
    # USB installation starts a new boot transaction history; retain settings.
    for offset in range(0x1f6000,0x1f8000,256):
        block=bytearray(512)
        struct.pack_into('<8I',block,0,0x0a324655,0x9e5d5157,0x2000,0x10000000+offset,256,0,0,0xe48bff56)
        block[32:288]=b'\xff'*256
        struct.pack_into('<I',block,508,0x0ab16f30)
        blocks.append(block)
    for i,block in enumerate(blocks):struct.pack_into('<II',block,20,i,len(blocks))
    (directory/'panel_initial_usb.uf2').write_bytes(b''.join(blocks))

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--binary', type=Path)
    p.add_argument('--revision', default='0.4.2-solar-readings')
    args=p.parse_args()
    data=prepare()
    if args.binary:
        target=args.binary.with_suffix('.panel')
        target.write_bytes(package(args.binary.read_bytes(),args.revision,data))
        bootstrap(args.binary.parent)
        print(f'PRIVATE OTA package: {target}')
    else:
        print('Private panel credentials ready (not printed).')
