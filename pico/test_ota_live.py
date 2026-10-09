"""Explicit, bounded hardware checks. Upload is NOT performed by this test."""
import argparse
import struct
import urllib.error
from pathlib import Path
from panel_admin import Panel

def rejected(call):
    try:call()
    except urllib.error.HTTPError as e:
        assert e.code in (400,401,409),e.code
        e.close()
    else:raise AssertionError('Unsafe request accepted')

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',default='192.168.1.172')
    p.add_argument('--package',type=Path,default=Path(__file__).parent/'generated/build-solar/pico_panel.panel')
    a=p.parse_args();panel=Panel(a.host)
    print(panel.wait(timeout=90),flush=True)
    rejected(lambda:panel.request('settings',token='wrong'))
    rejected(lambda:panel.request('settings',b'bad'))
    original=panel.settings()
    modified=dict(original,brightness=95 if original['brightness']!=95 else 90)
    try:
        panel.settings(modified);assert panel.settings()==modified
        bad=dict(original,brightness=101);rejected(lambda:panel.settings(bad))
    finally:panel.settings(original)
    package=a.package.read_bytes();bad=bytearray(package[:128]);bad[48]^=1
    rejected(lambda:panel.request('ota/begin',bad))
    panel.request('ota/begin',package[:128])
    rejected(lambda:panel.request('ota/chunk?offset=4096',package[128:4224]))
    panel.request('ota/chunk?offset=0',package[128:4224])
    rejected(lambda:panel.request('ota/finish',b''))
    panel.request('ota/abort',b'')
    assert not panel.request('status')['updating']
    # Full transfer with damaged content must fail the flash SHA256 check.
    panel.request('ota/begin',package[:128])
    try:
        for i in range(128,len(package),4096):
            chunk=bytearray(package[i:i+4096])
            if i==128:chunk[200]^=1
            panel.request('ota/chunk?offset='+str(i-128),chunk)
        rejected(lambda:panel.request('ota/finish',b''))
    finally:panel.request('ota/abort',b'')
    assert panel.settings()==original
    print('PASS authentication, saved settings, bounds, manifest MAC, interrupted transfer, corrupt image SHA256',flush=True)
    print(panel.request('status'),flush=True)

if __name__=='__main__':main()
