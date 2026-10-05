// SPDX-License-Identifier: GPL-3.0-only
// Private bootstrap pipe. Never print its contents or put credentials in URLs.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {once} from 'node:events';
export async function startBridge() {
  const child=spawn('cargo',['run','--locked','--quiet','-p','lapis-bridge','--','--bootstrap-stdio'],{cwd:new URL('..',import.meta.url),stdio:['pipe','pipe','inherit']});
  let bootstrap;
  const line=createInterface({input:child.stdout});
  try {
    bootstrap=await Promise.race([
      once(line,'line').then(([text])=>JSON.parse(text)),
      once(child,'exit').then(()=>{throw new Error('Bridge exited before startup');}),
      once(child,'error').then(([error])=>{throw error;}),
      new Promise((_,reject)=>{const timer=setTimeout(()=>reject(new Error('Bridge startup timed out')),120000);timer.unref();}),
    ]);
  } catch(error) {child.stdin.end();child.kill();throw error;}
  line.close();
  let stopped=false;
  return {bootstrap,async stop(){if(stopped)return;stopped=true;const exited=once(child,'exit');child.stdin.end();const timer=setTimeout(()=>child.kill(),5000);timer.unref();await exited;clearTimeout(timer);}};
}
