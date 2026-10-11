#!/usr/bin/env python3
"""Check runtime texture mappings, provenance and staged embedded artwork."""
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_engine', ROOT/'tools/prepare_engine.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class VisualAssets(unittest.TestCase):
    def test_runtime_mappings_and_provenance(self):
        plan = json.loads((ROOT/'assets/texture-sources.json').read_text())
        atlas = json.loads((ROOT/'assets/atlas.json').read_text())
        manifest = json.loads((ROOT/'assets/manifest.json').read_text())
        entries = {a['destination_filename']: a for a in manifest['assets']}
        self.assertEqual(plan['license'], 'CC0-1.0')
        self.assertIn('CC0 1.0 Universal', (ROOT/plan['notices']['LICENSE.txt']).read_text())
        facts = (ROOT/'src/LapisFacts.h').read_text()
        items = {name: int(slot) for slot,name in re.findall(r'\{\d+, (\d+), "([^"]+)"\}', facts)}
        blocks = {name: tuple(map(int, (side,top,bottom))) for side,top,bottom,name in
                  re.findall(r'\{\d+, \d+, (\d+), (\d+), (\d+), "([^"]+)"\}', facts)}
        for asset in plan['textures']:
            target = 'assets/terrain.png' if asset['target'] == 'terrain' else 'assets/gui.png'
            evidence = next(s for s in entries[target]['sources'] if s.get('source_path') == asset['path'])
            self.assertEqual(evidence['source_sha256'], asset['sha256'])
            self.assertEqual(evidence['author'], asset['author'])
            self.assertEqual(evidence['license'], 'CC0-1.0')
            if 'item' in asset:
                self.assertEqual(items[asset['item']], atlas['afcms_tiles'][asset['path']])
        self.assertEqual(blocks['tnt'], tuple(atlas['afcms_tiles']['NODES/default_tnt_'+face+'.png']
                                             for face in ('side','top','bottom')))
        self.assertEqual(blocks['enchanting_table'][1], atlas['afcms_tiles']['ITEMS/mcl_enchanting_table_top.png'])
        # Compatibility cells must remain available to the upstream renderer.
        self.assertEqual([atlas['classic_slots'][str(i)] for i in (8,9,10)], ['tnt_side','tnt_top','tnt_bottom'])
        slots = list(atlas['tiles'].values()) + list(atlas['afcms_tiles'].values())
        self.assertEqual(len(slots), len(set(slots)))
        self.assertTrue(all(256 <= s < 512 for s in slots))
        for name,entry in entries.items():
            if name.startswith('assets/audio/'):
                continue
            self.assertIn(entry['license'], ('CC0-1.0', 'LicenseRef-Public-Domain'))
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(), entry['sha256'])
        self.assertEqual(struct.unpack('>II', (ROOT/'assets/terrain.png').read_bytes()[16:24]), (512,1024))

    def test_embedded_art_is_replaced_in_staging(self):
        original = (ROOT/'engine/src/SystemFonts.c').read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            destination = pathlib.Path(tmp)
            for name in ('src/SystemFonts.c', 'misc/makefiles/windows.mk'):
                path = destination/name
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT/'engine'/name, path)
            (destination/'misc/x11').mkdir()
            prepare.stage_native_assets(destination)
            for source,target in [('LapisCube.ico','misc/CCicon.ico'),
                                  ('LapisIcon_X11.h','misc/x11/CCIcon_X11.h'), ('LapisFont.h','src/LapisFont.h')]:
                self.assertEqual((ROOT/'assets/native'/source).read_bytes(), (destination/target).read_bytes())
            staged = (destination/'src/SystemFonts.c').read_text()
            self.assertNotIn("Goodly's texture pack", staged)
            self.assertIn('#include "LapisFont.h"', staged)
            self.assertIn('misc/windows/CCicon.rc misc/CCicon.ico', (destination/'misc/makefiles/windows.mk').read_text())
        self.assertEqual((ROOT/'engine/src/SystemFonts.c').read_bytes(), original)
        glyphs = re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){8})\}', (ROOT/'assets/native/LapisFont.h').read_text())
        self.assertEqual(len(glyphs), 94)
        self.assertEqual([int(x,16) for x in glyphs[ord('A')-33].split(',')], [12,30,51,51,63,51,51,0])


if __name__ == '__main__':
    unittest.main()
