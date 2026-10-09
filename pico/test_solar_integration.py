"""Differential native C++ tests against the existing Solar captures (no hardware)."""
import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--solar', type=Path, required=True)
    ap.add_argument('--runner', type=Path, required=True)
    args = ap.parse_args()
    sys.path.insert(0, str(args.solar))
    sys.path.insert(0, str(args.solar / 'pico'))
    from huawei_passive import dpkt
    from solar_protocol import CloudDecoder
    sys.path.insert(0, str(ROOT.parent))
    from panel.make_pattern import coordinate
    target = ROOT / 'generated'
    for bits,w,h in re.findall(r'bitmap\(f,"([01]+)",(\d+),(\d+)',(ROOT/'solar_display.h').read_text(encoding='utf8')):
        assert len(bits)==int(w)*int(h), 'Bitmap dimensions mismatch'
    runner = str(args.runner.resolve())
    for file in sorted((args.solar / 'sdongle-evidence').glob('sdongle-cloud-*.pcap')):
        decoder = CloudDecoder()
        expected = []
        with file.open('rb') as f:
            frames = [frame for _, frame in dpkt.pcap.Reader(f)]
        for frame in frames:
            for r in decoder.feed_frame(frame):
                expected.append(dict(unit=r['unit'], stamp=r['device_timestamp_raw'], pv=r['pv_w'],
                    load=r.get('load_w', 0), grid=r.get('grid_w', 0), battery=r.get('battery_w', 0),
                    soc10=round(r.get('battery_soc_pct', 0)*10)))
        fixture = target / 'solar-frames.bin'
        fixture.write_bytes(b''.join(struct.pack('<I', len(f))+f for f in frames))
        actual = [json.loads(s) for s in subprocess.check_output([runner, 'frames', str(fixture)], text=True).splitlines()]
        assert actual == expected, (file.name, actual, expected)
        print('PASS capture', file.name, len(actual), 'readings')
        subprocess.run([runner,'malformed',str(fixture)],check=True)
        # A missing captured segment must not be spliced into the next record.
        lost = [f for f in frames if len(f)>800]
        decoder = CloudDecoder(); expected_lost=[]
        for f in lost:
            expected_lost.extend((r['unit'],r['device_timestamp_raw']) for r in decoder.feed_frame(f))
        fixture.write_bytes(b''.join(struct.pack('<I',len(f))+f for f in lost))
        result=[json.loads(s) for s in subprocess.check_output([runner,'frames',str(fixture)],text=True).splitlines()]
        assert [(r['unit'],r['stamp']) for r in result]==expected_lost
    subprocess.run([runner,'stream',str(args.solar/'sdongle-evidence/report-fragment-0.bin')],check=True)
    output=target/'solar-preview.bin'
    subprocess.run([runner,'render',str(output),'sample'],check=True)
    raw=output.read_bytes(); sheet=Image.new('RGB',(64*10*3,32*10+30),'#202020')
    draw=ImageDraw.Draw(sheet)
    for page in range(3):
        data=raw[page*6144:(page+1)*6144]; im=Image.new('RGB',(64,32))
        for row in range(8):
            for channel in range(16):
                for chip in range(8):
                    word=(row*16+channel)*8+chip
                    for lane in range(2):
                        im.putpixel(coordinate(row,channel,chip,lane),tuple(data[(lane*3+c)*1024+word] for c in range(3)))
        sheet.paste(im.resize((640,320),Image.Resampling.NEAREST),(page*640,30))
        draw.text((page*640+10,8),'LAYOUT SAMPLE - NOT LIVE',fill='white')
    sheet.save(target/'solar-layout-preview.png')
    print('PASS three energy-flow scenarios for the static 64x32 layout')


if __name__=='__main__': main()
