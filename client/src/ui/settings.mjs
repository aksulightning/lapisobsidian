export const defaultSettings=Object.freeze({volume:.5,renderDistance:3,sensitivity:1,ambience:true});
export function validateSettings(value={}){
 const bounded=(v,min,max,fallback)=>Number.isFinite(Number(v))?Math.max(min,Math.min(max,Number(v))):fallback;
 return {volume:bounded(value.volume,0,1,.5),renderDistance:Math.round(bounded(value.renderDistance,1,3,3)),sensitivity:bounded(value.sensitivity,.25,3,1),ambience:typeof value.ambience==='boolean'?value.ambience:true};
}
export function serverAddress(value,base){
 const url=new URL(value,base);
 if(!['http:','https:'].includes(url.protocol)||url.username||url.password||url.pathname!=='/'||url.search||url.hash)throw Error('Enter a web server origin, for example http://localhost:8080');
 return url.origin;
}
export function connectionFailure(now,lastReceived,started){
 if(now-lastReceived>60000)return 'No data received for 60 seconds. Check the server log and reconnect.';
 if(now-started>300000)return 'Terrain loading did not finish within 5 minutes. Check the server log.';
 return null;
}
