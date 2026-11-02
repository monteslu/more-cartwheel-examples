"""Check binary authoring/runtime assets for embedded workstation home paths."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]/'games/pirates'
checked=0
for directory in ('art','assets'):
 for path in sorted((root/directory).rglob('*')):
  if not path.is_file() or path.suffix not in ('.blend','.glb','.png','.jpg'):continue
  data=path.read_bytes()
  assert not re.search(rb'/home/|/Users/|[A-Za-z]:[\\/]Users[\\/]',data), str(path.relative_to(root))+' contains a home path'
  checked+=1
assert checked>20
print(f'{checked} binary assets contain no workstation home paths')
