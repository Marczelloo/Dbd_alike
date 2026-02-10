import bpy, sys, os

argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []

in_blend = argv[0] if len(argv) > 0 else "out/assets/tmp_asset.blend"
out_png  = argv[1] if len(argv) > 1 else "out/previews/tmp_asset.png"

# Normalize to absolute paths (critical!)
in_blend_abs = os.path.abspath(in_blend)
out_png_abs  = os.path.abspath(out_png)

os.makedirs(os.path.dirname(out_png_abs), exist_ok=True)

print("IN :", in_blend_abs)
print("OUT:", out_png_abs)

bpy.ops.wm.open_mainfile(filepath=in_blend_abs)

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 512
scene.render.resolution_y = 512

# Force PNG and correct filepath
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = out_png_abs

bpy.ops.render.render(write_still=True)

# Verify output exists
if os.path.exists(out_png_abs):
    print("OK: rendered preview:", out_png_abs)
else:
    print("ERROR: render finished but file not found at:", out_png_abs)
