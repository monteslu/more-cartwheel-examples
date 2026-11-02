"""Blender terrain and matching C collision polygons, from one coastline source."""
import bpy, math, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lagoon='--lagoon' in sys.argv
asset='lagoon' if lagoon else 'coast'
# World X,Z. Concave outlines deliberately form bays, headlands and an isthmus.
coasts=[
 [(-30,-8),(-28,-15),(-22,-18),(-16,-17),(-12,-13),(-12,-8),(-15,-6),(-17,-8),(-17,-12),(-21,-13),(-24,-10),(-23,-6),(-18,-3),(-15,1),(-18,4),(-24,1),(-28,-3)],
 [(-10,10),(-7,6),(-2,5),(1,2),(2,-3),(5,-8),(10,-9),(13,-6),(11,-2),(7,0),(5,4),(6,8),(10,10),(11,14),(7,17),(2,16),(-1,12),(-5,12),(-8,14)],
 [(19,-10),(22,-15),(28,-14),(31,-11),(29,-7),(25,-5),(27,-2),(31,0),(30,5),(27,8),(23,7),(20,4),(20,0),(22,-3),(18,-5)],
]
if lagoon:
 coasts=[
  [(-32,-6),(-30,-14),(-24,-18),(-16,-17),(-11,-13),(-12,-10),(-18,-12),(-23,-11),(-25,-6),(-24,-1),(-20,2),(-16,1),(-12,-3),(-9,-1),(-11,5),(-15,9),(-19,10),(-26,7),(-30,1)],
  [(-3,-15),(2,-18),(8,-16),(12,-12),(10,-8),(6,-7),(4,-3),(5,1),(9,3),(10,6),(6,7),(1,4),(-1,0),(0,-5),(-4,-9)],
  [(14,10),(17,5),(23,4),(28,6),(32,10),(30,15),(25,18),(19,16),(16,13),(11,15),(8,13),(10,10)],
 ]
def rounded_coast(poly):
 # Three corner-cutting passes produce continuous-looking coves and headlands.
 # Convex combinations stay close to the authored shoreline and preserve its
 # concavities without spline overshoot across the narrow bays/isthmuses.
 for _ in range(3):
  result=[]
  for i,a in enumerate(poly):
   b=poly[(i+1)%len(poly)]
   result.extend(((.75*a[0]+.25*b[0],.75*a[1]+.25*b[1]),
                  (.25*a[0]+.75*b[0],.25*a[1]+.75*b[1])))
  poly=result
 return poly
coasts=[rounded_coast(p) for p in coasts]

def distance(x,z,poly):
 inside=False;d=1e9
 for i,a in enumerate(poly):
  b=poly[(i+1)%len(poly)];dx=b[0]-a[0];dz=b[1]-a[1]
  t=max(0,min(1,((x-a[0])*dx+(z-a[1])*dz)/(dx*dx+dz*dz)))
  d=min(d,math.hypot(x-a[0]-t*dx,z-a[1]-t*dz))
  if (a[1]>z)!=(b[1]>z) and x<(b[0]-a[0])*(z-a[1])/(b[1]-a[1])+a[0]:inside=not inside
 return d if inside else -d
def ground_noise(x,z):
 # Seeded value noise with zero first/second derivatives at lattice borders.
 # Unlike sums of sine waves, this has no repeating directional ridges.
 ix=math.floor(x);iz=math.floor(z);u=x-ix;v=z-iz
 u=u*u*u*(u*(u*6-15)+10);v=v*v*v*(v*(v*6-15)+10)
 def sample(a,b):
  h=(a*374761393+b*668265263+1376312589)&0xffffffff
  h=((h^(h>>13))*1274126177)&0xffffffff
  return ((h^(h>>16))&0xffff)/32767.5-1
 return ((1-u)*sample(ix,iz)+u*sample(ix+1,iz))*(1-v)+((1-u)*sample(ix,iz+1)+u*sample(ix+1,iz+1))*v

