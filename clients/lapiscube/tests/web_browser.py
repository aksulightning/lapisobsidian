#!/usr/bin/env python3
"""Real WASM/WebGL browser acceptance against the optional server.
Playwright's touch emulation covers input/gating, not physical device certification.
"""
import json
import base64
import os
import pathlib
import secrets
import struct
import subprocess
import tempfile
import time
from playwright.sync_api import sync_playwright
from web_transport import BUILD, port, wait_port
from survival import identity_for

EVIDENCE=BUILD/'web-evidence'


class Wire:
    def __init__(self):self.pending=bytearray();self.packets=[]
    def feed(self,data):
        if isinstance(data,str):return
        self.pending+=data
        while self.pending:
            value=0;prefix=0
            for i,b in enumerate(self.pending[:5]):
                value|=(b&127)<<(7*i);prefix+=1
                if not b&128:break
            else:return
            if len(self.pending)<prefix+value:return
            packet=bytes(self.pending[prefix:prefix+value]);del self.pending[:prefix+value]
            self.packets.append(packet)
    def count(self,packet):return sum(bool(p) and p[0]==packet for p in self.packets)
    def positions(self):return [struct.unpack('!dddffB',p[1:]) for p in self.packets if len(p)==34 and p[0]==0x1e]


def main():
    EVIDENCE.mkdir(exist_ok=True)
    web,tcp=port(),port();token=secrets.token_hex(24)
    with tempfile.TemporaryDirectory(dir=BUILD) as directory,(EVIDENCE/'server.log').open('w') as log:
        subprocess.run([str(BUILD/'survival-fixture'),identity_for('SurvivalCube'),identity_for('CombatCube')],cwd=directory,check=True)
        pathlib.Path(directory,'server.txt').write_text(f'port={tcp}\nseed=42\ngamemode=survival\n')
        env={**os.environ,'LAPIS_WEB_PORT':str(web),'LAPIS_WEB_ROOT':str(BUILD/'webclient'),'LAPIS_ADMIN_TOKEN':token}
        server=subprocess.Popen([str(pathlib.Path(os.environ.get('LAPIS_WEB_TEST_SERVER',BUILD/'web-server')).resolve())],
            cwd=directory,env=env,stdin=subprocess.PIPE,stdout=log,stderr=log)
        try:
            wait_port(web,server)
            with sync_playwright() as playwright:
                launch={'headless':True,'args':['--no-sandbox','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']}
                if os.environ.get('LAPIS_BROWSER_EXECUTABLE'):launch['executable_path']=os.environ['LAPIS_BROWSER_EXECUTABLE']
                browser=playwright.chromium.launch(**launch)
                outcomes=[]
                for mobile in (False,True):
                    label='touch' if mobile else 'desktop'
                    context=browser.new_context(viewport={'width':390 if mobile else 1024,'height':844 if mobile else 640},
                        is_mobile=mobile,has_touch=mobile,device_scale_factor=1)
                    page=context.new_page();messages=[];errors=[];outbound=Wire();inbound=Wire();requests=[]
                    page.on('console',lambda event:messages.append(event.text))
                    page.on('pageerror',lambda error:errors.append(str(error)))
                    page.on('request',lambda request:requests.append(request.url))
                    page.on('dialog',lambda dialog:dialog.dismiss())
                    def socket(ws):
                        ws.on('framesent',outbound.feed);ws.on('framereceived',inbound.feed)
                    page.on('websocket',socket)
                    def await_turn(yaw):
                        deadline=time.monotonic()+5
                        while abs(outbound.positions()[-1][3]-yaw)<=1 and time.monotonic()<deadline:
                            page.wait_for_timeout(100)
                        return abs(outbound.positions()[-1][3]-yaw)>1
                    try:
                        page.goto(f'http://127.0.0.1:{web}/')
                        if mobile:
                            assert page.locator('#instruction').inner_text().startswith('Rotate')
                            page.screenshot(path=str(EVIDENCE/'touch-portrait-gate.png'))
                            assert page.evaluate('typeof Module')=='undefined'
                            page.set_viewport_size({'width':844,'height':390})
                        page.fill('#username','SurvivalCube');page.click('#play')
                        page.wait_for_function('window.Module && Module._LapisWeb_State && (Module._LapisWeb_State() & 1)',timeout=45000)
                        page.wait_for_timeout(1500)
                        assert any('world rendered and player loaded' in m for m in messages),messages
                        assert page.locator('#gate').is_hidden(),page.locator('#instruction').inner_text()
                        if mobile:assert page.evaluate('!!document.fullscreenElement'),'Real fullscreen must be active'
                        page.screenshot(path=str(EVIDENCE/f'{label}-world.png'))
                        before=outbound.positions()
                        if mobile:
                            cdp=context.new_cdp_session(page)
                            def point(selector):
                                box=page.locator(selector).bounding_box();return {'x':box['x']+box['width']/2,'y':box['y']+box['height']/2}
                            start=point('#walk [data-action="0"]');jump=point('#actions [data-action="4"]')
                            cdp.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[{**start,'id':1},{**jump,'id':2}]})
                            page.wait_for_timeout(450)
                            cdp.send('Input.dispatchTouchEvent',{'type':'touchEnd','touchPoints':[]})
                            page.wait_for_timeout(700)
                            look={'x':410,'y':140,'id':4}
                            yaw=outbound.positions()[-1][3]
                            page.evaluate('''() => {
                              window.lookTrace=[];
                              for(const type of ['pointerdown','pointermove','pointerup','pointercancel'])
                                canvas.addEventListener(type,e=>lookTrace.push([type,e.offsetX,e.offsetY,e.pointerType,menu,blocked,pointers.size]));
                              const look=Module._LapisWeb_Look;
                              Module._LapisWeb_Look=(x,y)=>{lookTrace.push(['C look',x,y]);look(x,y);};
                            }''')
                            cdp.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[look]})
                            page.wait_for_timeout(80)
                            for i in range(1,5):
                                cdp.send('Input.dispatchTouchEvent',{'type':'touchMove','touchPoints':[{**look,'x':410+i*15,'y':140+i*5}]})
                                page.wait_for_timeout(100)
                            cdp.send('Input.dispatchTouchEvent',{'type':'touchEnd','touchPoints':[]})
                            page.wait_for_timeout(300)
                            assert await_turn(yaw),('Touch drag must turn the C camera',yaw,
                                outbound.positions()[-1],page.evaluate('({trace:lookTrace,rect:canvas.getBoundingClientRect().toJSON(),state:Module._LapisWeb_State()})'))
                            page.locator('#toolbar [data-action="9"]').tap()
                        else:
                            page.keyboard.down('w');page.keyboard.down('Space');page.wait_for_timeout(450)
                            page.keyboard.up('w');page.keyboard.up('Space');page.wait_for_timeout(700)
                            yaw=outbound.positions()[-1][3]
                            # CDP absolute mouse coordinates do not model relative
                            # hardware motion while pointer lock is active. Check
                            # real mouse deltas first, then pointer capture separately.
                            page.evaluate('document.exitPointerLock()')
                            page.wait_for_function('!document.pointerLockElement')
                            page.evaluate('''() => {window.mouseTrace=[];
                              canvas.addEventListener('mousemove',e=>mouseTrace.push([e.movementX,e.movementY,e.clientX,e.clientY,Module._LapisWeb_State()]));}''')
                            for i in range(1,5):
                                page.mouse.move(512+i*15,320+i*3);page.wait_for_timeout(100)
                            assert await_turn(yaw),('Mouse motion must turn the C camera',yaw,outbound.positions()[-1],page.evaluate('mouseTrace'))
                            page.mouse.click(572,332)
                            page.wait_for_function('document.pointerLockElement === canvas',timeout=5000)
                            page.keyboard.press('b')
                        page.wait_for_function('(Module._LapisWeb_State() & 2) !== 0');page.wait_for_timeout(300)
                        assert len(outbound.positions())>len(before)+1
                        moved=outbound.positions()[-1];assert abs(moved[0]-before[-1][0])+abs(moved[2]-before[-1][2])>.1
                        assert max(p[1] for p in outbound.positions()[len(before):])>before[-1][1]+.2,'Jump must move upward'
                        page.screenshot(path=str(EVIDENCE/f'{label}-inventory.png'))
                        if mobile:
                            page.locator('[data-mode="1"]').tap()
                            page.touchscreen.tap(262,293);page.wait_for_timeout(250)
                            page.locator('#inventory-tools [data-action="12"]').tap()
                        else:
                            page.mouse.click(320,433);page.wait_for_timeout(250)
                            page.mouse.click(368,433);page.wait_for_timeout(250)
                            page.keyboard.press('b')
                        page.wait_for_function('(Module._LapisWeb_State() & 2) === 0')
                        assert outbound.count(0x11)>0,'Inventory must send server click requests'
                        if mobile:
                            page.touchscreen.tap(442,370);page.wait_for_timeout(200)
                            deadline=time.monotonic()+5
                            while not outbound.count(0x34) and time.monotonic()<deadline:page.wait_for_timeout(100)
                            assert outbound.count(0x34)>0,'Hotbar selection must reach server'
                            start=point('#walk [data-action="0"]')
                            cdp.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[{**start,'id':3}]})
                            page.wait_for_timeout(200);page.set_viewport_size({'width':390,'height':844});page.wait_for_timeout(200)
                            assert page.locator('#gate').is_visible()
                            assert page.evaluate('held.size')==0,'Rotation must release held controls'
                            page.screenshot(path=str(EVIDENCE/'touch-rotation-gate.png'))
                            page.set_viewport_size({'width':844,'height':390});page.wait_for_timeout(200)
                            await_exit=page.evaluate('document.exitFullscreen()');page.wait_for_timeout(200)
                            assert page.locator('#gate').is_visible()
                            page.click('#resume');page.wait_for_timeout(200)
                            assert page.locator('#gate').is_hidden()
                            page.locator('#toolbar [data-action="11"]').tap()
                            page.locator('textarea').fill('Touch browser connected')
                            page.locator('#inventory-tools [data-action="13"]').tap()
                        else:
                            page.keyboard.press('t');page.keyboard.type('/admin '+token);page.keyboard.press('Enter');page.wait_for_timeout(200)
                            page.keyboard.press('t');page.keyboard.type('/tp -17 150 33');page.keyboard.press('Enter');page.wait_for_timeout(1800)
                            assert inbound.count(0x41)>=3,'Teleport position synchronization'
                            page.screenshot(path=str(EVIDENCE/'desktop-teleport.png'))
                            # Restore grounded fixture position before the mobile reconnect.
                            page.keyboard.press('t');page.keyboard.type('/tp 8 70 8');page.keyboard.press('Enter');page.wait_for_timeout(1800)
                        assert not errors,errors
                        assert all(url.startswith(f'http://127.0.0.1:{web}/') for url in requests),requests
                        outcomes.append({'mode':label,'sent_packets':len(outbound.packets),'received_packets':len(inbound.packets),
                            'movement_packets':len(outbound.positions()),'console_errors':errors})
                    finally:
                        page.screenshot(path=str(EVIDENCE/f'{label}-last.png'))
                        (EVIDENCE/f'{label}.log').write_text('\n'.join(messages+errors)+'\n')
                        (EVIDENCE/f'{label}-wire.json').write_text(json.dumps({'sent':[p.hex() for p in outbound.packets],'received_ids':[p[0] for p in inbound.packets if p]},indent=2))
                        if mobile and os.environ.get('LAPIS_WEB_REVIEW_IMAGES')=='1':
                            for name in (f'{label}-world.png',f'{label}-inventory.png',f'{label}-portrait-gate.png'):
                                if (EVIDENCE/name).exists():
                                    print('LAPISCUBE_SCREENSHOT '+name+' '+base64.b64encode((EVIDENCE/name).read_bytes()).decode(),flush=True)
                        context.close()
                browser.close()
                (EVIDENCE/'results.json').write_text(json.dumps(outcomes,indent=2)+'\n');print(outcomes)
        finally:
            server.terminate();server.wait(5)


if __name__=='__main__':main()
