// Original authoritative inventory using upstream GUI widgets. SPDX-License-Identifier: MIT
import GuiScreen from '../src/js/net/minecraft/client/gui/GuiScreen.js';
import GuiButton from '../src/js/net/minecraft/client/gui/widgets/GuiButton.js';
import {items,blockToItem} from './registry.mjs';
export default class Inventory extends GuiScreen {
 constructor(connection){super();this.connection=connection;this.page=0;}
 init(){super.init();this.rebuild();}
 rebuild(){this.buttonList=[];const c=this.connection,entries=c.mode===1&&c.windowId===0?Array.from(blockToItem,([block,item])=>({block,item,count:64})):Array.from({length:c.windowId===2?63:46},(_,slot)=>({slot,...c.slots.get(`${c.windowId}:${slot}`)}));this.entries=entries;const visible=entries.slice(this.page*24,(this.page+1)*24);for(let n=0;n<visible.length;n++){const value=visible[n],label=`${value.slot===undefined?'':value.slot+': '}${items[value.item]||'Empty'} ${value.count||''}`;this.buttonList.push(new GuiButton(label.slice(0,23),this.width/2-180+(n%4)*90,65+Math.floor(n/4)*22,88,20,()=>{if(value.block!==undefined)c.creative(value.block);else c.send(0x11,p=>p.vi(c.windowId).vi(c.stateId).u16(value.slot).u8(0).vi(0).vi(0).u8(0));}));}this.buttonList.push(new GuiButton('Previous',this.width/2-150,205,98,20,()=>{this.page=Math.max(0,this.page-1);this.rebuild();}));this.buttonList.push(new GuiButton('Next',this.width/2-50,205,98,20,()=>{this.page=Math.min(Math.floor((entries.length-1)/24),this.page+1);this.rebuild();}));this.buttonList.push(new GuiButton('Done',this.width/2+50,205,98,20,()=>this.minecraft.displayScreen(null)));for(const button of this.buttonList)button.minecraft=this.minecraft;}
 updateScreen(){super.updateScreen();this.rebuild();}
 drawScreen(stack,x,y,ticks){this.drawDefaultBackground(stack);this.drawCenteredString(stack,this.connection.mode===1&&this.connection.windowId===0?'Creative blocks → selected hotbar slot':'Server inventory (left click)',this.width/2,42);const cursor=this.connection.cursor;this.drawCenteredString(stack,`Cursor: ${items[cursor?.item]||'Empty'} ${cursor?.count||''}`,this.width/2,232);super.drawScreen(stack,x,y,ticks);}
 keyTyped(key){if(key==='KeyE'||key==='Escape'){this.minecraft.displayScreen(null);return true;}return super.keyTyped(key);}
 onClose(){this.connection.send(0x12,p=>p.vi(this.connection.windowId));this.connection.windowId=0;}
}
