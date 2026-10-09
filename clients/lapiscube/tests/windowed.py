#!/usr/bin/env python3
"""Launch the extracted Linux ZIP, exercise native resize/travel, save review images.
Requires Xvfb, X11 and ImageMagick. Audio is muted. This is not human playthrough QA.
"""
import argparse
import ctypes as C
import json
import os
import pathlib
import re
import secrets
import shutil
import socket
import subprocess as S
import tempfile
import time
from survival import identity_for

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT/'build'
EVIDENCE = BUILD/'windowed-evidence'


class KeyEvent(C.Structure):
    _fields_ = [('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),('display',C.c_void_p),
                ('window',C.c_ulong),('root',C.c_ulong),('subwindow',C.c_ulong),('time',C.c_ulong),
                ('x',C.c_int),('y',C.c_int),('x_root',C.c_int),('y_root',C.c_int),('state',C.c_uint),
                ('keycode',C.c_uint),('same_screen',C.c_int),('padding',C.c_char*96)]


class ClientEvent(C.Structure):
    _fields_ = [('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),('display',C.c_void_p),
                ('window',C.c_ulong),('message_type',C.c_ulong),('format',C.c_int),('data',C.c_long*5),('padding',C.c_char*96)]


class NativeInput:
    def __init__(self, display):
        self.x = x = C.CDLL('libX11.so.6')
        signatures = {
            'XOpenDisplay': ([C.c_char_p],C.c_void_p), 'XDefaultRootWindow':([C.c_void_p],C.c_ulong),
            'XQueryTree':([C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.POINTER(C.c_ulong)),C.POINTER(C.c_uint)],C.c_int),
            'XStringToKeysym':([C.c_char_p],C.c_ulong), 'XKeysymToKeycode':([C.c_void_p,C.c_ulong],C.c_ubyte),
            'XSendEvent':([C.c_void_p,C.c_ulong,C.c_int,C.c_long,C.c_void_p],C.c_int),
            'XFlush':([C.c_void_p],C.c_int), 'XSetInputFocus':([C.c_void_p,C.c_ulong,C.c_int,C.c_ulong],C.c_int),
            'XResizeWindow':([C.c_void_p,C.c_ulong,C.c_uint,C.c_uint],C.c_int),
            'XMoveWindow':([C.c_void_p,C.c_ulong,C.c_int,C.c_int],C.c_int),
            'XInternAtom':([C.c_void_p,C.c_char_p,C.c_int],C.c_ulong),
            'XFree':([C.c_void_p],C.c_int), 'XCloseDisplay':([C.c_void_p],C.c_int)}
        for name,(args,result) in signatures.items():
            getattr(x,name).argtypes=args;getattr(x,name).restype=result
        self.d=x.XOpenDisplay(display.encode());assert self.d
        self.root=x.XDefaultRootWindow(self.d)
        # Supply the window-manager atoms before ClassiCube starts under bare Xvfb.
        x.XInternAtom(self.d,b'WM_DELETE_WINDOW',0);x.XInternAtom(self.d,b'WM_PROTOCOLS',0);x.XFlush(self.d)

    def attach(self):
        x=self.x
        r=C.c_ulong();p=C.c_ulong();kids=C.POINTER(C.c_ulong)();n=C.c_uint()
        x.XQueryTree(self.d,self.root,C.byref(r),C.byref(p),C.byref(kids),C.byref(n));assert n.value
        self.win=kids[n.value-1];x.XFree(kids)
        x.XSetInputFocus(self.d,self.win,1,0);x.XFlush(self.d)

    def key(self,name,hold=.025,pause=.025):
        e=KeyEvent();e.display=self.d;e.window=self.win;e.root=self.root;e.same_screen=1
        e.keycode=self.x.XKeysymToKeycode(self.d,self.x.XStringToKeysym(name.encode()));assert e.keycode
        e.type=2;self.x.XSendEvent(self.d,self.win,1,1,C.byref(e));self.x.XFlush(self.d);time.sleep(hold)
        e.type=3;self.x.XSendEvent(self.d,self.win,1,2,C.byref(e));self.x.XFlush(self.d);time.sleep(pause)

    def chat(self,text):
        self.key('t',pause=.12)
        for char in text:
            self.key({' ':'space','/':'slash','-':'minus'}.get(char,char),.012,.012)
        self.key('Return',pause=.4)

    def resize(self,w,h):
        self.x.XResizeWindow(self.d,self.win,w,h);self.x.XMoveWindow(self.d,self.win,0,0)
        self.x.XFlush(self.d);time.sleep(.6)

    def mouse(self,down):
        e=KeyEvent();e.display=self.d;e.window=self.win;e.root=self.root;e.same_screen=1
        e.keycode=1;e.type=4 if down else 5;e.x=427;e.y=240
        self.x.XSendEvent(self.d,self.win,1,4 if down else 8,C.byref(e));self.x.XFlush(self.d)

    def close(self):
        e=ClientEvent();e.type=33;e.display=self.d;e.window=self.win;e.format=32
        e.message_type=self.x.XInternAtom(self.d,b'WM_PROTOCOLS',0)
        e.data[0]=self.x.XInternAtom(self.d,b'WM_DELETE_WINDOW',0)
        self.x.XSendEvent(self.d,self.win,0,0,C.byref(e));self.x.XFlush(self.d)
        self.x.XCloseDisplay(self.d)


def stop(p):
    if p.poll() is None:
        p.terminate()
        try:p.wait(timeout=5)
        except S.TimeoutExpired:p.kill();p.wait()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--xvfb',default=shutil.which('Xvfb'))
    parser.add_argument('--xkb-dir')
    args=parser.parse_args()
    if not args.xvfb:raise SystemExit('Install Xvfb or pass --xvfb')
    EVIDENCE.mkdir(exist_ok=True)
    display='127.0.0.1:97'
    env={**os.environ,'DISPLAY':display,'LIBGL_ALWAYS_SOFTWARE':'1','MESA_SHADER_CACHE_DIR':str(BUILD/'mesa-cache')}
    xvfb=[str(pathlib.Path(args.xvfb).resolve()),':97','-screen','0','1100x800x24','-nolock','-nolisten','local','-nolisten','unix','-listen','tcp','-ac']
    if args.xkb_dir:xvfb+=['-xkbdir',str(pathlib.Path(args.xkb_dir).resolve())]
    processes=[]
    with tempfile.TemporaryDirectory(prefix='clean install ',dir=BUILD) as tmp:
        work=pathlib.Path(tmp);server_dir=work/'server';server_dir.mkdir()
        S.run(['python3',str(ROOT/'tools/check_package.py'),str(BUILD/'LapisCube-linux.zip')],check=True)
        S.run(['unzip','-q',str(BUILD/'LapisCube-linux.zip'),'-d',str(work)],check=True)
        install=work/'LapisCube-linux'
        # Configure only this isolated acceptance run; packaged defaults are unchanged.
        with (install/'options.txt').open('a') as out:out.write('soundsvolume=0\nfpslimit=Limit30FPS\n')
        S.run([str(BUILD/'survival-fixture'),identity_for('SurvivalCube'),identity_for('CombatCube')],cwd=server_dir,check=True)
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        (server_dir/'server.txt').write_text(f'port={port}\nseed=42\ngamemode=survival\n')
        token=secrets.token_hex(24)
        try:
            xv=S.Popen(xvfb,env=env,cwd=str(pathlib.Path(args.xkb_dir).resolve()) if args.xkb_dir else None,
                stdout=(EVIDENCE/'xvfb.log').open('w'),stderr=S.STDOUT);processes.append(xv)
            server=S.Popen([str(ROOT.parents[1]/'lapis-obsidian')],cwd=server_dir,env={**env,'LAPIS_ADMIN_TOKEN':token},
                stdin=S.DEVNULL,stdout=(EVIDENCE/'server.log').open('w'),stderr=S.STDOUT);processes.append(server)
            time.sleep(1)
            if xv.poll() is not None:raise RuntimeError('Xvfb failed: '+(EVIDENCE/'xvfb.log').read_text())
            ui=NativeInput(display)
            start=time.monotonic()
            game=S.Popen([str(install/'connect.sh'),'SurvivalCube','127.0.0.1',str(port)],cwd=work,env=env,
                stdout=(EVIDENCE/'client.log').open('w'),stderr=S.STDOUT);processes.append(game)
            deadline=time.monotonic()+35
            while 'world rendered and player loaded' not in (EVIDENCE/'client.log').read_text():
                if game.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('Packaged native join failed: '+(EVIDENCE/'client.log').read_text()[-2000:])
                time.sleep(.1)
            ui.attach()
            def shot(name):
                assert game.poll() is None
                S.run(['import','-display',display,'-window',str(ui.win),str(EVIDENCE/(name+'.png'))],env=env,check=True)
            time.sleep(1);shot('world')
            ui.key('b',pause=.4)
            for width,height in ((320,240),(640,360),(1024,640)):
                ui.resize(width,height);shot(f'inventory-{width}x{height}')
            ui.key('b',pause=.3);ui.resize(854,480)
            ui.chat('/admin '+token);ui.chat('/gamemode creative')
            ui.chat('/spawnmob cow 8 70 11');ui.key('7');ui.key('Down',.12,.1);ui.key('F1')
            ui.mouse(True);time.sleep(.12);shot('cow-hit');ui.mouse(False)
            time.sleep(1.2);ui.mouse(True);time.sleep(.15);ui.mouse(False);ui.key('F1');ui.key('Up',.12,.1)
            ui.chat('/time set night')
            for species,x,z in (('chicken',5,5),('cow',5,8),('pig',5,11),('sheep',8,5),
                                ('zombie',8,11),('skeleton',11,8),('spider',11,5),('creeper',11,11)):
                ui.chat(f'/spawnmob {species} {x} 70 {z}')
            ui.chat('/time set day')
            ui.key('F1');time.sleep(.3)
            for i in range(6):
                shot(f'mobs-{i}');ui.key('Right',.48,.3)
            ui.key('F1');ui.key('f',pause=.2);ui.key('Down',.38,.2)
            for x,z in ((17,17),(-17,-17),(3950,8),(-3950,-8),(8,8)):
                ui.chat(f'/tp {x} 150 {z}');time.sleep(2.5);shot(f'travel-{x}-{z}')
            ui.close()
            deadline=time.monotonic()+10
            while True:
                pid,status,usage=os.wait4(game.pid,os.WNOHANG)
                if pid:break
                if time.monotonic()>deadline:raise RuntimeError('Native close timed out')
                time.sleep(.05)
            game.returncode=os.waitstatus_to_exitcode(status)
            assert game.returncode==0
            log=(EVIDENCE/'client.log').read_text()
            assert 'Malformed' not in log and 'LapisCube streaming:' in log
            stats=re.search(r'full windows=(\d+), column copies=(\d+)',log)
            assert stats and int(stats[1])==6 and int(stats[2])>=125
            metrics={'wall_seconds':round(time.monotonic()-start,2), 'client_user_cpu_seconds':usage.ru_utime,
                     'client_system_cpu_seconds':usage.ru_stime,'client_peak_rss_kib':usage.ru_maxrss,
                     'full_window_copies':int(stats[1]),'column_copies':int(stats[2]),
                     'renderer':'Xvfb / software Mesa; 30 FPS limit; not a hardware benchmark',
                     'audio':'muted; no listening acceptance'}
            (EVIDENCE/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
            print('windowed: extracted package joined, resized, rendered mobs/travel and exited cleanly',flush=True)
            print(json.dumps(metrics),flush=True)
        finally:
            for process in reversed(processes):stop(process)


if __name__=='__main__':main()
