// SPDX-License-Identifier: GPL-3.0-only
import license from '../../LICENSE?raw';
import {loadSettings, saveSettings, touchDevice, setPersistence, validateSettings, validHost} from './settings';
import {BridgeTransport} from './transport';
import {Input} from './input';
import {Renderer} from './renderer';
import {protocolVersion, serverRelease} from './protocol-version';
const element = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
const settings=loadSettings(), canvas=element<HTMLCanvasElement>('scene');
document.body.classList.toggle('touch',touchDevice);
element('input-mode').textContent=touchDevice?'TOUCH CONTROLS':'KEYBOARD + MOUSE';
element('protocol-label').textContent=`Planned compatibility: protocol ${protocolVersion} (${serverRelease}), matching the testing branch. No server login in this preview.`;
const report=(message: string): void=>{element('scene-error').textContent=message;element('scene-error').hidden=false;};
const menu=(): void=>{input.stop();element('menu').hidden=false;element('hud').hidden=true;element('menu-toggle').hidden=true;document.body.classList.remove('playing');};
const input=new Input(canvas,settings,menu);
let renderer: Renderer | undefined;
try {renderer=new Renderer(canvas,input,settings,report);} catch(error) {report(error instanceof Error?error.message:'Graphics initialization failed.');element<HTMLButtonElement>('explore').disabled=true;}
element('explore').onclick=()=>{element('menu').hidden=true;element('hud').hidden=false;element('menu-toggle').hidden=false;document.body.classList.add('playing');input.active=true;canvas.focus();if(!touchDevice){try{canvas.requestPointerLock()?.catch(()=>report('Pointer lock is unavailable. Use touch controls or another supported runtime.'));}catch{report('Pointer lock is unavailable in this runtime.');}}};
element('menu-toggle').onclick=menu;
document.querySelector('.wordmark')!.addEventListener('click',e=>{e.preventDefault();menu();});
for(const tab of document.querySelectorAll<HTMLButtonElement>('[data-tab]'))tab.onclick=()=>{for(const button of document.querySelectorAll<HTMLButtonElement>('[data-tab]')){const selected=button===tab;button.setAttribute('aria-selected',String(selected));element(button.dataset.tab!).hidden=!selected;}};
for(const key of ['host','port','username'] as const)element<HTMLInputElement>(key).value=String(settings[key]);
element<HTMLFormElement>('connection-form').onsubmit=e=>{
  e.preventDefault();const host=element<HTMLInputElement>('host').value.trim(),port=Number(element<HTMLInputElement>('port').value);
  if(!validHost(host)||!Number.isInteger(port)||port<1||port>65535){element('save-message').textContent='Enter a hostname or IP address and a port from 1 to 65535.';return;}
  settings.host=host;settings.port=port;settings.username=element<HTMLInputElement>('username').value;
  element('save-message').textContent=saveSettings(settings)?'Server saved. Remote gameplay is coming in a later milestone.':'Settings could not be saved in this runtime.';
};
for(const key of ['quality','fps','mouse','touch','scale','distance'] as const){
  const control=element<HTMLInputElement | HTMLSelectElement>(key);control.value=String(settings[key]);
  control.addEventListener('input',()=>{
    if(key==='quality')settings.quality=control.value as typeof settings.quality;else settings[key]=Number(control.value);
    document.documentElement.style.setProperty('--ui-scale',String(settings.scale));
    element('settings-message').textContent=saveSettings(settings)?'Settings saved. Antialiasing changes apply after restart.':'Settings could not be saved.';
  });
}
document.documentElement.style.setProperty('--ui-scale',String(settings.scale));
element('fullscreen').onclick=async()=>{try{if(document.fullscreenElement)await document.exitFullscreen();else await document.documentElement.requestFullscreen();}catch{element('settings-message').textContent='Fullscreen is not available in this runtime.';}};
element('copy-source').onclick=async()=>{try{await navigator.clipboard.writeText('https://github.com/aksulightning/lapisobsidian');element('about-message').textContent='Source address copied.';}catch{element('about-message').textContent='https://github.com/aksulightning/lapisobsidian';}};
const transport=new BridgeTransport((text,state)=>{element('bridge-status').textContent=text;element('bridge-light').className=`status-light ${state}`;});
setPersistence(value=>transport.savePreferences(value));
transport.onPreferences=value=>{
  Object.assign(settings,validateSettings(value));
  for(const key of ['host','port','username','quality','fps','mouse','touch','scale','distance'] as const)element<HTMLInputElement | HTMLSelectElement>(key).value=String(settings[key]);
  document.documentElement.style.setProperty('--ui-scale',String(settings.scale));
};
transport.onError=error=>{element('connection-status').textContent=error.message;};
const bootstrap=window.__LAPIS_BOOTSTRAP__;delete window.__LAPIS_BOOTSTRAP__;
if(bootstrap){try{transport.start(bootstrap);}catch{element('bridge-status').textContent='Bridge error';element('connection-status').textContent='Invalid local startup. Restart the standalone application.';}}
else{element('bridge-status').textContent='Standalone runtime required';element('connection-status').textContent='Frontend preview only. Launch the application for its bundled bridge.';}
window.addEventListener('pagehide',()=>{transport.close();renderer?.dispose();input.stop();});

element('license-text').textContent=license;
element('read-license').onclick=()=>element<HTMLDialogElement>('license-dialog').showModal();
