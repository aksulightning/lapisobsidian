#!/usr/bin/env python3
"""Exercise actual ClassiCube platform/socket/entrypoint integration through a PTY.
This tests the loading/connection path; it is not visual or gameplay validation.
"""
import fcntl
import os
import pathlib
import pty
import select
import signal
import shutil
import socket
import struct
import subprocess
import tempfile
import termios
import time

CLIENT = pathlib.Path(__file__).resolve().parents[1]
BUILD = CLIENT / 'build'
ROOT = CLIENT.parents[1]


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def launch(args, cwd):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 0, 0))
    pathlib.Path(cwd, 'options.txt').write_text('soundsvolume=0\nmusicvolume=0\nfpslimit=Limit30FPS\n')
    shutil.copytree(BUILD/'engine/texpacks', pathlib.Path(cwd)/'texpacks')
    process = subprocess.Popen([str(BUILD / 'engine' / 'LapisCube'), *args], cwd=cwd,
                               stdin=slave, stdout=slave, stderr=slave,
                               env={**os.environ, 'TERM': 'xterm-256color'})
    os.close(slave)
    return process, master


def collect(process, master, marker, timeout=20):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and process.poll() is None:
        if select.select([master], [], [], .05)[0]:
            try:
                part = os.read(master, 65536)
            except OSError:
                break
            if not part:
                break
            data.extend(part)
            if marker in data:
                return bytes(data)
    raise RuntimeError(f'Native client did not reach {marker!r}; tail={data[-2500:]!r}')


def main():
    with tempfile.TemporaryDirectory(prefix='native-smoke-', dir=BUILD) as work:
        work = pathlib.Path(work)
        server_dir = work / 'server'
        server_dir.mkdir()
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        (server_dir / 'server.txt').write_text(f'port={port}\nseed=42\n')
        with (BUILD / 'native-server.log').open('w') as log:
            server = subprocess.Popen([str(ROOT / 'lapis-obsidian')], cwd=server_dir,
                                      stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while True:
                    try:
                        with socket.create_connection(('127.0.0.1', port), .1):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise RuntimeError('Lapis startup timed out')
                        time.sleep(.05)
                run_dir = work / 'lapis-client'
                run_dir.mkdir()
                game, master = launch(['--lapis', 'NativeCube', '127.0.0.1', str(port)], run_dir)
                try:
                    output = collect(game, master, b'LapisCube native: world rendered and player loaded')
                    assert b'LapisCube native: Play login accepted' in output
                    (BUILD / 'native-lapis.log').write_bytes(output)
                finally:
                    stop(game)
                    os.close(master)
                print('native Lapis: Play login, decoded world, loaded notification and spawn synchronization passed')
            finally:
                stop(server)
        # Synthetic Classic server verifies that ordinary arguments still select Classic.
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(15)
            port = listener.getsockname()[1]
            run_dir = work / 'classic-client'
            run_dir.mkdir()
            game, master = launch(['ClassicTest', 'unused', '127.0.0.1', str(port)], run_dir)
            try:
                # Drain the terminal so a full PTY buffer cannot delay connection setup.
                deadline = time.monotonic() + 15
                while not select.select([listener], [], [], .02)[0]:
                    if select.select([master], [], [], 0)[0]:
                        os.read(master, 65536)
                    if time.monotonic() > deadline:
                        raise RuntimeError('Classic connect timed out')
                peer, _ = listener.accept()
                with peer:
                    peer.setblocking(False)
                    login = bytearray()
                    deadline = time.monotonic() + 5
                    while len(login) < 131:
                        ready, _, _ = select.select([peer, master], [], [], .05)
                        if master in ready:
                            os.read(master, 65536)
                        if time.monotonic() > deadline:
                            raise RuntimeError(f'Classic login timed out at {len(login)} bytes')
                        if peer not in ready:
                            continue
                        part = peer.recv(131 - len(login))
                        if not part:
                            raise RuntimeError('Classic login truncated')
                        login.extend(part)
                    assert login[0] == 0 and login[1] == 7
                    assert login[2:66].rstrip(b' ') == b'ClassicTest'
                    assert login[66:130].rstrip(b' ') == b'unused'
                    peer.setblocking(True)
                    peer.sendall(b'\x0e' + b'Classic smoke complete'.ljust(64, b' '))
                (BUILD / 'native-classic-login.hex').write_text(login.hex() + '\n')
            finally:
                stop(game)
                os.close(master)
            print('native Classic: original 131-byte Classic login passed')


if __name__ == '__main__':
    main()
