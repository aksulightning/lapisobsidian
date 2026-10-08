// Optional Chromium smoke test. See docs/web-client.md for installation.
const assert=require('node:assert/strict');
const net=require('node:net');
const {spawn}=require('node:child_process');
const {mkdir,mkdtemp,writeFile,rm}=require('node:fs/promises');
const {resolve}=require('node:path');
const {once}=require('node:events');
const {chromium}=require('../.tests/browser/node_modules/playwright');
(async()=>{
  const listener=net.createServer();listener.listen(0,'127.0.0.1');await once(listener,'listening');
  const port=listener.address().port;await new Promise(resolve=>listener.close(resolve));
  await mkdir(resolve('.tests'),{recursive:true});
  const directory=await mkdtemp(resolve('.tests/browser-world-'));
  await writeFile(`${directory}/server.txt`,`port=${port}\ngamemode=creative\n`);
  const server=spawn(resolve('lapis-obsidian'),[],{cwd:directory,stdio:['pipe','pipe','pipe']});
  let logs='',browser;server.stdout.on('data',b=>logs+=b);server.stderr.on('data',b=>logs+=b);
  const errors=[];
  try {
    const deadline=Date.now()+10000;
    while(!logs.includes('Server listening')){assert.ok(Date.now()<deadline,logs);await new Promise(r=>setTimeout(r,20));}
    browser=await chromium.launch({headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
    const page=await browser.newPage({viewport:{width:1280,height:800}});
    page.on('pageerror',e=>errors.push(e.message));
    await page.goto(`http://127.0.0.1:${port}/`);await page.fill('#name','BrowserTester');await page.click('#play');
    await page.waitForSelector('#hud:not([hidden]) #vitals',{timeout:60000});
    await page.waitForFunction(()=>document.querySelector('#vitals').textContent.includes('Creative'));
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
    await page.keyboard.press('Escape');await page.click('#leave');
    await page.click('#play');await page.waitForSelector('#hud:not([hidden]) #vitals',{timeout:60000});
    await page.waitForFunction(()=>document.querySelector('#hotbar').textContent.includes('dirt ×64'));
    assert.deepEqual(errors,[]);
    console.log('Chromium: renders world, pointer lock, chat, inventory, jumping and reconnect passed');
  } catch(error) {
    if(browser) {
      const page=browser.contexts()[0]?.pages()[0];
      if(page) {
        console.error('Browser status:',await page.locator('#status').textContent());
        console.error('Browser errors:',errors);
        await page.screenshot({path:'.tests/web-client.png'});
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
