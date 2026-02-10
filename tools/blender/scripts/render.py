import bpy, sys, os

argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []
out_path = argv[0] if argv else "out/test.png"
os.makedirs(os.path.dirname(out_path), exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

bpy.ops.mesh.primitive_cube_add(size=2, location=(0,0,0))
bpy.ops.object.light_add(type='SUN', location=(5,5,5))
bpy.ops.object.camera_add(location=(6,-6,4), rotation=(1.1,0,0.8))
bpy.context.scene.camera = bpy.context.object

scene = bpy.context.scene
scene.render.filepath = out_path
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
bpy.ops.render.render(write_still=True)
print("OK:", out_path)
