"""
Blender Python Script: Create DBD-Style Low Poly Medkit
Usage: blender --background --python scripts/create_medkit.py

Target: Blender 5.0+
"""

import bpy
import bmesh
import math
import os
from pathlib import Path

# =============================================================================
# CONFIGURATION
# =============================================================================

# Dimensions (in meters)
BODY_WIDTH = 0.28   # 28 cm
BODY_HEIGHT = 0.18  # 18 cm
BODY_DEPTH = 0.10   # 10 cm

HANDLE_WIDTH = 0.12
HANDLE_THICKNESS = 0.025
HANDLE_HEIGHT_ABOVE = 0.04

BEVEL_RADIUS = 0.006

# Colors (sRGB)
COLOR_BODY = (0.545, 0.118, 0.118, 1.0)      # #8B1E1E
COLOR_CROSS = (0.902, 0.902, 0.902, 1.0)     # #E6E6E6

# Paths
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent
OUTPUT_DIR = PROJECT_ROOT / "assets" / "meshes" / "items"
TEXTURE_DIR = PROJECT_ROOT / "assets" / "textures" / "items" / "medkit"
GLB_PATH = OUTPUT_DIR / "medkit.glb"

MAX_TRIANGLES = 900

# =============================================================================
# UTILITIES
# =============================================================================

def clear_scene():
    """Clear all mesh objects"""
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

def srgb_to_linear(c):
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4

def srgb_to_linear_rgba(rgba):
    return (srgb_to_linear(rgba[0]), srgb_to_linear(rgba[1]), srgb_to_linear(rgba[2]), rgba[3])

# =============================================================================
# GEOMETRY
# =============================================================================

def create_body():
    """Create main medkit body"""
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    body = bpy.context.active_object
    body.name = "medkit_body"

    # Scale to dimensions
    body.scale = (BODY_WIDTH / 2, BODY_DEPTH / 2, BODY_HEIGHT / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    # Use bmesh to add edge loop for lid seam
    bm = bmesh.new()
    bm.from_mesh(body.data)

    # Subdivide for more geometry
    bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=1)

    bm.to_mesh(body.data)
    bm.free()

    # Add bevel modifier
    bevel = body.modifiers.new(name="Bevel", type='BEVEL')
    bevel.width = BEVEL_RADIUS
    bevel.segments = 2
    bevel.profile = 0.6
    bevel.limit_method = 'ANGLE'
    bevel.angle_limit = math.radians(30)

    return body

def create_handle():
    """Create curved handle"""
    # Create curve
    bpy.ops.curve.primitive_bezier_circle_add(radius=HANDLE_THICKNESS / 2)
    profile = bpy.context.active_object
    profile.name = "handle_profile"
    profile.data.resolution_u = 6

    # Create path
    bpy.ops.curve.primitive_bezier_curve_add()
    path = bpy.context.active_object
    path.name = "handle_path"

    # Edit path to be a simple arc
    bpy.context.view_layer.objects.active = path
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.curve.select_all(action='SELECT')

    spline = path.data.splines[0]

    # Left side
    p0 = spline.bezier_points[0]
    p0.co = (-HANDLE_WIDTH / 2, 0, 0)
    p0.handle_left = (-HANDLE_WIDTH / 2 - 0.02, 0, -0.02)
    p0.handle_right = (-HANDLE_WIDTH / 2 + 0.02, 0, 0.02)
    p0.handle_left_type = 'ALIGNED'
    p0.handle_right_type = 'ALIGNED'

    # Right side
    p1 = spline.bezier_points[1]
    p1.co = (HANDLE_WIDTH / 2, 0, 0)
    p1.handle_left = (HANDLE_WIDTH / 2 - 0.02, 0, 0.02)
    p1.handle_right = (HANDLE_WIDTH / 2 + 0.02, 0, -0.02)
    p1.handle_left_type = 'ALIGNED'
    p1.handle_right_type = 'ALIGNED'

    # Pull middle up to create arch
    mid_x = 0
    mid_z = HANDLE_HEIGHT_ABOVE + HANDLE_THICKNESS / 2

    # Move both control points' handles to arch up
    p0.handle_right = (-0.02, 0, mid_z)
    p1.handle_left = (0.02, 0, mid_z)

    bpy.ops.object.mode_set(mode='OBJECT')

    # Apply bevel
    path.data.bevel_mode = 'OBJECT'
    path.data.bevel_object = profile
    path.data.resolution_u = 8

    # Convert to mesh
    bpy.ops.object.select_all(action='DESELECT')
    path.select_set(True)
    bpy.context.view_layer.objects.active = path
    bpy.ops.object.convert(target='MESH')

    handle = bpy.context.active_object
    handle.name = "medkit_handle"

    # Position above body
    handle.location = (0, 0, BODY_HEIGHT / 2)

    # Clean up profile
    bpy.data.objects.remove(profile)

    return handle

