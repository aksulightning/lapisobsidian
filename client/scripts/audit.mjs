import {readFile,readdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {textures} from '../src/assets/registry.mjs';
const root=new URL('../',import.meta.url),assetsRoot=new URL('public/assets/',root);
export function validateRecord(a){
 for(const key of ['path','name','creator','kind','license','sourcePath','sourceUrl','licenseEvidence','verification','use','sha256'])if(typeof a[key]!=='string'||!a[key])throw Error('Missing provenance: '+key);
 if(a.license!=='CC0-1.0')throw Error('Unapproved asset license');
 if(!['original','external'].includes(a.kind)||a.path.includes('..')||a.path.startsWith('/'))throw Error('Invalid asset record');
 if(typeof a.modified!=='boolean'||!/^[a-f0-9]{64}$/.test(a.sha256))throw Error('Invalid asset integrity metadata');
 if(a.kind==='external'&&(!/^[a-f0-9]{40}$/.test(a.commit||'')||a.repository!=='https://github.com/EBonura/voxide'||a.sourceUrl!=='https://opengameart.org/content/16x16-block-texture-set'||a.licenseEvidence!==a.sourceUrl))throw Error('Unapproved external source');
 if(a.kind==='original'&&(a.licenseEvidence!=='LICENSE.md'||!a.sourceUrl.startsWith('repository:')))throw Error('Missing original dedication');
}
async function walk(dir){const out=[];for(const e of await readdir(dir,{withFileTypes:true})){const path=new URL(e.name+(e.isDirectory()?'/':''),dir);if(e.isDirectory())out.push(...await walk(path));else out.push(path);}return out;}
export async function audit(){
 const manifest=JSON.parse(await readFile(new URL('manifest.json',assetsRoot),'utf8')),listed=new Set();
 for(const a of manifest.assets){validateRecord(a);if(listed.has(a.path))throw Error('Duplicate asset');listed.add(a.path);const data=await readFile(new URL(a.path,assetsRoot));if(createHash('sha256').update(data).digest('hex')!==a.sha256)throw Error('Asset changed without review: '+a.path);}
 for(const url of await walk(assetsRoot)){
  const path=url.href.slice(assetsRoot.href.length);if(['manifest.json','LICENSE.md'].includes(path))continue;
  if(!listed.has(path))throw Error('Unrecorded asset: '+path);
 }
 for(const texture of Object.values(textures))if(!listed.has(texture.path.slice('/assets/'.length)))throw Error('Missing texture record');
 for(const url of await walk(new URL('src/',root))){const text=await readFile(url,'utf8');if(/resources\.download\.minecraft\.net|textures\.minecraft\.net|launchermeta\.mojang\.com|piston-data\.mojang\.com|assets\.minecraft\.net|libraries\.minecraft\.net/.test(text))throw Error('Proprietary endpoint: '+url.pathname);}
 const html=await readFile(new URL('index.html',root),'utf8');if(!html.includes('<title>Lapis Obsidian Client</title>')||/Minecraft|Mojang|Microsoft/.test(html))throw Error('Unexpected UI branding');
 console.log(`Asset audit passed: ${listed.size} provenance records, exact hashes, registry coverage and UI branding. Manual source/artwork review remains required.`);
 return manifest;
}
if(process.argv[1]&&new URL('file://'+process.argv[1]).href===import.meta.url)await audit();
