import bpy, sys, os, math
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []

# args: <asset_type> <out_blend>
asset_type = argv[0] if len(argv) > 0 else "crate"
out_blend = argv[1] if len(argv) > 1 else "out/assets/tmp_asset.blend"

os.makedirs(os.path.dirname(out_blend), exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

# basic lighting/camera for preview friendliness (optional)
bpy.ops.object.light_add(type='AREA', location=(3, -3, 4))
light = bpy.context.object
light.data.energy = 800

bpy.ops.object.camera_add(location=(3, -3, 2), rotation=(math.radians(65), 0, math.radians(45)))
bpy.context.scene.camera = bpy.context.object

def make_crate():
  bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0,0,0.5))
  obj = bpy.context.object
  obj.name = "ASSET"
  # bevel for nicer look
  bpy.ops.object.modifier_add(type='BEVEL')
  obj.modifiers["Bevel"].width = 0.03
  obj.modifiers["Bevel"].segments = 3
  bpy.ops.object.modifier_apply(modifier="Bevel")

def make_pillar():
  bpy.ops.mesh.primitive_cylinder_add(radius=0.25, depth=2.0, location=(0,0,1.0))
  obj = bpy.context.object
  obj.name = "ASSET"
  bpy.ops.object.shade_smooth()

def make_rock():
  bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.7, location=(0,0,0.7))
  obj = bpy.context.object
  obj.name = "ASSET"
  # deform with displace
  tex = bpy.data.textures.new("NoiseTex", type='CLOUDS')
  bpy.ops.object.modifier_add(type='DISPLACE')
  obj.modifiers["Displace"].texture = tex
  obj.modifiers["Displace"].strength = 0.25
  bpy.ops.object.modifier_apply(modifier="Displace")
  bpy.ops.object.shade_smooth()

if asset_type == "crate":
  make_crate()
elif asset_type == "pillar":
  make_pillar()
elif asset_type == "rock":
  make_rock()
else:
  make_crate()

# save .blend
bpy.ops.wm.save_as_mainfile(filepath=out_blend)
print("OK: saved blend:", out_blend)
