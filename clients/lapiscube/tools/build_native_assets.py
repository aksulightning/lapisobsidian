#!/usr/bin/env python3
"""Generate committed CC0 native icons and a public-domain bitmap font.

Regeneration requires Pillow, CairoSVG, and the pinned font8x8 checkout.
Normal client builds use the committed outputs without these dependencies.
"""
import argparse
import hashlib
import io
import json
import pathlib
import re
import subprocess

from PIL import Image
import cairosvg

ROOT = pathlib.Path(__file__).resolve().parents[1]
FONT_URL = 'https://github.com/dhepper/font8x8'
FONT_PIN = '8e279d2d864e79128e96188a6b9526cfa3fbfef9'
FONT_HASH = '49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5'
README_HASH = '885cc022fa9c844695b364e77e6a18ab2be74678a181827f1ce893071205cb65'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font8x8', type=pathlib.Path, required=True)
    source = parser.parse_args().font8x8
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != FONT_PIN:
        raise ValueError('Unexpected font8x8 revision')
    font, readme = (source/'font8x8_basic.h').read_bytes(), (source/'README').read_bytes()
    if hashlib.sha256(font).hexdigest() != FONT_HASH or hashlib.sha256(readme).hexdigest() != README_HASH:
        raise ValueError('Unexpected font8x8 source/license hash')
    (ROOT/'assets/licenses/font8x8-README.txt').write_bytes(readme)
    # Preserve the full author/license header with the adapted table.
    notice = font.decode().split('*/', 1)[0] + '*/\n'
    glyphs = re.findall(r'\{\s*((?:0x[0-9A-Fa-f]{2},?\s*){8})\}', font.decode())
    if len(glyphs) != 128:
        raise ValueError('Unexpected font8x8 table layout')
    native = ROOT/'assets/native'
    native.mkdir(exist_ok=True)
    header = notice + '/* U+0021 through U+007E; least-significant bit is the left pixel. */\n'
    header += 'static const cc_uint8 font_bitmap[][CELL_SIZE] = {\n'
    header += ''.join('    {' + ','.join(re.findall(r'0x[0-9A-Fa-f]{2}', glyphs[c])) + '}, /* U+%04X */\n' % c for c in range(33, 127))
    (native/'LapisFont.h').write_text(header + '};\n')
    svg = (ROOT/'assets/web-icon.svg').read_bytes()
    icon = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg, output_width=256, output_height=256))).convert('RGBA')
    icon.save(native/'LapisCube.ico', sizes=[(16,16), (24,24), (32,32), (48,48), (64,64), (128,128), (256,256)])
    pixels = ['/* Generated from CC0 assets/web-icon.svg. */', 'static const unsigned long CCIcon_Data[] = {']
    for size in (64, 32, 16):
        im = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg, output_width=size, output_height=size))).convert('RGBA')
        pixels.append(f'{size},{size},')
        data = [f'0x{a:02X}{r:02X}{g:02X}{b:02X}' for r,g,b,a in im.getdata()]
        pixels.extend(','.join(data[i:i+8])+',' for i in range(0,len(data),8))
    pixels += ['};', 'static const int CCIcon_Size = sizeof(CCIcon_Data) / sizeof(unsigned long);']
    (native/'LapisIcon_X11.h').write_text('\n'.join(pixels)+'\n')
    path = ROOT/'assets/manifest.json'
    manifest = json.loads(path.read_text())
    manifest['assets'] = [a for a in manifest['assets'] if not a['destination_filename'].startswith('assets/native/')]
    for output in sorted(native.iterdir()):
        is_font = output.suffix == '.h' and output.name == 'LapisFont.h'
        manifest['assets'].append(dict(destination_filename=str(output.relative_to(ROOT)),
            sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
            source_url=FONT_URL+'/blob/'+FONT_PIN+'/font8x8_basic.h' if is_font else 'https://github.com/aksulightning/lapisobsidian/blob/testing-cube/clients/lapiscube/assets/web-icon.svg',
            source_sha256=FONT_HASH if is_font else hashlib.sha256(svg).hexdigest(),
            author='Daniel Hepper; Marcel Sondaar; IBM' if is_font else 'LapisCube contributors',
            license='LicenseRef-Public-Domain' if is_font else 'CC0-1.0',
            license_url=FONT_URL+'/blob/'+FONT_PIN+'/README' if is_font else 'https://creativecommons.org/publicdomain/zero/1.0/',
            license_file='assets/licenses/font8x8-README.txt' if is_font else 'assets/licenses/LapisCube-Originals.txt',
            modifications='Selected printable ASCII U+0021-U+007E; original LSB-first pixel order retained' if is_font else 'Rasterized CC0 SVG into Windows ICO or X11 ARGB icon sizes',
            attribution='Daniel Hepper; Marcel Sondaar; IBM' if is_font else 'LapisCube',
            redistribution_notes='Public domain; provenance retained' if is_font else 'CC0; attribution appreciated, not required'))
    path.write_text(json.dumps(manifest, indent=2)+'\n')
    print('Generated public-domain fallback font and CC0 Windows/X11 icons')


if __name__ == '__main__':
    main()
