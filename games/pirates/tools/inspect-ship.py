import bpy, math, json
from mathutils import Vector
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root/'art/sources/GalleonOGA.blend'))
meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
print('SHIP_OBJECTS',json.dumps([{'name':o.name,'vertices':len(o.data.vertices),'dimensions':list(o.dimensions)} for o in meshes]))
points=[o.matrix_world@Vector(p) for o in meshes for p in o.bound_box]
lo=Vector([min(p[i] for p in points) for i in range(3)])
hi=Vector([max(p[i] for p in points) for i in range(3)])
center=(lo+hi)/2; extent=max(hi-lo)
for o in list(bpy.context.scene.objects):
 if o.type in ('LIGHT','CAMERA'):bpy.data.objects.remove(o,do_unlink=True)
mat=bpy.data.materials.new('Inspection clay');mat.diffuse_color=(.42,.32,.21,1);mat.use_nodes=True
mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.42,.32,.21,1)
for o in meshes:
 o.data.materials.clear();o.data.materials.append(mat)
bpy.ops.object.camera_add(location=center+Vector((1.1,-1.5,1.0))*extent)
camera=bpy.context.object;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=extent*1.35;bpy.context.scene.camera=camera
for offset,energy,size in [((1,-1,2),35,1.5),((-1,0,1),20,1)]:
 bpy.ops.object.light_add(type='AREA',location=center+Vector(offset)*extent)
 light=bpy.context.object;light.data.energy=energy*extent*extent;light.data.shape='DISK';light.data.size=size*extent
 light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler()
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16
scene.world.color=(.18,.18,.18)
scene.view_settings.view_transform='AgX'
scene.view_settings.exposure=0
scene.view_settings.gamma=1
scene.render.resolution_x=1000;scene.render.resolution_y=800;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(root/'art/galleon-inspection.png')
bpy.ops.render.render(write_still=True)
