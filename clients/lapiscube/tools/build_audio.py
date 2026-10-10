#!/usr/bin/env python3
"""Convert pinned CC0 recordings. Ordinary builds use the committed WAVs.

Requires ffmpeg only when regenerating audio. See docs/assets.md for inputs.
"""
import argparse
import hashlib
import io
import json
import pathlib
import struct
import subprocess
import wave
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
LICENSE_URL = 'https://creativecommons.org/publicdomain/zero/1.0/'


def convert(data, rate, max_seconds, peak):
    result = subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-i', 'pipe:0',
        '-fflags', '+bitexact', '-flags:a', '+bitexact', '-ac', '1', '-ar', str(rate),
        '-f', 's16le', 'pipe:1'], input=data, capture_output=True, check=True)
    samples = list(struct.unpack('<' + 'h' * (len(result.stdout) // 2), result.stdout))
    if not samples or not max(abs(s) for s in samples):
        raise ValueError('Empty or silent source recording')
    threshold = max(32, int(max(abs(s) for s in samples) * .005))
    audible = [i for i, s in enumerate(samples) if abs(s) >= threshold]
    start = max(0, audible[0] - rate // 200)
    end = min(len(samples), audible[-1] + rate // 40, start + int(max_seconds * rate))
    samples = samples[start:end]
    # Limit boosting to 6 dB; leave quieter performances quieter.
    gain = min(2.0, peak * 32767 / max(abs(s) for s in samples))
    fade_in, fade_out = max(1, rate // 200), max(1, rate // 40)
    samples = [int(s * gain * min(1, i / fade_in, (len(samples) - 1 - i) / fade_out))
               for i, s in enumerate(samples)]
    output = io.BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(struct.pack('<' + 'h' * len(samples), *samples))
    return output.getvalue()


def build(kenney_path, minetest_path):
    plan = json.loads((ROOT / 'assets/audio-sources.json').read_text())
    sources = plan['sources']
    if hashlib.sha256(kenney_path.read_bytes()).hexdigest() != sources['kenney']['sha256']:
        raise ValueError('Unexpected Kenney Impact Sounds archive hash')
    revision = subprocess.check_output(['git', '-C', str(minetest_path), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != sources['minetest']['revision']:
        raise ValueError('Unexpected Minetest Game revision')
    archive = zipfile.ZipFile(kenney_path)
    if archive.read('License.txt') != (ROOT / sources['kenney']['license_file']).read_bytes():
        raise ValueError('Kenney license evidence differs from the pinned source')
    for mod, name in [('default', 'default'), ('env_sounds', 'env-sounds'), ('doors', 'doors')]:
        if (minetest_path / f'mods/{mod}/README.md').read_bytes() != (ROOT / f'assets/licenses/Minetest-{name}-README.md').read_bytes():
            raise ValueError('Minetest license evidence differs from the pinned source')
    outputs, entries = {}, []
    for clip in plan['clips']:
        if clip['license'] != 'CC0-1.0':
            raise ValueError('Only reviewed CC0 recordings are admitted')
        kenney = clip['source'] == 'kenney'
        data = archive.read(clip['path']) if kenney else (minetest_path / clip['path']).read_bytes()
        if hashlib.sha256(data).hexdigest() != clip['source_sha256']:
            raise ValueError('Source recording hash mismatch: ' + clip['path'])
        wav = convert(data, plan['sample_rate'], clip['max_seconds'], clip['peak'])
        destination = 'assets/audio/' + clip['name'] + '.wav'
        outputs[destination] = wav
        source_url = sources['kenney']['url'] if kenney else sources['minetest']['url'] + '/blob/' + revision + '/' + clip['path']
        license_file = sources['kenney']['license_file'] if kenney else 'assets/licenses/Minetest-' + clip['path'].split('/')[1].replace('_', '-') + '-README.md'
        entries.append(dict(source_url=source_url, source_path=clip['path'],
            source_sha256=clip['source_sha256'], original_source_url=clip['original_url'],
            author=clip['author'], license='CC0-1.0', license_url=LICENSE_URL,
            license_file=license_file,
            modifications=f"Decoded to mono 16-bit PCM at {plan['sample_rate']} Hz; silence trimmed; limited to {clip['max_seconds']} s; peak ceiling {clip['peak']}, gain boost at most 6 dB; 5 ms attack/25 ms release fades",
            destination_filename=destination, sha256=hashlib.sha256(wav).hexdigest(),
            attribution=clip['author'], redistribution_notes='CC0; attribution appreciated, not required'))
    archive.close()
    manifest_path = ROOT / 'assets/manifest.json'
    manifest = json.loads(manifest_path.read_text())
    previous = [a['destination_filename'] for a in manifest['assets'] if a['destination_filename'].startswith('assets/audio/')]
    for path, data in outputs.items():
        (ROOT / path).write_bytes(data)
    # Retire only old manifest-owned files, never arbitrary user files.
    for path in set(previous) - outputs.keys():
        (ROOT / path).unlink(missing_ok=True)
    manifest['assets'] = [a for a in manifest['assets'] if not a['destination_filename'].startswith('assets/audio/')] + entries
    manifest['status'] = 'CC0 Kenney atlas, original UI/models and recorded gameplay audio; original pitched note synthesis'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    sound_map = {k: plan[k] for k in ('sample_rate', 'material', 'events', 'notes')}
    sound_map.update(license='CC0-1.0', implementation='src/LapisAudio.c',
        limits=['Animal/monster species voices are not included; unsupported ambient events are silent',
                'Door closing, eating, explosions and percussion use documented foley substitutes',
                'Harp, bass and bell/chime note-block tones remain original pitched synthesis'])
    (ROOT / 'assets/sound-map.json').write_text(json.dumps(sound_map, indent=2) + '\n')
    print(f'Converted {len(outputs)} verified CC0 recordings; updated manifest and sound map')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kenney', type=pathlib.Path, required=True, help='Pinned Kenney Impact Sounds ZIP')
    parser.add_argument('--minetest', type=pathlib.Path, required=True, help='Pinned Minetest Game checkout')
    args = parser.parse_args()
    build(args.kenney, args.minetest)
