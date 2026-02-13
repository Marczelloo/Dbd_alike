"""
Blender Python Script: Create DBD-Style Low Poly Medkit with Textures
v3 - Fixed color baking
Usage: blender --background --python scripts/create_medkit_bake.py
"""

import bpy
import bmesh
import math
from pathlib import Path

# =============================================================================
# CONFIGURATION
# =============================================================================

BODY_WIDTH = 0.28
BODY_HEIGHT = 0.18
BODY_DEPTH = 0.10
HANDLE_WIDTH = 0.12
HANDLE_THICKNESS = 0.025
HANDLE_HEIGHT = 0.035
HANDLE_MOUNT_SIZE = 0.025
BEVEL_RADIUS = 0.004

# sRGB colors (will display correctly)
COLOR_BODY = (0.55, 0.12, 0.12, 1.0)      # Dark red
COLOR_HANDLE = (0.45, 0.10, 0.10, 1.0)    # Darker red
COLOR_CROSS = (0.92, 0.92, 0.92, 1.0)     # Off-white

SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent
OUTPUT_DIR = PROJECT_ROOT / "assets" / "meshes" / "items"
TEXTURE_DIR = PROJECT_ROOT / "assets" / "textures" / "items" / "medkit"
GLB_PATH = OUTPUT_DIR / "medkit.glb"
TEXTURE_SIZE = 1024
MAX_TRIANGLES = 900

# =============================================================================
# UTILITIES
# =============================================================================

def clear_scene():
    bpy.ops.object.select_all(action='DESELECT')
    for obj in list(bpy.data.objects):
        if obj.type in ('MESH', 'CURVE'):
            obj.select_set(True)
    bpy.ops.object.delete()
    for mesh in bpy.data.meshes:
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for curve in bpy.data.curves:
        if curve.users == 0:
            bpy.data.curves.remove(curve)
    for img in bpy.data.images:
        if img.users == 0:
            bpy.data.images.remove(img)
    for mat in bpy.data.materials:
        if mat.users == 0:
            bpy.data.materials.remove(mat)

# =============================================================================
# GEOMETRY
# =============================================================================

