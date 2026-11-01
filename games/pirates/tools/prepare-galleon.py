"""Prepare Daniel Quevedo's CC0 source; bake materials and export game GLB."""
import bpy, bmesh, math
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root/'art/sources/GalleonOGA.blend'))
ship=bpy.data.objects['Ship']
bpy.ops.object.select_all(action='DESELECT');ship.select_set(True);bpy.context.view_layer.objects.active=ship
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.separate(type='LOOSE');bpy.ops.object.mode_set(mode='OBJECT')
def material(name,color,wood=False):
 m=bpy.data.materials.new(name);m.use_nodes=True;n=m.node_tree.nodes;l=m.node_tree.links
 bs=n.get('Principled BSDF');bs.inputs['Roughness'].default_value=.82
 tex=n.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=18;tex.inputs['Detail'].default_value=3
 coord=n.new('ShaderNodeTexCoord');mapping=n.new('ShaderNodeVectorMath');mapping.operation='MULTIPLY';mapping.inputs[1].default_value=(.3,22,30) if wood else (12,12,12)
 l.new(coord.outputs['Generated'],mapping.inputs[0]);l.new(mapping.outputs[0],tex.inputs['Vector'])
 ramp=n.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].color=tuple(c*.48 for c in color)+(1,);ramp.color_ramp.elements[1].color=tuple(color)+(1,)
 l.new(tex.outputs['Fac'],ramp.inputs[0]);l.new(ramp.outputs[0],bs.inputs['Base Color'])
 return m
oak=material('Weathered oak',(.23,.105,.043),True)
deck=material('Scrubbed deck',(.49,.32,.16),True)
cloth=material('Woven flax canvas',(.78,.72,.56))
iron=material('Iron guns',(.045,.052,.055))
flag=material('Dark red pennants',(.22,.025,.018))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in objects:
 idx=int(o.name.split('.')[-1]) if '.' in o.name else 0
 mat=cloth if 78<=idx<=85 else flag if idx>=86 else iron if idx<64 and idx%2 else oak
 o.data.materials.clear();o.data.materials.append(mat)
 if idx==64:
  o.data.materials.append(deck)
  for p in o.data.polygons:
   if p.normal.z>.65:p.material_index=1
 # Normalize source coordinates: bow +Y in Blender becomes +Z in glTF.
 for v in o.data.vertices:
  x,y,z=v.co;v.co=((y-4.5)*.17,(x+3)*.17,(z-.3)*.17)
 o.location=(0,0,0);o.rotation_euler=(0,0,0);o.scale=(1,1,1)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0001);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 if 78<=idx<=85:
  group=o.vertex_groups.new(name='Canvas')
  group.add(list(range(len(o.data.vertices))),1.0,'REPLACE')
bpy.ops.object.select_all(action='DESELECT')
for o in objects:o.select_set(True)
bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join();ship=bpy.context.object;ship.name='Galleon'
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=1.15,island_margin=.008);bpy.ops.object.mode_set(mode='OBJECT')
atlas=bpy.data.images.new('GalleonAlbedo',width=2048,height=2048)
for m in ship.data.materials:
 node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;m.node_tree.nodes.active=node
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=8
scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False;scene.render.bake.use_pass_color=True;scene.render.bake.margin=8
bpy.ops.object.bake(type='DIFFUSE')
atlas.filepath_raw=str(root/'art/galleon-albedo.png');atlas.file_format='PNG';atlas.save();atlas.pack()
final=bpy.data.materials.new('Galleon baked oak and canvas');final.use_nodes=True
node=final.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas
bs=final.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.8
final.node_tree.links.new(node.outputs['Color'],bs.inputs['Base Color'])
ship.data.materials.clear();ship.data.materials.append(final)
for p in ship.data.polygons:p.material_index=0
# Keep the baked atlas, but give only canvas a deformation target. Hull,
# rigging and spars remain rigid. Subdivision adds cloth tessellation without
# rounding away the authored sail edges or changing the hull's triangle count.
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='DESELECT');bpy.ops.object.mode_set(mode='OBJECT')
canvas=ship.vertex_groups.get('Canvas')
assert canvas is not None,'Source sails were not identified'
for v in ship.data.vertices:
 v.select=any(g.group==canvas.index for g in v.groups)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.separate(type='SELECTED');bpy.ops.object.mode_set(mode='OBJECT')
sails=next(o for o in bpy.context.selected_objects if o!=ship)
bpy.ops.object.select_all(action='DESELECT');sails.select_set(True);bpy.context.view_layer.objects.active=sails
sails.name='Wind-filled canvas'
sub=sails.modifiers.new('Cloth tessellation','SUBSURF');sub.subdivision_type='SIMPLE';sub.levels=1
bpy.ops.object.modifier_apply(modifier=sub.name)
# Connected components keep each sail's upper/lower/outer extrema pinned.
adj=[[] for _ in sails.data.vertices]
for e in sails.data.edges:
 a,b=e.vertices;adj[a].append(b);adj[b].append(a)
unseen=set(range(len(adj)));panels=[]
while unseen:
 stack=[unseen.pop()];panel=[]
 while stack:
  v=stack.pop();panel.append(v)
  for neighbor in adj[v]:
   if neighbor in unseen:unseen.remove(neighbor);stack.append(neighbor)
 panels.append(panel)
sails.shape_key_add(name='Basis');billow=sails.shape_key_add(name='Billow')
for panel in panels:
 points=[sails.data.vertices[i].co for i in panel]
 lo_x=min(v.x for v in points);hi_x=max(v.x for v in points)
 lo_z=min(v.z for v in points);hi_z=max(v.z for v in points)
 for i in panel:
  p=sails.data.vertices[i].co
  u=(p.x-lo_x)/max(.001,hi_x-lo_x);v=(p.z-lo_z)/max(.001,hi_z-lo_z)
  billow.data[i].co.y+=.18*math.sin(math.pi*u)*math.sin(math.pi*v)
for p in sails.data.polygons:p.use_smooth=True
ship.select_set(True)
out=root/'assets/models/galleon.glb'
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art/galleon-prepared.blend'))
bpy.ops.export_scene.gltf(filepath=str(out),export_format='GLB',use_selection=True,export_yup=True,export_apply=True)
print('EXPORTED',out)