def create_cross():
    """Create cross symbol as 3D geometry"""
    cross_objects = []

    # Cross arm thickness
    arm_thickness = 0.025
    arm_length = 0.08

    # Vertical bar
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    v_bar = bpy.context.active_object
    v_bar.name = "cross_v"
    v_bar.scale = (arm_thickness, 0.003, arm_length)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    cross_objects.append(v_bar)

    # Horizontal bar
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    h_bar = bpy.context.active_object
    h_bar.name = "cross_h"
    h_bar.scale = (arm_length, 0.003, arm_thickness)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    cross_objects.append(h_bar)

    # Select and join
    bpy.ops.object.select_all(action='DESELECT')
    for obj in cross_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = v_bar
    bpy.ops.object.join()

    cross = bpy.context.active_object
    cross.name = "medkit_cross"

    # Position on front face
    cross.location = (0, BODY_DEPTH / 2 + 0.001, 0)

    return cross

# =============================================================================
# UV MAPPING
# =============================================================================

def uv_unwrap_object(obj):
    """UV unwrap using lightmap pack for clean results"""
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')

    # Use lightmap pack for clean, non-overlapping UVs
    bpy.ops.uv.lightmap_pack(
        PREF_CONTEXT='ALL_FACES',
        PREF_MARGIN_DIV=0.1
    )

    bpy.ops.object.mode_set(mode='OBJECT')

# =============================================================================
# MATERIALS
# =============================================================================

