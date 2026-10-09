"""Static four-panel ICND1065L test. Preserve the single-panel generator."""
import json
import argparse
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from generate_wave import CONFIG, CLOCK, LAT, OE, cycle, initialization, scanning

ROOT = Path(__file__).resolve().parent
PANELS = 4
CHAIN_CLOCKS = 128 * PANELS
ROTATE_PANELS = True


def load_font(size):
    for name in (os.environ.get('PANEL_FONT',''), 'C:/Windows/Fonts/arialbd.ttf', 'DejaVuSans-Bold.ttf'):
        if not name:
            continue
        try:
            return ImageFont.truetype(name,size)
        except OSError:
            pass
    raise RuntimeError('Set PANEL_FONT to a TTF supporting Cyrillic')


def register(word):
    return [v for count in range(CHAIN_CLOCKS)
            for v in cycle((63 if word & (1 << (15-count % 16)) else 0)
                           | (LAT if count >= CHAIN_CLOCKS-5 else 0))]


def picture():
    image = Image.new('RGB', (64*PANELS, 32))
    draw = ImageDraw.Draw(image)
    colors = [(75, 0, 0), (0, 75, 0), (0, 30, 95), (75, 65, 0)]
    text = '\u0427\u0415\u0422\u0418\u0420\u0418 \u041f\u0410\u041d\u0415\u041b\u0410 - \u0415\u0414\u0418\u041d \u0415\u041a\u0420\u0410\u041d'
    for size in range(17, 7, -1):
        font = load_font(size)
        box = draw.textbbox((0, 0), text, font=font)
        if box[2]-box[0] <= 250:
            break
    draw.text(((256-(box[2]-box[0]))//2, 5-box[1]), text, font=font, fill=(75, 75, 75))
    small = load_font(11)
    for panel, color in enumerate(colors):
        x = panel*64
        draw.line((x, 0, x+63, 0), fill=color)
        draw.line((x, 31, x+63, 31), fill=color)
        draw.text((x+29, 19), str(panel+1), font=small, fill=color)
    # Sharp low-brightness glyphs also make repeated scan fragments shareable.
    return image.point(lambda value: 75 if value >= 30 else 0)


def coordinate(row, channel, chip, lane):
    # Far-panel-first is an initial cascade-order hypothesis; physical test verifies it.
    panel, local_chip = divmod(chip, 8)
    serial = local_chip*16+channel
    x = (PANELS-1-panel)*64 + (serial//64)*32 + serial % 32
    y = row + (8 if (serial//32) % 2 == 0 else 0) + lane*16
    # Rotate within each 64x32 tile, preserving its position in the chain.
    return ((x//64)*64 + 63-x%64, 31-y) if ROTATE_PANELS else (x,y)


def visible(image):
    wave = []
    for row in range(8):
        for channel in range(16):
            for chip in range(8*PANELS):
                levels = [value for lane in range(2) for value in image.getpixel(coordinate(row, channel, chip, lane))]
                for bit in range(15, -1, -1):
                    clock = len(wave)//2
                    phase = clock % 128
                    mask = (((clock+16)//128) % 8) << 6
                    if 100 <= phase < 104:
                        mask |= OE
                    if clock % CHAIN_CLOCKS == CHAIN_CLOCKS-1:
                        mask |= LAT
                    mask |= sum(((value >> bit) & 1) << lane for lane, value in enumerate(levels))
                    wave.extend(cycle(mask))
    return wave


def make_waves(blank=False):
    image = Image.new('RGB',(256,32)) if blank else picture()
    waves = {'prefix': initialization(0)[:110], 'tail': [v for _ in range(12) for v in cycle(OE)],
             'hold': scanning(1024), 'gap': [0]*625,
             'blank': [v for c in range(1024) for v in cycle((((c+16)//128)%8)<<6 |
                         (OE if 100 <= c % 128 < 104 else 0) |
                         (LAT if c % CHAIN_CLOCKS == CHAIN_CLOCKS-1 else 0))],
             'visible': visible(image)}
    for word in set(CONFIG) | {0x00aa, 0x01aa, 0x0055, 0x0155}:
        waves[f'reg_{word:04x}'] = register(word)
    plan = []
    for frame in range(24):
        plan += ['prefix'] + [f'reg_{w:04x}' for w in (0x00aa, 0x01aa, CONFIG[frame % 22], 0x0055, 0x0155)] + ['tail']
        plan += ['blank']*(16*PANELS) if frame < 22 else ['visible']
        if frame < 23:
            plan += ['hold']*32 + ['gap']
    plan += ['hold']
    return image, waves, plan


def compact(waves, plan):
    arrays = {name: values for name, values in waves.items() if name != 'visible'}
    chunks, names = {}, []
    for i in range(0, len(waves['visible']), 64):
        chunk = tuple(waves['visible'][i:i+64])
        if chunk not in chunks:
            name = f'pixels_{len(chunks)}'
            chunks[chunk] = name
            arrays[name] = chunk
        names.append(chunks[chunk])
    expanded = [piece for name in plan for piece in (names if name == 'visible' else [name])]
    descriptors = []
    for name in expanded:
        if name in ('blank', 'hold') and descriptors and descriptors[-1][0] == name:
            old = descriptors.pop()
            descriptors.append((name, old[1]+len(arrays[name]), 12))
        else:
            descriptors.append((name, len(arrays[name]), 0))
    return arrays, descriptors


def main():
    global ROTATE_PANELS
    parser=argparse.ArgumentParser()
    parser.add_argument('--blank',action='store_true')
    parser.add_argument('--no-panel-rotation',action='store_true')
    args=parser.parse_args()
    ROTATE_PANELS=not args.no_panel_rotation
    image, waves, plan = make_waves(blank=args.blank)
    arrays, descriptors = compact(waves, plan)
    target = ROOT/'generated'
    target.mkdir(exist_ok=True)
    lines = ['#pragma once', '#include <stdint.h>', '#include <stddef.h>',
             'struct Wave { const uint16_t *samples; uint32_t count; uint32_t ring_bits; };']
    for name, values in arrays.items():
        alignment = 'alignas(4096) ' if name in ('hold', 'blank') else ''
        lines.append(f'{alignment}static uint16_t wave_{name}[] = {{')
        lines.extend('    '+','.join(f'0x{v:03x}' for v in values[i:i+16])+',' for i in range(0, len(values), 16))
        lines.append('};')
    lines.append('static const Wave startup_plan[] = {')
    lines.extend(f'    {{wave_{name}, {count}, {ring}}},' for name, count, ring in descriptors)
    lines += ['};', f'static const size_t PLAN_COUNT = {len(descriptors)};']
    (target/'chain_wave_data.h').write_text('\n'.join(lines)+'\n', encoding='ascii')
    image.resize((1536, 192), Image.Resampling.NEAREST).save(target/'chain-test-preview.png')
    stats = {'panels': PANELS, 'width': 256, 'height': 32, 'register_clocks': CHAIN_CLOCKS,
             'upload_clocks': len(waves['visible'])//2, 'descriptors': len(descriptors),
             'wave_bytes': sum(len(w)*2 for w in arrays.values()),
             'startup_seconds': sum(len(waves[n]) for n in plan[:-1])*320e-9}
    print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
