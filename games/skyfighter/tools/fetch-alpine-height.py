#!/usr/bin/env python3
"""Prepare a fictional coastal map's inland relief from real alpine DEM samples.
Source: Mapzen/Tilezen Terrain Tiles, AWS Open Data; underlying Europe EU-DEM.
No imagery/map services are needed at runtime.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import math,struct,subprocess
from PIL import Image
root=Path(__file__).resolve().parents[1];cache=root/'assets/source-terrain/dem';cache.mkdir(exist_ok=True)
z=13;lat=46.57;lon=8.04;n=2**z
cx=(lon+180)/360*n;cy=(1-math.asinh(math.tan(math.radians(lat)))/math.pi)/2*n
metres=40075016.686*math.cos(math.radians(lat))/n/256
span=8192/metres;left=cx*256-span/2;top=cy*256-span/2
x0=int(left//256);y0=int(top//256);x1=int((left+span)//256);y1=int((top+span)//256)
def fetch(pair):
 x,y=pair;p=cache/f'{z}-{x}-{y}.png'
 if not p.exists():subprocess.run(['curl','-fsSL','--retry','2',f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png','-o',str(p)],check=True)
 return x,y,Image.open(p).convert('RGB')
height=Image.new('F',((x1-x0+1)*256,(y1-y0+1)*256))
for x,y,im in ThreadPoolExecutor(6).map(fetch,[(x,y)for y in range(y0,y1+1)for x in range(x0,x1+1)]):
 tile=Image.new('F',(256,256));tile.putdata([r*256+g+b/256-32768 for r,g,b in im.getdata()]);height.paste(tile,((x-x0)*256,(y-y0)*256))
a=left-x0*256;b=top-y0*256
height=height.resize((513,513),Image.Resampling.BILINEAR,box=(a,b,a+span,b+span));values=list(height.getdata());minimum=min(values)
# Coast and airfield are fictional; preserve the real drainage/ridge pattern,
# compressing vertical relief to fit this game's eight-kilometre theatre.
values=[(v-minimum)*0.48+80 for v in values]
(root/'assets/terrain/alpine-height.f32').write_bytes(struct.pack('<'+str(len(values))+'f',*values))
print('DEM metres',min(values),max(values),'tiles',(x1-x0+1)*(y1-y0+1))
