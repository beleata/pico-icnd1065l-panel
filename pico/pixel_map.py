"""Make a numbered, nearest-neighbor pixel grid for marking panel artifacts."""
import argparse
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def font(size):
    path = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts' / 'consola.ttf'
    try:
        return ImageFont.truetype(str(path), size)
    except OSError:
        return ImageFont.load_default(size=size)


def make_map(source, cell=28, rotate=0, title='PIXEL MAP'):
    if cell < 16:
        raise ValueError('Cell size must be at least 16 pixels')
    if rotate not in (0, 180):
        raise ValueError('Use 0 or 180 degrees')
    source = source.convert('RGB')
    if rotate:
        source = source.transpose(Image.Transpose.ROTATE_180)
    width, height = source.size
    left, top = 56, 100
    canvas = Image.new('RGB', (width * cell + 112, height * cell + 156), '#181b20')
    draw = ImageDraw.Draw(canvas)
    draw.text((left, 16), title, fill='white', font=font(20))
    draw.text((left, 48), 'COLUMN: left to right  |  ROW: bottom to top  |  1 square = 1 LED pixel',
              fill='#c6cbd2', font=font(16))
    canvas.paste(source.resize((width * cell, height * cell), Image.Resampling.NEAREST), (left, top))
    for x in range(width + 1):
        px = left + x * cell
        draw.line((px, top, px, top + height * cell), fill='#50545a', width=1)
    for y in range(height + 1):
        py = top + y * cell
        draw.line((left, py, left + width * cell, py), fill='#50545a', width=1)
    numbers = font(14)
    for x in range(width):
        px = left + (x + 0.5) * cell
        for py in (top - 16, top + height * cell + 20):
            draw.text((px, py), str(x + 1), anchor='mm', fill='#d8e6f5', font=numbers)
    for y in range(height):
        py = top + (y + 0.5) * cell
        for px in (left - 24, left + width * cell + 24):
            draw.text((px, py), str(height - y), anchor='mm', fill='#d8e6f5', font=numbers)
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cell', type=int, default=28)
    parser.add_argument('--rotate', type=int, choices=(0, 180), default=0)
    parser.add_argument('--title', default='PIXEL MAP')
    args = parser.parse_args()
    with Image.open(args.image) as source:
        result = make_map(source, args.cell, args.rotate, args.title)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result.save(args.output)
    print(f'{args.output.resolve()} - {result.width}x{result.height}')


if __name__ == '__main__':
    main()
