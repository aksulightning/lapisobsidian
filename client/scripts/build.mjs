import {readFile,mkdir,rm,writeFile,copyFile} from 'node:fs/promises';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {audit} from './audit.mjs';
const root=new URL('../../',import.meta.url),dist=new URL('../dist/',import.meta.url);
await audit();execFileSync(process.execPath,['client/scripts/routes.mjs','--check'],{cwd:fileURLToPath(root)});
await rm(dist,{recursive:true,force:true});await mkdir(dist,{recursive:true});
const routes=(await readFile(new URL('client/embed.list',root),'utf8')).trim().split('\n').map(s=>s.split('|'));
for(const [route,file] of routes){
 const path=new URL(route==='/'?'index.html':route.slice(1),dist);await mkdir(new URL('./',path),{recursive:true});
 if(file==='@catalog')await writeFile(path,execFileSync('sh',['tools/client-catalog.sh'],{cwd:fileURLToPath(root)}));
 else{if(file.endsWith('.mjs'))execFileSync(process.execPath,['--check',file],{cwd:fileURLToPath(root)});await copyFile(new URL(file,root),path);}
}
await copyFile(new URL('LICENSE',root),new URL('CODE-LICENSE.txt',dist));await copyFile(new URL('NOTICE.md',root),new URL('NOTICE.md',dist));
console.log('Lapis Obsidian Client production build: client/dist');
