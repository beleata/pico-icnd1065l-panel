import hashlib
import hmac
import struct
import unittest
import tempfile
from pathlib import Path
from ota_package import package, bootstrap, SLOT

class PackageTests(unittest.TestCase):
    def test_signed_image(self):
        binary=struct.pack('<II',0x20042000,0x1000a101)+b'x'*510
        secret={'signing_key':'ab'*32}
        data=package(binary,'test',secret)
        self.assertEqual(len(data),128+768)
        self.assertEqual(data[16:48],hashlib.sha256(data[128:]).digest())
        self.assertEqual(data[48:80],hmac.digest(bytes.fromhex(secret['signing_key']),data[:48]+data[80:128],'sha256'))
        self.assertEqual(struct.unpack_from('<I',data,8)[0],768)
        self.assertEqual(data[80:85],b'test\0')
    def test_wrong_application(self):
        secret={'signing_key':'ab'*32}
        with self.assertRaises(ValueError):package(b'\0'*256,'bad',secret)
        with self.assertRaises(ValueError):package(b'x'*(SLOT+1),'bad',secret)
    def test_usb_boot_journal_reset(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for name in ('panel_bootloader.uf2','pico_panel.uf2'):
                (root/name).write_bytes(bytes(512))
            bootstrap(root)
            data=(root/'panel_initial_usb.uf2').read_bytes()
            self.assertEqual(len(data),34*512)
            for i in range(34):
                block=data[i*512:(i+1)*512]
                self.assertEqual(struct.unpack_from('<II',block,20),(i,34))
                if i>=2:
                    self.assertEqual(struct.unpack_from('<I',block,12)[0],0x101f6000+(i-2)*256)
                    self.assertEqual(block[32:288],b'\xff'*256)

if __name__=='__main__':unittest.main()
