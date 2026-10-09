import unittest
from PIL import Image
from pixel_map import make_map


class PixelMapTests(unittest.TestCase):
    def test_centers_keep_exact_colors(self):
        image = Image.new('RGB', (64, 32))
        image.putdata([(x, y, (x + y) % 256) for y in range(32) for x in range(64)])
        for rotation in (0, 180):
            result = make_map(image, rotate=rotation)
            self.assertEqual(result.size, (1904, 1052))
            for y in range(32):
                for x in range(64):
                    original = (63 - x, 31 - y) if rotation else (x, y)
                    self.assertEqual(result.getpixel((56 + x * 28 + 14, 100 + y * 28 + 14)),
                                     image.getpixel(original))


if __name__ == '__main__':
    unittest.main()
