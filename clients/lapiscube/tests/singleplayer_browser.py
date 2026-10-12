#!/usr/bin/env python3
"""Real standalone WASM acceptance. The only host is a static HTTP server."""
import functools
import http.server
import json
import os
import pathlib
import threading
from playwright.sync_api import sync_playwright
from web_browser import Wire

ROOT=pathlib.Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'build/singleplayer-evidence'


def main():
    EVIDENCE.mkdir(exist_ok=True)
    host=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(
        http.server.SimpleHTTPRequestHandler,directory=str(ROOT/'build/singleplayer')))
    threading.Thread(target=host.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{host.server_port}/'
    logs=[];errors=[];sockets=[];requests=[]
    try:
        with sync_playwright() as p:
            launch=dict(headless=True,args=['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader'])
            if os.environ.get('LAPIS_BROWSER_EXECUTABLE'):launch['executable_path']=os.environ['LAPIS_BROWSER_EXECUTABLE']
            browser=p.chromium.launch(**launch)
            context=browser.new_context(viewport={'width':1024,'height':640})
            page=context.new_page()
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
                page.goto(url);join()
                assert page.workers, 'Embedded server must run in a worker'
                assert any('world rendered and player loaded' in s for s in logs)
                page.screenshot(path=str(EVIDENCE/'empty-hand-and-hud.png'))
                # Empty selection uses the hand, and inventory opens with no server.
                page.keyboard.press('2');page.keyboard.press('b')
                page.wait_for_function('(Module._LapisWeb_State() & 2) !== 0')
                page.screenshot(path=str(EVIDENCE/'inventory.png'))
                page.keyboard.press('b')
                page.wait_for_function('(Module._LapisWeb_State() & 2) === 0')
                initial=wire().positions()[-1]
                page.keyboard.down('w');page.keyboard.down('Space');page.wait_for_timeout(900)
                page.keyboard.up('w');page.keyboard.up('Space');page.wait_for_timeout(1800)
                moved=wire().positions()[-1]
                assert abs(initial[0]-moved[0])+abs(initial[2]-moved[2])>.1,(initial,moved)
                assert wire().count(0x34)>0,'Selection must reach the embedded server'
                second=context.new_page();second.goto(url);second.click('#play')
                second.wait_for_function("document.getElementById('logmsg').textContent.includes('already open')")
                assert not second.workers,'Second tab must not start a competing server'
                second.close()
                page.evaluate('document.exitPointerLock()')
                page.click('#save-world')
                page.wait_for_function("document.getElementById('save-status').textContent==='Saved on this device.'",timeout=20000)
                saved=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.bin'))")
                metadata=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.meta'))")
                assert len(saved)>100000,'World and player data must be persisted'
                with page.expect_navigation():page.click('#quit-world')
                join()
                resumed=wire().positions()[-1]
                assert abs(resumed[0]-moved[0])+abs(resumed[2]-moved[2])<1,(moved,resumed)
                restored=page.workers[0].evaluate("Array.from(FS.readFile('/save/world.bin'))")
                # Disconnect legitimately updates player connection/flag fields.
                # Position round-trips above; world length and seed remain stable.
                assert len(restored)==len(saved)
                assert page.workers[0].evaluate("Array.from(FS.readFile('/save/world.meta'))")==metadata
                page.screenshot(path=str(EVIDENCE/'resumed-world.png'))
                page.set_viewport_size({'width':390,'height':320})
                page.wait_for_timeout(300)
                page.screenshot(path=str(EVIDENCE/'compact-hud.png'))
                assert not sockets,sockets
                assert not errors,errors
                assert all(r.startswith(url) for r in requests),requests
                (EVIDENCE/'results.json').write_text(json.dumps(dict(
                    loaded=True,movement=True,save_reload=True,second_tab_locked=True,
                    websockets=sockets,errors=errors,saved_bytes=len(saved)),indent=2)+'\n')
            finally:
                page.screenshot(path=str(EVIDENCE/'last.png'))
                (EVIDENCE/'browser.log').write_text('\n'.join(logs+errors)+'\n')
                browser.close()
    finally:
        host.shutdown();host.server_close()
    print('Standalone: real worker/world, input, IndexedDB reload, tab lock and zero WebSockets passed')


if __name__=='__main__':main()
