// Adapted from LabyStudio/js-minecraft; CC BY-NC 4.0. See ../../LICENSE.
import Minecraft from './net/minecraft/client/Minecraft.js';
import {createResources} from '../../adapter/resources.mjs';
import {registerBlocks} from '../../adapter/registry.mjs';
export function require(module){return window[module];}
try{window.app=new Minecraft('canvas-container',createResources());registerBlocks();document.getElementById('background').hidden=true;window.addEventListener('pagehide',()=>window.app?.stop());}catch(error){document.getElementById('client-error').textContent=`Lapis Obsidian Client: ${error.message}. A WebGL-capable desktop browser is required.`;console.error(error);}
