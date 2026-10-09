import unittest
import generate_chain_test as chain
from generate_wave import CLOCK, LAT, OE


class ChainTests(unittest.TestCase):
    def test_local_rotation_preserves_panel_order(self):
        for r in range(8):
            for c in range(16):
                for k in range(32):
                    for lane in range(2):
                        serial = (k%8)*16+c
                        old_x = (3-k//8)*64 + (serial//64)*32 + serial%32
                        old_y = r + (8 if (serial//32)%2 == 0 else 0) + lane*16
                        x, y = chain.coordinate(r,c,k,lane)
                        self.assertEqual(x//64, old_x//64)
                        self.assertEqual(x%64 + old_x%64, 63)
                        self.assertEqual(y + old_y, 31)

    def test_mapping(self):
        visited = [chain.coordinate(r,c,k,l) for r in range(8) for c in range(16)
                   for k in range(32) for l in range(2)]
        self.assertEqual(len(set(visited)), 8192)
        self.assertEqual(set(visited), {(x,y) for y in range(32) for x in range(256)})

    def test_register(self):
        wave = chain.register(0x74a0)
        self.assertEqual(len(wave), 1024)
        for n, low in enumerate(wave[::2]):
            self.assertEqual(bool(low & LAT), n >= 507)
            self.assertEqual(low & 63, 63 if 0x74a0 & (1 << (15-n%16)) else 0)

    def test_pixels_and_timing(self):
        image, waves, plan = chain.make_waves()
        wave = waves['visible']
        self.assertEqual(len(wave), 131072)
        self.assertEqual(plan[-1], 'hold')
        self.assertEqual(plan.count('visible'), 2)
        self.assertEqual(plan.count('blank'), 22*64)
        self.assertEqual(sum(bool(v & LAT) for v in wave[::2]), 128)
        word = 0
        for r in range(8):
            for c in range(16):
                for k in range(32):
                    actual = [0]*6
                    for b in range(16):
                        clock = word*16+b
                        low, high = wave[clock*2:clock*2+2]
                        self.assertEqual(high, low | CLOCK)
                        self.assertEqual((low >> 6) & 7, ((clock+16)//128)%8)
                        self.assertEqual(bool(low & OE), 100 <= clock%128 < 104)
                        self.assertEqual(bool(low & LAT), clock%512 == 511)
                        for lane in range(6):
                            actual[lane] = actual[lane]*2+((low >> lane)&1)
                    expected = [v for lane in range(2) for v in image.getpixel(chain.coordinate(r,c,k,lane))]
                    self.assertEqual(actual, expected)
                    word += 1

    def test_compact_stream_identical(self):
        _, waves, plan = chain.make_waves()
        arrays, descriptors = chain.compact(waves, plan)
        original = (v for name in plan for v in waves[name])
        actual = (arrays[name][i % len(arrays[name])] for name, count, ring in descriptors for i in range(count))
        from itertools import zip_longest
        self.assertTrue(all(a == b for a, b in zip_longest(original, actual)))
        for name, count, ring in descriptors:
            if ring:
                self.assertEqual(len(arrays[name])*2, 1 << ring)
                self.assertEqual(count % len(arrays[name]), 0)


if __name__ == '__main__':
    unittest.main()
