#!/usr/bin/env python3
"""Generate the original short notification chime (GPL-3.0-or-later)."""
import math,struct
from pathlib import Path
rate=11025;count=int(rate*.28);samples=bytearray()
for i in range(count):
 t=i/rate;value=0
 for delay,freq in [(0,880),(.085,1174.66)]:
  u=t-delay
  if u>=0:value+=38*min(1,u/.004)*math.exp(-u/0.065)*math.sin(2*math.pi*freq*u)
 samples.append(int(max(-100,min(100,value)))&255)
vhdr=struct.pack('>IIIHBBI',count,0,0,rate,1,0,65536)
def chunk(tag,data):return tag+struct.pack('>I',len(data))+data+(b'\0' if len(data)%2 else b'')
content=b'8SVX'+chunk(b'VHDR',vhdr)+chunk(b'BODY',bytes(samples))
path=Path(__file__).resolve().parents[1]/'assets/sounds/Notification.8svx';path.parent.mkdir(exist_ok=True);path.write_bytes(b'FORM'+struct.pack('>I',len(content))+content)
