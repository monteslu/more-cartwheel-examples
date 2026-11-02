"""Re-save authoring files in Blender 4.2.9 with portable resource/UI paths."""
import bpy, hashlib
from array import array
from pathlib import Path
root=Path(__file__).resolve().parents[1]
assert bpy.app.version[:3]==(4,2,9), bpy.app.version_string
bpy.context.preferences.filepaths.save_version=0
def geometry_digest():
 h=hashlib.sha256()
 for mesh in sorted(bpy.data.meshes,key=lambda x:x.name):
  h.update(mesh.name.encode());coords=array('f',[0])*(3*len(mesh.vertices))
  mesh.vertices.foreach_get('co',coords);h.update(coords.tobytes())
  if mesh.shape_keys:
   for key in mesh.shape_keys.key_blocks:
    key.data.foreach_get('co',coords);h.update(key.name.encode());h.update(coords.tobytes())
 for image in sorted(bpy.data.images,key=lambda x:x.name):
  for packed in image.packed_files:h.update(bytes(packed.packed_file.data))
 return h.digest()
for path in sorted((root/'art').rglob('*.blend')):
 bpy.ops.wm.open_mainfile(filepath=str(path))
 before=geometry_digest()
 for image in bpy.data.images:
  if image.filepath:
   image.filepath='//'+image.filepath.replace('\\','/').rsplit('/',1)[-1]
  for packed in image.packed_files:
   packed.filepath='//'+packed.filepath.replace('\\','/').rsplit('/',1)[-1]
 for scene in bpy.data.scenes:
  scene.render.filepath='//renders/'
  scene.render.use_stamp=False
 for screen in bpy.data.screens:
  for area in screen.areas:
   for space in area.spaces:
    if space.type=='FILE_BROWSER' and space.params:
     space.params.directory=b'//'
 bpy.ops.wm.save_as_mainfile(filepath=str(path),relative_remap=False)
 # Reload with the pinned version, so validation covers the actual saved file.
 bpy.ops.wm.open_mainfile(filepath=str(path))
 assert geometry_digest()==before, path.name+' geometry/morph/packed-image change'
 assert b'/home/' not in path.read_bytes(), path.name+' retains a home path'
 print('PORTABLE',path.name,len(bpy.data.objects),flush=True)
