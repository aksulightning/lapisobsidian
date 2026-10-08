// Original pixel artwork, independently designed for Lapis Obsidian Client.
// Generator code: repository GPL. Output artwork: CC0-1.0 (assets/LICENSE.md).
import {mkdir,writeFile} from 'node:fs/promises';
const root=new URL('../public/assets/',import.meta.url);
const specs={
  fallback:['#382641','#c6a2ff','check'], water:['#386a92','#68b9b3','wave'], magma:['#a63d47','#ffbd69','wave'],
  frost:['#c4dce0','#f4f6e4','noise'], soot:['#293344','#576278','noise'],
  blue:['#364c96','#b1b8fc','ore'], copper:['#746574','#cda078','ore'], amber:['#726b77','#f4d490','ore'],
  crystal:['#586980','#8cf3dc','ore'], ember:['#59647b','#e7818e','ore'],
  gateway:['#9e775f','#e4d1a3','door'], plaque:['#ae956c','#f8e3b2','sign'],
  bench:['#66565d','#c49672','grid'], box:['#725c58','#cba07c','grid'], clay:['#a47c78','#e6b3a5','brick'],
  sprout:['#00000000','#a4d47f','plant'], bloom:['#00000000','#f5b5d6','flower'], grain:['#00000000','#e2c583','plant'],
  tool:['#00000000','#aacdcc','tool'], blade:['#00000000','#cee6f0','blade'], bow:['#00000000','#cba27b','bow'],
  morsel:['#00000000','#efc590','food'], vessel:['#00000000','#a9bbcc','bucket'], mineral:['#00000000','#8ec3de','gem'],
  explorer:['#354e69','#8ccec3','face'], grazer:['#877781','#decbbc','face'], crawler:['#5a596e','#d5a499','face'],
  spirit:['#3e6980','#a8d9da','face'], mote:['#00000000','#d2d9fb','gem'], panel:['#182536','#577290','grid'],
  button:['#324461','#95cdbf','grid'], heart:['#00000000','#ef9cb6','heart'], food:['#00000000','#e8c786','food']
};
await mkdir(new URL('textures/original/',root),{recursive:true});
function pixel(kind,x,y,a,b,seed){
 const n=(Math.imul(x+seed,374761393)^Math.imul(y+seed,668265263))>>>0;
 let on=n%11<3;
 if(kind==='check')on=(x>>2&1)!==(y>>2&1);
 if(kind==='wave')on=(y+Math.floor(x/3)+seed)%6<2;
 if(kind==='wood')on=x%5===0||(y%7===0&&x%3===0);
 if(kind==='leaf')on=n%5<2;
 if(kind==='ore')on=((x-4)**2+(y-5)**2<6)||((x-11)**2+(y-10)**2<5);
 if(kind==='glass')return x===0||y===0||x===y?b:a;
 if(kind==='door')on=x===1||x===14||y===1||y===14||(x===11&&y===9);
 if(kind==='sign')on=y===4||y===7||y===10;
 if(kind==='grid')on=x%7===0||y%7===0;
 if(kind==='brick')on=y%5===0||(x+(y%10<5?4:0))%8===0;
 if(kind==='plant')on=x===7&&y>4||(y===6&&Math.abs(x-7)<4)||(y===9&&Math.abs(x-7)<3);
 if(kind==='flower')on=x===7&&y>6||((x-7)**2+(y-5)**2<12);
 if(kind==='tool')on=(Math.abs(y-3)<2&&x>2&&x<14)||(Math.abs(x+y-16)<2&&y>4);
 if(kind==='blade')on=Math.abs(x+y-15)<2&&(x>2&&y>2)||(y===10&&x>2&&x<10);
 if(kind==='bow')on=Math.abs(Math.hypot(x-4,y-8)-6)<1.3&&x>4||x===4&&y>1&&y<15;
 if(kind==='food')on=((x-8)/6)**2+((y-8)/4)**2<1;
 if(kind==='bucket')on=(x===3||x===12)&&y>4&&y<12||y===12&&x>=3&&x<=12||y===4&&x>=3&&x<=12;
 if(kind==='gem')on=Math.abs(x-8)+Math.abs(y-8)<6;
 if(kind==='face')on=(y===5&&(x===4||x===11))||(y===11&&x>4&&x<11);
 if(kind==='heart')on=(y>3&&y<7&&((x>2&&x<7)||(x>8&&x<13)))||(y>=7&&Math.abs(x-7.5)<13-y);
 return on?b:a;
}
for(const [i,[name,[a,b,kind]]] of Object.entries(Object.entries(specs))){
 let rects='';for(let y=0;y<16;y++)for(let x=0;x<16;x++){const color=pixel(kind,x,y,a,b,+i+17);if(color!=='#00000000')rects+=`<rect x="${x}" y="${y}" width="1" height="1" fill="${color}"/>`;}
 await writeFile(new URL(`textures/original/${name}.svg`,root),`<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" shape-rendering="crispEdges">${rects}</svg>\n`);
}
await writeFile(new URL('../src/assets/original-tiles.mjs',import.meta.url),`export const originalTiles=${JSON.stringify(Object.keys(specs))};\n`);
console.log(`Generated ${Object.keys(specs).length} original CC0 tiles`);
