"""Weathered shore boulders: three faceted stone variants with flared, embedded feet.

Each variant is the convex hull of seeded points on a main mass and a smaller
lobe beside it, its crown pushed off centre, chipped, split once and dented by
smooth noise and a few cracks, so its outline is lopsided and broken.
Below the dome's widest ring a foot flares outward and down, so nothing of the
stone overhangs its own base: tilted to a beach slope and sunk until the foot is
under the terrain everywhere, every point of its underside stays in the ground.
Colour is a per-corner tint (grey to tan per stone, darker cracks and a damp
band near the ground) times a tiling grain, baked into one albedo per stone;
the surface is matte.

Run with Blender 4.2.9 from the repository root:
  blender -b --disable-autoexec --python games/pirates/tools/make-shore-rock.py
Writes art/shore-rock.blend and assets/models/shore-rock-0.glb .. shore-rock-2.glb.
"""
import bpy, bmesh, math, random
import numpy as np
from mathutils import Vector, noise
from pathlib import Path
root=Path(__file__).resolve().parents[1]
assert bpy.app.version[:3]==(4,2,9), bpy.app.version_string
bpy.ops.wm.read_factory_settings(use_empty=True)

FOOT_DEPTH=.55    # model units below the widest ring
FOOT_FLARE=.26    # how far the foot's rim reaches beyond that ring

def grain_texture(size=256,seed=7):
    """Tiling stone grain: band-limited noise with pits, values 0.70 to 1.0."""
    rng=np.random.default_rng(seed)
    f=np.fft.fftfreq(size)[:,None]**2+np.fft.fftfreq(size)[None,:]**2
    def band(lo,hi):
        spec=np.fft.fft2(rng.standard_normal((size,size)))
        spec*=np.exp(-f/(hi*hi))*(1-np.exp(-f/(lo*lo)))
        n=np.real(np.fft.ifft2(spec));return (n-n.mean())/n.std()
    coarse=band(.004,.03);fine=band(.03,.25);
    v=.86+.06*coarse+.05*fine
    pits=band(.05,.4);v-=.12*np.clip(pits-1.6,0,1.5)
    v=np.clip(v,.62,1.0)
    rgb=np.stack([v*1.0,v*.985,v*.96,np.ones_like(v)],-1).astype(np.float32)
    img=bpy.data.images.new('shore-rock-grain',size,size,alpha=False)
    img.colorspace_settings.name='sRGB'
    img.pixels.foreach_set(rgb.ravel());img.file_format='PNG';img.pack()
    return img

def source_material(img):
    """The grain texture (cube-projected UVs) times the per-corner tint; baked below."""
    m=bpy.data.materials.new('stone source');m.use_nodes=True
    nt=m.node_tree;bsdf=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
    uv=nt.nodes.new('ShaderNodeUVMap');uv.uv_map='Grain'
    tex=nt.nodes.new('ShaderNodeTexImage');tex.image=img;tex.extension='REPEAT'
    nt.links.new(uv.outputs['UV'],tex.inputs['Vector'])
    col=nt.nodes.new('ShaderNodeVertexColor');col.layer_name='Col'
    mix=nt.nodes.new('ShaderNodeMix');mix.data_type='RGBA';mix.blend_type='MULTIPLY'
    mix.inputs['Factor'].default_value=1.0
    nt.links.new(tex.outputs['Color'],mix.inputs[6]);nt.links.new(col.outputs['Color'],mix.inputs[7])
    nt.links.new(mix.outputs[2],bsdf.inputs['Base Color'])
    return m

def stone_material(name,img):
    """What ships: one baked albedo, matte, drawn from both sides like the harbors."""
    m=bpy.data.materials.new(name);m.use_nodes=True;m.use_backface_culling=False
    nt=m.node_tree;bsdf=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
    tex=nt.nodes.new('ShaderNodeTexImage');tex.image=img
    nt.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    bsdf.inputs['Roughness'].default_value=.96;bsdf.inputs['Metallic'].default_value=0
    bsdf.inputs['Specular IOR Level'].default_value=.5
    return m

def inside_scale(p,ring):
    """Largest factor <= 1 that keeps p's horizontal position inside the convex ring."""
    s=1.0
    for i in range(len(ring)):
        a=ring[i];b=ring[(i+1)%len(ring)]
        nx,ny=b.y-a.y,a.x-b.x   # outward for a counter-clockwise ring
        ln=math.hypot(nx,ny);nx/=ln;ny/=ln
        lim=(a.x*nx+a.y*ny)*.96;d=p.x*nx+p.y*ny
        if d>lim>0:s=min(s,lim/d)
    return s

def ray_ellipse(t,cx,cy,a,b):
    """Distance from the origin along angle t to the far side of an ellipse."""
    dx,dy=math.cos(t)/a,math.sin(t)/b;ox,oy=-cx/a,-cy/b
    A=dx*dx+dy*dy;B=2*(dx*ox+dy*oy);C=ox*ox+oy*oy-1;D=B*B-4*A*C
    return 0 if D<0 else max(0,(-B+math.sqrt(D))/(2*A))