def terrain(x,z):
 ds=[distance(x,z,p) for p in coasts];d=max(ds);k=ds.index(d)
 if d<0:
  # The ocean-floor box is centered at -12 with a 0.5 m thickness.
  # Ease the last part of the shelf into its top (-11.75), with matching
  # tangents rather than a clipped ramp and a hard underwater contour.
  # A narrow beach shelf rolls into deeper water instead of extending a
  # broad turquoise apron into the channels between islands. Preserve the
  # .85 waterline tangent, then steepen smoothly; no depth cutoff/band.
  offshore=-d
  depth=.85*offshore+1.25*(offshore-1.2*(1-math.exp(-offshore/1.2)))
  if depth>7.75:
   t=min(1,(depth-7.75)/8)
   depth=7.75+8*(t-.5*t*t)
  return -depth,d,k
 noise=.22*ground_noise(x*.45,z*.45)+.08*ground_noise(x*1.2+17,z*1.2-9)
 # Ease continuously from beach to inland plateau. A clamped linear ramp
 # produced a hard shoulder wherever distance reached 3.8 meters, while
 # min(d,1) introduced another visible crease in the small-scale relief.
 relief=1-math.exp(-d)
 # Meet the submerged shelf at height zero with the same .85 slope.
 # The previous .1 m step at d=0 turned crossing grid cells into little
 # triangular ledges, even though the authored coastline was rounded.
 y=3.8*.42*(1-math.exp(-d/3.8))+.43*d*math.exp(-d/1.4)+noise*relief*relief
 # Localized limestone escarpments; beaches and harbor approaches remain low.
 ridge=0
 for cx,cz,rx,rz,height in [(-22,-16,7,2.8,3.8),(-19,0,4,2.5,2.6),(8,-6,4,4,4.2),(5,13,5,3,3.0)]:
  q=((x-cx)/rx)**2+((z-cz)/rz)**2
  # A zero-slope shoulder avoids the old fractional-power cliff cusp.
  ridge=max(ridge,height*max(0,1-q)**1.5)
 edge=max(0,min(1,(d-.2)/.65));edge=edge*edge*(3-2*edge)
 y+=ridge*(.4 if lagoon else 1)*edge*(.9+.1*ground_noise(x*.8+5,z*.8-13))
 if k==2 and not lagoon:
  r=math.hypot(x-25,z+10)
  cone=max(0,5.7-r*1.12)
  crater=3.3*max(0,1-(r/1.8)**2)
  ribs=.28*ground_noise(x*1.1-8,z*1.1+31)*min(1,r)*max(0,1-r/5)
  y+=max(0,cone-crater+ribs)
 return y,d,k
bpy.ops.wm.read_factory_settings(use_empty=True)
verts=[];faces=[];meta=[];nx=241;nz=181
for j in range(nz):
 for i in range(nx):
  x=-60+i*.5;z=-45+j*.5;y,d,k=terrain(x,z);verts.append((x,-z,y));meta.append((d,k,y))
# Round the inland surface as well as the outline. The old height function
# left sharp ridgeline intersections and small triangular spikes even with
# smooth normals. A separable approximately 0.61-meter Gaussian softens those geometric
# creases. Fade it in behind the beach so the waterline/collision stays put.
raw_heights=[v[2] for v in verts]
kernel=(1/64,6/64,15/64,20/64,15/64,6/64,1/64)
horizontal=[sum(kernel[t+3]*raw_heights[j*nx+max(0,min(nx-1,i+t))]
                for t in range(-3,4)) for j in range(nz) for i in range(nx)]
for j in range(nz):
 for i in range(nx):
  index=j*nx+i;x,z,y=verts[index];d,k,_=meta[index]
  smooth=sum(kernel[t+3]*horizontal[max(0,min(nz-1,j+t))*nx+i] for t in range(-3,4))
  blend=max(0,min(1,(d-.25)/1.0));blend=blend*blend*(3-2*blend)
  y+=(smooth-y)*blend
  verts[index]=(x,z,y);meta[index]=(d,k,y)

def surface_height(x,z):
 # Refined beach vertices share the filtered surface with the base grid.
 gx=(x+60)*2;gz=(z+45)*2
 i=max(0,min(nx-2,int(gx)));j=max(0,min(nz-2,int(gz)))
 u=gx-i;v=gz-j
 correction=lambda a,b: verts[b*nx+a][2]-raw_heights[b*nx+a]
 delta=(correction(i,j)*(1-u)+correction(i+1,j)*u)*(1-v) + (correction(i,j+1)*(1-u)+correction(i+1,j+1)*u)*v
 y,d,k=terrain(x,z)
 return y+delta,d,k
