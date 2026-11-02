"""Four original harbor variants built from the shared pier construction."""
from pathlib import Path
import bpy, math
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
source=(root/'tools/make-harbor.py').read_text().split("bpy.ops.object.select_all(action='SELECT')")[0]
def beam(name,a,b,width,material):
 mid=(Vector(a)+Vector(b))*.5;delta=Vector(b)-Vector(a)
 o=box(name,mid,(width,width,delta.length),material,.015)
 o.rotation_euler=delta.to_track_quat('Z','Y').to_euler();return o
def weather_material(material,kind):
 nodes=material.node_tree.nodes;links=material.node_tree.links
 bs=nodes.get('Principled BSDF');color=tuple(bs.inputs['Base Color'].default_value)
 geometry=nodes.new('ShaderNodeNewGeometry')
 noise=nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=5;noise.inputs['Detail'].default_value=4
 mapping=nodes.new('ShaderNodeVectorMath');mapping.operation='MULTIPLY'
 mapping.inputs[1].default_value=(.6,14,14) if kind=='wood' else (1,1,1)
 links.new(geometry.outputs['Position'],mapping.inputs[0]);links.new(mapping.outputs[0],noise.inputs['Vector'])
 ramp=nodes.new('ShaderNodeValToRGB')
 ramp.color_ramp.elements[0].color=tuple(c*.45 for c in color[:3])+(1,)
 ramp.color_ramp.elements[1].color=tuple(c*1.2 for c in color[:3])+(1,)
 links.new(noise.outputs['Fac'],ramp.inputs[0]);surface=ramp.outputs[0]
 if kind=='stone':
  # Choose a planar masonry projection by face orientation. World units
  # keep block sizes consistent across towers, walls and quay faces.
  pos=nodes.new('ShaderNodeSeparateXYZ');normal=nodes.new('ShaderNodeSeparateXYZ')
  links.new(geometry.outputs['Position'],pos.inputs[0]);links.new(geometry.outputs['Normal'],normal.inputs[0])
  front=nodes.new('ShaderNodeCombineXYZ');side=nodes.new('ShaderNodeCombineXYZ')
  links.new(pos.outputs['X'],front.inputs['X']);links.new(pos.outputs['Z'],front.inputs['Y'])
  links.new(pos.outputs['Y'],side.inputs['X']);links.new(pos.outputs['Z'],side.inputs['Y'])
  ax=nodes.new('ShaderNodeMath');ax.operation='ABSOLUTE';links.new(normal.outputs['X'],ax.inputs[0])
  side_on=nodes.new('ShaderNodeMath');side_on.operation='GREATER_THAN';side_on.inputs[1].default_value=.7;links.new(ax.outputs[0],side_on.inputs[0])
  projection=nodes.new('ShaderNodeMixRGB');links.new(side_on.outputs[0],projection.inputs[0]);links.new(front.outputs[0],projection.inputs[1]);links.new(side.outputs[0],projection.inputs[2])
  brick=nodes.new('ShaderNodeTexBrick');brick.inputs['Scale'].default_value=1
  brick.inputs['Brick Width'].default_value=.55;brick.inputs['Row Height'].default_value=.24;brick.inputs['Mortar Size'].default_value=.012
  brick.inputs['Color1'].default_value=tuple(c*.65 for c in color[:3])+(1,)
  brick.inputs['Color2'].default_value=tuple(c*1.2 for c in color[:3])+(1,)
  brick.inputs['Mortar'].default_value=tuple(c*.3 for c in color[:3])+(1,)
  links.new(projection.outputs[0],brick.inputs['Vector'])
  grain=nodes.new('ShaderNodeMixRGB');grain.blend_type='MULTIPLY';grain.inputs[0].default_value=.25
  links.new(brick.outputs['Color'],grain.inputs[1]);links.new(surface,grain.inputs[2]);surface=grain.outputs[0]
 ao=nodes.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.65
 contact=nodes.new('ShaderNodeMixRGB');contact.blend_type='MULTIPLY';contact.inputs[0].default_value=.55
 links.new(surface,contact.inputs[1]);links.new(ao.outputs['AO'],contact.inputs[2]);links.new(contact.outputs[0],bs.inputs['Base Color'])

def bake_port(obj,variant):
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
 bpy.ops.uv.smart_project(angle_limit=1.15,island_margin=.008);bpy.ops.object.mode_set(mode='OBJECT')
 atlas=bpy.data.images.new('Port '+variant+' albedo',width=1024,height=1024)
 for material in obj.data.materials:
  node=material.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;material.node_tree.nodes.active=node
 scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16
 scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False;scene.render.bake.use_pass_color=True
 bpy.ops.object.bake(type='DIFFUSE')
 atlas.filepath_raw=str(root/'art'/f'port-{variant}-albedo.png');atlas.file_format='PNG';atlas.save();atlas.pack()
 material=bpy.data.materials.new('Weathered '+variant);material.use_nodes=True
 bs=material.node_tree.nodes.get('Principled BSDF');bs.inputs['Roughness'].default_value=.88
 node=material.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas
 material.node_tree.links.new(node.outputs['Color'],bs.inputs['Base Color'])
 obj.data.materials.clear();obj.data.materials.append(material)
 for face in obj.data.polygons:face.material_index=0

