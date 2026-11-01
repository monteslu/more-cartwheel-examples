"""Original cargo miniatures. Shared matte materials keep batching inexpensive."""
import bpy, math
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,color):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=.72
 return m
wood=mat('Salt worn oak',(.28,.13,.045));end=mat('Cut timber',(.55,.32,.13));iron=mat('Forged iron',(.075,.09,.10));brass=mat('Treasure brass',(.62,.35,.065));rope=mat('Hemp lashings',(.5,.39,.22))
def box(name,pos,size,m):
 bpy.ops.mesh.primitive_cube_add(size=1,location=pos);o=bpy.context.object;o.name=name;o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m);return o
def cyl(name,pos,r,depth,m,axis=False):
 bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=r,depth=depth,location=pos);o=bpy.context.object;o.name=name;o.data.materials.append(m)
 if axis:o.rotation_euler.x=math.pi/2
 return o
def ring(name,pos,r,thick,m,axis=False):
 bpy.ops.mesh.primitive_torus_add(major_segments=16,minor_segments=4,location=pos,major_radius=r,minor_radius=thick);o=bpy.context.object;o.name=name;o.data.materials.append(m)
 if axis:o.rotation_euler.x=math.pi/2
 return o
def finish(name):
 bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join()
 o=bpy.context.object;o.name=name
 # Bake placement into geometry so instantiation has a predictable origin.
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/f'{name}.blend'))
 bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/f'{name}.glb'),export_format='GLB',use_selection=True,export_yup=True)
 bpy.ops.object.delete(use_global=False)
# Three logs with visible end grain discs and paired rope loops.
for x,z in [(-.28,0),(.28,0),(0,.42)]:
 cyl('Oak log',(x,0,z),.27,1.65,wood,True)
 for y in [-.83,.83]:cyl('End grain',(x,y,z),.245,.018,end,True)
 for y in [-.5,.5]:ring('Rope binding',(x,y,z),.28,.035,rope,True)
finish('cargo-timber')
# Barrel staves follow a bulging profile, with three forged hoops.
verts=[];faces=[]
for j,(z,r) in enumerate([(-.38,.32),(-.28,.38),(.0,.43),(.28,.38),(.38,.32)]):
 for i in range(16):
  a=2*math.pi*i/16;verts.append((math.cos(a)*r,math.sin(a)*r,z))
for j in range(4):
 for i in range(16):faces.append((j*16+i,j*16+(i+1)%16,(j+1)*16+(i+1)%16,(j+1)*16+i))
me=bpy.data.meshes.new('Staves');me.from_pydata(verts,[],faces);me.materials.append(wood);o=bpy.data.objects.new('Iron cargo barrel',me);bpy.context.collection.objects.link(o)
for z,r in [(-.29,.38),(0,.43),(.29,.38)]:ring('Iron hoop',(0,0,z),r,.045,iron)
for z in [-.38,.38]:
 cyl('Barrel lid',(0,0,z),.32,.035,end)
 for x in [-.16,0,.16]:box('Lid seam',(x,0,z+.02),(.014,.5,.012),wood)
# An iron ingot on the lid identifies this cargo at the overhead camera.
box('Iron ingot',(0,0,.45),(.46,.22,.12),iron)
finish('cargo-iron')
# Planked chest with an arched lid and brass straps.
for j in range(4):box('Chest plank',(0,0,-.24+j*.14),(1.05,.66,.13),wood)
for i in range(9):
 a=math.pi*i/8;x=math.cos(a)*.53;z=.22+math.sin(a)*.32
 o=box('Arched lid slat',(x,0,z),(.19,.7,.09),wood);o.rotation_euler.y=a-math.pi/2
for y in [-.26,.26]:
 for i in range(13):
  a=math.pi*i/12;o=box('Brass lid strap',(math.cos(a)*.55,y,.22+math.sin(a)*.34),(.15,.07,.035),brass);o.rotation_euler.y=a-math.pi/2
 for x in [-.54,.54]:box('Brass corner',(x,y,-.02),(.04,.07,.5),brass)
box('Lock plate',(0,-.37,.12),(.16,.05,.23),brass)
ring('Lock loop',(0,-.41,.09),.055,.018,iron,True)
finish('cargo-treasure')
