#!/usr/bin/env python3
"""Full REST+DDP reconnect test: force proxy drops, recover missed deltas, deduplicate."""
import base64
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from http.server import ThreadingHTTPServer
import mock_rocketchat as mock

CLIENT=Path(sys.argv[1] if len(sys.argv)>1 else 'build/host/realtime_check').resolve()

class RealtimeHandler(mock.Handler):
    def do_GET(self):
        if self.path!=self.server.prefix+'/websocket': return super().do_GET()
        key=self.headers.get('Sec-WebSocket-Key','')
        self.send_response(101); self.send_header('Upgrade','websocket'); self.send_header('Connection','Upgrade')
        self.send_header('Sec-WebSocket-Accept',base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()); self.end_headers()
        active=''; subscriptions={}; generation=self.server.generation; signature=None; last_ping=time.monotonic()
        with self.server.state_lock:
            self.server.active+=1; self.server.peak=max(self.server.peak,self.server.active)
        try:
            while generation==self.server.generation:
                if select.select([self.connection],[],[],.05)[0]:
                    opcode,value=self.receive()
                    if opcode==8: return
                    if opcode!=1: continue
                    if value['msg']=='connect': self.send({'msg':'connected','session':'test'})
                    elif value['msg']=='method':
                        assert value['method']=='login' and value['params']==[{'resume':'recovery-token'}]
                        self.server.logins+=1; self.send({'msg':'result','id':value['id'],'result':{'id':'uid-tommy'}})
                    elif value['msg']=='sub':
                        subscriptions[value['id']]=value
                        if value['name']=='stream-room-messages': active=value['params'][0]; signature=None
                        self.send({'msg':'ready','subs':[value['id']]})
                        if len(subscriptions)==4: self.server.subscriptions.append(tuple(sorted(v['name'] for v in subscriptions.values())))
                    elif value['msg']=='unsub': subscriptions.pop(value['id'],None)
                    elif value['msg']=='pong': self.server.pongs+=1
                if active:
                    with mock.LOCK:
                        current=(tuple((m['_id'],m['_updatedAt']) for m in mock.MESSAGES[active]),tuple(mock.DELETED[active]))
                    if signature is not None and current!=signature:
                        event=active+'/deleteMessage' if current[1]!=signature[1] else active
                        self.send({'msg':'changed','collection':'stream-notify-room' if '/' in event else 'stream-room-messages','fields':{'eventName':event}})
                    signature=current
                if time.monotonic()-last_ping>1:
                    self.send({'msg':'ping','id':'heartbeat'}); last_ping=time.monotonic()
            # Transport close without a Close frame mimics a dropped proxy connection.
        except (EOFError,ConnectionError,OSError): pass
        except Exception as error: self.server.errors.append(repr(error))
        finally:
            with self.server.state_lock: self.server.active-=1
            self.close_connection=True
    def receive(self):
        def exact(n):
            data=self.rfile.read(n)
            if len(data)!=n: raise EOFError()
            return data
        a,b=exact(2); assert b&128
        n=b&127
        if n==126: n=struct.unpack('!H',exact(2))[0]
        assert n<8192
        mask=exact(4); data=exact(n); data=bytes(v^mask[i%4] for i,v in enumerate(data))
        return a&15,json.loads(data) if a&15==1 else data
    def send(self,value):
        data=json.dumps(value).encode(); header=bytes([0x81,len(data)]) if len(data)<126 else bytes([0x81,126])+struct.pack('!H',len(data))
        self.connection.sendall(header+data)


