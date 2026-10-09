#!/usr/bin/env python3
"""Exercise the unchanged server using a clearly labelled, isolated saved-game fixture."""
import hashlib
import pathlib
import socket
import subprocess
import tempfile
import time

CLIENT = pathlib.Path(__file__).resolve().parents[1]
BUILD = CLIENT/'build'
ROOT = CLIENT.parents[1]


def identity_for(name):
    identity = bytearray(hashlib.md5(('OfflinePlayer:'+name).encode()).digest())
    identity[6] = identity[6] & 15 | 48
    identity[8] = identity[8] & 63 | 128
    return identity.hex()


def main():
    victim=None
    with tempfile.TemporaryDirectory(prefix='survival-acceptance-', dir=BUILD) as cwd:
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1', 0))
            port = reserve.getsockname()[1]
        subprocess.run([str(BUILD/'survival-fixture'),identity_for('SurvivalCube'),identity_for('CombatCube')],cwd=cwd,check=True)
        pathlib.Path(cwd,'server.txt').write_text(f'port={port}\nseed=42\ngamemode=survival\n')
        with (BUILD/'survival-server.log').open('w') as log:
            server=subprocess.Popen([str(ROOT/'lapis-obsidian')],cwd=cwd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+15
                while True:
                    if server.poll() is not None:raise RuntimeError('Server rejected fixture: '+(BUILD/'survival-server.log').read_text())
                    try:
                        with socket.create_connection(('127.0.0.1',port),.1):break
                    except OSError:
                        if time.monotonic()>deadline:raise RuntimeError('Startup timeout')
                        time.sleep(.05)
                victim=subprocess.Popen([str(BUILD/'lapiscube-probe'),'combat','127.0.0.1',str(port),'CombatCube'],stdout=(BUILD/'combat-client.log').open('w'),stderr=subprocess.STDOUT)
                deadline=time.monotonic()+15
                while 'combat: ready' not in (BUILD/'combat-client.log').read_text():
                    if victim.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Combat client join failed')
                    time.sleep(.05)
                run=subprocess.run([str(BUILD/'lapiscube-probe'),'survival','127.0.0.1',str(port),'SurvivalCube'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=130)
                (BUILD/'survival-client.log').write_text(run.stdout)
                print(run.stdout)
                assert run.returncode==0 and 'exercise_stage=36' in run.stdout
                assert victim.wait(timeout=15)==0
                assert "death and respawn round trip passed" in (BUILD/"combat-client.log").read_text()
                print("combat: second real client confirmed lethal damage and respawn")
                assert server.poll() is None
            finally:
                if victim and victim.poll() is None:victim.terminate();victim.wait(timeout=5)
                server.terminate()
                try:server.wait(timeout=5)
                except subprocess.TimeoutExpired:server.kill();server.wait()


if __name__=='__main__':main()
