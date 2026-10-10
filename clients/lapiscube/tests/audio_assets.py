#!/usr/bin/env python3
"""Check shipped recording provenance, PCM safety and native/web variant parity."""
import hashlib
import importlib.util
import json
import pathlib
import re
import struct
import tempfile
import unittest
import wave
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/f'{name}.py')
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class AudioAssets(unittest.TestCase):
    def test_recordings_and_provenance(self):
        mapping = json.loads((ROOT/'assets/sound-map.json').read_text())
        manifest = json.loads((ROOT/'assets/manifest.json').read_text())
        entries = {a['destination_filename']: a for a in manifest['assets']}
        expected = {n for section in ('material', 'events') for clips in mapping[section].values() for n in clips}
        actual = {p.stem for p in (ROOT/'assets/audio').glob('*.wav')}
        self.assertEqual(actual, expected)
        self.assertEqual(set(entries), {str(p.relative_to(ROOT)) for p in (ROOT/'assets').rglob('*') if p.suffix in ('.wav', '.png', '.svg')})
        for name in expected:
            path = ROOT/f'assets/audio/{name}.wav'
            entry = entries[str(path.relative_to(ROOT))]
            self.assertEqual(entry['license'], 'CC0-1.0')
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry['sha256'])
            self.assertRegex(entry['source_sha256'], '^[a-f0-9]{64}$')
            self.assertTrue((ROOT/entry['license_file']).is_file())
            with wave.open(str(path)) as wav:
                self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (1, 2, 22050))
                raw = wav.readframes(wav.getnframes())
            pcm = struct.unpack('<'+'h'*(len(raw)//2), raw)
            self.assertGreater(len(pcm), 100)
            self.assertLessEqual(len(pcm), 22050)
            self.assertGreater(max(abs(s) for s in pcm), 100)
            self.assertLess(max(abs(s) for s in pcm), 32767)
            self.assertEqual((pcm[0], pcm[-1]), (0, 0))
        for clips in mapping['material'].values():
            self.assertEqual(len(clips), len({entries[f'assets/audio/{n}.wav']['sha256'] for n in clips}))

    def test_packaging_matches_web_sound_list(self):
        mapping = json.loads((ROOT/'assets/sound-map.json').read_text())
        names = {n for clips in mapping['material'].values() for n in clips}
        with tempfile.TemporaryDirectory() as tmp:
            destination = pathlib.Path(tmp)
            module('package').stage(destination)
            module('embed_audio').generate(destination)
            with zipfile.ZipFile(destination/'audio/default.zip') as pack:
                self.assertEqual(set(pack.namelist()), {n+'.wav' for n in names})
                for name in names:
                    self.assertEqual(pack.read(name+'.wav'), (ROOT/f'assets/audio/{name}.wav').read_bytes())
            web_names = set(re.findall(r'"([a-z_]+\d+)"', (destination/'LapisMaterialSounds.h').read_text()))
            self.assertEqual(web_names, names)
            self.assertTrue(any(n.startswith('step_metal') for n in names))
            self.assertTrue(any(n.startswith('step_glass') for n in names))


if __name__ == '__main__':
    unittest.main()
