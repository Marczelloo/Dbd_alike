"""
Large Mossy Rock Generator - Fresh Procedural Generation
Creates a rock with moss patches using procedural displacement and materials.
High-res mesh -> Bake -> Low-res mesh with textures -> LODs + Collider
"""

import bpy
import sys
import os
import random
from mathutils import Vector

# Parse arguments
argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []

blend_path = "out/assets/rock_moss_large.blend"
if len(argv) > 0:
    blend_path = argv[0]

output_dir = os.path.dirname(blend_path)
if not output_dir:
    output_dir = "out/assets"
asset_name = os.path.splitext(os.path.basename(blend_path))[0]
texture_dir = os.path.join(output_dir, asset_name)
os.makedirs(texture_dir, exist_ok=True)
os.makedirs("out/previews", exist_ok=True)

# FRESH RANDOM SEED for different shape
random.seed(42)
noise_seed = 789456  # Different seed for fresh generation

print("=" * 60)
print(f"LARGE MOSSY ROCK GENERATOR - Fresh Build")
print(f"Asset: {asset_name}")
print(f"Seed: {noise_seed}")
print("=" * 60)

# === CLEAR SCENE TO FACTORY STATE ===
print("\n[1/12] Clearing scene to factory state...")
bpy.ops.wm.read_factory_settings(use_empty=True)

# === SETUP PREVIEW SCENE ===
print("\n[2/12] Setting up preview scene...")

# Camera
bpy.ops.object.camera_add(location=(4, -4, 2.5))
cam = bpy.context.object
cam.name = "PreviewCamera"
cam.data.dof.use_dof = False
bpy.context.scene.camera = cam
direction = Vector((0, 0, 0.5)) - Vector((4, -4, 2.5))
rot_quat = direction.to_track_quat('-Z', 'Y')
cam.rotation_euler = rot_quat.to_euler()

# Lighting
bpy.ops.object.light_add(type='SUN', location=(5, -5, 10))
sun = bpy.context.object
sun.name = "KeyLight"
sun.data.energy = 5
sun.data.angle = 0.5
sun.data.shadow_soft_size = 0.2

bpy.ops.object.light_add(type='AREA', location=(-3, 3, 5))
fill = bpy.context.object
fill.name = "FillLight"
fill.data.energy = 300
fill.data.size = 5

bpy.context.scene.render.film_transparent = True

# === CREATE HIGH-RES ROCK MESH ===
print("\n[3/12] Creating high-res rock mesh...")

# Use different base settings for fresh variation
bpy.ops.mesh.primitive_ico_sphere_add(radius=1.2, subdivisions=8, location=(0, 0, 0.8))
rock_high = bpy.context.object
rock_high.name = "rock_high_res"

# Apply non-uniform scale for organic shape
scale_x = 1.0 + random.uniform(-0.25, 0.15)
scale_y = 1.0 + random.uniform(-0.15, 0.25)
scale_z = 1.0 + random.uniform(0.1, 0.4)
rock_high.scale = (scale_x * 1.4, scale_y * 1.3, scale_z * 1.2)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

# === ADD DISPLACEMENT FOR DETAIL ===
print("  - Adding displacement layers...")

# Layer 1: Large shape variation
clouds_tex = bpy.data.textures.new(name="rock_clouds", type='CLOUDS')
clouds_tex.noise_scale = 0.35
clouds_tex.noise_depth = 5

displace1 = rock_high.modifiers.new(name="displace_large", type='DISPLACE')
displace1.texture = clouds_tex
displace1.strength = 0.5

# Layer 2: Medium detail
voronoi_tex = bpy.data.textures.new(name="rock_voronoi", type='VORONOI')
voronoi_tex.distance_metric = 'DISTANCE'
voronoi_tex.noise_scale = 1.5

displace2 = rock_high.modifiers.new(name="displace_medium", type='DISPLACE')
displace2.texture = voronoi_tex
displace2.strength = 0.25

# Layer 3: Fine surface detail
fine_tex = bpy.data.textures.new(name="rock_fine", type='CLOUDS')
fine_tex.noise_scale = 4.0
fine_tex.noise_depth = 3

displace3 = rock_high.modifiers.new(name="displace_fine", type='DISPLACE')
displace3.texture = fine_tex
displace3.strength = 0.12

# Apply displacement modifiers
bpy.context.view_layer.objects.active = rock_high
for mod in reversed(rock_high.modifiers):
    if mod.type == 'DISPLACE':
        bpy.ops.object.modifier_apply(modifier=mod.name)

