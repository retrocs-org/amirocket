#!/usr/bin/env python3
"""Opt-in live Rocket.Chat check. Never put credentials in command arguments.

A --self-test run creates/uses ONLY the authenticated user's one-person self-chat.
It sends two labelled messages, edits/deletes them and cleans up in finally.
The default mode is read-only and requires room_id in the private config.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import signal
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


def save(path, obj):
    temp=path.with_suffix(path.suffix+'.new')
    with open(temp,'w') as stream: json.dump(obj,stream,indent=2)
    os.chmod(temp,0o600); temp.replace(path)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--credentials',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--duration',type=int,default=7200)
    ap.add_argument('--self-test',action='store_true',help='Explicitly allow temporary messages in your private self-chat')
    ap.add_argument('--client',type=Path,default=Path('build/host/realtime_check'))
    args=ap.parse_args(); os.umask(0o077)
    def interrupted(signum,frame): raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM,interrupted)
    if not 60<=args.duration<=86400: ap.error('duration must be 60–86400 seconds')
    if stat.S_IMODE(args.credentials.stat().st_mode)&0o077: ap.error('credentials must be private (chmod 600)')
    c=json.loads(args.credentials.read_text()); base=c['url'].rstrip('/')
    if urllib.parse.urlsplit(base).scheme!='https': ap.error('live checks require verified HTTPS')
    args.output.mkdir(parents=True,exist_ok=True); os.chmod(args.output,0o700)
    result={'checks':{},'samples':[],'cleanup_pending':[],'complete':False}; pending=[]; proc=None; last_request=0; seq=0
    start=time.monotonic()
    def api(path,obj=None):
        nonlocal last_request
        delay=last_request+1.1-time.monotonic()
        if delay>0: time.sleep(delay)
        headers={'User-Agent':'AmiRocket-0.2-release-check','X-User-Id':c['user_id'],'X-Auth-Token':c['token']}
        if obj is not None: headers['Content-Type']='application/json'
        request=urllib.request.Request(base+path,data=json.dumps(obj).encode() if obj is not None else None,headers=headers)
        last_request=time.monotonic()
        try:
            with urllib.request.urlopen(request,timeout=15) as response: value=json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError('Live endpoint %s returned HTTP %d (request not replayed)'%(path.split('?')[0],error.code)) from None
        if value.get('success') is False: raise RuntimeError('Live API rejected the test operation (response omitted)')
        return value
    report=args.output/'client-status.json'; control=args.output/'control.json'; log=args.output/'client.log'
    def status():
        try: return json.loads(report.read_text())
        except (OSError,ValueError): return {}
    def wait(predicate,label,seconds=30):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            value=status()
            if predicate(value): result['checks'][label]=True; print('PASS',label,flush=True); return value
            if proc and proc.poll() is not None: raise RuntimeError('Check client exited before '+label)
            time.sleep(.2)
        raise RuntimeError('Timed out waiting for '+label)
    def command(watches=(),pause=0,stop=False):
        nonlocal seq
        seq+=1; save(control,{'seq':seq,'watches':list(watches),'pause_seconds':pause,'stop':stop})
    try:
        me=api('/api/v1/me')
        if me.get('_id')!=c['user_id']: raise RuntimeError('Authenticated account does not match configured user id')
        result['checks']['verified_https_authentication']=True
        if args.self_test:
            room=api('/api/v1/im.create',{'usernames':me['username']})['room']; rid=room.get('rid') or room.get('_id')
            info=api('/api/v1/rooms.info?'+urllib.parse.urlencode({'roomId':rid}))['room']
            if info.get('t')!='d' or set(info.get('uids',[]))!={c['user_id']}:
                raise RuntimeError('Refusing writes: conversation is not a verified one-person self-chat')
            c['room_id']=rid; result['checks']['one_person_self_chat_verified']=True
        elif not c.get('room_id'): raise RuntimeError('Read-only mode requires room_id in the credentials file')
        with tempfile.TemporaryDirectory(prefix='amirocket-live-') as private:
            config=Path(private)/'session.json'; save(config,c)
            command()
            with open(log,'w') as logfile:
                proc=subprocess.Popen([str(args.client.resolve()),str(config),str(args.duration),str(report),str(control)],stdout=logfile,stderr=subprocess.STDOUT)
            initial=wait(lambda s:s.get('live')==1,'authenticated_websocket_subscriptions')
            if args.self_test:
                tag='AmiRocket release check '+uuid.uuid4().hex[:12]
                message=api('/api/v1/chat.sendMessage',{'message':{'rid':c['room_id'],'msg':tag}})['message']
                mid=message['_id']; pending.append(mid)
                if message.get('rid')!=c['room_id'] or message.get('u',{}).get('_id')!=c['user_id']: raise RuntimeError('Unexpected message owner or destination')
                command([{'id':mid,'text':tag}]); sent=time.monotonic()
                wait(lambda s:any(w['id']==mid and w['present'] and w['text_matches'] and w['count']==1 for w in s.get('watches',[])),'live_message_delivery')
                result['delivery_seconds']=round(time.monotonic()-sent,3)
                edited=tag+' (edited)'; api('/api/v1/chat.update',{'roomId':c['room_id'],'msgId':mid,'text':edited})
                command([{'id':mid,'text':edited}])
                wait(lambda s:any(w['id']==mid and w['present'] and w['text_matches'] for w in s.get('watches',[])),'live_edit_delivery')
                before=status(); api('/api/v1/chat.delete',{'roomId':c['room_id'],'msgId':mid}); pending.remove(mid)
                wait(lambda s:s.get('syncs',0)>before.get('syncs',0) and any(w['id']==mid and not w['present'] for w in s.get('watches',[])),'live_deletion_delivery')
                before=status(); command(pause=12)
                wait(lambda s:s.get('forced_drops',0)>before.get('forced_drops',0) and not s.get('live'),'forced_disconnect')
                missed=tag+' (sent while stream disconnected)'
                message=api('/api/v1/chat.sendMessage',{'message':{'rid':c['room_id'],'msg':missed}})['message']; lost_id=message['_id']; pending.append(lost_id)
                command([{'id':lost_id,'text':missed}])
                recovered=wait(lambda s:s.get('live') and s.get('reconnects',0)>before.get('reconnects',0) and any(w['id']==lost_id and w['present'] and w['text_matches'] and w['count']==1 for w in s.get('watches',[])),'reconnect_resubscribe_and_missed_message_recovery',45)
                if recovered.get('duplicates'): raise RuntimeError('Duplicate messages after recovery')
                api('/api/v1/chat.delete',{'roomId':c['room_id'],'msgId':lost_id}); pending.remove(lost_id)
                wait(lambda s:any(w['id']==lost_id and not w['present'] for w in s.get('watches',[])),'test_messages_removed')
            first=status(); baseline=None; next_drop=time.monotonic()+300; next_sample=time.monotonic(); max_rss=0; min_rss=None
            while proc.poll() is None:
                t=time.monotonic(); s=status()
                if t>=next_sample:
                    sample={k:s.get(k) for k in ('elapsed_ms','live','reconnects','forced_drops','syncs','requests','rpm','http_errors','duplicates','rss_kib','socket_fds')}
                    result['samples'].append(sample); save(args.output/'summary.json',result); next_sample=t+60
                    if s.get('duplicates',0): raise RuntimeError('Duplicate message cache entry during soak')
                    if s.get('socket_fds',0)>3: raise RuntimeError('Unexpected socket accumulation during soak')
                    if s.get('elapsed_ms',0)>60000 and s.get('rss_kib',0)>0:
                        rss=s['rss_kib']; min_rss=rss if min_rss is None else min(min_rss,rss); max_rss=max(max_rss,rss)
                        if baseline is None: baseline=rss
                        if rss>baseline+8192: raise RuntimeError('Resident memory grew by more than 8 MiB after warm-up')
                if t>=next_drop:
                    command(pause=8); next_drop=t+300
                time.sleep(.25)
            if proc.returncode: raise RuntimeError('Check client failed; inspect its credential-free log')
            final=status(); result['final']={k:v for k,v in final.items() if k!='watches'}
            result['checks']['no_duplicate_cache_entries']=not final.get('duplicates')
            result['checks']['bounded_socket_count']=final.get('socket_fds',99)<=3
            result['checks']['completed_requested_duration']=final.get('elapsed_ms',0)>=(args.duration-2)*1000
            result['checks']['realtime_connected_at_finish']=bool(final.get('live'))
            result['checks']['periodic_reconnect_recovery']=args.duration<400 or final.get('reconnects',0)>first.get('reconnects',0)
            result['rss_range_kib']=[min_rss,max_rss]; result['complete']=all(result['checks'].values())
            if not result['complete']: raise RuntimeError('One or more release checks failed')
    except Exception as error:
        result['error']=str(error) if isinstance(error,(RuntimeError,AssertionError)) else type(error).__name__
        print('FAIL',result['error'],flush=True)
    finally:
        if proc and proc.poll() is None:
            command(stop=True)
            try: proc.wait(timeout=12)
            except subprocess.TimeoutExpired: proc.terminate(); proc.wait(timeout=8)
        for mid in pending:
            try: api('/api/v1/chat.delete',{'roomId':c['room_id'],'msgId':mid})
            except Exception: result['cleanup_pending'].append(mid)
        result['wall_seconds']=round(time.monotonic()-start,2); save(args.output/'summary.json',result)
        if result['complete']: print('PASS live release check; summary:',args.output/'summary.json',flush=True)
    return 0 if result['complete'] and not result['cleanup_pending'] else 1

if __name__=='__main__': raise SystemExit(main())
