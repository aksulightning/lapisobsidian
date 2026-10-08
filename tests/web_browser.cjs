// Optional Chromium smoke test. See docs/web-client.md for installation.
const assert=require('node:assert/strict');
const net=require('node:net');
const {spawn}=require('node:child_process');
const {mkdir,mkdtemp,writeFile,rm}=require('node:fs/promises');
const {resolve}=require('node:path');
const {once}=require('node:events');
const {chromium}=require('../.tests/browser/node_modules/playwright');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function until(predicate,label) {
  const deadline=Date.now()+20000;
  while(!predicate()){assert.ok(Date.now()<deadline,label);await delay(25);}
}
async function terrainVisible(page) {
  await page.waitForFunction(()=>new Promise(resolve=>requestAnimationFrame(()=>{
    const canvas=document.querySelector('#world'),gl=canvas.getContext('webgl');
    const pixel=new Uint8Array(4),colors=new Set();
    for(let x=1;x<8;x++)for(let y=1;y<8;y++) {
      gl.readPixels(Math.floor(canvas.width*x/8),Math.floor(canvas.height*y/8),1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);
      colors.add(pixel.join(','));
    }
    resolve(colors.size>4);
  })));
}
(async()=>{
  const {PacketStream,packet}=await import('../web/protocol.mjs');
  const listener=net.createServer();listener.listen(0,'127.0.0.1');await once(listener,'listening');
  const port=listener.address().port;
  const webListener=net.createServer();webListener.listen(0,'127.0.0.1');await once(webListener,'listening');
  const webPort=webListener.address().port;
  await Promise.all([listener,webListener].map(s=>new Promise(resolve=>s.close(resolve))));
  await mkdir(resolve('.tests'),{recursive:true});
  const directory=await mkdtemp(resolve('.tests/browser-world-'));
  await writeFile(`${directory}/server.txt`,`port=${port}\nweb-address=127.0.0.1\nweb-port=${webPort}\ngamemode=creative\n`);
  const server=spawn(resolve('lapis-obsidian'),[],{cwd:directory,stdio:['pipe','pipe','pipe']});
  let logs='',browser;server.stdout.on('data',b=>logs+=b);server.stderr.on('data',b=>logs+=b);
  const errors=[];
  try {
    const deadline=Date.now()+10000;
    while(!logs.includes('Server listening')){assert.ok(Date.now()<deadline,logs);await new Promise(r=>setTimeout(r,20));}
    browser=await chromium.launch({headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
    const page=await browser.newPage({viewport:{width:1280,height:800}});
    page.on('pageerror',e=>errors.push(e.message));
    // The first teleport precedes terrain. Suppress the redundant second one:
    // receiving the spawn chunk must unlock the UI without another teleport.
    await page.routeWebSocket('**/ws',route=>{
      const server=route.connectToServer();let teleports=0;
      const stream=new PacketStream((id,r)=>{
        if(id===0x41&&++teleports===2)return;
        route.send(Buffer.from(packet(id,w=>w.raw(r.take(r.bytes.length-r.pos)))));
      });
      server.onMessage(bytes=>stream.push(new Uint8Array(bytes)));
    });
    await page.goto(`http://127.0.0.1:${webPort}/`);await page.fill('#name','BrowserTester');await page.click('#play');
    await page.waitForSelector('#hud:not([hidden]) #vitals',{timeout:60000});
    await page.waitForFunction(()=>document.querySelector('#vitals').textContent.includes('Creative'));
    await terrainVisible(page);
    await page.screenshot({path:'.tests/web-client.png'});
    await page.click('#resume');
    await page.waitForFunction(()=>document.pointerLockElement?.id==='world');
    await page.keyboard.press('KeyT');await page.fill('#message','/tps');await page.keyboard.press('Enter');
    await page.waitForFunction(()=>document.querySelector('#messages').textContent.includes('TPS'));
    await page.keyboard.press('KeyE');assert.equal(await page.locator('#inventory').evaluate(e=>e.open),true);
    await page.selectOption('#block',{label:'dirt'});await page.click('#give');
    await page.waitForFunction(()=>document.querySelector('#hotbar').textContent.includes('dirt ×64'));
    await page.click('#close-inventory');
    const before=await page.locator('#location').textContent();
    await page.keyboard.down('Space');await page.waitForTimeout(250);await page.keyboard.up('Space');
    assert.notEqual(await page.locator('#location').textContent(),before,'jump changes player position');
    await page.keyboard.press('Escape');
    await page.waitForFunction(()=>document.pointerLockElement===null);
    await page.click('#leave');
    await page.click('#play');await page.waitForSelector('#hud:not([hidden]) #vitals',{timeout:60000});
    await page.waitForFunction(()=>document.querySelector('#hotbar').textContent.includes('dirt ×64'));
    await page.click('#leave');
    const context=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true,deviceScaleFactor:1});
    const phone=await context.newPage();phone.on('pageerror',e=>errors.push(e.message));
    let position=null,angle=null;const updates=[];
    phone.on('websocket',ws=>{
      const input=new PacketStream((id,r)=>{
        if(id===0x1e){position=[r.double(),r.double(),r.double()];angle=[r.float(),r.float()];}
      });
      const output=new PacketStream((id,r)=>{if(id===0x08)updates.push([...r.position(),r.varint()]);});
      ws.on('framesent',event=>input.push(new Uint8Array(event.payload)));
      ws.on('framereceived',event=>output.push(new Uint8Array(event.payload)));
    });
    await phone.goto(`http://127.0.0.1:${webPort}/`);await phone.fill('#name','TouchTester');await phone.tap('#play');
    await phone.waitForSelector('#touch-controls:not([hidden]) #move-pad',{timeout:60000});
    assert.equal(await phone.evaluate(()=>document.pointerLockElement),null);
    await phone.tap('#touch-chat');await phone.fill('#message','/tps');await phone.tap('#send-message');
    await phone.waitForFunction(()=>document.querySelector('#messages').textContent.includes('TPS'));
    await until(()=>position!==null,'phone sends its position');
    const cdp=await context.newCDPSession(phone);
    const center=async selector=>{const b=await phone.locator(selector).boundingBox();assert.ok(b);return {x:b.x+b.width/2,y:b.y+b.height/2};};
    const touch=(type,points=[])=>cdp.send('Input.dispatchTouchEvent',{type,touchPoints:points.map(p=>({...p,radiusX:3,radiusY:3,force:1}))});
    const stick=await center('#move-pad'),start=[...position];
    await touch('touchStart',[{id:1,...stick},{id:2,x:220,y:250}]);
    await touch('touchMove',[{id:1,x:stick.x,y:stick.y-35},{id:2,x:250,y:270}]);await delay(500);
    await touch('touchEnd');
    await until(()=>Math.hypot(position[0]-start[0],position[2]-start[2])>.2,'touch joystick moves');
    assert.ok(Math.abs(angle[0])>2&&Math.abs(angle[1])>2,'drag changes yaw and pitch');
    // A cancelled finger cannot leave movement held down.
    await touch('touchStart',[{id:3,...stick}]);await touch('touchMove',[{id:3,x:stick.x+35,y:stick.y}]);await delay(150);
    await touch('touchCancel');await delay(150);const stopped=[...position];await delay(1100);
    assert.ok(Math.hypot(position[0]-stopped[0],position[2]-stopped[2])<.05,'pointercancel stops movement');
    const jump=await center('#touch-jump'),height=position[1];
    await touch('touchStart',[{id:4,...jump}]);await delay(300);await touch('touchEnd');
    assert.ok(position[1]>height+.1,'touch jump raises the player');await delay(900);
    await phone.tap('#touch-inventory');await phone.selectOption('#block',{label:'dirt'});await phone.tap('#give');
    await phone.waitForFunction(()=>document.querySelector('#hotbar').textContent.includes('dirt ×64'));
    await phone.tap('#close-inventory');
    await touch('touchStart',[{id:5,x:220,y:250}]);await touch('touchMove',[{id:5,x:220,y:370}]);await touch('touchEnd');
    await phone.waitForFunction(()=>document.querySelector('#location').textContent.includes(' · '));
    const mined=updates.length,mine=await center('#touch-mine');
    await touch('touchStart',[{id:6,...mine}]);await delay(250);await touch('touchEnd');
    await until(()=>updates.slice(mined).some(p=>p[3]===0),'touch mining changes a block');
    const placed=updates.length;await phone.tap('#touch-use');
    await until(()=>updates.slice(placed).some(p=>p[3]===10),'touch use places dirt');
    await phone.locator('#hotbar button').nth(1).tap();
    assert.ok((await phone.locator('#hotbar .selected').textContent()).startsWith('2'));
    await terrainVisible(phone);await phone.screenshot({path:'.tests/web-client-mobile.png'});
    await phone.setViewportSize({width:844,height:390});
    for(const selector of ['#move-pad','#touch-jump','#touch-inventory','#leave','#hotbar']) {
      const b=await phone.locator(selector).boundingBox();assert.ok(b&&b.x>=0&&b.y>=0&&b.x+b.width<=845&&b.y+b.height<=391,selector);
    }
    await phone.screenshot({path:'.tests/web-client-mobile-landscape.png'});
    await phone.tap('#leave');await context.close();
    assert.deepEqual(errors,[]);
    console.log('Chromium: spawn-chunk readiness, terrain pixels, desktop controls, phone multitouch movement/look/jump, cancellation, mining/placement, chat, inventory and portrait/landscape passed');
  } catch(error) {
    if(browser) {
      let index=0;
      for(const page of browser.contexts().flatMap(c=>c.pages())) {
        console.error('Browser status:',await page.locator('#status').textContent(),await page.locator('#location').textContent());
        console.error('Browser errors:',errors);
        console.error('Pointer lock:',await page.evaluate(()=>document.pointerLockElement?.id||null));
        await page.screenshot({path:`.tests/web-client-failure-${index++}.png`});
      }
    }
    console.error(logs);throw error;
  } finally {
    if(browser)await browser.close();server.stdin.end('stop\n');
    const timer=setTimeout(()=>server.kill(),5000);if(server.exitCode===null)await once(server,'exit');clearTimeout(timer);
    if(server.exitCode!==0)console.error(logs);
    await rm(directory,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
