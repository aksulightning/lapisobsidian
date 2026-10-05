// SPDX-License-Identifier: GPL-3.0-only
import {chromium,expect} from '@playwright/test';
import {mkdir} from 'node:fs/promises';
import {startBridge} from '../scripts/harness.mjs';
const bridge=await startBridge();let browser;
try {
  browser=await chromium.launch({executablePath:process.env.LAPIS_TEST_BROWSER,args:['--enable-unsafe-swiftshader']});
  await mkdir('test-results',{recursive:true});
  for(const mobile of [false,true]) {
    const context=await browser.newContext({viewport:mobile?{width:390,height:844}:{width:1440,height:1000},isMobile:mobile,hasTouch:mobile,deviceScaleFactor:1});
    await context.addInitScript(value=>{if(location.origin===value.origin)window.__LAPIS_BOOTSTRAP__=value;},bridge.bootstrap);
    const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(bridge.bootstrap.origin);
    await expect(page).toHaveTitle('Lapis Obsidian Client');
    await expect(page.locator('#bridge-status')).toHaveText('Bridge ready');
    await expect(page.locator('#scene')).toHaveAttribute('data-rendered','true');
    await expect(page.locator('#scene-error')).toBeHidden();
    await expect(page.locator('#connect-server')).toBeDisabled();
    const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);expect(overflow).toBe(false);
    await page.locator('#host').fill('play.example.org');await page.getByRole('button',{name:'Save server',exact:false}).click();await expect(page.locator('#save-message')).toContainText('Server saved');
    await page.screenshot({path:`test-results/${mobile?'mobile':'desktop'}.png`,fullPage:true});
    await page.getByRole('button',{name:'Settings',exact:true}).click();await page.locator('#fps').selectOption('30');await expect(page.locator('#settings-message')).toContainText('Settings saved');
    await page.getByRole('button',{name:'About',exact:true}).click();await expect(page.locator('#protocol-label')).toContainText('772');
    await page.getByRole('button',{name:'Explore test scene',exact:false}).click();await expect(page.locator('#hud')).toBeVisible();
    if(mobile) {
      await page.setViewportSize({width:844,height:390});
      await expect(page.locator('#joystick')).toBeVisible();
      // Three simultaneous touch pointers on independent capture surfaces.
      const session=await context.newCDPSession(page);
      const joystick=await page.locator('#joystick').boundingBox(), look=await page.locator('#look-area').boundingBox(), rise=await page.locator('#rise').boundingBox();
      const touches=[{x:joystick.x+56,y:joystick.y+56,id:1},{x:look.x+30,y:look.y+30,id:2},{x:rise.x+30,y:rise.y+30,id:3}];
      await session.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:touches});
      await session.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:touches.map((p,i)=>({...p,x:p.x+(i===1?20:0),y:p.y-(i===0?25:0)}))});
      await session.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
      await page.screenshot({path:'test-results/mobile-landscape.png'});
      await page.locator('#menu-toggle').click();
    } else {await page.keyboard.press('Escape');}
    await expect(page.locator('#menu')).toBeVisible();expect(errors).toEqual([]);
    await context.close();
    // Wait for server-side authenticated session cleanup without fixed sleeps.
    await expect.poll(async()=>{const c=await browser.newContext();const p=await c.newPage();await p.goto(bridge.bootstrap.origin);const response=await p.evaluate(async token=>await new Promise(resolve=>{const ws=new WebSocket(location.origin.replace('http:','ws:')+'/bridge');ws.onopen=()=>ws.send(JSON.stringify({type:'AUTH',token}));ws.onmessage=e=>{resolve(JSON.parse(e.data).type);ws.close();};}),bridge.bootstrap.token);await c.close();return response;}).toBe('AUTH_OK');
  }
  console.log('Browser checks passed: authenticated startup, WebGL, desktop/mobile layout, settings and multi-touch.');
} finally {await browser?.close();await bridge.stop();}
