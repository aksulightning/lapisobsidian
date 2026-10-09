import {readFile,readdir,writeFile} from 'node:fs/promises';
const root=new URL('../../',import.meta.url);
async function walk(dir){const out=[];for(const entry of await readdir(new URL(dir+'/',root),{withFileTypes:true})){const path=dir+'/'+entry.name;if(entry.isDirectory())out.push(...await walk(path));else out.push(path);}return out;}
const routes=[['/','client/index.html','text/html; charset=utf-8'],['/style.css','client/style.css','text/css; charset=utf-8'],['/catalog.mjs','@catalog','text/javascript; charset=utf-8']];
for(const path of (await walk('client/src')).sort())routes.push(['/'+path.slice(7),path,'text/javascript; charset=utf-8']);
for(const path of (await walk('client/public/assets')).sort()){
 const type=path.endsWith('.png')?'image/png':path.endsWith('.svg')?'image/svg+xml':path.endsWith('.json')?'application/json':'text/plain; charset=utf-8';
 routes.push(['/'+path.slice('client/public/'.length),path,type]);
}
const contents=routes.map(r=>r.join('|')).join('\n')+'\n';
if(process.argv.includes('--check')){if(contents!==await readFile(new URL('client/embed.list',root),'utf8'))throw Error('Run node client/scripts/routes.mjs after adding/removing served files');}
else await writeFile(new URL('client/embed.list',root),contents);