# === FLATTEN BOTTOM ===
print("  - Flattening bottom for ground contact...")
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')

# Use simple bottom scale
import bmesh
bm = bmesh.from_edit_mesh(rock_high.data)
bm.verts.ensure_lookup_table()

for v in bm.verts:
    if v.co.z < 0.15:
        v.select = True

# Scale down bottom
bpy.ops.transform.resize(value=(0.6, 0.6, 0.3))
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')

# Subsurf for smooth
subsurf = rock_high.modifiers.new(name="subsurf", type='SUBSURF')
subsurf.levels = 2
bpy.ops.object.modifier_apply(modifier='subsurf')

bpy.ops.object.shade_smooth()

# === CREATE LOW-RES MESH FOR BAKING ===
print("\n[4/12] Creating low-res mesh (target: 6-8k tris)...")

# Duplicate for low-res
bpy.ops.object.duplicate()
rock_low = bpy.context.object
rock_low.name = f"{asset_name}_LOD0"

# Remove modifiers
for mod in rock_low.modifiers[:]:
    rock_low.modifiers.remove(mod)

# Subdivide to reach target polycount
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.subdivide(number_cuts=2)
bpy.ops.mesh.subdivide(number_cuts=1)
bpy.ops.object.mode_set(mode='OBJECT')

bpy.ops.object.shade_smooth()

# === CREATE PROCEDURAL MATERIAL (HIGH-RES) ===
print("\n[5/12] Creating procedural rock+moss material...")

mat_high = bpy.data.materials.new(name="rock_moss_procedural")
mat_high.use_nodes = True
nodes = mat_high.node_tree.nodes
links = mat_high.node_tree.links
nodes.clear()

# Output
output = nodes.new(type='ShaderNodeOutputMaterial')
output.location = (500, 0)

# Principled
principled = nodes.new(type='ShaderNodeBsdfPrincipled')
principled.location = (300, 0)
links.new(principled.outputs[0], output.inputs[0])

# Rock base colors
rock_color_1 = nodes.new(type='ShaderNodeRGB')
rock_color_1.location = (-500, 200)
rock_color_1.outputs[0].default_value = (0.32, 0.30, 0.28, 1.0)

rock_color_2 = nodes.new(type='ShaderNodeRGB')
rock_color_2.location = (-500, 100)
rock_color_2.outputs[0].default_value = (0.20, 0.19, 0.18, 1.0)

# Rock color variation noise
rock_noise = nodes.new(type='ShaderNodeTexNoise')
rock_noise.location = (-700, 150)
rock_noise.inputs['Scale'].default_value = 4.0
rock_noise.inputs['Detail'].default_value = 5.0

# Mix rock colors
rock_mix = nodes.new(type='ShaderNodeMixRGB')
rock_mix.location = (-300, 150)
rock_mix.blend_type = 'MIX'
links.new(rock_noise.outputs[0], rock_mix.inputs['Fac'])
links.new(rock_color_1.outputs[0], rock_mix.inputs['Color1'])
links.new(rock_color_2.outputs[0], rock_mix.inputs['Color2'])

# Moss colors
moss_color_1 = nodes.new(type='ShaderNodeRGB')
moss_color_1.location = (-500, -100)
moss_color_1.outputs[0].default_value = (0.18, 0.24, 0.12, 1.0)

moss_color_2 = nodes.new(type='ShaderNodeRGB')
moss_color_2.location = (-500, -200)
moss_color_2.outputs[0].default_value = (0.28, 0.32, 0.16, 1.0)

# Moss color noise
moss_noise = nodes.new(type='ShaderNodeTexNoise')
moss_noise.location = (-700, -150)
moss_noise.inputs['Scale'].default_value = 30.0
moss_noise.inputs['Detail'].default_value = 4.0

# Mix moss colors
moss_mix = nodes.new(type='ShaderNodeMixRGB')
moss_mix.location = (-300, -150)
moss_mix.blend_type = 'MIX'
links.new(moss_noise.outputs[0], moss_mix.inputs['Fac'])
links.new(moss_color_1.outputs[0], moss_mix.inputs['Color1'])
links.new(moss_color_2.outputs[0], moss_mix.inputs['Color2'])

# === MOSS MASK ===
geometry = nodes.new(type='ShaderNodeNewGeometry')
geometry.location = (-900, 0)

