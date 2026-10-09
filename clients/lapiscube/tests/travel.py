#!/usr/bin/env python3
"""Real teleports across signed boundaries/Far Lands and eight mob packet types.
Uses an isolated saved platform and an ephemeral, ordinarily authenticated admin.
"""
import os
import pathlib
import secrets
import socket
import subprocess
import tempfile
import time
from survival import identity_for

CLIENT = pathlib.Path(__file__).resolve().parents[1]
BUILD = CLIENT/'build'
ROOT = CLIENT.parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='travel-acceptance-', dir=BUILD) as cwd:
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        subprocess.run([str(BUILD/'survival-fixture'), identity_for('SurvivalCube'), identity_for('CombatCube')], cwd=cwd, check=True)
        pathlib.Path(cwd, 'server.txt').write_text(f'port={port}\nseed=42\ngamemode=survival\n')
        token = secrets.token_hex(24)
        with (BUILD/'travel-server.log').open('w') as log:
            server = subprocess.Popen([str(ROOT/'lapis-obsidian')], cwd=cwd,
                env={**os.environ, 'LAPIS_ADMIN_TOKEN': token}, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+15
                while True:
                    try:
                        with socket.create_connection(('127.0.0.1', port), .1):
                            break
                    except OSError:
                        if server.poll() is not None or time.monotonic()>deadline:
                            raise RuntimeError('Server startup failed')
                        time.sleep(.05)
                run = subprocess.run([str(BUILD/'lapiscube-probe'), 'travel', '127.0.0.1', str(port), 'SurvivalCube'],
                    env={**os.environ, 'LAPIS_TEST_ADMIN': token}, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=190)
                (BUILD/'travel-client.log').write_text(run.stdout)
                print(run.stdout, flush=True)
                assert run.returncode == 0 and 'travel_stage=8' in run.stdout
                assert server.poll() is None
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill();server.wait()


if __name__ == '__main__':
    main()
