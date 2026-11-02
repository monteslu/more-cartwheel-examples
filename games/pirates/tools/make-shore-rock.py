"""Give the original repository rock a solid, broad foot for sloping beaches."""
import bpy, bmesh
from pathlib import Path
root=Path(__file__).resolve().parents[1]
assert bpy.app.version[:3]==(4,2,9)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(root/'assets/models/rock0.glb'))
obj=next(o for o in bpy.context.scene.objects if o.type=='MESH')
bpy.context.view_layer.objects.active=obj
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bm=bmesh.new();bm.from_mesh(obj.data)
# Remove the debris mesh's overlapping lobes and inward folds. Preserve its
# outer silhouette, then extend a broad base below the visible rock cap.
points=[v.co.copy() for v in bm.verts]
bm.clear()
for point in points:bm.verts.new(point)
hull=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
bmesh.ops.delete(bm,geom=list(set(hull['geom_interior'])|set(hull['geom_unused'])),context='VERTS')
low=min(v.co.z for v in bm.verts);high=max(v.co.z for v in bm.verts)
cut=low+(high-low)*.28
bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=.00001,
 plane_co=(0,0,cut),plane_no=(0,0,1),clear_inner=True,clear_outer=False)
base=[v for v in bm.verts if abs(v.co.z-cut)<.0001]
for v in base:v.co.z=low-.45
bmesh.ops.holes_fill(bm,edges=[e for e in bm.edges if e.is_boundary],sides=0)
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
assert all(e.is_manifold for e in bm.edges), 'shore boulder must be a closed solid'
bm.to_mesh(obj.data);bm.free();obj.data.update()
obj.name='Shore boulder with embedded base'
for face in obj.data.polygons:face.use_smooth=True
bpy.ops.object.select_all(action='DESELECT');obj.select_set(True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'art/shore-rock.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'assets/models/shore-rock.glb'),export_format='GLB',use_selection=True,export_yup=True)
