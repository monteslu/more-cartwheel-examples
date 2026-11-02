"""Check binary assets, including unused bytes in Blender's saved path fields."""
from pathlib import Path
import re
import struct

HOME_PATH = re.compile(rb'/home/|/Users/|[A-Za-z]:[\\/]Users[\\/]|\.config[\\/]blender')

def check_blend(data):
 # The authoring tool pins 64-bit, little-endian Blender 4.2.9. Read its SDNA
 # rather than searching only visible strings: DNA saves the entire char array.
 assert data[:12] == b'BLENDER-v402', 'unexpected Blender file format'
 blocks=[]
 offset=12
 while offset<len(data):
  code,size,address,schema,count=struct.unpack_from('<4sIQII',data,offset)
  offset+=24
  blocks.append((code,schema,count,data[offset:offset+size]))
  offset+=size
 dna=next(payload for code,_,_,payload in blocks if code==b'DNA1')
 assert dna[:8]==b'SDNANAME'
 offset=8
 def strings():
  nonlocal offset
  count=struct.unpack_from('<I',dna,offset)[0];offset+=4
  result=[]
  for _ in range(count):
   end=dna.index(b'\0',offset);result.append(dna[offset:end].decode());offset=end+1
  offset=(offset+3)&~3
  return result
 names=strings()
 assert dna[offset:offset+4]==b'TYPE';offset+=4
 types=strings()
 assert dna[offset:offset+4]==b'TLEN';offset+=4
 sizes=struct.unpack_from('<'+'H'*len(types),dna,offset);offset+=2*len(types)
 offset=(offset+3)&~3
 assert dna[offset:offset+4]==b'STRC';offset+=4
 count=struct.unpack_from('<I',dna,offset)[0];offset+=4
 checked=0
 for schema in range(count):
  type_id,fields=struct.unpack_from('<HH',dna,offset);offset+=4
  layout=[]
  for _ in range(fields):
   field_type,field_name=struct.unpack_from('<HH',dna,offset);offset+=4
   layout.append((types[field_type],names[field_name]))
  if types[type_id]!='FileSelectParams':continue
  # Both leading fields are char arrays, so there is no alignment padding.
  assert layout[:2]==[('char','title[96]'),('char','dir[1090]')], layout[:2]
  for _,block_schema,instances,payload in blocks:
   if block_schema!=schema:continue
   for i in range(instances):
    start=i*sizes[type_id]+96
    directory=payload[start:start+1090]
    visible,tail=directory.split(b'\0',1)
    assert visible==b'//', 'nonportable file browser directory'
    assert not tail.strip(b'\0 '), 'stale bytes after file browser directory terminator'
    checked+=1
 return checked

def main():
 root=Path(__file__).resolve().parents[1]/'games/pirates'
 checked=directories=0
 for directory in ('art','assets'):
  for path in sorted((root/directory).rglob('*')):
   if not path.is_file() or path.suffix not in ('.blend','.glb','.png','.jpg'):continue
   data=path.read_bytes()
   assert not HOME_PATH.search(data), str(path.relative_to(root))+' contains a home-path fragment'
   if path.suffix=='.blend':
    try:directories+=check_blend(data)
    except AssertionError as error:raise AssertionError(str(path.relative_to(root))+': '+str(error)) from error
   checked+=1
 assert checked>20 and directories>0
 print(f'{checked} binary assets and {directories} complete Blender directory fields are portable')

if __name__=='__main__':main()