def main():
    mock.seed(); mock.TOKENS['recovery-token']='tommy'
    srv=ThreadingHTTPServer(('127.0.0.1',0),RealtimeHandler); srv.daemon_threads=True
    for k,v in dict(prefix='/prefix',verbose=False,ascii_json=False,close_every=0,chunked=False,lose_send_reply=False,read_calls=[],generation=0,active=0,peak=0,logins=0,pongs=0,subscriptions=[],errors=[]).items(): setattr(srv,k,v)
    srv.state_lock=threading.Lock(); thread=threading.Thread(target=srv.serve_forever,daemon=True);thread.start()
    base='http://127.0.0.1:%d/prefix'%srv.server_port; process=None
    def api(path,value):
        req=urllib.request.Request(base+path,data=json.dumps(value).encode(),headers={'Content-Type':'application/json'})
        return json.load(urllib.request.urlopen(req,timeout=5))
    try:
        with tempfile.TemporaryDirectory(prefix='ar-recovery-') as td:
            root=Path(td); config=root/'config.json'; report=root/'status.json'; control=root/'control.json'; seq=0
            config.write_text(json.dumps({'url':base,'user_id':'uid-tommy','token':'recovery-token','room_id':'GENERAL'})); os.chmod(config,0o600)
            def command(watches=(),stop=False):
                nonlocal seq
                seq+=1; tmp=root/'control.new'; tmp.write_text(json.dumps({'seq':seq,'watches':list(watches),'stop':stop})); tmp.replace(control)
            def status():
                try: return json.loads(report.read_text())
                except (OSError,ValueError): return {}
            def wait(predicate,label,timeout=25):
                until=time.monotonic()+timeout
                while time.monotonic()<until:
                    s=status()
                    if predicate(s): print('PASS',label,flush=True); return s
                    assert process.poll() is None,(label,'client exited',process.returncode)
                    time.sleep(.05)
                raise AssertionError((label,status(),srv.errors))
            command()
            process=subprocess.Popen([str(CLIENT),str(config),'90',str(report),str(control)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            wait(lambda s:s.get('live'),'initial four subscriptions')
            message=api('/mock/inject',{'rid':'GENERAL','msg':'before proxy drop'})['message']; mid=message['_id']
            command([{'id':mid,'text':'before proxy drop'}])
            wait(lambda s:any(w['present'] and w['text_matches'] and w['count']==1 for w in s.get('watches',[])),'event-triggered REST delivery')
            before=status()['reconnects']; srv.generation+=1
            wait(lambda s:not s.get('live') and s.get('phase')==0,'server-side transport drop detected')
            api('/mock/edit',{'rid':'GENERAL','id':mid,'msg':'edited while disconnected'})
            missed=api('/mock/inject',{'rid':'GENERAL','msg':'sent while disconnected'})['message']; second=missed['_id']
            command([{'id':mid,'text':'edited while disconnected'},{'id':second,'text':'sent while disconnected'}])
            wait(lambda s:s.get('live') and s.get('reconnects',0)>before and len(s.get('watches',[]))==2 and all(w['present'] and w['text_matches'] and w['count']==1 for w in s['watches']),'automatic reconnect, resubscription and missed edit/message recovery')
            for cycle in range(2):
                before=status()['reconnects']; srv.generation+=1
                wait(lambda s:s.get('reconnects',0)>before and s.get('live'),'repeated recovery %d'%(cycle+1))
                assert status()['duplicates']==0
            api('/mock/delete',{'rid':'GENERAL','id':mid})
            wait(lambda s:len(s.get('watches',[]))==2 and not s['watches'][0]['present'] and s['watches'][1]['count']==1,'live deletion with another message retained')
            command(stop=True); out,err=process.communicate(timeout=12); assert process.returncode==0,(out,err)
            deadline=time.monotonic()+3
            while srv.active and time.monotonic()<deadline: time.sleep(.05)
            assert srv.active==0 and srv.peak<=1,(srv.active,srv.peak)
            assert srv.logins>=4 and len(srv.subscriptions)>=4 and srv.pongs>0
            assert not srv.errors,srv.errors
            assert not srv.read_calls,'background sync must not mark rooms read'
            assert status()['duplicates']==0
            print('PASS reconnect socket cleanup and background unread preservation')
    finally:
        if process and process.poll() is None: process.terminate();process.wait(timeout=12)
        srv.shutdown();srv.server_close();thread.join()
    print('Realtime recovery integration: 8 checks passed')

if __name__=='__main__': main()