def create_body():
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    body = bpy.context.active_object
    body.name = "medkit_body"
    body.scale = (BODY_WIDTH / 2, BODY_DEPTH / 2, BODY_HEIGHT / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    bm = bmesh.new()
    bm.from_mesh(body.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=1)
    bm.to_mesh(body.data)
    bm.free()

    bevel = body.modifiers.new(name="Bevel", type='BEVEL')
    bevel.width = BEVEL_RADIUS
    bevel.segments = 2
    bevel.profile = 0.7
    bevel.limit_method = 'ANGLE'
    bevel.angle_limit = math.radians(35)

    return body

def create_handle():
    handle_objects = []
    
    # Create arch-shaped handle with rectangular profile
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    handle_bar = bpy.context.active_object
    handle_bar.name = "handle_bar"
    
    bm = bmesh.new()
    bm.from_mesh(handle_bar.data)
    bm.clear()
    
    segments = 5
    arch_height = HANDLE_HEIGHT
    arch_width = HANDLE_WIDTH * 0.8
    
    verts = []
    for i in range(segments + 1):
        t = i / segments
        x = (t - 0.5) * arch_width
        z = arch_height * (1 - (2 * t - 1) ** 2)
        
        v1 = bm.verts.new((x, -HANDLE_THICKNESS/2, z - HANDLE_THICKNESS/4))
        v2 = bm.verts.new((x, HANDLE_THICKNESS/2, z - HANDLE_THICKNESS/4))
        v3 = bm.verts.new((x, HANDLE_THICKNESS/2, z + HANDLE_THICKNESS/4))
        v4 = bm.verts.new((x, -HANDLE_THICKNESS/2, z + HANDLE_THICKNESS/4))
        verts.append((v1, v2, v3, v4))
    
    bm.verts.ensure_lookup_table()
    
    for i in range(segments):
        v1, v2, v3, v4 = verts[i]
        v5, v6, v7, v8 = verts[i + 1]
        bm.faces.new([v1, v2, v6, v5])
        bm.faces.new([v4, v3, v7, v8])
        bm.faces.new([v1, v5, v8, v4])
        bm.faces.new([v2, v6, v7, v3])
    
    bm.faces.new([verts[0][0], verts[0][3], verts[0][2], verts[0][1]])
    bm.faces.new([verts[-1][0], verts[-1][1], verts[-1][2], verts[-1][3]])
    
    bm.to_mesh(handle_bar.data)
    bm.free()
    
    handle_bar.location = (0, 0, BODY_HEIGHT / 2)
    handle_objects.append(handle_bar)
    
    # Mounting blocks
    for side in [-1, 1]:
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        mount = bpy.context.active_object
        mount.name = f"mount_{'L' if side < 0 else 'R'}"
        mount.scale = (HANDLE_MOUNT_SIZE / 2, HANDLE_MOUNT_SIZE / 2, HANDLE_MOUNT_SIZE / 2)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        mount.location = (side * HANDLE_WIDTH * 0.35, 0, BODY_HEIGHT / 2 + HANDLE_MOUNT_SIZE / 2)
        handle_objects.append(mount)
    
    bpy.ops.object.select_all(action='DESELECT')
    for obj in handle_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = handle_bar
    bpy.ops.object.join()
    
    handle = bpy.context.active_object
    handle.name = "medkit_handle"
    
    bevel = handle.modifiers.new(name="Bevel", type='BEVEL')
    bevel.width = BEVEL_RADIUS * 0.5
    bevel.segments = 1
    bevel.limit_method = 'ANGLE'
    bevel.angle_limit = math.radians(45)
    
    return handle

# =============================================================================
# UV MAPPING
# =============================================================================

def uv_unwrap_object(obj):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.lightmap_pack(PREF_CONTEXT='ALL_FACES', PREF_MARGIN_DIV=0.08)
    bpy.ops.object.mode_set(mode='OBJECT')

# =============================================================================
# MATERIALS - Using simple node setup for reliable baking
# =============================================================================

def create_body_material():
    """Body material with procedural cross - uses simple nodes"""
    mat = bpy.data.materials.new(name="medkit_body_mat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (300, 0)
    principled.inputs['Metallic'].default_value = 0.0
    principled.inputs['Roughness'].default_value = 0.7

    # Object coordinates
    tex_coord = nodes.new('ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    # Separate for cross mask
    separate = nodes.new('ShaderNodeSeparateXYZ')
    separate.location = (-400, 0)

    # Absolute X and Z
    abs_x = nodes.new('ShaderNodeMath')
    abs_x.operation = 'ABSOLUTE'
    abs_x.location = (-200, 100)

    abs_z = nodes.new('ShaderNodeMath')
    abs_z.operation = 'ABSOLUTE'
    abs_z.location = (-200, -100)

    # Cross vertical: |X| < 0.015 AND |Z| < 0.04
    lt_x1 = nodes.new('ShaderNodeMath')
    lt_x1.operation = 'LESS_THAN'
    lt_x1.inputs[1].default_value = 0.015
    lt_x1.location = (0, 100)

    lt_z1 = nodes.new('ShaderNodeMath')
    lt_z1.operation = 'LESS_THAN'
    lt_z1.inputs[1].default_value = 0.04
    lt_z1.location = (0, -50)

    and1 = nodes.new('ShaderNodeMath')
    and1.operation = 'MULTIPLY'
    and1.location = (100, 25)

    # Cross horizontal: |Z| < 0.015 AND |X| < 0.04
    lt_z2 = nodes.new('ShaderNodeMath')
    lt_z2.operation = 'LESS_THAN'
    lt_z2.inputs[1].default_value = 0.015
    lt_z2.location = (0, -200)

    lt_x2 = nodes.new('ShaderNodeMath')
    lt_x2.operation = 'LESS_THAN'
    lt_x2.inputs[1].default_value = 0.04
    lt_x2.location = (0, -350)

    and2 = nodes.new('ShaderNodeMath')
    and2.operation = 'MULTIPLY'
    and2.location = (100, -275)

    # Combine
    combine = nodes.new('ShaderNodeMath')
    combine.operation = 'ADD'
    combine.use_clamp = True
    combine.location = (200, -125)

    # Colors
    red = nodes.new('ShaderNodeRGB')
    red.location = (200, 250)
    red.outputs[0].default_value = COLOR_BODY

    white = nodes.new('ShaderNodeRGB')
    white.location = (200, 150)
    white.outputs[0].default_value = COLOR_CROSS

    # Mix
    mix = nodes.new('ShaderNodeMixRGB')
    mix.location = (350, 200)

    # Connect
    links.new(tex_coord.outputs['Object'], separate.inputs['Vector'])
    links.new(separate.outputs['X'], abs_x.inputs[0])
    links.new(separate.outputs['Z'], abs_z.inputs[0])
    links.new(abs_x.outputs[0], lt_x1.inputs[0])
    links.new(abs_z.outputs[0], lt_z1.inputs[0])
    links.new(lt_x1.outputs[0], and1.inputs[0])
    links.new(lt_z1.outputs[0], and1.inputs[1])
    links.new(abs_z.outputs[0], lt_z2.inputs[0])
    links.new(abs_x.outputs[0], lt_x2.inputs[0])
    links.new(lt_z2.outputs[0], and2.inputs[0])
    links.new(lt_x2.outputs[0], and2.inputs[1])
    links.new(and1.outputs[0], combine.inputs[0])
    links.new(and2.outputs[0], combine.inputs[1])
    links.new(red.outputs[0], mix.inputs['Color1'])
    links.new(white.outputs[0], mix.inputs['Color2'])
    links.new(combine.outputs[0], mix.inputs['Fac'])
    links.new(mix.outputs[0], principled.inputs['Base Color'])
    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    return mat

def create_handle_material():
    mat = bpy.data.materials.new(name="medkit_handle_mat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (300, 0)

    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (0, 0)
    principled.inputs['Base Color'].default_value = COLOR_HANDLE
    principled.inputs['Roughness'].default_value = 0.75
    principled.inputs['Metallic'].default_value = 0.0

    links.new(principled.outputs['BSDF'], output.inputs['Surface'])
    return mat

# =============================================================================
# TEXTURE BAKING - Simplified and more robust
# =============================================================================

def bake_textures_for_object(obj, output_dir, prefix):
    """Bake all textures for an object"""
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Store original material
    original_mat = obj.data.materials[0] if obj.data.materials else None
    
    # Setup for baking
    bpy.context.scene.render.engine = 'CYCLES'
    bpy.context.scene.cycles.samples = 1
    bpy.context.scene.view_settings.view_transform = 'Standard'
    
    # Select object
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    
    # Create images
    img_color = bpy.data.images.new(f"{prefix}_color", TEXTURE_SIZE, TEXTURE_SIZE, alpha=True)
    img_rough = bpy.data.images.new(f"{prefix}_rough", TEXTURE_SIZE, TEXTURE_SIZE)
    img_normal = bpy.data.images.new(f"{prefix}_normal", TEXTURE_SIZE, TEXTURE_SIZE)
    
    # Create temporary bake material
    bake_mat = bpy.data.materials.new(name=f"bake_{prefix}")
    bake_mat.use_nodes = True
    nodes = bake_mat.node_tree.nodes
    nodes.clear()
    
    # Image texture node
    img_node = nodes.new('ShaderNodeTexImage')
    img_node.location = (0, 0)
    
    # Assign bake material
    obj.data.materials[0] = bake_mat
    
    results = {}
    
    # Bake Base Color (Diffuse)
    print(f"  Baking {prefix} base color...")
    img_node.image = img_color
    nodes.active = img_node
    bpy.context.scene.cycles.bake_type = 'DIFFUSE'
    bpy.context.scene.render.bake.use_pass_direct = False
    bpy.context.scene.render.bake.use_pass_indirect = False
    bpy.context.scene.render.bake.use_pass_color = True
    bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'})
    
    path_color = output_dir / f"{prefix}_base_color.png"
    img_color.save_render(str(path_color))
    results['base_color'] = str(path_color)
    
    # Bake Roughness
    print(f"  Baking {prefix} roughness...")
    img_node.image = img_rough
    nodes.active = img_node
    bpy.context.scene.cycles.bake_type = 'ROUGHNESS'
    bpy.ops.object.bake(type='ROUGHNESS')
    
    path_rough = output_dir / f"{prefix}_roughness.png"
    img_rough.save_render(str(path_rough))
    results['roughness'] = str(path_rough)
    
    # Bake Normal
    print(f"  Baking {prefix} normal...")
    img_node.image = img_normal
    nodes.active = img_node
    bpy.context.scene.cycles.bake_type = 'NORMAL'
    bpy.ops.object.bake(type='NORMAL')
    
    path_normal = output_dir / f"{prefix}_normal.png"
    img_normal.save_render(str(path_normal))
    results['normal'] = str(path_normal)
    
    # Cleanup
    bpy.data.images.remove(img_color)
    bpy.data.images.remove(img_rough)
    bpy.data.images.remove(img_normal)
    
    # Restore original material
    if original_mat:
        obj.data.materials[0] = original_mat
    
    bpy.data.materials.remove(bake_mat)
    
    return results

def create_final_material(textures, name):
    """Create material from baked textures"""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (300, 0)
    principled.inputs['Metallic'].default_value = 0.0

    # Base color
    bc_tex = nodes.new('ShaderNodeTexImage')
    bc_tex.image = bpy.data.images.load(textures['base_color'])
    bc_tex.location = (0, 200)

    # Roughness
    rough_tex = nodes.new('ShaderNodeTexImage')
    rough_tex.image = bpy.data.images.load(textures['roughness'])
    rough_tex.location = (0, 0)
    rough_tex.image.colorspace_settings.name = 'Non-Color'

    # Normal
    normal_tex = nodes.new('ShaderNodeTexImage')
    normal_tex.image = bpy.data.images.load(textures['normal'])
    normal_tex.location = (0, -200)
    normal_tex.image.colorspace_settings.name = 'Non-Color'

    normal_map = nodes.new('ShaderNodeNormalMap')
    normal_map.location = (150, -200)
    normal_map.inputs['Strength'].default_value = 0.4

    sep = nodes.new('ShaderNodeSeparateColor')
    sep.location = (150, 0)

    # Connect
    links.new(bc_tex.outputs['Color'], principled.inputs['Base Color'])
    links.new(rough_tex.outputs['Color'], sep.inputs['Color'])
    links.new(sep.outputs['Red'], principled.inputs['Roughness'])
    links.new(normal_tex.outputs['Color'], normal_map.inputs['Color'])
    links.new(normal_map.outputs['Normal'], principled.inputs['Normal'])
    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    return mat

# =============================================================================
# EXPORT
# =============================================================================

def apply_modifiers(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    for mod in obj.modifiers[:]:
        try:
            bpy.ops.object.modifier_apply(modifier=mod.name)
        except:
            pass

def export_glb():
    GLB_PATH.parent.mkdir(parents=True, exist_ok=True)
    
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            apply_modifiers(obj)
    
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)

    bpy.ops.export_scene.gltf(
        filepath=str(GLB_PATH),
        export_format='GLB',
        use_selection=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_materials='EXPORT',
        export_cameras=False,
        export_lights=False,
        export_yup=True,
    )

def count_triangles():
    total = 0
    breakdown = {}
    for obj in list(bpy.data.objects):
        if obj.type != 'MESH':
            continue
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.duplicate()
        copy = bpy.context.active_object
        for mod in copy.modifiers[:]:
            try:
                bpy.ops.object.modifier_apply(modifier=mod.name)
            except:
                pass
        tris = sum(1 if len(p.vertices) == 3 else 2 if len(p.vertices) == 4 else len(p.vertices) - 2 for p in copy.data.polygons)
        breakdown[obj.name] = tris
        total += tris
        bpy.data.objects.remove(copy)
    return total, breakdown

# =============================================================================
# MAIN
# =============================================================================

def main():
    print("\n" + "="*60)
    print("DBD-STYLE MEDKIT GENERATOR v3")
    print("="*60)

    print("\n[1/6] Clearing scene...")
    clear_scene()

    print("[2/6] Creating geometry...")
    body = create_body()
    handle = create_handle()

    print("[3/6] UV unwrapping...")
    uv_unwrap_object(body)
    uv_unwrap_object(handle)

    print("[4/6] Creating materials...")
    body_mat = create_body_material()
    handle_mat = create_handle_material()
    body.data.materials.append(body_mat)
    handle.data.materials.append(handle_mat)

    print("[5/6] Baking textures...")
    body_tex = bake_textures_for_object(body, TEXTURE_DIR, "body")
    handle_tex = bake_textures_for_object(handle, TEXTURE_DIR, "handle")

    print("[6/6] Creating final materials...")
    body_final = create_final_material(body_tex, "body_final")
    handle_final = create_final_material(handle_tex, "handle_final")
    body.data.materials[0] = body_final
    handle.data.materials[0] = handle_final

    total, breakdown = count_triangles()
    print("\n" + "-"*50)
    print("TRIANGLE COUNT")
    print("-"*50)
    for name, count in breakdown.items():
        print(f"  {name}: {count}")
    print(f"  TOTAL: {total} / {MAX_TRIANGLES}")
    print(f"  STATUS: {'OK' if total <= MAX_TRIANGLES else 'OVER!'}")
    print("-"*50)

    print(f"\nExporting to: {GLB_PATH}")
    export_glb()

    size = GLB_PATH.stat().st_size if GLB_PATH.exists() else 0
    print("\n" + "="*60)
    print("COMPLETE")
    print(f"Model: {GLB_PATH} ({size/1024:.1f} KB)")
    print(f"Textures: {TEXTURE_DIR}")
    print(f"Triangles: {total}")
    print("="*60 + "\n")

if __name__ == "__main__":
    main()
