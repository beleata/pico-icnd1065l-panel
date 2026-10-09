"""Local network management client for the Solar panel (secrets stay on this PC)."""
import argparse
import json
import struct
import time
import urllib.request
from pathlib import Path

FIELDS=('version','brightness','load','export_color','import_color','swap_lower','rotate')
class Panel:
    def __init__(self,host='192.168.1.172',credentials=None):
        self.base='http://'+host
        credentials=credentials or Path(__file__).parent/'generated/panel-admin.json'
        self.token=json.loads(Path(credentials).read_text())['token']
    def request(self,path,body=None,token=None):
        r=urllib.request.Request(self.base+'/api/'+path,data=body,headers={
            'Authorization':'Bearer '+(self.token if token is None else token),
            'Content-Type':'application/octet-stream','Connection':'close'})
        with urllib.request.urlopen(r,timeout=20) as response:
            return json.load(response)
    def settings(self,values=None):
        if values is None:return self.request('settings')
        return self.request('settings',struct.pack('<7I',*(values[f] for f in FIELDS)))
    def upload(self,path):
        data=Path(path).read_bytes()
        if data[:8]!=b'PNLOTA1\0' or len(data)!=128+struct.unpack_from('<I',data,8)[0]:
            raise ValueError('Invalid .panel package')
        self.request('ota/begin',data[:128])
        try:
            for offset in range(128,len(data),4096):
                time.sleep(0.15)  # Let the single-connection server close the previous response.
                self.request('ota/chunk?offset='+str(offset-128),data[offset:offset+4096])
                if (offset-128)%65536==0:print(f'Uploaded {offset-128}/{len(data)-128}',flush=True)
            time.sleep(0.15)
            self.request('ota/finish',b'')
        except Exception:
            try:self.request('ota/abort',b'')
            except Exception:pass
            raise
        print('Image verified; reboot scheduled.',flush=True)
    def wait(self,version=None,timeout=180):
        start=time.monotonic();time.sleep(3)
        while time.monotonic()-start<timeout:
            try:
                s=self.request('status')
                if s['confirmed'] and (version is None or s['version']==version):return s
            except Exception:pass
            time.sleep(3)
        raise TimeoutError('Panel did not confirm requested firmware')

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--host',default='192.168.1.172')
    p.add_argument('--credentials',type=Path)
    p.add_argument('--upload',type=Path)
    p.add_argument('--wait-version')
    p.add_argument('--rollback',action='store_true')
    p.add_argument('--settings',type=Path)
    a=p.parse_args();panel=Panel(a.host,a.credentials)
    if a.upload:panel.upload(a.upload)
    elif a.rollback:print(panel.request('ota/rollback',b''))
    elif a.settings:print(panel.settings(json.loads(a.settings.read_text())))
    if a.wait_version:print(panel.wait(a.wait_version))
    else:print(panel.request('status'))
