#!/usr/bin/env python3
"""Crossfade public-domain F101 test-cell excerpts into quiet game engine loops.
Excerpts are extracted with the two ffmpeg commands documented in README.
"""
from pathlib import Path
import array,math,wave
root=Path(__file__).resolve().parents[1]
for source,name in [('/private/tmp/f101-idle.pcm','engine-idle'),('/private/tmp/f101-burn.pcm','engine-burn')]:
 data=array.array('h',Path(source).read_bytes());fade=32000
 # Equal-power overlap of the last second and the first: the output's
 # boundary is inside the continuous recording, never an abrupt sample cut.
 result=list(data[fade:-fade]);tail=data[-fade:];head=data[:fade]
 result += [tail[i]*math.cos(i/fade*math.pi/2)+head[i]*math.sin(i/fade*math.pi/2)for i in range(fade)]
 rms=math.sqrt(sum(v*v for v in result)/len(result));gain=min(.18*32768/rms,30000/max(abs(v)for v in result))
 out=array.array('h',[int(max(-30000,min(30000,v*gain)))for v in result]);(root/f'assets/audio/{name}.s16').write_bytes(out.tobytes())
 print(name,'seconds',len(out)/32000,'rms',round(math.sqrt(sum(v*v for v in out)/len(out))/32768,3),'peak',max(abs(v)for v in out)/32768)
