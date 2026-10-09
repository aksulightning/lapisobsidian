#!/usr/bin/env python3
"""Exercise the real opt-in server over RFC 6455; no protocol mock server."""
import base64
import hashlib
import http.client
import json
import os
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import time

CLIENT=pathlib.Path(__file__).resolve().parents[1]
ROOT=CLIENT.parents[1]
BUILD=CLIENT/'build'


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));return s.getsockname()[1]


def wait_port(number,process):
    for _ in range(300):
        if process.poll() is not None:raise AssertionError('Server exited during startup')
        try:
            with socket.create_connection(('127.0.0.1',number),.1):return
        except OSError:time.sleep(.05)
    raise AssertionError('Server startup timeout')


def exact(s,size):
    result=bytearray()
    while len(result)<size:
        chunk=s.recv(size-len(result))
        if not chunk:raise EOFError()
        result+=chunk
    return bytes(result)


def frame(data,opcode=2,fin=True,masked=True):
    size=len(data);header=bytes([(128 if fin else 0)|opcode])
    mask=128 if masked else 0
    if size<126:header+=bytes([mask|size])
    elif size<65536:header+=bytes([mask|126])+struct.pack('!H',size)
    else:header+=bytes([mask|127])+struct.pack('!Q',size)
    key=b'\x01\x72\x03\x44'
    return header+key+bytes(x^key[i%4] for i,x in enumerate(data)) if masked else header+data


def receive(s):
    h=exact(s,2);n=h[1]&127
    assert not h[1]&128,'Server frames must not be masked'
    if n==126:n=struct.unpack('!H',exact(s,2))[0]
    if n==127:n=struct.unpack('!Q',exact(s,8))[0]
    assert n<=4*1024*1024
    return h[0]&15,exact(s,n)


