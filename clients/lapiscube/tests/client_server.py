#!/usr/bin/env python3
"""Smoke-test the packaged launcher and the compile-time web-host opt-in."""
import http.client
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile

from web_transport import frame, port, receive, upgrade, wait_port


def check(package, default):
    package = pathlib.Path(package).resolve()
    default = pathlib.Path(default).resolve()
    for enabled in (False, True):
        with tempfile.TemporaryDirectory() as directory:
            tcp, web = port(), port()
            while tcp == web:
                web = port()
            pathlib.Path(directory, 'server.txt').write_text(f'port={tcp}\nseed=42\n')
            env = {key: value for key, value in os.environ.items() if not key.startswith('LAPIS_WEB_')}
            # A runtime flag cannot reverse the compile-time choice in either direction.
            env.update(LAPIS_WEB_PORT=str(web), LAPIS_ENABLE_WEBCLIENT='0' if enabled else '1')
            command = package / 'start.sh' if enabled else default
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen([str(command)], cwd=directory, env=env,
                                           stdin=subprocess.DEVNULL, stdout=log, stderr=log)
                try:
                    wait_port(tcp, process)
                    if not enabled:
                        try:
                            with socket.create_connection(('127.0.0.1', web), .2):
                                raise AssertionError('Default server enabled web hosting at runtime')
                        except OSError:
                            pass
                        continue
                    wait_port(web, process)
                    for path, name, mime in [('/', 'index.html', 'text/html'),
                                             ('/LapisCube.wasm', 'LapisCube.wasm', 'application/wasm'),
                                             ('/default.zip', 'default.zip', 'application/zip')]:
                        connection = http.client.HTTPConnection('127.0.0.1', web, timeout=10)
                        try:
                            connection.request('GET', path)
                            response = connection.getresponse()
                            assert response.status == 200, (path, response.status)
                            assert response.headers['Content-Type'].startswith(mime)
                            assert response.read() == (package / 'webclient' / name).read_bytes()
                        finally:
                            connection.close()
                    stream, response = upgrade(web, 'https://wrong.example')
                    with stream:
                        assert b'403 Forbidden' in response
                    stream, response = upgrade(web)
                    with stream:
                        assert b'101 Switching Protocols' in response
                        assert b's3pPLMBiTxaQ9kYGzzhZRbK+xOo=' in response
                        stream.sendall(frame(b'host-test', 9))
                        assert receive(stream) == (10, b'host-test')
                        status = b'\x10\x00\x84\x06\x09localhost' + struct.pack('!H', tcp) + b'\x01\x01\x00'
                        stream.sendall(frame(status))
                        opcode, data = receive(stream)
                        assert opcode == 2 and b'"protocol":772' in data
                    assert process.poll() is None
                except BaseException:
                    log.seek(0)
                    print(log.read().decode(errors='replace'), file=sys.stderr)
                    raise
                finally:
                    process.terminate()
                    process.wait(10)
    assert not (package / 'server.txt').exists(), 'Launcher wrote settings into the package'
    print('Client/server: default-off gate, external working directory, bundled HTTP assets and WebSocket status passed')


if __name__ == '__main__':
    check(*sys.argv[1:])
