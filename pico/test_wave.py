import unittest
from generate_wave import make_waves,CONFIG,CLOCK,LAT,OE,refresh_without_config,initialization,continuous_frame_start,scanning


class WaveTests(unittest.TestCase):
    def test_no_gap_refresh_keeps_commands_and_clock_edges(self):
        waves, _ = make_waves()
        old = ['gap', 'refresh_no_config', 'visible'] + ['hold'] * 32 + ['gap', 'refresh_no_config', 'visible']
        new = [name for name in old if name != 'gap']
        self.assertEqual(len(new), 36)
        self.assertEqual(new.count('refresh_no_config'), 2)
        self.assertEqual(new.count('visible'), 2)
        samples = [sample for name in new for sample in waves[name]]
        for i, sample in enumerate(samples):
            self.assertEqual(bool(sample & CLOCK), bool(i % 2))
        self.assertEqual(sum(len(waves[name]) for name in old) - len(samples), 1250)

    def test_continuous_prefix_scan(self):
        wave = continuous_frame_start()
        hold = scanning(1024)
        self.assertEqual(len(wave), 2048)
        self.assertEqual([v & ~LAT for v in wave], hold)
        expected = list(range(3)) + list(range(16, 27)) + list(range(31, 45))
        self.assertEqual([i for i, v in enumerate(wave[::2]) if v & LAT], expected)
        prefix = initialization(CONFIG[0])[:110]
        self.assertEqual([v & LAT for v in wave[:110]], [v & LAT for v in prefix])
        waves, startup = make_waves()
        self.assertNotIn('continuous_frame_start', startup)
        # Full runtime plan keeps the exact same scan/clock phase across every boundary.
        names = ['continuous_frame_start', 'visible'] + ['hold'] * 32 + ['continuous_frame_start', 'visible']
        self.assertEqual(len(names), 36)
        offset = 0
        for name in names:
            for value in waves[name]:
                self.assertEqual(value & ~(LAT | 63), hold[offset % 2048])
                offset += 1
        self.assertEqual(offset % 2048, 0)

    def test_experimental_refresh(self):
        wave = refresh_without_config()
        self.assertEqual(len(wave), 134)
        full = initialization(CONFIG[0])
        self.assertEqual(wave[:110], full[:110])
        self.assertEqual(wave[110:], full[-24:])
        self.assertTrue(all((sample & (63 | (7 << 6))) == 0 for sample in wave))
        low = wave[::2]
        self.assertEqual(sum(bool(sample & LAT) for sample in low), 3 + 11 + 14)
        self.assertEqual(sum(bool(sample & OE) for sample in low), 12)
        # Startup must still write the full configuration exactly as before.
        waves, plan = make_waves()
        self.assertNotIn('refresh_no_config', plan)
        self.assertEqual(waves['init_0'], full)

    def test_pins_and_clock(self):
        waves,plan=make_waves()
        for name,wave in waves.items():
            self.assertTrue(all(0 <= value < 4096 for value in wave))
            if name=='gap':
                self.assertEqual(wave,[0]*625)
                continue
            for lo,hi in zip(wave[::2],wave[1::2]):
                self.assertEqual(lo&CLOCK,0)
                self.assertEqual(hi,lo|CLOCK)
        self.assertLess(sum(2*len(w) for w in waves.values())+16*len(plan),200000)

    def test_config(self):
        waves,_=make_waves()
        for index,config in enumerate(CONFIG):
            low=waves[f'init_{index}'][::2]
            self.assertEqual(len(low),707)
            for i,word in enumerate((0x00aa,0x01aa,config,0x0055,0x0155)):
                block=low[55+i*128:55+(i+1)*128]
                for chip in range(8):
                    value=0
                    for mask in block[chip*16:(chip+1)*16]:
                        self.assertIn(mask&63,(0,63))
                        value=(value<<1)|(mask&1)
                    self.assertEqual(value,word)
                self.assertEqual(sum(bool(mask&LAT) for mask in block),5)
            self.assertTrue(all(x==OE for x in low[-12:]))

    def test_scan(self):
        waves,plan=make_waves()
        low=waves['hold'][::2]
        for count,mask in enumerate(low):
            self.assertEqual((mask>>6)&7,((count+16)//128)%8)
            self.assertEqual(bool(mask&OE),100 <= count%128 < 104)
            self.assertEqual(mask & (LAT|63),0)
        self.assertEqual(plan.count('visible'),2)
        self.assertEqual(plan.count('blank'),22*16)
        self.assertEqual(plan.count('gap'),23)
        self.assertEqual(plan[-1],'hold')


if __name__=='__main__':
    unittest.main()