def upgrade(number,origin=None,extra=''):
    s=socket.create_connection(('127.0.0.1',number),5)
    origin=origin or f'http://127.0.0.1:{number}'
    key='dGhlIHNhbXBsZSBub25jZQ=='
    s.sendall((f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{number}\r\nOrigin: {origin}\r\n'
        'Upgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\n'
        f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Protocol: LapisCube\r\n{extra}\r\n').encode())
    response=bytearray()
    while not response.endswith(b'\r\n\r\n'):response+=exact(s,1)
    return s,bytes(response)


def proxy_probe(number,mode):
    """The existing C probe uses an isolated test-only byte bridge to WebSocket."""
    errors=[]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(1)
        def bridge():
            local,_=listener.accept()
            remote,response=upgrade(number)
            assert b'101 Switching' in response
            def upload():
                try:
                    while data:=local.recv(32768):remote.sendall(frame(data))
                except OSError:pass
                finally:
                    try:remote.shutdown(socket.SHUT_RDWR)
                    except OSError:pass
            thread=threading.Thread(target=upload);thread.start()
            try:
                while True:
                    op,data=receive(remote)
                    if op==2:local.sendall(data)
                    elif op==8:break
                    elif op==9:remote.sendall(frame(data,10))
            except (OSError,EOFError):pass
            finally:
                try:local.shutdown(socket.SHUT_RDWR)
                except OSError:pass
                local.close();remote.close();thread.join(5)
        thread=threading.Thread(target=bridge,daemon=True);thread.start()
        run=subprocess.run([str(BUILD/'lapiscube-probe'),mode,'127.0.0.1',str(listener.getsockname()[1]),'WebTransport'],
            text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=55)
        thread.join(5);assert not thread.is_alive()
    (BUILD/f'web-probe-{mode}.log').write_text(run.stdout)
    assert run.returncode==0,run.stdout
    if mode!='status':assert 'registries=11 entries=68 tags=1 joined=1 loaded=1' in run.stdout,run.stdout
    if mode=='exercise':assert 'exercise_stage=9' in run.stdout,run.stdout
    return run.stdout


def main():
    enabled=pathlib.Path(os.environ.get('LAPIS_WEB_TEST_SERVER',BUILD/'web-server')).resolve()
    default=ROOT/'lapis-obsidian'
    # A runtime flag must not activate the default binary.
    with tempfile.TemporaryDirectory(dir=BUILD) as directory:
        tcp,web=port(),port()
        pathlib.Path(directory,'server.txt').write_text(f'port={tcp}\nseed=42\n')
        env={**os.environ,'LAPIS_ENABLE_WEBCLIENT':'1','LAPIS_WEB_PORT':str(web),'LAPIS_WEB_ROOT':str(BUILD/'webclient')}
        with (BUILD/'web-default-run.log').open('w') as log:
            process=subprocess.Popen([str(default)],cwd=directory,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log)
            try:
                wait_port(tcp,process)
                try:s=socket.create_connection(('127.0.0.1',web),.2)
                except OSError:pass
                else:s.close();raise AssertionError('Default binary enabled web hosting at runtime')
            finally:process.terminate();process.wait(5)
    results=[]
    with tempfile.TemporaryDirectory(dir=BUILD) as directory:
        tcp,web=port(),port()
        pathlib.Path(directory,'server.txt').write_text(f'port={tcp}\nseed=42\ngamemode=survival\n')
        env={**os.environ,'LAPIS_WEB_PORT':str(web),'LAPIS_WEB_ROOT':str(BUILD/'webclient')}
        with (BUILD/'web-transport-server.log').open('w') as log:
            process=subprocess.Popen([str(enabled)],cwd=directory,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log)
            try:
                wait_port(web,process)
                for path,status,mime in [('/',200,'text/html'),('/LapisCube.wasm',200,'application/wasm'),
                    ('/default.zip',200,'application/zip'),('/../server.txt',400,None),('/%2e%2e/server.txt',400,None),
                    ('/missing.html',404,None),('/.git/config',400,None)]:
                    h=http.client.HTTPConnection('127.0.0.1',web,timeout=5);h.request('GET',path);r=h.getresponse()
                    assert r.status==status,(path,r.status)
                    data=r.read()
                    if mime:assert r.headers['Content-Type'].startswith(mime)
                    if path.endswith('.wasm'):assert data[:4]==b'\0asm'
                    h.close();results.append(path)
                s,r=upgrade(web,'https://wrong.example');assert b'403 Forbidden' in r;s.close()
                s,r=upgrade(web,extra='Sec-WebSocket-Version: 13\r\n');assert b'400 Bad Request' in r;s.close()
                s,r=upgrade(web);assert b's3pPLMBiTxaQ9kYGzzhZRbK+xOo=' in r,r
                s.sendall(frame(b'hello',9));assert receive(s)==(10,b'hello')
                # Status request split across multiple masked frames and TCP writes.
                handshake=b'\x10\x00\x84\x06\x09localhost'+struct.pack('!H',tcp)+b'\x01\x01\x00'
                s.sendall(frame(handshake[:4],2,False));s.sendall(frame(handshake[4:],0,True))
                op,payload=receive(s);assert op==2 and b'"protocol":772' in payload,payload
                s.close();results.append('fragmented status / RFC handshake / ping')
                for bad in [frame(b'\x01\x00',masked=False),frame(b'hi',1),frame(b'hi',0),frame(b'x',9,False),b'\x82\xff'+b'\xff'*8]:
                    s,r=upgrade(web);assert b'101' in r;s.sendall(bad)
                    assert s.recv(1)==b'',bad;s.close()
                results.append('malformed frames rejected')
                for mode in ('status','login','login','exercise'):
                    proxy_probe(web,mode);time.sleep(.15);results.append('websocket '+mode)
                # Native TCP still connects to the same enabled server.
                run=subprocess.run([str(BUILD/'lapiscube-probe'),'login','127.0.0.1',str(tcp),'NativeAlongside'],
                    text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=50)
                assert run.returncode==0,run.stdout;results.append('native TCP alongside web')
                assert process.poll() is None
            finally:process.terminate();process.wait(5)
    (BUILD/'web-transport-results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Web transport acceptance passed:',', '.join(results))


if __name__=='__main__':main()
