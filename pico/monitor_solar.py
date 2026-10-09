"""Bounded, read-only USB diagnostics for the combined solar firmware."""
import argparse
import json
import time
from pathlib import Path
import serial
from serial.tools.list_ports import comports


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--serial',default='E661640843969126')
    ap.add_argument('--seconds',type=float,default=30)
    ap.add_argument('--log',type=Path)
    ap.add_argument('--until-complete',action='store_true',help='Stop after fresh site and both battery records')
    args=ap.parse_args()
    ports=[p.device for p in comports() if (p.serial_number or '').upper()==args.serial.upper()]
    if len(ports)!=1: raise RuntimeError('Expected one identified Pico')
    print('Reading',ports[0],flush=True)
    lines=[]; units=set()
    with serial.Serial(ports[0],115200,timeout=1) as connection:
        deadline=time.monotonic()+args.seconds
        while time.monotonic()<deadline:
            line=connection.readline().decode(errors='replace').strip()
            if line:
                print(line,flush=True); lines.append(line)
                try:
                    record=json.loads(line)
                    if record.get('event')=='reading': units.add(record.get('unit'))
                except ValueError: pass
                if args.until_complete and {100,2,3}<=units: break
    if args.log:
        args.log.write_text('\n'.join(lines)+'\n',encoding='utf8')


if __name__=='__main__': main()