for variant in ['fort','watchtower','fishery','shipyard']:
 exec(compile(source,str(root/'tools/make-harbor.py'),'exec'))
 for material,kind in [(wood,'wood'),(light,'wood'),(wall,'plaster'),(roof,'roof'),(stone,'stone'),(dark,'iron')]:weather_material(material,kind)
 if variant in ['watchtower','fishery']:
  for o in list(bpy.context.scene.objects):
   if o.name.startswith(('Storehouse','Door','Window','Corner','Roof','Ridge')):bpy.data.objects.remove(o,do_unlink=True)
 if variant=='fort':
  # Two masonry towers and a crenellated wall framing a trading courtyard.
  for x in [-2.15,2.15]:
   cylinder('Round bastion',(x,2.6,2.0),.95,3.5,stone)
   cylinder('Bastion coping',(x,2.6,3.8),1.03,.25,wall)
   for j in range(10):
    a=j*math.tau/10;box('Battlement',(x+.83*math.cos(a),2.6+.83*math.sin(a),4.1),(.35,.35,.45),stone,.025)
   box('Tower arrow slit',(x,1.65,2.5),(.14,.035,.65),dark)
  box('Fort curtain',(0,3.3,2.2),(4.4,.6,2.8),stone,.06)
  for x in [-1.8,-1.2,-.6,0,.6,1.2,1.8]:box('Wall merlon',(x,3.3,3.85),(.34,.65,.5),wall,.02)
  for z in [1.4,2,2.6,3.2]:box('Masonry course',(0,2.98,z),(3.3,.03,.035),dark)
 elif variant=='watchtower':
  # Octagonal whitewashed beacon above the volcanic anchorage.
  cylinder('Tower foot',(0,.65,.65),1.25,.8,stone)
  cylinder('Beacon tower',(0,.65,2.65),.88,3.6,wall)
  for z in [1.1,2.6,4.35]:cylinder('Tower stone band',(0,.65,z),.95,.17,stone)
  cylinder('Lookout deck',(0,.65,4.6),1.2,.25,wood)
  for j in range(8):
   a=j*math.tau/8;x=math.cos(a);y=.65+math.sin(a)
   cylinder('Lookout pillar',(x,y,5.15),.055,1.0,wood)
  bpy.ops.mesh.primitive_cone_add(vertices=8,radius1=1.4,radius2=.08,depth=.85,location=(0,.65,6));bpy.context.object.data.materials.append(roof)
  box('Beacon window',(0,-.24,3.5),(.35,.035,.55),dark)
  box('Tower door',(0,-.25,1.35),(.5,.035,1),wood)
 elif variant=='fishery':
  # Open shelter, drying nets and a skiff pulled onto a slipway.
  for x in [-1.2,1.2]:
   for y in [0,1.5]:cylinder('Shelter post',(x,y,1.7),.08,2.2,wood)
  for j in range(15):box('Shelter thatch',(-1.4+j*.2,.75,2.85),(.22,2.2,.22),light,.06)
  for x in [-2.1,2.1]:cylinder('Net rack',(x,2.5,1.9),.07,2.4,wood)
  beam('Net top',(-2.1,2.5,2.9),(2.1,2.5,2.9),.065,wood)
  for j in range(16):beam('Hanging net',(-2+j*.26,2.5,1.3),(-2+j*.26,2.5,2.85),.018,light)
  for j in range(7):beam('Net weave',(-2,2.5,1.3+j*.24),(2,2.5,1.3+j*.24),.018,light)
  for y in [-1.2,-.8,-.4,0,.4]:
   beam('Skiff ribs',(2.0,y,.75),(2.7,y,.95),.09,wood)
  for x in [2,2.7]:beam('Skiff gunwale',(x,-1.5,.9),(x,.7,.9),.12,light)
  beam('Skiff bow',(2,-1.5,.9),(2.35,-1.9,1),.12,light);beam('Skiff bow',(2.7,-1.5,.9),(2.35,-1.9,1),.12,light)
 else:
  # Timber lifting crane and stocked repair racks.
  cylinder('Crane footing',(-2.15,1.2,.9),.6,.8,stone)
  beam('Crane mast',(-2.15,1.2,1),(-2.15,1.2,5.5),.22,wood)
  beam('Crane jib',(-2.15,1.2,5),(-2.15,-2.5,4.8),.2,wood)
  beam('Crane brace',(-2.15,1.2,3),(-2.15,-1.3,4.9),.16,light)
  beam('Lifting rope',(-2.15,-2.4,4.8),(-2.15,-2.4,2.1),.035,dark)
  box('Suspended crate',(-2.15,-2.4,1.8),(.7,.7,.6),light,.025)
  for j in range(5):box('Stacked spar',(0,2.3+j*.25,1.1),(3.5,.2,.22),wood,.025)
  for x in [-1.4,1.4]:box('Spar rack',(x,2.8,.85),(.15,1.5,.5),light)
 bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=bpy.context.selected_objects[0];bpy.ops.object.join();bpy.context.object.name='Port '+variant
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bake_port(bpy.context.object,variant)
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/f'port-{variant}.blend'))
 bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/f'port-{variant}.glb'),export_format='GLB',use_selection=True,export_yup=True)