# Refine only the shoreline. Shared edge midpoints and transition fans keep
# the fine beach mesh watertight against the original half-meter inland grid.
quads=[];refined=set();midpoints={}
for j in range(nz-1):
 for i in range(nx-1):
  a=j*nx+i;quad=(a,a+nx,a+nx+1,a+1)
  # The scene already draws the flat seabed. Keep all slopes and shore
  # detail; omit only cells wholly coplanar with that hidden floor.
  if any(verts[v][2]>-11.749 for v in quad):
   quads.append(quad)
   if min(abs(meta[v][0]) for v in quad)<2:
    refined.add(len(quads)-1)
def add_vertex(x,z):
 y,d,k=surface_height(x,z);index=len(verts)
 verts.append((x,-z,y));meta.append((d,k,y));return index
for qi in sorted(refined):
 quad=quads[qi]
 for a,b in zip(quad,quad[1:]+quad[:1]):
  edge=tuple(sorted((a,b)))
  if edge not in midpoints:
   midpoints[edge]=add_vertex((verts[a][0]+verts[b][0])*.5,-(verts[a][1]+verts[b][1])*.5)
for quad in quads:
 ring=[]
 for a,b in zip(quad,quad[1:]+quad[:1]):
  ring.append(a)
  mid=midpoints.get(tuple(sorted((a,b))))
  if mid is not None:ring.append(mid)
 if len(ring)==4:faces.append(quad)
 else:
  center=add_vertex(sum(verts[v][0] for v in quad)/4,-sum(verts[v][1] for v in quad)/4)
  faces.extend((center,a,b) for a,b in zip(ring,ring[1:]+ring[:1]))
mesh=bpy.data.meshes.new('Coast heightfield');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('Coast',mesh);bpy.context.collection.objects.link(obj)
colors=[('Sand',(.63,.48,.27)),('Grass',(.12,.23,.055)),('Basalt',(.13,.115,.095)),('Crater',(.3,.08,.025))]
for name,c in colors:
 m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True;bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*c,1);bs.inputs['Roughness'].default_value=.95;mesh.materials.append(m)
for p in mesh.polygons:
 d=sum(meta[v][0] for v in p.vertices)/len(p.vertices);k=meta[p.vertices[0]][1];y=sum(meta[v][2] for v in p.vertices)/len(p.vertices)
 p.material_index=2 if k==2 and y>1.8 else 1 if d>1.0 else 0;p.use_smooth=True
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
# Blend biome colors per vertex, then bake organic variation into a planar
# atlas. This avoids assigning abrupt material bands to individual grid cells.
attr=mesh.color_attributes.new(name='Biome',type='FLOAT_COLOR',domain='POINT')
relief_attr=mesh.color_attributes.new(name='SurfaceRelief',type='FLOAT_COLOR',domain='POINT')
for i,(d,k,y) in enumerate(meta):
 t=max(0,min(1,(d-.4)/1.4));t=t*t*(3-2*t)
 sand=(.63,.48,.27);grass=(.12,.23,.055);rock=(.13,.115,.095)
 c=[sand[q]*(1-t)+grass[q]*t for q in range(3)]
 if k==2 and not lagoon:
  r=max(0,min(1,(y-1.3)/1.3));c=[c[q]*(1-r)+rock[q]*r for q in range(3)]
 x,z=verts[i][0],-verts[i][1]
 # Central differences on the generated heightfield avoid evaluating the
 # denser coastline four extra times per vertex just for cliff coloration.
 col=max(0,min(nx-1,round((x+60)*2)));row=max(0,min(nz-1,round((z+45)*2)))
 left=max(0,col-1);right=min(nx-1,col+1)
 down=max(0,row-1);up=min(nz-1,row+1)
 dx=(verts[row*nx+right][2]-verts[row*nx+left][2])/((right-left)*.5)
 dz=(verts[up*nx+col][2]-verts[down*nx+col][2])/((up-down)*.5)
 slope=math.hypot(dx,dz)
 exposed=max(0,min(1,(slope-.65)/1.0)) if d>0 else 0
 limestone=(.39,.32,.22) if k!=2 else (.105,.085,.07)
 c=[c[q]*(1-exposed)+limestone[q]*exposed for q in range(3)]
 attr.data[i].color=(*c,1)
 rock_weight=max(exposed,max(0,min(1,(y-1.3)/1.3)) if k==2 and not lagoon else 0)
 strength=(.015+.15*rock_weight)*max(0,min(1,d/.8))
 relief_attr.data[i].color=(strength,strength,strength,1)
