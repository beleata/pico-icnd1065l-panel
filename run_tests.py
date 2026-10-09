"""Offline public tests; uses synthetic telemetry, never private captures."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default=shutil.which('g++') or shutil.which('clang++'))
    args=parser.parse_args()
    if not args.compiler:
        parser.error('Install g++/clang++ or pass --compiler /path/to/zig')
    pico=ROOT/'pico';target=pico/'generated';target.mkdir(exist_ok=True)
    def run(*cmd):subprocess.run([str(c) for c in cmd],cwd=ROOT,check=True)
    run(sys.executable,pico/'generate_wave.py')
    run(sys.executable,'-m','unittest','discover','-s',pico,'-p','test_*.py','-v')
    zig=Path(args.compiler).stem=='zig'
    for name in ('test_arrow_native','test_readings_native','test_ota_native','test_solar_native'):
        output=target/(name+('.exe' if sys.platform=='win32' else ''))
        options=['c++','-fno-threadsafe-statics','-nostdlib++'] if zig else []
        run(args.compiler,*options,'-std=c++17','-O1',pico/(name+'.cpp'),'-o',output)
        if name!='test_solar_native':
            run(output)
        else:
            # Valid synthetic SPC119 site record, padded past stream split offsets.
            report=bytearray(256)
            struct.pack_into('>H',report,4,len(report)-6)
            report[6:9]=bytes((100,0x41,0x47))
            struct.pack_into('>H',report,9,len(report)-11)
            struct.pack_into('>HHH',report,13,100,0x100,57)
            struct.pack_into('>I',report,19,1760000000)
            struct.pack_into('>iiii',report,19+17,5000,2000,-2000,1000)
            fixture=target/'synthetic-solar.bin';fixture.write_bytes(report)
            run(output,'stream',fixture)
            run(output,'render',target/'synthetic-render.bin','sample')
    print('PASS public offline suite; no private Solar data required')


if __name__=='__main__':main()
