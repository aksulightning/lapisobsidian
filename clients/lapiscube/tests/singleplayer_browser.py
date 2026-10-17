#!/usr/bin/env python3
"""Run the real single HTML file offline, both via HTTP and file://."""
import argparse
import functools
import http.server
import json
import math
import zlib
import os
import pathlib
import struct
import shutil
import tempfile
import threading
from playwright.sync_api import sync_playwright
from web_browser import Wire

ROOT=pathlib.Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'build/singleplayer-evidence'


def main(mode):
    evidence=EVIDENCE/mode
    evidence.mkdir(parents=True,exist_ok=True)
    # There must be no adjacent JS, WASM, texture, sound or license dependencies.
    isolated=tempfile.TemporaryDirectory(prefix='single-html-',dir=ROOT/'build')
    document=pathlib.Path(isolated.name)/'LapisCube-singleplayer.html'
    shutil.copy2(ROOT/'build/singleplayer'/document.name,document)
    host=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(
        http.server.SimpleHTTPRequestHandler,directory=isolated.name))
    threading.Thread(target=host.serve_forever,daemon=True).start()
    url=document.as_uri() if mode=='file' else f'http://127.0.0.1:{host.server_port}/{document.name}'
    logs=[];errors=[];sockets=[];requests=[]
    try:
        with sync_playwright() as p:
            launch=dict(headless=True,args=['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader'])
            if os.environ.get('LAPIS_BROWSER_EXECUTABLE'):launch['executable_path']=os.environ['LAPIS_BROWSER_EXECUTABLE']
            browser=p.chromium.launch(**launch)
            context=browser.new_context(viewport={'width':1024,'height':640})
            if mode=='file':context.set_offline(True)
            page=context.new_page()
            page.on('dialog',lambda dialog:dialog.dismiss())
            page.on('console',lambda e:logs.append(e.text))
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.on('websocket',lambda s:sockets.append(s.url))
            context.on('request',lambda r:requests.append(r.url))
            def join():
                page.fill('#username','SoloCube')
                page.evaluate('''() => {
                    window.sent=[];
                    const socket=LapisSingleplayer.socket.bind(LapisSingleplayer);
                    LapisSingleplayer.socket=()=>{
                        const stream=socket(),send=stream.send.bind(stream);
                        stream.send=data=>{sent.push(Array.from(data));send(data);};
                        return stream;
                    };
                }''')
                page.click('#play')
                page.wait_for_function('window.Module && Module._LapisWeb_State && (Module._LapisWeb_State() & 1)',timeout=90000)
                page.wait_for_timeout(2000)
                assert page.locator('#gate').is_hidden()
            def wire():
                result=Wire()
                for chunk in page.evaluate('sent'):result.feed(bytes(chunk))
                return result
            try:
                page.goto(url)
                context.set_offline(True)
                page.click('#show-credits')
                assert page.locator('#embedded-credits').is_visible()
                assert 'GNU GENERAL PUBLIC LICENSE' in page.locator('#embedded-credits').text_content()
                with page.expect_download() as download:
                    page.click('#download-notices')
                assert download.value.suggested_filename=='LapisCube-notices.zip'
                assert download.value.failure() is None
                page.click('#close-credits')
                join()
                assert page.workers, 'Embedded server must run in a worker'
                assert page.workers[0].url.startswith('blob:'),page.workers[0].url
                assert any('world rendered and player loaded' in s for s in logs)
                # Decode a material sound through the compiled engine interop.
                page.evaluate("_interop_AudioDownload('dig_stone1')")
                page.wait_for_function("window.AUDIO && AUDIO.buffers.dig_stone1 && AUDIO.buffers.dig_stone1.length > 0")
                page.screenshot(path=str(evidence/'empty-hand-and-hud.png'))
                # Presentation fixtures pass through the real client decoder.
                # They never mutate the worker's authoritative inventory/health.
                def inject(payload):
                    assert len(payload)<128
                    page.evaluate('(data)=>LapisSingleplayer.stream.onmessage({data:new Uint8Array(data).buffer})',list(bytes([len(payload)])+payload))
                inject(b'\x61'+struct.pack('!fBf',11.0,7,0.0))
                inject(bytes([0x14,0,0,0,36,1,0x81,0x07,0,0])) # diamond pickaxe (897)
                page.wait_for_timeout(300)
                page.screenshot(path=str(evidence/'held-tool-and-partial-vitals.png'))
                inject(bytes([0x14,0,0,0,36,0]))
                inject(b'\x61'+struct.pack('!fBf',20.0,20,0.0))
                page.wait_for_timeout(300)
                page.screenshot(path=str(evidence/'returned-empty-hand.png'))
                # Empty selection uses the hand, and inventory opens with no server.
                page.keyboard.press('2');page.keyboard.press('b')
                page.wait_for_function('(Module._LapisWeb_State() & 2) !== 0')
                page.screenshot(path=str(evidence/'inventory.png'))
                page.keyboard.press('b')
                page.wait_for_function('(Module._LapisWeb_State() & 2) === 0')
                initial=wire().positions()[-1]
                page.keyboard.down('w');page.keyboard.down('Space');page.wait_for_timeout(900)
                page.keyboard.up('w');page.keyboard.up('Space');page.wait_for_timeout(1800)
                moved=wire().positions()[-1]
                assert abs(initial[0]-moved[0])+abs(initial[2]-moved[2])>.1,(initial,moved)
                assert wire().count(0x34)>0,'Selection must reach the embedded server'
                second=context.new_page()
                if mode=='http':context.set_offline(False)
                second.goto(url)
                context.set_offline(True)
                second.click('#play')
                second.wait_for_function("document.getElementById('logmsg').textContent.includes('already open')")
                assert not second.workers,'Second tab must not start a competing server'
                second.close()
                persistent_drop=None
                def drop_records():
                    raw=bytes(page.workers[0].evaluate("FS.analyzePath('/save/drops.bin').exists ? Array.from(FS.readFile('/save/drops.bin')) : []"))
                    if not raw:return []
                    assert raw[:8]==b'LAPDROP1' and zlib.crc32(raw[:-4])==struct.unpack_from('<I',raw,len(raw)-4)[0]
                    count=struct.unpack_from('<I',raw,20)[0]
                    assert len(raw)==36+count*32
                    return [struct.unpack_from('<QffhhHBBII',raw,32+i*32) for i in range(count)]
                if os.environ.get('LAPIS_ENABLE_EXPERIMENTAL_DROPSYSTEM')=='1':
                    # Mine real nearby blocks through protocol 772; the server
                    # owns the loot. Keep it beyond pickup range for the reload.
                    x,y,z=(math.floor(v) for v in moved)
                    for dx,dz in ((3,0),(-3,0),(0,3),(0,-3)):
                        for dy in range(-4,3):
                            by=y+dy
                            if not 0<=by<=255:continue
                            packed=(((x+dx)&0x3ffffff)<<38)|(((z+dz)&0x3ffffff)<<12)|(by&0xfff)
                            payload=bytes([0x28,2])+struct.pack('!Q',packed)+bytes([1,0])
                            page.evaluate('(data)=>LapisSingleplayer.stream.send(new Uint8Array(data))',list(bytes([len(payload)])+payload))
                        page.wait_for_timeout(500)
                        records=drop_records()
                        if records:
                            persistent_drop=records[0]
                            break
                    assert persistent_drop,'Mining must produce an authoritative persistent drop'
                else:
                    assert not drop_records(),'Default singleplayer must remain transient'
                page.evaluate('document.exitPointerLock()')
                page.click('#save-world')
                page.wait_for_function("document.getElementById('save-status').textContent==='Saved on this device.'",timeout=20000)
                saved=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.bin'))")
                metadata=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.meta'))")
                assert len(saved)>100000,'World and player data must be persisted'
                # Only the top-level HTML is reloaded; gameplay stays offline.
                if mode=='http':context.set_offline(False)
                with page.expect_navigation():page.click('#quit-world')
                context.set_offline(True)
                join()
                resumed=wire().positions()[-1]
                assert abs(resumed[0]-moved[0])+abs(resumed[2]-moved[2])<1,(moved,resumed)
                restored=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.bin'))")
                # Disconnect legitimately updates player connection/flag fields.
                # Position round-trips above; world length and seed remain stable.
                assert len(restored)==len(saved)
                assert page.workers[0].evaluate("Array.from(FS.readFile('/save/world.meta'))")==metadata
                if persistent_drop:
                    records=drop_records()
                    restored_drop=next((r for r in records if r[0]==persistent_drop[0]),None)
                    assert restored_drop and restored_drop[5:7]==persistent_drop[5:7],(persistent_drop,records)
                    assert restored_drop[8]>=persistent_drop[8],'Reload must retain elapsed lifetime'
                page.screenshot(path=str(evidence/'resumed-world.png'))
                page.set_viewport_size({'width':390,'height':320})
                page.wait_for_timeout(300)
                page.screenshot(path=str(evidence/'compact-hud.png'))
                assert not sockets,sockets
                assert not errors,errors
                assert all(r==url or r.startswith(('blob:','data:')) for r in requests),requests
                (evidence/'results.json').write_text(json.dumps(dict(
                    single_file=True,offline=True,mode=mode,embedded_sound=True,embedded_notices=True,
                    loaded=True,movement=True,save_reload=True,second_tab_locked=True,persistent_drop_reload=bool(persistent_drop),
                    websockets=sockets,errors=errors,saved_bytes=len(saved)),indent=2)+'\n')
            finally:
                (evidence/'browser.log').write_text('\n'.join(logs+errors)+'\n')
                (evidence/'requests.json').write_text(json.dumps(requests,indent=2)+'\n')
                try:page.screenshot(path=str(evidence/'last.png'),timeout=5000)
                finally:browser.close()
    finally:
        host.shutdown();host.server_close()
        isolated.cleanup()
    print(f'Single HTML ({mode}): offline worker/world, sound, notices, input, IndexedDB reload, tab lock and zero WebSockets passed')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--mode',choices=('http','file','both'),default='both')
    mode=parser.parse_args().mode
    for choice in ('http','file') if mode=='both' else (mode,):main(choice)
