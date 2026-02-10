import bpy, sys, os, math, bmesh
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--")+1:] if "--" in argv else []

# args: <out_blend> [texture_size]
out_blend = argv[0] if len(argv) > 0 else "out/assets/mossy_rock.blend"
texture_size = int(argv[1]) if len(argv) > 1 else 1024

# Extract asset name for texture folder
asset_name = os.path.splitext(os.path.basename(out_blend))[0]
texture_dir = os.path.join(os.path.dirname(out_blend), asset_name)
os.makedirs(texture_dir, exist_ok=True)
os.makedirs("out/previews", exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

# Scene setup - basic lighting and camera
bpy.ops.object.light_add(type='AREA', location=(3, -3, 5))
light = bpy.context.object
light.data.energy = 1000
light.data.shadow_soft_size = 0.5

bpy.ops.object.light_add(type='SUN', location=(-2, 2, 3))
fill_light = bpy.context.object
fill_light.data.energy = 5
fill_light.data.angle = 0.5

bpy.ops.object.camera_add(location=(2.5, -2.5, 1.5), rotation=(math.radians(55), 0, math.radians(35)))
bpy.context.scene.camera = bpy.context.object

# =============================================================================
# MESH GENERATION - Procedural Rock
# =============================================================================

def create_mossy_rock():
    # Start with icosphere for organic base
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=5, radius=0.6, location=(0,0,0.5))
    obj = bpy.context.object
    obj.name = "ASSET"

    # Use displacement modifier with vertex group for controlled deformation
    # Create a vertex group for areas we want more detail
    for vert in obj.data.vertices:
        weight = 1.0
        obj.vertex_groups.new(name="displace_weight")
        break

    # Add multiple displacements using the modifier
    # First pass - large deformation using noise texture
    tex1 = bpy.data.textures.new("LargeNoise", type='CLOUDS')
    tex1.noise_scale = 2.0

    mod1 = obj.modifiers.new(name="LargeDisplace", type='DISPLACE')
    mod1.texture = tex1
    mod1.strength = 0.2
    mod1.mid_level = 0.5

    # Second pass - medium detail
    tex2 = bpy.data.textures.new("MediumNoise", type='CLOUDS')
    tex2.noise_scale = 4.0

    mod2 = obj.modifiers.new(name="MediumDisplace", type='DISPLACE')
    mod2.texture = tex2
    mod2.strength = 0.1
    mod2.mid_level = 0.5

    # Third pass - fine detail with Voronoi
    tex3 = bpy.data.textures.new("FineNoise", type='VORONOI')
    tex3.distance_metric = 'MINKOVSKY'

    mod3 = obj.modifiers.new(name="FineDisplace", type='DISPLACE')
    mod3.texture = tex3
    mod3.strength = 0.04
    mod3.mid_level = 0.5

    # Apply all modifiers
    bpy.ops.object.modifier_apply(modifier="FineDisplace")
    bpy.ops.object.modifier_apply(modifier="MediumDisplace")
    bpy.ops.object.modifier_apply(modifier="LargeDisplace")

    # Slightly flatten bottom for stability
    # Select bottom vertices and scale them down
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_mode(type='VERT')
    bpy.ops.mesh.select_all(action='DESELECT')

    # Select vertices with Z < 0.3 (bottom part)
    bm = bmesh.from_edit_mesh(obj.data)
    for vert in bm.verts:
        if vert.co.z < 0.35:
            vert.select = True

    # Scale down the selected bottom vertices
    bpy.ops.transform.resize(value=(0.7, 0.7, 0.5))
    bpy.ops.object.mode_set(mode='OBJECT')

    # Smooth shading - in Blender 4+ auto smooth is always on with shade_smooth
    bpy.ops.object.shade_smooth()
    # Add edge split modifier for sharp edges on low poly areas
    edge_mod = obj.modifiers.new(name="EdgeSplit", type='EDGE_SPLIT')
    edge_mod.split_angle = math.radians(30)

    return obj

# =============================================================================
# MATERIAL CREATION - Rock with Moss
# =============================================================================

