"""Original coastal harbor asset, authored and exported with Blender."""
import bpy, math
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,color,rough=.85):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough
 return m
wood=mat('Weathered pier',(.22,.13,.065));light=mat('Plank ends',(.36,.24,.12));wall=mat('Warm plaster',(.65,.53,.34));roof=mat('Terracotta',(.32,.07,.035));dark=mat('Iron and openings',(.025,.032,.03));stone=mat('Quay stone',(.24,.23,.19))
def box(name,pos,size,material,bevel=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=pos);o=bpy.context.object;o.name=name;o.dimensions=size
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(material)
 if bevel:
  m=o.modifiers.new('Worn edges','BEVEL');m.width=bevel;m.segments=2;bpy.ops.object.modifier_apply(modifier=m.name)
 return o
def cylinder(name,pos,r,depth,material):
 bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=r,depth=depth,location=pos);o=bpy.context.object;o.name=name;o.data.materials.append(material);return o
# Blender -Y becomes game +Z: the pier projects seaward from shore origin.
box('Stone quay',(0,.55,.35),(4.7,2.4,.7),stone,.12)
for j in range(22):box('Individual dock plank',(0,-.8-j*.18,.75),(2.2,.165,.14),light if j%4==0 else wood,.012)
for y in [-1,-2.3,-3.6,-4.8]:
 for x in [-1,1]:
  cylinder('Timber pile',(x,y,.1),.12,2.0,wood)
  cylinder('Mooring cap',(x,y,1.1),.16,.12,dark)
box('Pier brace',(0,-2.7,.38),(2.2,.15,.18),wood)
box('Storehouse',(0,.65,1.45),(2.8,1.8,1.65),wall,.045)
box('Door',(0,-.266,1.25),(.65,.035,1.15),wood)
for x in [-.92,.92]:
 box('Window frame',(x,-.28,1.62),(.55,.06,.55),wood)
 box('Window',(x,-.32,1.62),(.42,.035,.4),dark)
 box('Window mullion',(x,-.35,1.62),(.035,.02,.45),light)
for x in [-1.4,1.4]:box('Corner post',(x,.65,1.45),(.12,1.85,1.75),wood)
# Pitched roof with individually overlapping tile courses.
for side in [-1,1]:
 for row in range(6):
  t=(row+.5)/6;x=side*t*1.7;z=3.05-t*.75
  for col in range(9):
   tile=box('Roof tile',(x,-.35+col*.25,z),(.33,.26,.065),roof,.018)
   tile.rotation_euler.y=side*math.atan(.75/1.7)
for y in [-.35+i*.25 for i in range(9)]:
 ridge=cylinder('Ridge tile',(0,y,3.08),.1,.26,roof);ridge.rotation_euler.x=math.pi/2
for x,y,z in [(1.8,.4,.95),(-1.8,.2,.95),(-1.8,.85,.95),(-1.8,.5,1.55)]:
 box('Cargo crate',(x,y,z),(.55,.55,.55),light,.025)
 for dz in [-.2,.2]:box('Crate binding',(x,y,z+dz),(.58,.58,.045),dark)
for x in [-1.8,1.8]:
 cylinder('Mooring bollard',(x,-.4,.95),.13,.55,dark)
 cylinder('Bollard head',(x,-.4,1.2),.2,.09,dark)
bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join();bpy.context.object.name='Harbor'
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art/harbor.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models/harbor.glb'),export_format='GLB',use_selection=True,export_yup=True)
