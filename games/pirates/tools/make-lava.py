"""Original lava basin fitted inside the volcanic map's existing crater."""
import bpy,math,random
from pathlib import Path
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
rng=random.Random(291)
verts=[];faces=[];styles=[]
def patch(cx,cy,radius,height,segments,style):
 base=len(verts);verts.append((cx,cy,height))
 for i in range(segments):
  angle=i*math.tau/segments;r=radius*(.87+.13*rng.random())
  verts.append((cx+r*math.cos(angle),cy+r*math.sin(angle),height))
 for i in range(segments):faces.append((base,base+1+i,base+1+(i+1)%segments));styles.append(style)
# World (25,-10) maps to Blender (25,10). Terrain naturally occludes the rim
# where the crater wall rises above this liquid level.
patch(25,10,1.3,4.28,64,0)
# Uneven crust plates, with shared warped boundaries rather than a regular
# straight-edged orange lattice. Rejection sampling avoids sliver cells.
sites=[(-.12,.08)]
for _ in range(400):
 x,y=rng.uniform(-1.15,1.15),rng.uniform(-1.15,1.15)
 if x*x+y*y>1.25 or any((x-a)**2+(y-b)**2<.22**2 for a,b in sites):continue
 sites.append((x,y))
 if len(sites)==24:break
def warp(x,y):
 return (x+.055*math.sin(y*11+x*4)+.025*math.sin(y*23-x*13),
         y+.055*math.sin(x*9-y*5)+.025*math.sin(x*21+y*16))
for sx,sy in sites:
 poly=[(1.2*math.cos(i*math.tau/48),1.2*math.sin(i*math.tau/48)) for i in range(48)]
 for ox,oy in sites:
  if (ox,oy)==(sx,sy):continue
  nx,ny=ox-sx,oy-sy;limit=(ox*ox+oy*oy-sx*sx-sy*sy)*.5
  result=[]
  for a,b in zip(poly,poly[1:]+poly[:1]):
   da=a[0]*nx+a[1]*ny-limit;db=b[0]*nx+b[1]*ny-limit
   if da<=0:result.append(a)
   if (da<=0)!=(db<=0):
    t=da/(da-db);result.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
  poly=result
  if not poly:break
 if len(poly)<3:continue
 edge=[]
 for a,b in zip(poly,poly[1:]+poly[:1]):
  steps=max(1,math.ceil(math.hypot(b[0]-a[0],b[1]-a[1])/.07))
  for j in range(steps):
   t=j/steps;edge.append(warp(a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t))
 cx=sum(p[0] for p in edge)/len(edge);cy=sum(p[1] for p in edge)/len(edge)
 shrink=rng.uniform(.86,.97);height=rng.uniform(4.305,4.355)
 base=len(verts);verts.append((25+cx,10+cy,height+.025))
 # A raised, uneven inner lip gives the cooling crust thickness; the lower
 # perimeter sits just above the molten surface, leaving irregular fissures.
 for ring in [.72,1]:
  for x,y in edge:
   px=cx+(x-cx)*shrink*ring;py=cy+(y-cy)*shrink*ring
   h=height+.012*math.sin(px*19+py*7) if ring<1 else 4.29
   verts.append((25+px,10+py,h))
 count=len(edge)
 for i in range(count):
  a=base+1+i;b=base+1+(i+1)%count
  faces.append((base,a,b));styles.append(1)
  faces.append((a,a+count,b+count,b));styles.append(1)
mesh=bpy.data.meshes.new('Lava with cooling crust');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('Crater lava basin',mesh);bpy.context.collection.objects.link(obj)
for name,color,emission,strength in [('Molten lava',(.48,.025,.002),(1,.12,.005),1.1),('Cooling basalt',(.025,.022,.019),(.035,.004,.001),.12)]:
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
 bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1)
 bs.inputs['Roughness'].default_value=.9;bs.inputs['Emission Color'].default_value=(*emission,1);bs.inputs['Emission Strength'].default_value=strength
 nodes=m.node_tree.nodes;links=m.node_tree.links
 geometry=nodes.new('ShaderNodeNewGeometry');noise=nodes.new('ShaderNodeTexNoise')
 noise.inputs['Scale'].default_value=7;noise.inputs['Detail'].default_value=4
 links.new(geometry.outputs['Position'],noise.inputs['Vector'])
 grain=nodes.new('ShaderNodeValToRGB')
 grain.color_ramp.elements[0].color=tuple(c*.35 for c in color)+(1,)
 grain.color_ramp.elements[1].color=tuple(c*1.6 for c in color)+(1,)
 links.new(noise.outputs['Fac'],grain.inputs[0]);links.new(grain.outputs[0],bs.inputs['Base Color'])
 if name=='Molten lava':
  heat=nodes.new('ShaderNodeValToRGB')
  heat.color_ramp.elements[0].color=(.35,.006,.001,1)
  heat.color_ramp.elements[1].color=(1,.32,.025,1)
  links.new(noise.outputs['Fac'],heat.inputs[0]);links.new(heat.outputs[0],bs.inputs['Emission Color'])
 mesh.materials.append(m)
for p,style in zip(mesh.polygons,styles):p.material_index=style;p.use_smooth=style==1
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
# Bake crust grain and varying heat into a compact shared atlas. This keeps
# the detailed surface to one material/draw in the game.
uv=mesh.uv_layers.new(name='LavaUV')
for loop in mesh.loops:
 x,y,z=mesh.vertices[loop.vertex_index].co
 uv.data[loop.index].uv=((x-23.6)/2.8,(y-8.6)/2.8)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=8
scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False;scene.render.bake.use_pass_color=True
maps=[]
for label,kind in [('lava-albedo','DIFFUSE'),('lava-emission','EMIT')]:
 image=bpy.data.images.new(label,width=512,height=512)
 for mat in mesh.materials:
  node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;mat.node_tree.nodes.active=node
 bpy.ops.object.bake(type=kind)
 image.filepath_raw=str(root/'art'/f'{label}.png');image.file_format='PNG';image.save();image.pack();maps.append(image)
material=bpy.data.materials.new('Textured basalt and molten fissures');material.use_nodes=True
nodes=material.node_tree.nodes;links=material.node_tree.links;bs=nodes.get('Principled BSDF')
bs.inputs['Roughness'].default_value=.9;bs.inputs['Emission Strength'].default_value=1
for image,socket in zip(maps,['Base Color','Emission Color']):
 node=nodes.new('ShaderNodeTexImage');node.image=image;links.new(node.outputs['Color'],bs.inputs[socket])
mesh.materials.clear();mesh.materials.append(material)
for p in mesh.polygons:p.material_index=0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/'crater-lava.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/'crater-lava.glb'),export_format='GLB',use_selection=True,export_yup=True)
