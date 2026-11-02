"""Original coastal broadleaf shrub, with curved leaves and open branchwork."""
import bpy, math, random
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
rng=random.Random(742)
verts=[];faces=[];styles=[]
def face(indices,material):
 faces.append(indices);styles.append(material)
def branch(a,b,radius):
 axis=(b-a).normalized();side=axis.cross(Vector((0,1,0))).normalized();other=axis.cross(side)
 base=len(verts)
 for p,r in [(a,radius),(b,radius*.45)]:
  for i in range(6):
   v=p+r*(side*math.cos(i*math.tau/6)+other*math.sin(i*math.tau/6))
   # Extend only the root ring; leaf origins and branch tips stay authored.
   if p is a:v.z-=2.0
   verts.append(tuple(v))
 for i in range(6):face((base+i,base+(i+1)%6,base+(i+1)%6+6,base+i+6),2)
def leaf(origin,heading,length,width,material):
 forward=Vector((math.cos(heading),math.sin(heading),0));side=Vector((-forward.y,forward.x,0))
 base=len(verts)
 # A central raised rib and curved blade catch sunlight without alpha cards.
 for i in range(6):
  t=i/5;half=width*.5*math.sin(math.pi*t)**.7
  for cross in [-1,0,1]:
   p=origin+forward*(length*t)+side*(half*cross)
   p.z+=length*(.4*math.sin(math.pi*t)-.22*t)-abs(cross)*.055*math.sin(math.pi*t)
   verts.append(tuple(p))
 for i in range(5):
  for j in range(2):
   a=base+i*3+j;face((a,a+3,a+4,a+1),material)
for j in range(7):
 angle=j*2.39996;reach=.27+.17*rng.random();height=.38+.38*rng.random()
 start=Vector((0,0,.02));end=Vector((math.cos(angle)*reach,math.sin(angle)*reach,height))
 branch(start,end,.018)
 for k in range(3):
  t=.48+k*.24;origin=start.lerp(end,t)
  heading=angle+(-1 if k%2 else 1)*(.65+.3*rng.random())
  leaf(origin,heading,.37+.16*rng.random(),.22+.10*rng.random(),(j+k)%2)
mesh=bpy.data.meshes.new('Curved coastal leaves');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('Coastal broadleaf shrub',mesh);bpy.context.collection.objects.link(obj)
for name,color in [('Mature leaf',(.045,.12,.018)),('Young leaf',(.085,.19,.028)),('Branch',(.12,.075,.036))]:
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
 bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.72
 mesh.materials.append(m)
for p,style in zip(mesh.polygons,styles):p.material_index=style;p.use_smooth=True
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
solid=obj.modifiers.new('Leaf thickness','SOLIDIFY');solid.thickness=.004
bpy.ops.object.modifier_apply(modifier=solid.name)
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/'undergrowth.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/'undergrowth.glb'),export_format='GLB',use_selection=True,export_yup=True)