def convex_ring(points):
    """Counter-clockwise convex hull of 2D points (monotone chain)."""
    pts=sorted(points,key=lambda p:(p.x,p.y))
    def half(seq):
        out=[]
        for p in seq:
            while len(out)>=2 and (out[-1].x-out[-2].x)*(p.y-out[-2].y)-(out[-1].y-out[-2].y)*(p.x-out[-2].x)<=0:out.pop()
            out.append(p)
        return out
    lo=half(pts);hi=half(reversed(pts));return lo[:-1]+hi[:-1]

def stone(index,seed,rx,ry,h,lobe,lean_top,img,mat):
    """A main mass and a smaller lobe beside it, the top pushed off centre."""
    rng=random.Random(seed)
    lx,ly,lrx,lry,lh=lobe
    # widest outline near the ground plane z=0: both masses' footprints, unevenly
    n=18;ring=[]
    for i in range(n):
        t=(i+rng.uniform(-.35,.35))/n*math.tau
        r=max(ray_ellipse(t,0,0,rx,ry),ray_ellipse(t,lx,ly,lrx,lry))*rng.uniform(.9,1.03)
        # an uneven rim, so no level crease runs round the stone
        ring.append(Vector((r*math.cos(t),r*math.sin(t),h*rng.uniform(0,.16))))
    hull2d=convex_ring(ring)
    bm=bmesh.new()
    for p in ring:bm.verts.new(p)
    for i in range(46):
        main=i%3!=2
        cx,cy,a,b,hh=(0,0,rx,ry,h) if main else (lx,ly,lrx,lry,lh)
        t=rng.uniform(0,math.tau);e=math.asin(rng.uniform(.1,.98));k=rng.uniform(.80,1.0)
        # the sides lean in from the ground up, like a half-buried stone, then round over
        lean=1-.40*math.sin(e)
        z=hh*math.sin(e)*k*rng.uniform(.9,1.0)
        p=Vector((cx+a*math.cos(e)*math.cos(t)*k*lean,cy+b*math.cos(e)*math.sin(t)*k*lean,z))
        # a lopsided crown: the higher a point, the further it slides to one side
        p.x+=lean_top[0]*(z/h)**1.5;p.y+=lean_top[1]*(z/h)**1.5
        s=inside_scale(p,hull2d);p.x*=s;p.y*=s
        bm.verts.new(p)
    hull=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
    bmesh.ops.delete(bm,geom=list(set(hull['geom_interior'])|set(hull['geom_unused'])),context='VERTS')
    # broken faces: flat chips sliced off the upper shoulders
    for _ in range(5):
        t=rng.uniform(0,math.tau);e=math.radians(rng.uniform(12,70))
        nrm=Vector((math.cos(t)*math.cos(e),math.sin(t)*math.cos(e),math.sin(e)))
        reach=max(v.co.dot(nrm) for v in bm.verts)
        bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=1e-5,
            plane_co=nrm*reach*rng.uniform(.78,.9),plane_no=nrm,clear_outer=True)
        bmesh.ops.holes_fill(bm,edges=[e for e in bm.edges if e.is_boundary],sides=0)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    # open the underside (the faces across the rim) for the foot
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.normal.z<-.5 and f.calc_center_median().z<.2*h],context='FACES_ONLY')
    bmesh.ops.delete(bm,geom=[e for e in bm.edges if not e.link_faces],context='EDGES')
    # one split, then cracks pushed in toward the core (the ring stays put)
    bmesh.ops.subdivide_edges(bm,edges=list(bm.edges),cuts=1,use_grid_fill=True)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    cracks=[]
    for _ in range(3):
        nrm=Vector((rng.uniform(-1,1),rng.uniform(-1,1),rng.uniform(-.4,.4))).normalized()
        cracks.append((nrm,rng.uniform(-.35,.35),rng.uniform(.08,.13),rng.uniform(.12,.18)))
    depth={}
    for v in bm.verts:
        if v.is_boundary:depth[v]=0;continue
        d=0
        for nrm,off,width,dep in cracks:
            dist=abs(v.co.dot(nrm)-off)
            if dist<width:d=max(d,dep*(1-dist/width))
        lump=.13*max(0,noise.noise(v.co*1.6+Vector((seed,0,0))))+.05*max(0,noise.noise(v.co*4.1-Vector((0,seed,0))))
        c=Vector((0,0,v.co.z*.35));v.co=c+(v.co-c)*(1-(d+lump)/max((v.co-c).length,.3))
        depth[v]=d
    # flared foot: the rim reaches out and down so the stone never overhangs its base
    # in two steps, the first a little uneven, so the exposed foot reads as rock
    ring_radius={}
    for stage in (0,1):
        rim=[e for e in bm.edges if e.is_boundary]
        ext=bmesh.ops.extrude_edge_only(bm,edges=rim)
        for v in [g for g in ext['geom'] if isinstance(g,bmesh.types.BMVert)]:
            here=math.hypot(v.co.x,v.co.y);key=(round(v.co.x,4),round(v.co.y,4))
            if stage==0:
                out=here+FOOT_FLARE*.18/FOOT_DEPTH*rng.uniform(.75,1.25);z=-.18
            else:
                out=ring_radius[key]+FOOT_FLARE;z=-FOOT_DEPTH
            k=out/max(here,.2);v.co=Vector((v.co.x*k,v.co.y*k,z));depth[v]=0
            if stage==0:ring_radius[(round(v.co.x,4),round(v.co.y,4))]=here
    bmesh.ops.holes_fill(bm,edges=[e for e in bm.edges if e.is_boundary],sides=0)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    assert all(e.is_manifold for e in bm.edges),'shore boulder must be a closed solid'
    # colour, per corner so facets keep their own shade
    tone=[(.74,.70,.62),(.70,.69,.66),(.76,.68,.56)][index]
    col=bm.loops.layers.color.new('Col');uv=bm.loops.layers.uv.new('Grain')
    for f in bm.faces:
        fshade=rng.uniform(.90,1.10)*(1+.16*max(f.normal.z,0))
        ax=max(range(3),key=lambda i:abs(f.normal[i]))
        for l in f.loops:
            p=l.vert.co;q=[p[i] for i in range(3) if i!=ax]
            l[uv].uv=(q[0]*.9+index*.31,q[1]*.9+index*.17)
            n1=math.sin(p.x*2.1+seed)*math.cos(p.y*1.7-seed*.3)+.5*math.sin(p.z*3.3+p.x)
            value=fshade*(1+.10*n1)
            value*=1-min(depth.get(l.vert,0)*4.0,.45)
            # damp band near the ground and the buried foot: darker and a little cooler
            wet=min(max((.10+.08*n1-p.z)/.3,0),1)
            value*=1-.14*wet
            warm=1+.05*n1
            c=(tone[0]*value*warm,tone[1]*value,tone[2]*value*(1+.06*wet)/warm)
            l[col]=(c[0],c[1],c[2],1)
    me=bpy.data.meshes.new(f'shore-rock-{index}');bm.to_mesh(me);bm.free()
    for poly in me.polygons:poly.use_smooth=False
    me.materials.append(mat)
    obj=bpy.data.objects.new(f'Shore boulder {index}',me);bpy.context.scene.collection.objects.link(obj)
    obj.location.x=index*3.2
    return obj