# Normal-based (upward facing)
sep_normal = nodes.new(type='ShaderNodeSeparateXYZ')
sep_normal.location = (-750, 50)
links.new(geometry.outputs['Normal'], sep_normal.inputs[0])

normal_up = nodes.new(type='ShaderNodeMath')
normal_up.operation = 'MULTIPLY'
normal_up.inputs[1].default_value = -1.0  # Invert Z
normal_up.location = (-600, 50)
links.new(sep_normal.outputs[2], normal_up.inputs[0])

normal_clamp = nodes.new(type='ShaderNodeMath')
normal_clamp.operation = 'MAXIMUM'
normal_clamp.use_clamp = True
normal_clamp.inputs[1].default_value = 0.0
normal_clamp.location = (-450, 50)
links.new(normal_up.outputs[0], normal_clamp.inputs[0])

# Position-based (higher = more moss)
sep_pos = nodes.new(type='ShaderNodeSeparateXYZ')
sep_pos.location = (-750, 120)
links.new(geometry.outputs['Position'], sep_pos.inputs[0])

pos_height = nodes.new(type='ShaderNodeMath')
pos_height.operation = 'MULTIPLY'
pos_height.inputs[1].default_value = 0.25
pos_height.location = (-600, 120)
links.new(sep_pos.outputs[2], pos_height.inputs[0])

# Pattern noise
pattern1 = nodes.new(type='ShaderNodeTexNoise')
pattern1.location = (-750, -100)
pattern1.inputs['Scale'].default_value = 3.0
pattern1.inputs['Roughness'].default_value = 0.4

pattern2 = nodes.new(type='ShaderNodeTexNoise')
pattern2.location = (-750, -180)
pattern2.inputs['Scale'].default_value = 10.0
pattern2.inputs['Roughness'].default_value = 0.7

pattern_mult = nodes.new(type='ShaderNodeMath')
pattern_mult.operation = 'MULTIPLY'
pattern_mult.use_clamp = True
pattern_mult.location = (-600, -140)
links.new(pattern1.outputs[0], pattern_mult.inputs[0])
links.new(pattern2.outputs[0], pattern_mult.inputs[1])

# Combine mask
mask1 = nodes.new(type='ShaderNodeMath')
mask1.operation = 'MULTIPLY'
mask1.use_clamp = True
mask1.location = (-400, -50)
links.new(normal_clamp.outputs[0], mask1.inputs[0])
links.new(pattern_mult.outputs[0], mask1.inputs[1])

mask2 = nodes.new(type='ShaderNodeMath')
mask2.operation = 'MULTIPLY'
mask2.use_clamp = True
mask2.location = (-250, -50)
links.new(mask1.outputs[0], mask2.inputs[0])
links.new(pos_height.outputs[0], mask2.inputs[1])

mask_sharpen = nodes.new(type='ShaderNodeMath')
mask_sharpen.operation = 'POWER'
mask_sharpen.inputs[1].default_value = 2.0
mask_sharpen.use_clamp = True
mask_sharpen.location = (-100, -50)
links.new(mask2.outputs[0], mask_sharpen.inputs[0])

mask_final = nodes.new(type='ShaderNodeMath')
mask_final.operation = 'MULTIPLY'
mask_final.inputs[1].default_value = 0.8
mask_final.use_clamp = True
mask_final.location = (50, -50)
links.new(mask_sharpen.outputs[0], mask_final.inputs[0])

# Final mix
final_mix = nodes.new(type='ShaderNodeMixRGB')
final_mix.location = (100, 50)
final_mix.blend_type = 'MIX'
links.new(mask_final.outputs[0], final_mix.inputs['Fac'])
links.new(rock_mix.outputs[0], final_mix.inputs['Color1'])
links.new(moss_mix.outputs[0], final_mix.inputs['Color2'])

# Roughness
rough_rock = nodes.new(type='ShaderNodeValue')
rough_rock.location = (-100, 300)
rough_rock.outputs[0].default_value = 0.80

rough_moss = nodes.new(type='ShaderNodeValue')
rough_moss.location = (-100, 250)
rough_moss.outputs[0].default_value = 0.95

rough_mix = nodes.new(type='ShaderNodeMix')
rough_mix.data_type = 'FLOAT'
rough_mix.location = (100, 300)
links.new(mask_final.outputs[0], rough_mix.inputs[0])
links.new(rough_rock.outputs[0], rough_mix.inputs[1])
links.new(rough_moss.outputs[0], rough_mix.inputs[2])