def create_body_material():
    """Create body material with cross via UV-based masking"""
    mat = bpy.data.materials.new(name="medkit_body_mat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    # Output
    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (600, 0)

    # Principled
    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (300, 0)
    principled.inputs['Metallic'].default_value = 0.0
    principled.inputs['Roughness'].default_value = 0.7

    # UV input
    uv = nodes.new('ShaderNodeTexCoord')
    uv.location = (-600, 0)

    # Separate UV
    sep_uv = nodes.new('ShaderNodeSeparateXYZ')
    sep_uv.location = (-400, 0)

    # For cross: we use object coordinates centered on front face
    # X: -0.04 to 0.04, Z: -0.09 to 0.09
    # Cross vertical: |X| < 0.0125 AND |Z| < 0.04
    # Cross horizontal: |Z| < 0.0125 AND |X| < 0.04

    # Object coordinates
    obj_coord = nodes.new('ShaderNodeTexCoord')
    obj_coord.location = (-600, -200)

    sep_obj = nodes.new('ShaderNodeSeparateXYZ')
    sep_obj.location = (-400, -200)

    # Math for cross mask - vertical bar
    abs_x = nodes.new('ShaderNodeMath')
    abs_x.operation = 'ABSOLUTE'
    abs_x.location = (-200, -100)

    less_x = nodes.new('ShaderNodeMath')
    less_x.operation = 'LESS_THAN'
    less_x.inputs[1].default_value = 0.015
    less_x.location = (0, -100)

    abs_z = nodes.new('ShaderNodeMath')
    abs_z.operation = 'ABSOLUTE'
    abs_z.location = (-200, -300)

    less_z = nodes.new('ShaderNodeMath')
    less_z.operation = 'LESS_THAN'
    less_z.inputs[1].default_value = 0.035
    less_z.location = (0, -300)

    and_v = nodes.new('ShaderNodeMath')
    and_v.operation = 'MULTIPLY'
    and_v.location = (100, -200)

    # Horizontal bar
    less_z2 = nodes.new('ShaderNodeMath')
    less_z2.operation = 'LESS_THAN'
    less_z2.inputs[1].default_value = 0.015
    less_z2.location = (0, -500)

    less_x2 = nodes.new('ShaderNodeMath')
    less_x2.operation = 'LESS_THAN'
    less_x2.inputs[1].default_value = 0.035
    less_x2.location = (0, -650)

    and_h = nodes.new('ShaderNodeMath')
    and_h.operation = 'MULTIPLY'
    and_h.location = (100, -550)

    # Combine
    combine = nodes.new('ShaderNodeMath')
    combine.operation = 'ADD'
    combine.location = (200, -350)
    combine.use_clamp = True

    # Colors
    red = nodes.new('ShaderNodeRGB')
    red.location = (0, 200)
    red.outputs[0].default_value = srgb_to_linear_rgba(COLOR_BODY)

    white = nodes.new('ShaderNodeRGB')
    white.location = (0, 100)
    white.outputs[0].default_value = srgb_to_linear_rgba(COLOR_CROSS)

    # Mix colors
    mix = nodes.new('ShaderNodeMixRGB')
    mix.location = (150, 150)

    # Noise for wear
    noise = nodes.new('ShaderNodeTexNoise')
    noise.location = (-200, -800)
    noise.inputs['Scale'].default_value = 20.0
    noise.inputs['Detail'].default_value = 2.0

    color_ramp = nodes.new('ShaderNodeValToRGB')
    color_ramp.location = (0, -800)
    color_ramp.color_ramp.elements[0].position = 0.4
    color_ramp.color_ramp.elements[1].position = 0.6

    # Connect
    links.new(obj_coord.outputs['Object'], sep_obj.inputs['Vector'])
    links.new(sep_obj.outputs['X'], abs_x.inputs[0])
    links.new(abs_x.outputs[0], less_x.inputs[0])
    links.new(sep_obj.outputs['Z'], abs_z.inputs[0])
    links.new(abs_z.outputs[0], less_z.inputs[0])
    links.new(less_x.outputs[0], and_v.inputs[0])
    links.new(less_z.outputs[0], and_v.inputs[1])

    links.new(abs_z.outputs[0], less_z2.inputs[0])
    links.new(abs_x.outputs[0], less_x2.inputs[0])
    links.new(less_z2.outputs[0], and_h.inputs[0])
    links.new(less_x2.outputs[0], and_h.inputs[1])

    links.new(and_v.outputs[0], combine.inputs[0])
    links.new(and_h.outputs[0], combine.inputs[1])

    links.new(red.outputs[0], mix.inputs['Color1'])
    links.new(white.outputs[0], mix.inputs['Color2'])
    links.new(combine.outputs[0], mix.inputs['Fac'])

    links.new(mix.outputs[0], principled.inputs['Base Color'])
    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    # Add subtle noise to roughness
    links.new(noise.outputs['Fac'], color_ramp.inputs['Fac'])

    return mat

def create_handle_material():
    """Create handle material (slightly darker red)"""
    mat = bpy.data.materials.new(name="medkit_handle_mat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (300, 0)

    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (0, 0)

    # Slightly darker red
    color = srgb_to_linear_rgba((0.45, 0.10, 0.10, 1.0))
    principled.inputs['Base Color'].default_value = color
    principled.inputs['Roughness'].default_value = 0.75
    principled.inputs['Metallic'].default_value = 0.0

    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    return mat

def create_cross_material():
    """Create cross material (off-white)"""
    mat = bpy.data.materials.new(name="medkit_cross_mat")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (300, 0)

    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (0, 0)
    principled.inputs['Base Color'].default_value = srgb_to_linear_rgba(COLOR_CROSS)
    principled.inputs['Roughness'].default_value = 0.6
    principled.inputs['Metallic'].default_value = 0.0

    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    return mat

# =============================================================================
# EXPORT
# =============================================================================

def export_glb(output_path):
    """Export all mesh objects to GLB"""
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Select all mesh objects
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)

    bpy.ops.export_scene.gltf(
        filepath=str(output_path),
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
    """Count total triangles in all mesh objects"""
    total = 0
    breakdown = {}

    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue

        # Duplicate to apply modifiers
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.duplicate()

        copy = bpy.context.active_object

        # Apply modifiers
        for mod in copy.modifiers[:]:
            bpy.ops.object.modifier_apply(modifier=mod.name)

        # Count faces
        mesh = copy.data
        tris = 0
        for poly in mesh.polygons:
            verts = len(poly.vertices)
            if verts == 3:
                tris += 1
            elif verts == 4:
                tris += 2
            else:
                tris += verts - 2

        breakdown[obj.name] = tris
        total += tris

        # Remove copy
        bpy.data.objects.remove(copy)

    return total, breakdown

# =============================================================================
# MAIN
# =============================================================================

def main():
    print("\n" + "="*60)
    print("DBD-STYLE MEDKIT GENERATOR")
    print("="*60)

    # Clear
    print("\n[1/5] Clearing scene...")
    clear_scene()

    # Create geometry
    print("[2/5] Creating geometry...")
    body = create_body()
    handle = create_handle()
    cross = create_cross()

    # UV unwrap
    print("[3/5] UV unwrapping...")
    for obj in [body, handle, cross]:
        uv_unwrap_object(obj)

    # Materials
    print("[4/5] Creating materials...")
    body_mat = create_body_material()
    handle_mat = create_handle_material()
    cross_mat = create_cross_material()

    body.data.materials.append(body_mat)
    handle.data.materials.append(handle_mat)
    cross.data.materials.append(cross_mat)

    # Count triangles
    print("[5/5] Verifying triangle count...")
    total, breakdown = count_triangles()

    print("\n" + "-"*50)
    print("TRIANGLE COUNT")
    print("-"*50)
    for name, count in breakdown.items():
        print(f"  {name}: {count}")
    print(f"  {'-'*30}")
    print(f"  TOTAL: {total} / {MAX_TRIANGLES}")
    print(f"  STATUS: {'OK' if total <= MAX_TRIANGLES else 'OVER BUDGET!'}")
    print("-"*50)

    # Export
    print(f"\nExporting to: {GLB_PATH}")
    export_glb(GLB_PATH)

    print("\n" + "="*60)
    print("COMPLETE")
    print("="*60)
    print(f"Output: {GLB_PATH}")
    print(f"Triangles: {total}")
    print("="*60 + "\n")

    return total <= MAX_TRIANGLES

if __name__ == "__main__":
    main()
