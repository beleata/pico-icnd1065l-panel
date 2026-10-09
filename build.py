"""Build a named firmware variant without carrying flags over from other modes."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parent
VERSIONS={'solar':'0.4.2-solar-readings','chain-text':'0.4.3-chain4-rot180',
          'chain-unrotated':'0.4.3-chain4-ram','chain-arrow':'0.4.4-chain4-arrow'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('variant',choices=['usb','usb-no-config','usb-no-gap','usb-continuous',*VERSIONS])
    parser.add_argument('--board')
    parser.add_argument('--cmake-option',action='append',default=[],metavar='NAME=VALUE')
    args=parser.parse_args()
    if not os.environ.get('PICO_SDK_PATH'):
        parser.error('Set PICO_SDK_PATH to Pico SDK (with submodules)')
    solar=args.variant in VERSIONS
    chain=args.variant.startswith('chain-')
    arrow=args.variant=='chain-arrow'
    board=args.board or ('pico_w' if solar else 'vcc-gnd_yd-rp2040_8m')
    if solar and board!='pico_w':
        parser.error('Solar/chain networking and OTA require pico_w')
    pico=ROOT/'pico'
    build=pico/'generated'/('build-'+args.variant)
    def run(*cmd):
        subprocess.run([str(x) for x in cmd],cwd=ROOT,check=True)
    run(sys.executable,pico/'generate_wave.py')
    if solar:
        if not (pico/'generated/solar_wifi.h').exists():
            parser.error('Run pico/prepare_solar_wifi.py --config wifi.local.json first')
        run(sys.executable,pico/'ota_package.py')
    if chain:
        options=['--blank'] if arrow else ['--no-panel-rotation'] if args.variant=='chain-unrotated' else []
        run(sys.executable,pico/'generate_chain_test.py',*options)
    flags={
        'PICO_BOARD':board,'CMAKE_BUILD_TYPE':'Release',
        'PICO_PANEL_SOLAR':solar,'PICO_PANEL_CHAIN_TEST':chain,'PICO_PANEL_CHAIN_ARROW':arrow,
        'PANEL_OTA_TEST_CRASH':False,
        'PICO_PANEL_REFRESH_NO_CONFIG':solar or args.variant in ('usb-no-config','usb-no-gap'),
        'PICO_PANEL_REFRESH_NO_GAP':solar or args.variant=='usb-no-gap',
        'PICO_PANEL_REFRESH_CONTINUOUS':args.variant=='usb-continuous',
        'PANEL_VERSION':VERSIONS.get(args.variant,'0.2.0')}
    options=['-D'+k+'='+('ON' if v is True else 'OFF' if v is False else v) for k,v in flags.items()]
    run('cmake','-S',pico,'-B',build,'-G','Ninja',*options,*['-D'+v for v in args.cmake_option])
    run('cmake','--build',build,'--parallel','8')
    if solar:
        run(sys.executable,pico/'ota_package.py','--binary',build/'pico_panel.bin','--revision',VERSIONS[args.variant])
        print('PRIVATE firmware and keys: do not publish generated/.')
    print('Build directory:',build)


if __name__=='__main__':
    main()