def create_mossy_rock_material(obj):
    mat = bpy.data.materials.new(name="MossyRockMat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links

    # Clear default nodes
    nodes.clear()

    # Create all nodes
    output = nodes.new(type='ShaderNodeOutputMaterial')
    output.location = (400, 0)

    principled = nodes.new(type='ShaderNodeBsdfPrincipled')
    principled.location = (200, 0)

    # === ROCK MATERIAL ===
    # Color mix for rock (gray/brown tones)
    rock_color_1 = nodes.new(type='ShaderNodeRGB')
    rock_color_1.location = (-400, 200)
    rock_color_1.outputs[0].default_value = (0.35, 0.32, 0.28, 1)  # Brownish gray

    rock_color_2 = nodes.new(type='ShaderNodeRGB')
    rock_color_2.location = (-400, 100)
    rock_color_2.outputs[0].default_value = (0.25, 0.24, 0.22, 1)  # Darker gray

    # Noise for rock color variation
    rock_noise = nodes.new(type='ShaderNodeTexNoise')
    rock_noise.location = (-600, 150)
    rock_noise.inputs['Scale'].default_value = 10.0
    rock_noise.inputs['Detail'].default_value = 4.0

    rock_color_mix = nodes.new(type='ShaderNodeMixRGB')
    rock_color_mix.location = (-200, 200)
    rock_color_mix.blend_type = 'MIX'

    # === MOSS MATERIAL ===
    moss_color = nodes.new(type='ShaderNodeRGB')
    moss_color.location = (-400, -150)
    moss_color.outputs[0].default_value = (0.18, 0.35, 0.12, 1)  # Green

    moss_dark = nodes.new(type='ShaderNodeRGB')
    moss_dark.location = (-400, -250)
    moss_dark.outputs[0].default_value = (0.10, 0.22, 0.08, 1)  # Darker green

    # Noise for moss variation
    moss_noise = nodes.new(type='ShaderNodeTexNoise')
    moss_noise.location = (-600, -200)
    moss_noise.inputs['Scale'].default_value = 25.0
    moss_noise.inputs['Detail'].default_value = 6.0
    moss_noise.inputs['Roughness'].default_value = 0.7

    moss_color_mix = nodes.new(type='ShaderNodeMixRGB')
    moss_color_mix.location = (-200, -200)
    moss_color_mix.blend_type = 'MIX'

    # === MOSS MASK (based on normal/up direction + noise) ===
    # Get normal for upward-facing detection
    geometry = nodes.new(type='ShaderNodeNewGeometry')
    geometry.location = (-800, -50)

    # Separate normal to get Z component
    separate_normal = nodes.new(type='ShaderNodeSeparateXYZ')
    separate_normal.location = (-600, -50)

    # Noise-based moss pattern (irregular patches)
    moss_pattern_noise = nodes.new(type='ShaderNodeTexNoise')
    moss_pattern_noise.location = (-900, -150)
    moss_pattern_noise.inputs['Scale'].default_value = 5.0
    moss_pattern_noise.inputs['Detail'].default_value = 3.0
    moss_pattern_noise.inputs['Roughness'].default_value = 0.5

    # Another noise for finer detail
    moss_detail_noise = nodes.new(type='ShaderNodeTexNoise')
    moss_detail_noise.location = (-900, -250)
    moss_detail_noise.inputs['Scale'].default_value = 15.0
    moss_detail_noise.inputs['Detail'].default_value = 2.0

    # Combine moss pattern noises
    moss_noise_combine = nodes.new(type='ShaderNodeMath')
    moss_noise_combine.location = (-700, -200)
    moss_noise_combine.operation = 'MULTIPLY'

    # Mix the two noise patterns
    moss_pattern_mix = nodes.new(type='ShaderNodeMixRGB')
    moss_pattern_mix.location = (-500, -200)
    moss_pattern_mix.blend_type = 'MULTIPLY'
    moss_pattern_mix.inputs['Fac'].default_value = 0.5

    # Calculate moss mask (upward faces + noise pattern)
    moss_mask_up = nodes.new(type='ShaderNodeMath')
    moss_mask_up.location = (-400, -50)
    moss_mask_up.operation = 'MAXIMUM'
    moss_mask_up.inputs[0].default_value = 0.0  # Will be connected to normal Z
    moss_mask_up.inputs[1].default_value = 0.3  # Minimum threshold

    # Final moss mask = upward factor * noise pattern
    moss_mask_math = nodes.new(type='ShaderNodeMath')
    moss_mask_math.location = (-200, -100)
    moss_mask_math.operation = 'MULTIPLY'

    # Sharpen moss edge
    moss_mask_sharpen = nodes.new(type='ShaderNodeMath')
    moss_mask_sharpen.location = (0, -100)
    moss_mask_sharpen.operation = 'POWER'
    moss_mask_sharpen.inputs[1].default_value = 2.0

    # === MIX ROCK AND MOSS ===
    rock_moss_mix = nodes.new(type='ShaderNodeMixRGB')
    rock_moss_mix.location = (0, 50)
    rock_moss_mix.blend_type = 'MIX'

    # Roughness variation
    rock_roughness = nodes.new(type='ShaderNodeValue')
    rock_roughness.location = (-200, 350)
    rock_roughness.outputs[0].default_value = 0.85

    moss_roughness = nodes.new(type='ShaderNodeValue')
    moss_roughness.location = (-200, -350)
    moss_roughness.outputs[0].default_value = 0.95

    roughness_mix = nodes.new(type='ShaderNodeMix')
    roughness_mix.location = (0, 200)
    roughness_mix.data_type = 'FLOAT'
    # Mix node in Blender 4+ uses 'A' and 'B' inputs

    # === CONNECT NODES ===

    # Rock color variation
    links.new(rock_noise.outputs['Fac'], rock_color_mix.inputs['Fac'])
    links.new(rock_color_1.outputs[0], rock_color_mix.inputs['Color1'])
    links.new(rock_color_2.outputs[0], rock_color_mix.inputs['Color2'])

    # Moss color variation
    links.new(moss_noise.outputs['Fac'], moss_color_mix.inputs['Fac'])
    links.new(moss_color.outputs[0], moss_color_mix.inputs['Color1'])
    links.new(moss_dark.outputs[0], moss_color_mix.inputs['Color2'])

    # Moss mask - normal upward detection
    links.new(geometry.outputs['Normal'], separate_normal.inputs['Vector'])

    # Moss pattern combination
    links.new(moss_pattern_noise.outputs['Fac'], moss_pattern_mix.inputs['Color1'])
    links.new(moss_detail_noise.outputs['Fac'], moss_pattern_mix.inputs['Color2'])

    # Moss mask = upward factor * noise pattern
    links.new(separate_normal.outputs['Z'], moss_mask_up.inputs[0])
    links.new(moss_mask_up.outputs['Value'], moss_mask_math.inputs[0])
    links.new(moss_pattern_mix.outputs['Color'], moss_mask_math.inputs[1])

    # Sharpen mask edge
    links.new(moss_mask_math.outputs['Value'], moss_mask_sharpen.inputs[0])

    # Mix rock and moss colors
    links.new(moss_mask_sharpen.outputs['Value'], rock_moss_mix.inputs['Fac'])
    links.new(rock_color_mix.outputs['Color'], rock_moss_mix.inputs['Color1'])
    links.new(moss_color_mix.outputs['Color'], rock_moss_mix.inputs['Color2'])

    # Mix roughness
    links.new(moss_mask_sharpen.outputs['Value'], roughness_mix.inputs['Factor'])
    links.new(rock_roughness.outputs[0], roughness_mix.inputs['A'])
    links.new(moss_roughness.outputs[0], roughness_mix.inputs['B'])

    # Final outputs
    links.new(rock_moss_mix.outputs['Color'], principled.inputs['Base Color'])
    links.new(roughness_mix.outputs['Result'], principled.inputs['Roughness'])
    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    # Assign material
    obj.data.materials.append(mat)

    return mat

# =============================================================================
# UV UNWRAP AND BAKE
# =============================================================================

def uv_unwrap_and_bake(obj, mat, texture_dir, texture_size):
    # Switch to Cycles for baking
    original_engine = bpy.context.scene.render.engine
    bpy.context.scene.render.engine = 'CYCLES'

    # Set bake settings for Blender 5.0+
    bpy.context.scene.cycles.device = 'CPU'  # More compatible
    bpy.context.scene.render.bake.use_pass_direct = False
    bpy.context.scene.render.bake.use_pass_indirect = False
    bpy.context.scene.render.bake.use_pass_color = True

    # Enter edit mode and UV unwrap
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=1.3, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

    # Create image nodes for baked textures
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links

    # Create bake images
    albedo_image = bpy.data.images.new(
        name=f"{obj.name}_albedo",
        width=texture_size,
        height=texture_size,
        alpha=True
    )
    normal_image = bpy.data.images.new(
        name=f"{obj.name}_normal",
        width=texture_size,
        height=texture_size
    )
    roughness_image = bpy.data.images.new(
        name=f"{obj.name}_roughness",
        width=texture_size,
        height=texture_size
    )

    # Create image texture nodes
    albedo_node = nodes.new(type='ShaderNodeTexImage')
    albedo_node.location = (600, 100)
    albedo_node.image = albedo_image
    albedo_node.label = "Baked Albedo"

    normal_node = nodes.new(type='ShaderNodeTexImage')
    normal_node.location = (600, -50)
    normal_node.image = normal_image
    normal_node.label = "Baked Normal"

    roughness_node = nodes.new(type='ShaderNodeTexImage')
    roughness_node.location = (600, -200)
    roughness_node.image = roughness_image
    roughness_node.label = "Baked Roughness"

    # Find the principled BSDF and output nodes
    principled = None
    output = None
    for node in nodes:
        if node.type == 'BSDF_PRINCIPLED':
            principled = node
        elif node.type == 'OUTPUT_MATERIAL':
            output = node

    if principled and output:
        # Store original connections
        orig_color = principled.inputs['Base Color'].links[0].from_socket if principled.inputs['Base Color'].links else None
        orig_rough = principled.inputs['Roughness'].links[0].from_socket if principled.inputs['Roughness'].links else None

        # === BAKE ALBEDO ===
        albedo_node.select = True
        nodes.active = albedo_node
        bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'})
        albedo_node.select = False

        # === BAKE NORMAL ===
        normal_node.select = True
        nodes.active = normal_node
        bpy.ops.object.bake(type='NORMAL')
        normal_node.select = False

        # === BAKE ROUGHNESS ===
        roughness_node.select = True
        nodes.active = roughness_node
        bpy.ops.object.bake(type='ROUGHNESS')
        roughness_node.select = False

        # Add Normal Map node for Eevee compatibility
        normal_map_node = nodes.new(type='ShaderNodeNormalMap')
        normal_map_node.location = (800, -50)

        # Reconnect with baked textures
        links.new(albedo_node.outputs['Color'], principled.inputs['Base Color'])
        links.new(normal_node.outputs['Color'], normal_map_node.inputs['Color'])
        links.new(normal_map_node.outputs['Normal'], principled.inputs['Normal'])
        links.new(roughness_node.outputs['Color'], principled.inputs['Roughness'])

    # Save baked textures - use pack and save method
    albedo_path = os.path.abspath(os.path.join(texture_dir, f"{asset_name}_albedo.png"))
    normal_path = os.path.abspath(os.path.join(texture_dir, f"{asset_name}_normal.png"))
    roughness_path = os.path.abspath(os.path.join(texture_dir, f"{asset_name}_roughness.png"))

    # Pack images first to ensure they have data
    albedo_image.pack()
    normal_image.pack()
    roughness_image.pack()

    # Save packed images to external files
    albedo_image.filepath_raw = albedo_path
    albedo_image.save()

    normal_image.filepath_raw = normal_path
    normal_image.save()

    roughness_image.filepath_raw = roughness_path
    roughness_image.save()

    # Unpack and reload from files to ensure they're properly linked
    for img in [albedo_image, normal_image, roughness_image]:
        img.unpack(method='USE_ORIGINAL')
        img.source = 'FILE'

    albedo_image.filepath = albedo_path
    normal_image.filepath = normal_path
    roughness_image.filepath = roughness_path

    # Restore original render engine
    bpy.context.scene.render.engine = original_engine

    print(f"OK: baked albedo to {albedo_path}")
    print(f"OK: baked normal to {normal_path}")
    print(f"OK: baked roughness to {roughness_path}")

# =============================================================================
# MAIN EXECUTION
# =============================================================================

# Create the rock
print("Creating mossy rock mesh...")
rock_obj = create_mossy_rock()
print("OK: mesh created")

# Create material
print("Creating mossy rock material...")
mat = create_mossy_rock_material(rock_obj)
print("OK: material created")

# UV unwrap and bake
print("UV unwrapping and baking textures...")
uv_unwrap_and_bake(rock_obj, mat, texture_dir, texture_size)
print("OK: textures baked")

# Save .blend
# Pack all images into the blend file for portability
for image in bpy.data.images:
    if image.source != 'GENERATED':
        try:
            image.pack()
        except:
            pass

bpy.ops.wm.save_as_mainfile(filepath=out_blend, compress=True)
print(f"OK: saved blend: {out_blend}")

# Also save the absolute path for next script
print(f"BLEND_PATH: {os.path.abspath(out_blend)}")
