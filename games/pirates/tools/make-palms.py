"""Original coconut palm with arching pinnate fronds; no alpha-card foliage."""
import bpy, math
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
verts=[];faces=[];styles=[]
def surface(points,triangles,style,two_sided=False):
 base=len(verts);verts.extend(tuple(p) for p in points)
 for tri in triangles:faces.append(tuple(base+i for i in tri));styles.append(style)
 if two_sided:
  # Separate back vertices preserve outward normals on both sides.
  base=len(verts);verts.extend(tuple(Vector(p)-Vector((0,0,.012))) for p in points)
  for tri in triangles:faces.append(tuple(base+i for i in reversed(tri)));styles.append(style)
def trunk_center(t):return Vector((.85*t*t,.16*math.sin(t*math.pi),7.6*t))
# Slightly swollen base, leaning trunk and subtle leaf-scar rings.
for j in range(24):
 points=[]
 for step in (j,j+1):
  t=step/24;r=.23+.18*(1-t)**4+(.022 if step%2==0 else 0)
  c=trunk_center(t)
  points.extend(c+Vector((r*math.cos(k*math.tau/8),r*math.sin(k*math.tau/8),0)) for k in range(8))
 tris=[]
 for k in range(8):
  n=(k+1)%8;tris.extend(((k,n,n+8),(k,n+8,k+8)))
 surface(points,tris,0)
crown=trunk_center(1)
for f in range(8):
 angle=f*2.39996;forward=Vector((math.cos(angle),math.sin(angle),0));side=Vector((-forward.y,forward.x,0))
 length=3.5+.4*math.sin(f*4.7);lift=1.5+.35*math.sin(f*1.7)
 def center(t):return crown+forward*(length*t)+Vector((0,0,lift*math.sin(math.pi*t)-1.3*t*t))
 # The rachis is narrow but continuous through the feathered frond.
 for j in range(8):
  a=j/8;b=(j+1)/8;wa=.055*(1-a)+.008;wb=.055*(1-b)+.008
  surface([center(a)-side*wa,center(a)+side*wa,center(b)+side*wb,center(b)-side*wb],[(0,1,2),(0,2,3)],1,True)
 for j in range(9):
  t=.15+j*.087
  for sign in (-1,1):
   origin=center(t+(.018 if sign>0 else 0))
   span=(.38+.65*math.sin(math.pi*t))*(1-.3*t)
   direction=(side*sign+forward*.38).normalized()
   tip=origin+direction*span+Vector((0,0,-.4*span))
   mid=origin.lerp(tip,.45)+Vector((0,0,.13))
   width=.25*(1-.4*t)
   surface([origin,mid-forward*width,mid+Vector((0,0,.025)),mid+forward*width,tip],
           [(0,1,2),(0,2,3),(1,4,2),(2,4,3)],1 if f%3 else 2,True)
mesh=bpy.data.meshes.new('Coconut palm leaves and trunk');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('Coconut palm',mesh);bpy.context.collection.objects.link(obj)
for name,color in [('Weathered trunk',(.22,.155,.085)),('Palm green',(.075,.22,.035)),('Sunlit young fronds',(.12,.28,.045))]:
 m=bpy.data.materials.new(name);m.use_nodes=True;m.diffuse_color=(*color,1)
 bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.8
 mesh.materials.append(m)
for p,style in zip(mesh.polygons,styles):p.material_index=style;p.use_smooth=True
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/'coconut-palm.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/'coconut-palm.glb'),export_format='GLB',use_selection=True,export_yup=True)