# Bump
bump_tex = nodes.new(type='ShaderNodeTexNoise')
bump_tex.location = (-300, 400)
bump_tex.inputs['Scale'].default_value = 40.0

bump_strength = nodes.new(type='ShaderNodeMath')
bump_strength.operation = 'MULTIPLY'
bump_strength.inputs[1].default_value = 0.25
bump_strength.location = (-150, 400)
links.new(bump_tex.outputs[0], bump_strength.inputs[0])

bump = nodes.new(type='ShaderNodeBump')
bump.location = (0, 400)
bump.inputs['Strength'].default_value = 0.4
links.new(bump_strength.outputs[0], bump.inputs[0])
links.new(geometry.outputs['Normal'], bump.inputs[1])

# Connect to principled
links.new(final_mix.outputs[0], principled.inputs['Base Color'])
links.new(rough_mix.outputs[0], principled.inputs['Roughness'])
links.new(bump.outputs[0], principled.inputs['Normal'])

# Assign to high-res
rock_high.data.materials.append(mat_high)

# === CREATE LOW-RES MATERIAL (FOR BAKING) ===
print("\n[6/12] Setting up low-res material for baking...")

mat_low = bpy.data.materials.new(name="rock_low_bake")
mat_low.use_nodes = True
nodes_low = mat_low.node_tree.nodes
links_low = mat_low.node_tree.links
nodes_low.clear()

output_low = nodes_low.new(type='ShaderNodeOutputMaterial')
output_low.location = (500, 0)

principled_low = nodes_low.new(type='ShaderNodeBsdfPrincipled')
principled_low.location = (300, 0)
links_low.new(principled_low.outputs[0], output_low.inputs[0])

# Create bake textures (1024 for fast mode)
tex_size = 1024

img_albedo = bpy.data.images.new(name=f"{asset_name}_albedo", width=tex_size, height=tex_size)
node_albedo = nodes_low.new(type='ShaderNodeTexImage')
node_albedo.location = (0, 100)
node_albedo.image = img_albedo
links_low.new(node_albedo.outputs[0], principled_low.inputs['Base Color'])

img_normal = bpy.data.images.new(name=f"{asset_name}_normal", width=tex_size, height=tex_size)
node_normal = nodes_low.new(type='ShaderNodeTexImage')
node_normal.location = (0, 0)
node_normal.image = img_normal

normal_map = nodes_low.new(type='ShaderNodeNormalMap')
normal_map.location = (150, -50)
links_low.new(node_normal.outputs[0], normal_map.inputs[0])
links_low.new(normal_map.outputs[0], principled_low.inputs['Normal'])

img_rough = bpy.data.images.new(name=f"{asset_name}_roughness", width=tex_size, height=tex_size)
node_rough = nodes_low.new(type='ShaderNodeTexImage')
node_rough.location = (0, -100)
node_rough.image = img_rough
links_low.new(node_rough.outputs[0], principled_low.inputs['Roughness'])

# Assign to low-res
rock_low.data.materials.append(mat_low)

# === GENERATE UVs ===
print("\n[7/12] Generating UVs...")
bpy.context.view_layer.objects.active = rock_low
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=66, island_margin=0.02)
bpy.ops.object.mode_set(mode='OBJECT')

# === BAKE TEXTURES ===
print("\n[8/12] Baking textures (1024, fast mode)...")

# Setup Cycles
bpy.context.scene.render.engine = 'CYCLES'
bpy.context.scene.cycles.device = 'CPU'
bpy.context.scene.cycles.samples = 8  # Fast mode
bpy.context.scene.cycles.use_denoising = True

bake = bpy.context.scene.render.bake
bake.use_pass_direct = False
bake.use_pass_indirect = False
bake.margin = 8

# Select both
rock_high.select_set(True)
rock_low.select_set(True)
bpy.context.view_layer.objects.active = rock_low

# Bake Albedo
print("  - Baking Albedo...")
node_albedo.select = True
nodes_low.active = node_albedo
bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'})
albedo_path = os.path.join(texture_dir, f"{asset_name}_albedo.png")
img_albedo.save_render(filepath=albedo_path)
print(f"    Saved: {albedo_path}")

# Bake Normal
print("  - Baking Normal...")
node_normal.select = True
nodes_low.active = node_normal
bake.normal_space = 'OBJECT'
bpy.ops.object.bake(type='NORMAL')
normal_path = os.path.join(texture_dir, f"{asset_name}_normal.png")
img_normal.save_render(filepath=normal_path)
print(f"    Saved: {normal_path}")

