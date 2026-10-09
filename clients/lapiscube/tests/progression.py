#!/usr/bin/env python3
"""Fresh seed-42 acceptance: no saved supplies, admin commands or server edits."""
import os
import pathlib
import socket
import subprocess
import tempfile
import time

CLIENT = pathlib.Path(__file__).resolve().parents[1]
BUILD = CLIENT/'build'
ROOT = CLIENT.parents[1]


def main():
    if not (ROOT/'lapis-obsidian').exists():
        subprocess.run(['bash', 'build.sh'], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix='fresh-progression-', dir=BUILD) as cwd:
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        pathlib.Path(cwd, 'server.txt').write_text(f'port={port}\nseed=42\ngamemode=survival\n')
        assert not pathlib.Path(cwd, 'world.bin').exists()
        env = dict(os.environ)
        env.pop('LAPIS_ADMIN_TOKEN', None)
        with (BUILD/'progression-server.log').open('w') as log:
            server = subprocess.Popen([str(ROOT/'lapis-obsidian')], cwd=cwd, env=env,
                                      stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+15
                while True:
                    if server.poll() is not None:
                        raise RuntimeError('Fresh server exited: '+(BUILD/'progression-server.log').read_text())
                    try:
                        with socket.create_connection(('127.0.0.1', port), .1):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise RuntimeError('Fresh server startup timed out')
                        time.sleep(.05)
                for mode in ('progression', 'resume'):
                    run = subprocess.run([str(BUILD/'lapiscube-probe'), mode, '127.0.0.1', str(port), 'FreshCube'],
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=190)
                    (BUILD/f'progression-{mode}.log').write_text(run.stdout)
                    print(run.stdout, flush=True)
                    assert run.returncode == 0 and 'progression_stage=30' in run.stdout
                    time.sleep(.2)
                assert server.poll() is None
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    main()
