// Loopback-only development HTTP server; fixed server endpoint, never a TCP proxy.
import http from 'node:http';
import {readFile} from 'node:fs/promises';
const target=new URL(process.env.CLIENT_SERVER_ORIGIN||'http://127.0.0.1:8080');
if(target.protocol!=='http:'||target.username||target.password||target.pathname!=='/')throw Error('CLIENT_SERVER_ORIGIN must be an HTTP origin');
const port=Number(process.env.CLIENT_DEV_PORT||8081),dist=new URL('../dist/',import.meta.url);
const routes=new Map((await readFile(new URL('../embed.list',import.meta.url),'utf8')).trim().split('\n').map(s=>{const [route,,mime]=s.split('|');return [route,mime];}));
const server=http.createServer(async(req,res)=>{
 const path=req.url;if(req.method!=='GET'||!routes.has(path)){res.writeHead(404);res.end();return;}
 try{const data=await readFile(new URL(path==='/'?'index.html':path.slice(1),dist));res.writeHead(200,{'Content-Type':routes.get(path),'Content-Security-Policy':"default-src 'self'; connect-src 'self'; frame-ancestors 'none'",'X-Content-Type-Options':'nosniff','Cache-Control':'no-store'});res.end(data);}catch{res.writeHead(503);res.end('Run npm --prefix client run build first.');}
});
server.on('upgrade',(req,socket,head)=>{
 if(req.url!=='/ws'||req.headers.origin!==`http://${req.headers.host}`){socket.destroy();return;}
 const proxy=http.request(target.origin+'/ws',{headers:req.headers});
 proxy.on('upgrade',(response,upstream,data)=>{socket.write(`HTTP/1.1 ${response.statusCode} Switching Protocols\r\n`+Object.entries(response.headers).map(([k,v])=>`${k}: ${v}\r\n`).join('')+'\r\n');if(data.length)socket.write(data);if(head.length)upstream.write(head);socket.pipe(upstream).pipe(socket);socket.on('error',()=>upstream.destroy());upstream.on('error',()=>socket.destroy());socket.on('close',()=>upstream.destroy());upstream.on('close',()=>socket.destroy());});
 proxy.on('response',()=>socket.destroy());proxy.on('error',()=>socket.destroy());socket.on('close',()=>proxy.destroy());proxy.end();
});
server.listen(port,'127.0.0.1',()=>console.log(`Lapis Obsidian Client: http://127.0.0.1:${port} → ${target.origin} (fixed HTTP/WebSocket backend)`));