# Bake Roughness
print("  - Baking Roughness...")
node_rough.select = True
nodes_low.active = node_rough
bpy.ops.object.bake(type='ROUGHNESS')
rough_path = os.path.join(texture_dir, f"{asset_name}_roughness.png")
img_rough.save_render(filepath=rough_path)
print(f"    Saved: {rough_path}")

node_albedo.select = False
node_normal.select = False
node_rough.select = False

# === GENERATE LODs AND COLLIDER ===
print("\n[9/12] Generating LOD1, LOD2, and Collider...")

# LOD1 (50% reduction)
bpy.ops.object.duplicate()
lod1 = bpy.context.object
lod1.name = f"{asset_name}_LOD1"
decimate_lod1 = lod1.modifiers.new(name="decimate", type='DECIMATE')
decimate_lod1.ratio = 0.5
bpy.context.view_layer.objects.active = lod1
bpy.ops.object.modifier_apply(modifier='decimate')

# LOD2 (75% reduction = 25% of original)
bpy.ops.object.duplicate()
lod2 = bpy.context.object
lod2.name = f"{asset_name}_LOD2"
decimate_lod2 = lod2.modifiers.new(name="decimate", type='DECIMATE')
decimate_lod2.ratio = 0.25
bpy.context.view_layer.objects.active = lod2
bpy.ops.object.modifier_apply(modifier='decimate')

# Collider (simple convex)
bpy.ops.object.duplicate()
collider = bpy.context.object
collider.name = f"{asset_name}_COLLIDER"
decimate_col = collider.modifiers.new(name="decimate", type='DECIMATE')
decimate_col.ratio = 0.12
bpy.context.view_layer.objects.active = collider
bpy.ops.object.modifier_apply(modifier='decimate')

# Remove material from collider
collider.data.materials.clear()
collider.hide_render = True

# === DELETE HIGH-RES ===
print("\n[10/12] Cleaning up high-res mesh...")
bpy.context.view_layer.objects.active = rock_low
rock_high.select_set(True)
bpy.ops.object.delete()

# === APPLY TRANSFORMS ===
print("\n[11/12] Applying transforms...")
for obj in [rock_low, lod1, lod2, collider]:
    if obj and obj.name.startswith(asset_name):
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# === VALIDATION REPORT ===
print("\n[12/12] Generating validation report...")

def count_triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons if len(p.vertices) >= 3)

tris_lod0 = count_triangles(rock_low)
tris_lod1 = count_triangles(lod1)
tris_lod2 = count_triangles(lod2)
tris_collider = count_triangles(collider)

print("\n" + "=" * 60)
print("ASSET VALIDATION REPORT")
print("=" * 60)
print(f"Asset: {asset_name}")
print("-" * 60)
print(f"Triangle counts:")
print(f"  LOD0: {tris_lod0:,} tris")
print(f"  LOD1: {tris_lod1:,} tris ({100*tris_lod1/tris_lod0:.0f}% of LOD0)")
print(f"  LOD2: {tris_lod2:,} tris ({100*tris_lod2/tris_lod0:.0f}% of LOD0)")
print(f"  Collider: {tris_collider:,} tris")
print("-" * 60)
print(f"Textures: 3 × {tex_size}×{tex_size} PNG")
print(f"  - Albedo (RGB)")
print(f"  - Normal (RGB)")
print(f"  - Roughness (G)")
print("-" * 60)

# Memory estimate
total_texels = tex_size * tex_size * 3  # 3 textures
mem_mb = (total_texels * 4) / (1024 * 1024)
print(f"Texture memory: ~{mem_mb:.1f} MB")
print("-" * 60)

# Validation
status = "OK"
notes = []

if tris_lod0 > 8000:
    notes.append(f"LOD0 exceeds 8k limit ({tris_lod0:,})")
    status = "WARNING"
elif tris_lod0 < 5000:
    notes.append(f"LOD0 below target ({tris_lod0:,})")

if tris_collider > 500:
    notes.append(f"Collider too complex ({tris_collider:,})")

print(f"Status: {status}")
if notes:
    print("Notes:")
    for note in notes:
        print(f"  - {note}")
else:
    print("All optimization rules followed.")
print("=" * 60)

# === SAVE BLEND ===
print("\nSaving blend file...")
bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
print(f"Saved: {blend_path}")

print("\n" + "=" * 60)
print("GENERATION COMPLETE")
print("=" * 60)
