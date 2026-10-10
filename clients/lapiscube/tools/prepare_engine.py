#!/usr/bin/env python3
"""Stage the pinned engine and apply the small, reviewable integration patch."""
import pathlib
import shutil
import subprocess
import argparse

ROOT = pathlib.Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'engine'
DEST = ROOT / 'build' / 'engine'
PIN = 'd41c3f7eef2038f59702b58bdb373483fb0d28f9'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['native', 'terminal', 'windows', 'web'], default='native')
    mode = parser.parse_args().mode
    head = subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise SystemExit('Unexpected ClassiCube revision; update the integration patch explicitly')
    if subprocess.check_output(['git', '-C', str(UPSTREAM), 'status', '--porcelain'], text=True).strip():
        raise SystemExit('Engine submodule has local changes; keep modifications in the integration patch')
    stamp = DEST / '.lapiscube-build-mode'
    build_key = mode + '-release-v2'
    if not stamp.exists() or stamp.read_text() != build_key:
        shutil.rmtree(DEST / 'build', ignore_errors=True)
        for name in ('LapisCube', 'LapisCube.exe'):
            (DEST / name).unlink(missing_ok=True)
    # Overwrite only source inputs, retaining build objects for incremental recompilation.
    shutil.copytree(UPSTREAM, DEST, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'build', 'ClassiCube', 'LapisCube'))
    for path in (ROOT / 'src').glob('*.[ch]'):
        shutil.copy2(path, DEST / 'src' / path.name)
    subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT / 'patches' / 'engine.patch')],
                   cwd=DEST, check=True)
    subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT/'patches/audio.patch')],
                   cwd=DEST, check=True)
    subprocess.run(['python3', str(ROOT/'tools/embed_audio.py'), str(DEST/'src')], check=True)
    if mode == 'web':
        subprocess.run(['patch', '-p1', '--binary', '--forward', '-i', str(ROOT/'patches/web.patch')],
                       cwd=DEST, check=True)
    subprocess.run(['python3', str(ROOT/'tools/package.py'), '--stage-only'], check=True)
    stamp.write_text(build_key)


if __name__ == '__main__':
    main()
