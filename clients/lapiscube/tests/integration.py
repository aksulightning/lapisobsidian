#!/usr/bin/env python3
"""Real TCP tests against the unchanged Lapis server, in an isolated save directory."""
import json
import pathlib
import socket
import subprocess
import tempfile
import time

CLIENT = pathlib.Path(__file__).resolve().parents[1]
ROOT = CLIENT.parents[1]
BUILD = CLIENT / 'build'


def main():
    server = ROOT / 'lapis-obsidian'
    if not server.exists():
        subprocess.run(['bash', 'build.sh'], cwd=ROOT, check=True)
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    outcomes = []
    with tempfile.TemporaryDirectory(prefix='lapiscube-server-', dir=BUILD) as cwd:
        pathlib.Path(cwd, 'server.txt').write_text(f'port={port}\nseed=42\ngamemode=survival\n')
        with (BUILD / 'integration-server.log').open('w') as log:
            process = subprocess.Popen([str(server)], cwd=cwd, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while True:
                    if process.poll() is not None:
                        raise RuntimeError('Server exited before accepting TCP')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.1):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise RuntimeError('Server startup timed out')
                        time.sleep(.05)
                # Two independent joins with the SAME UUID exercise release of the player slot.
                for label, mode in [('status', 'status'), ('login', 'login'), ('reconnect', 'login'), ('interaction', 'exercise')]:
                    run = subprocess.run([str(BUILD / 'lapiscube-probe'), mode, '127.0.0.1',
                                          str(port), 'LapisCubeTest'], text=True,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=50)
                    (BUILD / f'integration-{label}.log').write_text(run.stdout)
                    print(f'{label}: {run.stdout}', flush=True)
                    if run.returncode:
                        raise RuntimeError(f'{label} failed: {run.returncode}')
                    if mode == 'status':
                        info = json.loads(run.stdout.split('status=', 1)[1])
                        assert info['version']['protocol'] == 772
                    else:
                        assert 'registries=11 entries=68 tags=1 joined=1 loaded=1' in run.stdout
                        assert 'teleports=2' in run.stdout and 'queued=0' in run.stdout
                        assert 'decoded=25' in run.stdout
                        if mode == 'exercise':
                            assert 'exercise_stage=9' in run.stdout
                    outcomes.append({'case': label, 'returncode': run.returncode})
                    time.sleep(.15)
                assert process.poll() is None, 'Server crashed during integration'
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    (BUILD / 'integration-results.json').write_text(json.dumps(outcomes, indent=2) + '\n')


if __name__ == '__main__':
    main()
