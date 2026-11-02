"""Original billowing cloth standards; one shared asset per captain/role."""
import bpy, math
from pathlib import Path
root=Path(__file__).resolve().parents[1]
colors=['e85642','4e97e6','e4bd47','79d28a','dfb96b','405779']
def linear(c):return c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4
for captain,hexcolor in enumerate(colors):
 bpy.ops.wm.read_factory_settings(use_empty=True)
 mats=[]
 for name,col in [('Dyed linen',hexcolor),('Canvas stitching','efe2c2')]:
  m=bpy.data.materials.new(name);m.use_nodes=True
  c=tuple(linear(int(col[i:i+2],16)/255) for i in (0,2,4))
  m.diffuse_color=(*c,1);bs=m.node_tree.nodes.get('Principled BSDF')
  bs.inputs['Base Color'].default_value=(*c,1);bs.inputs['Roughness'].default_value=.95;mats.append(m)
 # Blender Z-up: fly along X, billow along Y. glTF turns Z into world Y.
 nx,nz=32,16;verts=[];faces=[];styles=[]
 for j in range(nz+1):
  v=j/nz
  for i in range(nx+1):
   u=i/nx;fly=1.6-.28*(1-abs(v*2-1))
   verts.append((u*fly,.17*u*math.sin(u*math.tau*1.35+v*.9),-.78*v-.08*u*u))
 for j in range(nz):
  for i in range(nx):
   a=j*(nx+1)+i;faces.append((a,a+1,a+nx+2,a+nx+1))
   u=(i+.5)/nx;v=(j+.5)/nz;bars=captain+1 if captain<4 else 1
   marked=.27<v<.73 and any(abs(u-(.18+b*.135))<.028 for b in range(bars))
   styles.append(1 if j in (0,nz-1) or i==0 or marked else 0)
 mesh=bpy.data.meshes.new('Woven standard');mesh.from_pydata(verts,[],faces);mesh.update()
 obj=bpy.data.objects.new('Captain standard',mesh);bpy.context.collection.objects.link(obj)
 for m in mats:mesh.materials.append(m)
 for face,style in zip(mesh.polygons,styles):face.material_index=style;face.use_smooth=True
 bpy.context.view_layer.objects.active=obj;obj.select_set(True)
 solid=obj.modifiers.new('Two sided cloth thickness','SOLIDIFY');solid.thickness=.012
 bpy.ops.object.modifier_apply(modifier=solid.name)
 bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/f'flag-{captain}.blend'))
 bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/f'flag-{captain}.glb'),export_format='GLB',use_selection=True,export_yup=True)
