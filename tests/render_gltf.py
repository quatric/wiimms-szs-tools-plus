import bpy
import sys
import os
from mathutils import Vector

def main():
    argv = sys.argv
    if "--" in argv:
        args = argv[argv.index("--") + 1:]
    else:
        args = []

    if len(args) < 2:
        print("Usage: blender -b -P render_gltf.py -- <input.gltf> <output.png>")
        sys.exit(1)

    gltf_path = os.path.abspath(args[0])
    out_png = os.path.abspath(args[1])

    # Reset scene
    bpy.ops.wm.read_factory_settings(use_empty=True)

    # Import glTF
    res = bpy.ops.import_scene.gltf(filepath=gltf_path)
    print(f"[RENDER] Import status: {res}")

    mesh_objs = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    if not mesh_objs:
        print("[RENDER] ERROR: No mesh objects in scene")
        sys.exit(2)

    total_verts = sum(len(m.data.vertices) for m in mesh_objs)
    print(f"[RENDER] Loaded {len(mesh_objs)} mesh objects with {total_verts} vertices")

    # Bounding box
    min_co = [float('inf')]*3
    max_co = [float('-inf')]*3
    for obj in mesh_objs:
        for v in obj.bound_box:
            world_v = obj.matrix_world @ Vector(v)
            for i in range(3):
                min_co[i] = min(min_co[i], world_v[i])
                max_co[i] = max(max_co[i], world_v[i])

    center = Vector([(min_co[i] + max_co[i]) / 2 for i in range(3)])
    size = max(max_co[i] - min_co[i] for i in range(3))
    if size <= 1e-4:
        size = 1.0

    print(f"[RENDER] Center: {center}, Size: {size}")

    # Camera
    cam_data = bpy.data.cameras.new("RenderCamera")
    cam_obj = bpy.data.objects.new("RenderCamera", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    bpy.context.scene.camera = cam_obj

    cam_dist = size * 2.0
    cam_obj.location = center + Vector((cam_dist * 0.7, -cam_dist * 0.8, cam_dist * 0.6))
    direction = center - cam_obj.location
    cam_obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

    # Sun Light
    light_data = bpy.data.lights.new("Sun", 'SUN')
    light_data.energy = 5.0
    light_obj = bpy.data.objects.new("Sun", light_data)
    bpy.context.scene.collection.objects.link(light_obj)
    light_obj.location = center + Vector((cam_dist, -cam_dist, cam_dist * 1.5))
    light_obj.rotation_euler = (center - light_obj.location).to_track_quat('-Z', 'Y').to_euler()

    # Fill Light
    fill_data = bpy.data.lights.new("Fill", 'SUN')
    fill_data.energy = 2.5
    fill_obj = bpy.data.objects.new("Fill", fill_data)
    bpy.context.scene.collection.objects.link(fill_obj)
    fill_obj.location = center + Vector((-cam_dist, cam_dist, cam_dist))
    fill_obj.rotation_euler = (center - fill_obj.location).to_track_quat('-Z', 'Y').to_euler()

    # Render settings
    bpy.context.scene.render.resolution_x = 512
    bpy.context.scene.render.resolution_y = 512
    bpy.context.scene.render.filepath = out_png
    bpy.context.scene.render.image_settings.file_format = 'PNG'

    # Render image
    bpy.ops.render.render(write_still=True)
    if os.path.exists(out_png) and os.path.getsize(out_png) > 0:
        print(f"[RENDER] SUCCESS: Rendered {out_png} ({os.path.getsize(out_png)} bytes)")
    else:
        print(f"[RENDER] ERROR: Output file {out_png} missing or empty")
        sys.exit(3)

if __name__ == "__main__":
    main()