uv=mesh.uv_layers.new(name='CoastUV')
for loop in mesh.loops:
 x,y,z=mesh.vertices[loop.vertex_index].co;uv.data[loop.index].uv=((x+60)/120,(y+45)/90)
m=bpy.data.materials.new('Coastal strata');m.use_nodes=True;n=m.node_tree.nodes;l=m.node_tree.links
bs=n.get('Principled BSDF');bs.inputs['Roughness'].default_value=.93
vc=n.new('ShaderNodeVertexColor');vc.layer_name='Biome'
noise=n.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=1.1;noise.inputs['Detail'].default_value=5
ramp=n.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].color=(.4,.4,.4,1);ramp.color_ramp.elements[1].color=(1,1,1,1)
l.new(noise.outputs['Fac'],ramp.inputs[0])
mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=.65
l.new(vc.outputs['Color'],mix.inputs[1]);l.new(ramp.outputs[0],mix.inputs[2]);l.new(mix.outputs[0],bs.inputs['Base Color'])
# Fade the textured shelf into the exact material color of the flat ocean
# floor. Noise and a different sand tint otherwise reveal the mesh boundary
# even through deep water. Height is local Blender Z (world Y on export).
geometry=n.new('ShaderNodeNewGeometry');separate=n.new('ShaderNodeSeparateXYZ')
# Generated coordinates normalize each bounding-box axis independently.
# On this wide, shallow mesh that compressed noise vertically into contour
# rings. World coordinates give sand/rock variation the same scale on X/Y/Z.
l.new(geometry.outputs['Position'],noise.inputs['Vector'])
l.new(geometry.outputs['Position'],separate.inputs[0])
depth_blend=n.new('ShaderNodeMapRange');depth_blend.interpolation_type='SMOOTHSTEP'
depth_blend.inputs['From Min'].default_value=-8
depth_blend.inputs['From Max'].default_value=-11.5
l.new(separate.outputs['Z'],depth_blend.inputs['Value'])
floor_mix=n.new('ShaderNodeMixRGB');floor_mix.inputs[2].default_value=(.58,.46,.29,1)
l.new(depth_blend.outputs['Result'],floor_mix.inputs[0]);l.new(mix.outputs[0],floor_mix.inputs[1])
l.new(floor_mix.outputs[0],bs.inputs['Base Color'])
# Bake world-scaled rock fissures and granular relief into tangent normals.
# Stronger on exposed limestone/basalt, subtle on grass, zero under water.
# This adds surface lighting detail without changing silhouettes/collision.
grain=n.new('ShaderNodeTexNoise');grain.inputs['Scale'].default_value=3.5;grain.inputs['Detail'].default_value=3
l.new(geometry.outputs['Position'],grain.inputs['Vector'])
cracks=n.new('ShaderNodeTexVoronoi');cracks.feature='DISTANCE_TO_EDGE';cracks.inputs['Scale'].default_value=1.8
warp=n.new('ShaderNodeTexNoise');warp.inputs['Scale'].default_value=1.3;warp.inputs['Detail'].default_value=2
l.new(geometry.outputs['Position'],warp.inputs['Vector'])
warp_scale=n.new('ShaderNodeVectorMath');warp_scale.operation='SCALE';warp_scale.inputs['Scale'].default_value=.7
l.new(warp.outputs['Color'],warp_scale.inputs[0])
warp_add=n.new('ShaderNodeVectorMath');warp_add.operation='ADD'
l.new(geometry.outputs['Position'],warp_add.inputs[0]);l.new(warp_scale.outputs[0],warp_add.inputs[1])
l.new(warp_add.outputs[0],cracks.inputs['Vector'])
crack_ramp=n.new('ShaderNodeMapRange');crack_ramp.interpolation_type='SMOOTHSTEP'
crack_ramp.inputs['From Min'].default_value=.015;crack_ramp.inputs['From Max'].default_value=.14
l.new(cracks.outputs['Distance'],crack_ramp.inputs['Value'])
height_mix=n.new('ShaderNodeMixRGB');height_mix.inputs[0].default_value=.18
l.new(grain.outputs['Fac'],height_mix.inputs[1]);l.new(crack_ramp.outputs['Result'],height_mix.inputs[2])
relief_color=n.new('ShaderNodeVertexColor');relief_color.layer_name='SurfaceRelief'
bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.8
l.new(relief_color.outputs['Color'],bump.inputs['Distance']);l.new(height_mix.outputs[0],bump.inputs['Height'])
l.new(bump.outputs['Normal'],bs.inputs['Normal'])
mesh.materials.clear();mesh.materials.append(m)
for p in mesh.polygons:p.material_index=0
image=bpy.data.images.new('CoastAlbedo',width=2048,height=2048)
tex=n.new('ShaderNodeTexImage');tex.image=image;n.active=tex
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=8
scene.render.bake.use_pass_direct=False;scene.render.bake.use_pass_indirect=False;scene.render.bake.use_pass_color=True
bpy.ops.object.bake(type='DIFFUSE')
image.filepath_raw=str(root/'art'/f'{asset}-albedo.png');image.file_format='PNG';image.save();image.pack()
l.new(tex.outputs['Color'],bs.inputs['Base Color'])
normal_image=bpy.data.images.new('CoastNormal',width=2048,height=2048)
normal_image.colorspace_settings.name='Non-Color'
normal_tex=n.new('ShaderNodeTexImage');normal_tex.image=normal_image;n.active=normal_tex
scene.render.bake.normal_space='TANGENT'
bpy.ops.object.bake(type='NORMAL')
normal_image.filepath_raw=str(root/'art'/f'{asset}-normal.png');normal_image.file_format='PNG';normal_image.save();normal_image.pack()
normal_map=n.new('ShaderNodeNormalMap');normal_map.uv_map='CoastUV'
l.new(normal_tex.outputs['Color'],normal_map.inputs['Color']);l.new(normal_map.outputs['Normal'],bs.inputs['Normal'])
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art'/f'{asset}.blend'))
# Biome and relief attributes are authoring inputs already baked into maps.
# Avoid exporting redundant per-vertex colors and a white COLOR_0 stream.
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models'/f'{asset}.glb'),export_format='GLB',use_selection=True,export_yup=True,export_vertex_color='NONE')
header='/* Generated by tools/make-coast.py; X,Z coastline used by rendering and collision. */\n'
header+='static const int COAST_N[3] = {'+','.join(str(len(p)) for p in coasts)+'};\n'
header+='static const float COAST[3][%d][2] = {\n'%max(map(len,coasts))
for poly in coasts:header+='{'+','.join('{%s,%s}'%p for p in poly)+'},\n'
header+='};\n'
# Export the exact mesh heights so props sample the same surface as Blender.
header+='static const float COAST_HEIGHT[181][241] = {\n'
for j in range(nz):header+='{'+','.join('%.4ff'%verts[j*nx+i][2] for i in range(nx))+'},\n'
header+='};\n'
header+='''static float coast_height(float x,float z) {
 float gx=(x+60)*2,gz=(z+45)*2;
 int ix=(int)gx,iz=(int)gz;
 if(ix<0||ix>=240||iz<0||iz>=180)return -12;
 float u=gx-ix,v=gz-iz;
 return (COAST_HEIGHT[iz][ix]*(1-u)+COAST_HEIGHT[iz][ix+1]*u)*(1-v)
       +(COAST_HEIGHT[iz+1][ix]*(1-u)+COAST_HEIGHT[iz+1][ix+1]*u)*v;
}
'''

(root/'src'/f'{asset}.h').write_text(header)
