"""Original removable fittings for the galleon, authored in Blender."""
import bpy, math
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,c):
 m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=.75
 return m
canvas=mat('Flax canvas',(.72,.65,.49));rope=mat('Tarred rigging',(.11,.075,.035));wood=mat('Gun carriage oak',(.25,.115,.04));iron=mat('Cast iron',(.055,.065,.07));copper=mat('Copper hull fastenings',(.31,.16,.065))
def rod(name,a,b,r,m):
 d=Vector(b)-Vector(a);bpy.ops.mesh.primitive_cylinder_add(vertices=10,radius=r,depth=d.length,location=(Vector(a)+Vector(b))*.5);o=bpy.context.object;o.name=name;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();o.data.materials.append(m);return o
def box(name,p,s,m):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.name=name;o.dimensions=s;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m)
def finish(name):
 bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join();o=bpy.context.object;o.name=name
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/f'{name}.blend'))
 bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/f'{name}.glb'),export_format='GLB',use_selection=True,export_yup=True)
 bpy.ops.object.delete(use_global=False)
# Triangular fore-and-aft canvas: a curved grid, not a flat solid wedge.
verts=[];faces=[];rows=[];N=12
A=Vector((0,0,0));B=Vector((0,-1.9,0));C=Vector((0,0,2.6))
for i in range(N+1):
 row=[]
 for j in range(N-i+1):
  u=i/N;v=j/N;p=A*(1-u-v)+B*u+C*v;p.x=.42*math.sin(math.pi*u)*math.sin(math.pi*v)*math.sin(math.pi*(u+v))
  row.append(len(verts));verts.append(tuple(p))
 rows.append(row)
for i in range(N):
 for j in range(len(rows[i+1])):
  faces.append((rows[i][j],rows[i+1][j],rows[i][j+1]))
  if j+1<len(rows[i+1]):faces.append((rows[i][j+1],rows[i+1][j],rows[i+1][j+1]))
me=bpy.data.meshes.new('Billowing canvas');me.from_pydata(verts,[],faces);me.materials.append(canvas);o=bpy.data.objects.new('Staysail',me);bpy.context.collection.objects.link(o)
bpy.context.view_layer.objects.active=o;o.select_set(True);mod=o.modifiers.new('Canvas thickness','SOLIDIFY');mod.thickness=.012;bpy.ops.object.modifier_apply(modifier=mod.name)
for a,b in [(A,B),(B,C),(C,A)]:rod('Bolt rope',a,b,.025,rope)
for t in [.2,.4,.6,.8]:rod('Canvas seam',A.lerp(C,t),B.lerp(C,t),.008,rope)
finish('upgrade-sail')
# Three iron guns with wheels, reinforcing rings and dark muzzles.
for y in [-1.3,0,1.3]:
 box('Carriage',(0,y,.02),(.65,.47,.22),wood)
 for x in [-.22,.22]:
  for dy in [-.26,.26]:rod('Carriage wheel',(x,y+dy-.05,-.06),(x,y+dy+.05,-.06),.13,wood)
 rod('Cannon barrel',(-.25,y,.26),(.65,y,.26),.13,iron)
 for x in [-.15,.3,.6]:rod('Barrel ring',(x-.035,y,.26),(x+.035,y,.26),.15,iron)
 rod('Muzzle shadow',(.655,y,.26),(.67,y,.26),.085,rope)
finish('upgrade-cannons')
# Upper hull wales follow tapered ends and leave the deck unobstructed.
for side in [-1,1]:
 for z in [-.15,.05]:
  points=[(side*.65,-2.7,z),(side*1.1,-1.7,z),(side*1.22,0,z),(side*1.1,1.7,z),(side*.65,2.7,z)]
  for a,b in zip(points,points[1:]):rod('Reinforced wale',a,b,.065,copper)
  for y in [-2,-1,0,1,2]:rod('Hull fastening',(side*1.1,y,z-.08),(side*1.1,y,z+.08),.035,iron)
finish('upgrade-hull')