img=grain_texture();source=source_material(img)
# main radii and height, the lobe (centre, radii, height) and the crown's lean
shapes=[(1.15,.95,.95,(.75,.45,.7,.6,.6),(.30,-.12)),
        (1.0,.8,1.0,(-.6,.55,.65,.7,.7),(-.20,.25)),
        (1.3,.75,.72,(.2,-.7,.8,.55,.5),(.35,.15))]
objs=[stone(i,101+37*i,*shapes[i],img,source) for i in range(3)]
# Bake each stone's tint and grain into its own albedo on an unwrapped atlas,
# so the shipped material is a plain textured one.
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=4
for i,obj in enumerate(objs):
    me=obj.data;bake_uv=me.uv_layers.new(name='UVMap')
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    me.uv_layers.active=bake_uv
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60),island_margin=.01)
    bpy.ops.object.mode_set(mode='OBJECT')
    albedo=bpy.data.images.new(f'shore-rock-{i}-albedo',512,512,alpha=False)
    albedo.colorspace_settings.name='sRGB'
    nt=source.node_tree;target=nt.nodes.new('ShaderNodeTexImage');target.image=albedo
    target_uv=nt.nodes.new('ShaderNodeUVMap');target_uv.uv_map='UVMap';nt.links.new(target_uv.outputs['UV'],target.inputs['Vector'])
    nt.nodes.active=target
    bpy.ops.object.bake(type='DIFFUSE',pass_filter={'COLOR'},margin=6,use_clear=True)
    nt.nodes.remove(target);nt.nodes.remove(target_uv)
    albedo.file_format='PNG';albedo.pack()
    me.materials.clear();me.materials.append(stone_material(f'weathered stone {i}',albedo))
    me.uv_layers.remove(me.uv_layers['Grain']);me.color_attributes.remove(me.color_attributes['Col'])
bpy.data.materials.remove(source);bpy.data.images.remove(img)
scene.render.engine='BLENDER_EEVEE_NEXT'
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art/shore-rock.blend'))
for i,obj in enumerate(objs):
    loc=obj.location.copy();obj.location=(0,0,0)
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    bpy.ops.export_scene.gltf(filepath=str(root/f'assets/models/shore-rock-{i}.glb'),export_format='GLB',
        use_selection=True,export_yup=True,export_vertex_color='NONE',export_image_format='AUTO')
    obj.location=loc
