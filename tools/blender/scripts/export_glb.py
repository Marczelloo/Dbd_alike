import bpy, sys, os

argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []

# args: <in_blend> <out_glb>
in_blend = argv[0] if len(argv) > 0 else "out/assets/tmp_asset.blend"
out_glb  = argv[1] if len(argv) > 1 else "out/assets/tmp_asset.glb"

os.makedirs(os.path.dirname(out_glb), exist_ok=True)

# open blend
bpy.ops.wm.open_mainfile(filepath=in_blend)

# select only object named ASSET if exists, else export all
bpy.ops.object.select_all(action='DESELECT')
asset = bpy.data.objects.get("ASSET")
if asset:
  asset.select_set(True)
  bpy.context.view_layer.objects.active = asset
  use_selection = True
  # apply transforms (scale/rot)
  bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
else:
  use_selection = False

bpy.ops.export_scene.gltf(
  filepath=out_glb,
  export_format='GLB',
  use_selection=use_selection,
  export_apply=True,
  export_yup=True
)

print("OK: exported glb:", out_glb)
