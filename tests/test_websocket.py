#!/usr/bin/env python3
"""Real TCP/TLS WebSocket tests; no third-party Python dependencies."""
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROBE = str(Path(sys.argv[1] if len(sys.argv)>1 else 'build/host/realtime_probe').resolve())

class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args): pass
    def exact(self,n):
        result=b''
        while len(result)<n:
            block=self.rfile.read(n-len(result))
            if not block: raise EOFError()
            result+=block
        return result
    def receive(self):
        a,b=self.exact(2)
        assert a&128 and b&128, 'client frames must be final and masked'
        size=b&127
        if size==126: size=struct.unpack('!H',self.exact(2))[0]
        assert size<8192
        key=self.exact(4); data=self.exact(size)
        data=bytes(v^key[i%4] for i,v in enumerate(data))
        return a&15,json.loads(data) if a&15==1 else data
    def send(self,obj,op=1,fragment=False):
        data=json.dumps(obj).encode() if isinstance(obj,dict) else obj
        def part(payload,opcode,fin):
            h=bytes([(128 if fin else 0)|opcode])
            h+=bytes([len(payload)]) if len(payload)<126 else bytes([126])+struct.pack('!H',len(payload))
            self.connection.sendall(h+payload)
        if fragment:
            mid=len(data)//2; part(data[:mid],op,False); part(b'wire-ping',9,True); part(data[mid:],0,True)
        else: part(data,op,True)
    def handle(self):
        if self.server.mode=="slow-tls" and not isinstance(self.connection,ssl.SSLSocket):
            time.sleep(12); return
        super().handle()
    def do_GET(self):
        try:
            if self.path=='/prefix/api/info':
                body=b'{"success":true,"version":"test"}'; self.send_response(200); self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body); return
            assert self.path=='/prefix/websocket'
            assert self.headers['Upgrade'].lower()=='websocket'
            if self.server.mode in ('slow-header','silent-upgrade'):
                if self.server.mode=='silent-upgrade': time.sleep(12)
                else:
                    for byte in b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n':
                        self.connection.sendall(bytes([byte])); time.sleep(.2)
                return
            key=self.headers['Sec-WebSocket-Key']
            assert len(base64.b64decode(key))==16
            accept=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
            self.send_response(101); self.send_header('Upgrade','websocket'); self.send_header('Connection','Upgrade')
            self.send_header('Sec-WebSocket-Accept','bad' if self.server.mode=='bad-accept' else accept); self.end_headers()
            if self.server.mode=='bad-accept': return
            op,connect=self.receive(); assert connect=={'msg':'connect','version':'1','support':['1']}
            self.send({'msg':'connected','session':'test'})
            op,login=self.receive(); assert login['method']=='login' and login['params']==[{'resume':'test-resume-token'}]
            self.server.log.append('login')
            if self.server.mode=='auth-error': self.send({'msg':'result','id':'login','error':{'error':403}}); return
            self.send({'msg':'result','id':'login','result':{'id':'U'}})
            subscriptions=[]
            while len(subscriptions)<4:
                op,msg=self.receive(); assert msg['msg']=='sub'; subscriptions.append(msg)
            assert {x['name'] for x in subscriptions}=={'stream-room-messages','stream-notify-user','stream-notify-room'}
            if self.server.mode=='sub-error': self.send({'msg':'nosub','id':'rooms','error':{'error':403}}); return
            self.send({'msg':'ready','subs':[x['id'] for x in subscriptions]}); time.sleep(.1)
            self.send({'msg':'changed','collection':'stream-room-messages','fields':{'eventName':'first','args':[{'msg':'new message'}]}},fragment=True)
            room_ids=[]
            while len(room_ids)<2:
                op,msg=self.receive()
                if op==10: assert msg==b'wire-ping'; self.server.log.append('control-pong'); continue
                if msg['msg']=='unsub': self.server.log.append('unsub'); continue
                if msg['msg']=='sub':
                    assert msg['params'] in (['second',False],['second/deleteMessage',False]); room_ids.append(msg['id'])
            self.send({'msg':'ready','subs':room_ids}); time.sleep(.1)
            self.send({'msg':'changed','collection':'stream-notify-room','fields':{'eventName':'second/deleteMessage'}})
            self.send({'msg':'changed','collection':'stream-notify-user','fields':{'eventName':'U/subscriptions-changed'}})
            self.send({'msg':'ping','id':'heartbeat'})
            op,pong=self.receive(); assert pong=={'msg':'pong','id':'heartbeat'}; self.server.log.append('ddp-pong')
            time.sleep(.05); self.send(struct.pack('!H',1000),8)
        except (EOFError,ConnectionError): pass
        except Exception as error: self.server.errors.append(repr(error))
        finally: self.close_connection=True

class SlowTLSServer(ThreadingHTTPServer):
    accepted=0
    def get_request(self):
        sock,addr=self.socket.accept(); self.accepted+=1
        if self.accepted==1: sock=self.context.wrap_socket(sock,server_side=True)
        return sock,addr

def run(mode,context=None,ca=None):
    server=(SlowTLSServer if mode=='slow-tls' else ThreadingHTTPServer)(('127.0.0.1',0),Handler); server.daemon_threads=True
    server.mode=mode; server.log=[]; server.errors=[]
    if mode=="slow-tls": server.context=context
    elif context: server.socket=context.wrap_socket(server.socket,server_side=True)
    thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    try:
        env=dict(os.environ); env.pop("AMIROCKET_CA_FILE",None)
        if ca: env['AMIROCKET_CA_FILE']=str(ca)
        url=('https' if context else 'http')+'://localhost:%d/prefix'%server.server_port
        started=time.monotonic()
        p=subprocess.run([PROBE,url,'success' if mode=='ok' else 'failure'],env=env,capture_output=True,text=True,timeout=15)
        elapsed=time.monotonic()-started
        if mode in ("slow-header","silent-upgrade","slow-tls"): assert 9<=elapsed<12,(mode,elapsed)
        assert p.returncode==0,(mode,p.stdout,p.stderr,server.errors)
        assert not server.errors,server.errors
        if mode=='ok': assert server.log==['login','control-pong','unsub','unsub','ddp-pong'],server.log
        print('PASS',mode,'TLS' if context else 'TCP',p.stdout.strip())
    finally: server.shutdown(); server.server_close(); thread.join()

for mode in ['ok','bad-accept','auth-error','sub-error','slow-header','silent-upgrade']: run(mode)
with tempfile.TemporaryDirectory() as d:
    cert=Path(d)/'cert.pem'; key=Path(d)/'key.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(cert,key)
    run('ok',ctx,cert)
    run('tls-reject',ctx)
    run('slow-tls',ctx,cert)
print('WebSocket integration: 9 scenarios passed')
