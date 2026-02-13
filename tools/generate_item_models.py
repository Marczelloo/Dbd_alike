#!/usr/bin/env python3
"""
Generate Dead by Daylight-style low poly item models.
Creates GLB files for: flashlight, medkit, toolbox, map
Matches DBD dark, gritty horror aesthetic.
"""

import struct
import numpy as np
from pathlib import Path

try:
    from pygltflib import GLTF2, Buffer, BufferView, Accessor, Mesh, Primitive, Node, Scene
    from pygltflib import ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, UNSIGNED_INT
    from pygltflib import TRIANGLES
except ImportError:
    print("Error: pygltflib required. Run: python -m pip install pygltflib")
    exit(1)


def create_box(w, h, d, color, origin=(0, 0, 0)):
    """Create a box mesh with vertex colors."""
    ox, oy, oz = origin
    hw, hh, hd = w / 2, h / 2, d / 2
    
    positions = np.array([
        [ox - hw, oy - hh, oz + hd], [ox + hw, oy - hh, oz + hd], [ox + hw, oy + hh, oz + hd], [ox - hw, oy + hh, oz + hd],
        [ox + hw, oy - hh, oz - hd], [ox - hw, oy - hh, oz - hd], [ox - hw, oy + hh, oz - hd], [ox + hw, oy + hh, oz - hd],
        [ox - hw, oy + hh, oz + hd], [ox + hw, oy + hh, oz + hd], [ox + hw, oy + hh, oz - hd], [ox - hw, oy + hh, oz - hd],
        [ox - hw, oy - hh, oz - hd], [ox + hw, oy - hh, oz - hd], [ox + hw, oy - hh, oz + hd], [ox - hw, oy - hh, oz + hd],
        [ox + hw, oy - hh, oz + hd], [ox + hw, oy - hh, oz - hd], [ox + hw, oy + hh, oz - hd], [ox + hw, oy + hh, oz + hd],
        [ox - hw, oy - hh, oz - hd], [ox - hw, oy - hh, oz + hd], [ox - hw, oy + hh, oz + hd], [ox - hw, oy + hh, oz - hd],
    ], dtype=np.float32)
    
    normals = np.array([
        [0, 0, 1], [0, 0, 1], [0, 0, 1], [0, 0, 1],
        [0, 0, -1], [0, 0, -1], [0, 0, -1], [0, 0, -1],
        [0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0],
        [0, -1, 0], [0, -1, 0], [0, -1, 0], [0, -1, 0],
        [1, 0, 0], [1, 0, 0], [1, 0, 0], [1, 0, 0],
        [-1, 0, 0], [-1, 0, 0], [-1, 0, 0], [-1, 0, 0],
    ], dtype=np.float32)
    
    colors = np.array([color] * 24, dtype=np.float32)
    
    indices = np.array([
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    ], dtype=np.uint32)
    
    return positions, normals, colors, indices


def create_cylinder(radius, height, segments, color, origin=(0, 0, 0), top_radius=None):
    """Create a cylinder mesh with vertex colors."""
    if top_radius is None:
        top_radius = radius
    
    ox, oy, oz = origin
    positions = []
    normals = []
    colors = []
    indices = []
    
    for i in range(segments):
        angle = 2 * np.pi * i / segments
        cos_a, sin_a = np.cos(angle), np.sin(angle)
        positions.append([ox + radius * cos_a, oy, oz + radius * sin_a])
        normals.append([cos_a, 0, sin_a])
        colors.append(color)
        positions.append([ox + top_radius * cos_a, oy + height, oz + top_radius * sin_a])
        normals.append([cos_a, 0, sin_a])
        colors.append(color)
    
    for i in range(segments):
        i0 = i * 2
        i1 = i * 2 + 1
        i2 = ((i + 1) % segments) * 2
        i3 = ((i + 1) % segments) * 2 + 1
        indices.extend([i0, i2, i1, i1, i2, i3])
    
    top_center_idx = len(positions)
    positions.append([ox, oy + height, oz])
    normals.append([0, 1, 0])
    colors.append(color)
    
    for i in range(segments):
        angle = 2 * np.pi * i / segments
        positions.append([ox + top_radius * np.cos(angle), oy + height, oz + top_radius * np.sin(angle)])
        normals.append([0, 1, 0])
        colors.append(color)
    
    for i in range(segments):
        indices.extend([top_center_idx, top_center_idx + 1 + i, top_center_idx + 1 + ((i + 1) % segments)])
    
    bot_center_idx = len(positions)
    positions.append([ox, oy, oz])
    normals.append([0, -1, 0])
    colors.append(color)
    
    for i in range(segments):
        angle = 2 * np.pi * i / segments
        positions.append([ox + radius * np.cos(angle), oy, oz + radius * np.sin(angle)])
        normals.append([0, -1, 0])
        colors.append(color)
    
    for i in range(segments):
        indices.extend([bot_center_idx, bot_center_idx + 1 + ((i + 1) % segments), bot_center_idx + 1 + i])
    
    return np.array(positions, dtype=np.float32), np.array(normals, dtype=np.float32), np.array(colors, dtype=np.float32), np.array(indices, dtype=np.uint32)


def create_cone(radius, height, segments, color, origin=(0, 0, 0)):
    """Create a cone mesh."""
    ox, oy, oz = origin
    positions = []
    normals = []
    colors = []
    indices = []
    
    apex_idx = 0
    positions.append([ox, oy + height, oz])
    slope = radius / height
    normal_len = np.sqrt(1 + slope * slope)
    normals.append([0, 1 / normal_len, 0])
    colors.append(color)
    
    for i in range(segments):
        angle = 2 * np.pi * i / segments
        cos_a, sin_a = np.cos(angle), np.sin(angle)
        positions.append([ox + radius * cos_a, oy, oz + radius * sin_a])
        normals.append([cos_a / normal_len, slope / normal_len, sin_a / normal_len])
        colors.append(color)
    
    for i in range(segments):
        i1 = 1 + i
        i2 = 1 + ((i + 1) % segments)
        indices.extend([apex_idx, i1, i2])
    
    bot_center_idx = len(positions)
    positions.append([ox, oy, oz])
    normals.append([0, -1, 0])
    colors.append(color)
    
    bot_start = len(positions)
    for i in range(segments):
        angle = 2 * np.pi * i / segments
        positions.append([ox + radius * np.cos(angle), oy, oz + radius * np.sin(angle)])
        normals.append([0, -1, 0])
        colors.append(color)
    
    for i in range(segments):
        indices.extend([bot_center_idx, bot_start + ((i + 1) % segments), bot_start + i])
    
    return np.array(positions, dtype=np.float32), np.array(normals, dtype=np.float32), np.array(colors, dtype=np.float32), np.array(indices, dtype=np.uint32)


def create_rounded_box(w, h, d, corner_r, segments, color, origin=(0, 0, 0)):
    """Create a rounded box by combining boxes and cylinders for edges."""
    meshes = []
    ox, oy, oz = origin
    hw, hh, hd = w / 2, h / 2, d / 2
    inner_w = w - corner_r * 2
    inner_d = d - corner_r * 2
    
    p1, n1, c1, i1 = create_box(inner_w, h, d, color, origin=(ox, oy + hh, oz))
    meshes.append((p1, n1, c1, i1))
    
    for side in [-1, 1]:
        edge_x = ox + side * (inner_w / 2)
        cyl_p, cyl_n, cyl_c, cyl_i = create_cylinder(corner_r, h, segments, color, origin=(edge_x, oy, oz))
        meshes.append((cyl_p, cyl_n, cyl_c, cyl_i))
    
    return merge_meshes(meshes)


def create_torus(outer_r, inner_r, segments, rings, color, origin=(0, 0, 0)):
    """Create a torus (donut) shape for handles."""
    ox, oy, oz = origin
    positions = []
    normals = []
    colors = []
    indices = []
    
    for i in range(rings):
        theta = 2 * np.pi * i / rings
        cos_t, sin_t = np.cos(theta), np.sin(theta)
        center_x = ox + (outer_r - inner_r) * cos_t
        center_z = oz + (outer_r - inner_r) * sin_t
        
        for j in range(segments):
            phi = 2 * np.pi * j / segments
            cos_p, sin_p = np.cos(phi), np.sin(phi)
            
            x = center_x + inner_r * cos_p * cos_t
            y = oy + inner_r * sin_p
            z = center_z + inner_r * cos_p * sin_t
            
            positions.append([x, y, z])
            
            nx = cos_p * cos_t
            ny = sin_p
            nz = cos_p * sin_t
            normals.append([nx, ny, nz])
            colors.append(color)
    
    for i in range(rings):
        for j in range(segments):
            curr = i * segments + j
            next_i = ((i + 1) % rings) * segments + j
            next_j = i * segments + ((j + 1) % segments)
            next_ij = ((i + 1) % rings) * segments + ((j + 1) % segments)
            
            indices.extend([curr, next_i, next_j, next_i, next_ij, next_j])
    
    return np.array(positions, dtype=np.float32), np.array(normals, dtype=np.float32), np.array(colors, dtype=np.float32), np.array(indices, dtype=np.uint32)


def merge_meshes(meshes):
    """Merge multiple meshes into one."""
    all_positions = []
    all_normals = []
    all_colors = []
    all_indices = []
    index_offset = 0
    
    for positions, normals, colors, indices in meshes:
        all_positions.append(positions)
        all_normals.append(normals)
        all_colors.append(colors)
        all_indices.append(indices + index_offset)
        index_offset += len(positions)
    
    return (
        np.vstack(all_positions),
        np.vstack(all_normals),
        np.vstack(all_colors),
        np.hstack(all_indices)
    )


def create_flashlight():
    """
    DBD-style Flashlight - bulky handheld, industrial, hardware-store aesthetic.
    Muted yellow/dirty beige body, dark gray head rim, reflective lens.
    Target: ~700 tris, max 800.
    """
    meshes = []
    
    # Main body - dirty yellow/beige (muted industrial yellow)
    body_yellow = [0.72, 0.62, 0.38]  # Dirty yellow/beige
    body_dark = [0.25, 0.22, 0.18]    # Dark grip sections
    head_gray = [0.35, 0.35, 0.38]    # Dark gray head rim
    lens_color = [0.75, 0.82, 0.88]   # Slightly reflective glass
    worn_edge = [0.55, 0.48, 0.32]    # Worn/scratched areas
    
    # Main cylindrical body - 12 segments for industrial look
    body_p, body_n, body_c, body_i = create_cylinder(0.032, 0.14, 12, body_yellow, origin=(0, 0, 0))
    meshes.append((body_p, body_n, body_c, body_i))
    
    # Slightly wider middle section (bulky feel)
    mid_p, mid_n, mid_c, mid_i = create_cylinder(0.036, 0.04, 12, body_yellow, origin=(0, 0.05, 0))
    meshes.append((mid_p, mid_n, mid_c, mid_i))
    
    # Grip rings - dark rubberized sections (3 rings)
    for i in range(3):
        grip_y = 0.02 + i * 0.04
        grip_p, grip_n, grip_c, grip_i = create_cylinder(0.034, 0.018, 12, body_dark, origin=(0, grip_y, 0))
        meshes.append((grip_p, grip_n, grip_c, grip_i))
    
    # Wider head section - angled industrial look
    head_p, head_n, head_c, head_i = create_cylinder(0.042, 0.055, 12, body_yellow, origin=(0, 0.14, 0))
    meshes.append((head_p, head_n, head_c, head_i))
    
    # Head rim - dark gray ring
    rim_p, rim_n, rim_c, rim_i = create_cylinder(0.046, 0.02, 12, head_gray, origin=(0, 0.195, 0))
    meshes.append((rim_p, rim_n, rim_c, rim_i))
    
    # Lens recess (slightly inset)
    lens_p, lens_n, lens_c, lens_i = create_cylinder(0.038, 0.008, 12, lens_color, origin=(0, 0.215, 0))
    meshes.append((lens_p, lens_n, lens_c, lens_i))
    
    # End cap - dark
    cap_p, cap_n, cap_c, cap_i = create_cylinder(0.033, 0.018, 12, head_gray, origin=(0, -0.018, 0))
    meshes.append((cap_p, cap_n, cap_c, cap_i))
    
    # Button - small red accent
    button_color = [0.65, 0.18, 0.15]
    btn_p, btn_n, btn_c, btn_i = create_box(0.012, 0.01, 0.012, button_color, origin=(0, 0.08, 0.035))
    meshes.append((btn_p, btn_n, btn_c, btn_i))
    
    # Metal clip - side mounted
    clip_color = [0.5, 0.5, 0.52]
    clip_p, clip_n, clip_c, clip_i = create_box(0.003, 0.08, 0.012, clip_color, origin=(-0.038, 0.06, 0))
    meshes.append((clip_p, clip_n, clip_c, clip_i))
    clip_top_p, clip_top_n, clip_top_c, clip_top_i = create_box(0.006, 0.015, 0.012, clip_color, origin=(-0.038, 0.11, 0))
    meshes.append((clip_top_p, clip_top_n, clip_top_c, clip_top_i))
    
    # Worn edge details - small scratches as thin boxes
    scratch1_p, scratch1_n, scratch1_c, scratch1_i = create_box(0.025, 0.003, 0.002, worn_edge, origin=(0.025, 0.07, 0.03))
    meshes.append((scratch1_p, scratch1_n, scratch1_c, scratch1_i))
    scratch2_p, scratch2_n, scratch2_c, scratch2_i = create_box(0.02, 0.003, 0.002, worn_edge, origin=(-0.02, 0.12, -0.032))
    meshes.append((scratch2_p, scratch2_n, scratch2_c, scratch2_i))
    
    # Bevel detail on head
    bevel_p, bevel_n, bevel_c, bevel_i = create_box(0.045, 0.008, 0.045, head_gray, origin=(0, 0.148, 0))
    meshes.append((bevel_p, bevel_n, bevel_c, bevel_i))
    
    return merge_meshes(meshes)


def create_medkit():
    """
    DBD-style Medkit - compact rectangular, rounded edges, survival-ready.
    Dark red base, white cross, matte plastic look with dirt.
    Target: ~850 tris, max 900.
    """
    meshes = []
    
    # Colors - dark red, worn white, gray plastic
    base_red = [0.55, 0.12, 0.1]       # Dark red plastic
    base_red_dark = [0.4, 0.08, 0.06]  # Darker red for depth
    cross_white = [0.88, 0.85, 0.8]    # Off-white
    handle_gray = [0.4, 0.38, 0.35]    # Gray handle
    latch_metal = [0.55, 0.53, 0.5]    # Metal latches
    dirt_edge = [0.35, 0.12, 0.1]      # Dirt/wear
    
    # Main body - rounded box using multiple boxes + cylinders
    # Center section
    body_p, body_n, body_c, body_i = create_box(0.13, 0.085, 0.085, base_red, origin=(0, 0.0425, 0))
    meshes.append((body_p, body_n, body_c, body_i))
    
    # Rounded corners using quarter cylinders
    corner_r = 0.015
    corner_segments = 6
    corners = [
        (-0.0575, 0, 0.035),
        (0.0575, 0, 0.035),
        (-0.0575, 0, -0.035),
        (0.0575, 0, -0.035),
    ]
    for cx, _, cz in corners:
        cyl_p, cyl_n, cyl_c, cyl_i = create_cylinder(corner_r, 0.085, corner_segments, base_red, origin=(cx, 0, cz))
        meshes.append((cyl_p, cyl_n, cyl_c, cyl_i))
    
    # Lid - slightly raised
    lid_p, lid_n, lid_c, lid_i = create_box(0.135, 0.015, 0.09, base_red_dark, origin=(0, 0.0925, 0))
    meshes.append((lid_p, lid_n, lid_c, lid_i))
    
    # Red cross on lid - horizontal bar
    cross_h_p, cross_h_n, cross_h_c, cross_h_i = create_box(0.065, 0.018, 0.006, cross_white, origin=(0, 0.1, 0.045))
    meshes.append((cross_h_p, cross_h_n, cross_h_c, cross_h_i))
    
    # Red cross - vertical bar
    cross_v_p, cross_v_n, cross_v_c, cross_v_i = create_box(0.018, 0.065, 0.006, cross_white, origin=(0, 0.1, 0.045))
    meshes.append((cross_v_p, cross_v_n, cross_v_c, cross_v_i))
    
    # Handle base
    handle_base_p, handle_base_n, handle_base_c, handle_base_i = create_box(0.055, 0.018, 0.025, handle_gray, origin=(0, 0.1, 0))
    meshes.append((handle_base_p, handle_base_n, handle_base_c, handle_base_i))
    
    # Handle arc - simplified torus segment
    handle_arc_p, handle_arc_n, handle_arc_c, handle_arc_i = create_box(0.045, 0.015, 0.012, handle_gray, origin=(0, 0.118, 0))
    meshes.append((handle_arc_p, handle_arc_n, handle_arc_c, handle_arc_i))
    
    # Handle supports
    for side in [-1, 1]:
        supp_p, supp_n, supp_c, supp_i = create_box(0.012, 0.025, 0.012, handle_gray, origin=(side * 0.025, 0.108, 0))
        meshes.append((supp_p, supp_n, supp_c, supp_i))
    
    # Latches - left and right
    for side in [-1, 1]:
        latch_p, latch_n, latch_c, latch_i = create_box(0.022, 0.035, 0.018, latch_metal, origin=(side * 0.055, 0.09, 0.035))
        meshes.append((latch_p, latch_n, latch_c, latch_i))
        # Latch clasp
        clasp_p, clasp_n, clasp_c, clasp_i = create_box(0.015, 0.012, 0.008, latch_metal, origin=(side * 0.055, 0.1, 0.043))
        meshes.append((clasp_p, clasp_n, clasp_c, clasp_i))
    
    # Corner protectors / wear marks
    protector_corners = [
        (-0.065, 0.0425, 0.043),
        (0.065, 0.0425, 0.043),
        (-0.065, 0.0425, -0.043),
        (0.065, 0.0425, -0.043),
    ]
    for cx, cy, cz in protector_corners:
        prot_p, prot_n, prot_c, prot_i = create_box(0.015, 0.085, 0.015, base_red_dark, origin=(cx, cy, cz))
        meshes.append((prot_p, prot_n, prot_c, prot_i))
    
    # Dirt/edge wear details
    for i in range(3):
        dirt_x = -0.04 + i * 0.04
        dirt_p, dirt_n, dirt_c, dirt_i = create_box(0.015, 0.003, 0.003, dirt_edge, origin=(dirt_x, 0.087, 0.042))
        meshes.append((dirt_p, dirt_n, dirt_c, dirt_i))
    
    # Side texture lines (molded plastic look)
    for side in [-1, 1]:
        line_p, line_n, line_c, line_i = create_box(0.003, 0.06, 0.003, base_red_dark, origin=(side * 0.065, 0.04, 0))
        meshes.append((line_p, line_n, line_c, line_i))
    
    return merge_meshes(meshes)


def create_toolbox():
    """
    DBD-style Toolbox - metal, rectangular, industrial/mechanic vibe.
    Desaturated blue/gray, metal roughness, scratches, edge highlights.
    Target: ~950 tris, max 1000.
    """
    meshes = []
    
    # Colors - desaturated industrial
    body_blue = [0.28, 0.32, 0.38]      # Desaturated blue-gray metal
    body_blue_dark = [0.2, 0.22, 0.26]  # Darker shade
    body_red = [0.45, 0.18, 0.15]       # Red accent option
    handle_black = [0.15, 0.15, 0.15]   # Black rubber handle
    metal_light = [0.5, 0.5, 0.52]      # Light metal for bands
    metal_dark = [0.35, 0.35, 0.38]     # Dark metal
    scratch_color = [0.45, 0.47, 0.5]   # Scratch highlights
    
    # Main body - rectangular metal box
    body_p, body_n, body_c, body_i = create_box(0.18, 0.09, 0.095, body_blue, origin=(0, 0.045, 0))
    meshes.append((body_p, body_n, body_c, body_i))
    
    # Bottom edge detail
    bottom_p, bottom_n, bottom_c, bottom_i = create_box(0.185, 0.012, 0.1, body_blue_dark, origin=(0, 0.006, 0))
    meshes.append((bottom_p, bottom_n, bottom_c, bottom_i))
    
    # Lid - slightly larger top
    lid_p, lid_n, lid_c, lid_i = create_box(0.185, 0.018, 0.1, body_blue, origin=(0, 0.099, 0))
    meshes.append((lid_p, lid_n, lid_c, lid_i))
    
    # Lid rounded top edge
    lid_top_p, lid_top_n, lid_top_c, lid_top_i = create_box(0.188, 0.01, 0.102, metal_dark, origin=(0, 0.113, 0))
    meshes.append((lid_top_p, lid_top_n, lid_top_c, lid_top_i))
    
    # Metal bands - horizontal reinforcement
    band1_p, band1_n, band1_c, band1_i = create_box(0.19, 0.012, 0.1, metal_light, origin=(0, 0.025, 0))
    meshes.append((band1_p, band1_n, band1_c, band1_i))
    band2_p, band2_n, band2_c, band2_i = create_box(0.19, 0.012, 0.1, metal_light, origin=(0, 0.065, 0))
    meshes.append((band2_p, band2_n, band2_c, band2_i))
    
    # Handle mounts - side brackets
    for side in [-1, 1]:
        mount_p, mount_n, mount_c, mount_i = create_box(0.03, 0.035, 0.025, metal_dark, origin=(side * 0.07, 0.116, 0))
        meshes.append((mount_p, mount_n, mount_c, mount_i))
    
    # Handle bar - black rubber grip
    handle_p, handle_n, handle_c, handle_i = create_cylinder(0.014, 0.12, 8, handle_black, origin=(0, 0.135, 0))
    meshes.append((handle_p, handle_n, handle_c, handle_i))
    
    # Handle end caps
    for side in [-1, 1]:
        cap_p, cap_n, cap_c, cap_i = create_cylinder(0.016, 0.01, 8, metal_dark, origin=(side * 0.062, 0.135, 0))
        meshes.append((cap_p, cap_n, cap_c, cap_i))
    
    # Central latch mechanism
    latch_base_p, latch_base_n, latch_base_c, latch_base_i = create_box(0.045, 0.025, 0.018, metal_light, origin=(0, 0.105, 0.05))
    meshes.append((latch_base_p, latch_base_n, latch_base_c, latch_base_i))
    latch_top_p, latch_top_n, latch_top_c, latch_top_i = create_box(0.035, 0.015, 0.012, metal_dark, origin=(0, 0.118, 0.053))
    meshes.append((latch_top_p, latch_top_n, latch_top_c, latch_top_i))
    
    # Corner brackets - metal corner protectors
    bracket_corners = [
        (-0.0925, 0.045, 0.048),
        (0.0925, 0.045, 0.048),
        (-0.0925, 0.045, -0.048),
        (0.0925, 0.045, -0.048),
    ]
    for cx, cy, cz in bracket_corners:
        bracket_p, bracket_n, bracket_c, bracket_i = create_box(0.018, 0.09, 0.018, metal_dark, origin=(cx, cy, cz))
        meshes.append((bracket_p, bracket_n, bracket_c, bracket_i))
    
    # Interior tray divider (visible detail)
    tray_p, tray_n, tray_c, tray_i = create_box(0.14, 0.02, 0.008, body_blue_dark, origin=(0, 0.085, 0))
    meshes.append((tray_p, tray_n, tray_c, tray_i))
    
    # Scratch details - metal wear marks
    scratches = [
        (0.05, 0.03, 0.048, 0.025, 0.002),
        (-0.06, 0.055, 0.048, 0.03, 0.002),
        (0.02, 0.075, 0.048, 0.02, 0.002),
        (-0.04, 0.04, -0.048, 0.025, 0.002),
    ]
    for sx, sy, sz, slen, swid in scratches:
        scr_p, scr_n, scr_c, scr_i = create_box(slen, 0.002, swid, scratch_color, origin=(sx, sy, sz))
        meshes.append((scr_p, scr_n, scr_c, scr_i))
    
    # Metal edge highlights on lid
    edge_f_p, edge_f_n, edge_f_c, edge_f_i = create_box(0.185, 0.006, 0.006, metal_light, origin=(0, 0.115, 0.05))
    meshes.append((edge_f_p, edge_f_n, edge_f_c, edge_f_i))
    edge_b_p, edge_b_n, edge_b_c, edge_b_i = create_box(0.185, 0.006, 0.006, metal_light, origin=(0, 0.115, -0.05))
    meshes.append((edge_b_p, edge_b_n, edge_b_c, edge_b_i))
    
    # Side latches (smaller)
    for side in [-1, 1]:
        side_latch_p, side_latch_n, side_latch_c, side_latch_i = create_box(0.015, 0.02, 0.01, metal_light, origin=(side * 0.08, 0.1, 0.045))
        meshes.append((side_latch_p, side_latch_n, side_latch_c, side_latch_i))
    
    return merge_meshes(meshes)


def create_map():
    """
    DBD-style Map - folded paper, creased, fragile and worn.
    Dirty beige paper, faded printed lines, fold shading.
    Target: ~350 tris, max 400.
    """
    meshes = []
    
    # Colors - aged paper tones
    paper_base = [0.78, 0.72, 0.55]     # Dirty beige
    paper_dark = [0.65, 0.58, 0.42]     # Darker folds
    paper_light = [0.85, 0.8, 0.65]     # Highlight edges
    print_color = [0.35, 0.4, 0.35]     # Faded green/gray print
    mark_red = [0.6, 0.2, 0.18]         # Red X mark
    edge_worn = [0.55, 0.5, 0.38]       # Worn edges
    
    # Main folded body - thin rectangular shape
    base_p, base_n, base_c, base_i = create_box(0.15, 0.004, 0.11, paper_base, origin=(0, 0.002, 0))
    meshes.append((base_p, base_n, base_c, base_i))
    
    # Top fold layer
    fold1_p, fold1_n, fold1_c, fold1_i = create_box(0.15, 0.003, 0.052, paper_light, origin=(0, 0.0055, 0.03))
    meshes.append((fold1_p, fold1_n, fold1_c, fold1_i))
    
    # Bottom fold layer
    fold2_p, fold2_n, fold2_c, fold2_i = create_box(0.15, 0.003, 0.052, paper_dark, origin=(0, 0.0055, -0.03))
    meshes.append((fold2_p, fold2_n, fold2_c, fold2_i))
    
    # Fold crease lines (darker)
    crease1_p, crease1_n, crease1_c, crease1_i = create_box(0.15, 0.005, 0.002, paper_dark, origin=(0, 0.004, 0.0))
    meshes.append((crease1_p, crease1_n, crease1_c, crease1_i))
    
    # Secondary creases
    crease2_p, crease2_n, crease2_c, crease2_i = create_box(0.15, 0.005, 0.002, paper_dark, origin=(0, 0.004, 0.055))
    meshes.append((crease2_p, crease2_n, crease2_c, crease2_i))
    crease3_p, crease3_n, crease3_c, crease3_i = create_box(0.15, 0.005, 0.002, paper_dark, origin=(0, 0.004, -0.055))
    meshes.append((crease3_p, crease3_n, crease3_c, crease3_i))
    
    # Rolled/curling front edge
    roll_front_p, roll_front_n, roll_front_c, roll_front_i = create_cylinder(0.006, 0.15, 6, edge_worn, origin=(0, 0.006, 0.058))
    meshes.append((roll_front_p, roll_front_n, roll_front_c, roll_front_i))
    
    # Rolled back edge
    roll_back_p, roll_back_n, roll_back_c, roll_back_i = create_cylinder(0.005, 0.15, 6, edge_worn, origin=(0, 0.005, -0.058))
    meshes.append((roll_back_p, roll_back_n, roll_back_c, roll_back_i))
    
    # Red X mark (destination/target)
    x_h_p, x_h_n, x_h_c, x_h_i = create_box(0.022, 0.005, 0.003, mark_red, origin=(-0.035, 0.005, 0.015))
    meshes.append((x_h_p, x_h_n, x_h_c, x_h_i))
    x_v_p, x_v_n, x_v_c, x_v_i = create_box(0.003, 0.005, 0.022, mark_red, origin=(-0.035, 0.005, 0.015))
    meshes.append((x_v_p, x_v_n, x_v_c, x_v_i))
    
    # Printed route line (faded)
    route1_p, route1_n, route1_c, route1_i = create_box(0.05, 0.005, 0.001, print_color, origin=(0.02, 0.005, 0.0))
    meshes.append((route1_p, route1_n, route1_c, route1_i))
    route2_p, route2_n, route2_c, route2_i = create_box(0.001, 0.005, 0.04, print_color, origin=(0.045, 0.005, -0.02))
    meshes.append((route2_p, route2_n, route2_c, route2_i))
    
    # Compass hint - small circle
    compass_p, compass_n, compass_c, compass_i = create_cylinder(0.012, 0.005, 6, print_color, origin=(0.055, 0.005, -0.04))
    meshes.append((compass_p, compass_n, compass_c, compass_i))
    
    # Compass needle
    needle_p, needle_n, needle_c, needle_i = create_box(0.002, 0.005, 0.015, print_color, origin=(0.055, 0.005, -0.04))
    meshes.append((needle_p, needle_n, needle_c, needle_i))
    
    # Worn corner detail
    worn1_p, worn1_n, worn1_c, worn1_i = create_box(0.025, 0.003, 0.003, edge_worn, origin=(0.07, 0.0035, 0.054))
    meshes.append((worn1_p, worn1_n, worn1_c, worn1_i))
    worn2_p, worn2_n, worn2_c, worn2_i = create_box(0.02, 0.003, 0.003, edge_worn, origin=(-0.065, 0.0035, -0.053))
    meshes.append((worn2_p, worn2_n, worn2_c, worn2_i))
    
    # Small text block hint
    text_p, text_n, text_c, text_i = create_box(0.025, 0.005, 0.015, print_color, origin=(-0.05, 0.005, 0.025))
    meshes.append((text_p, text_n, text_c, text_i))
    
    return merge_meshes(meshes)


def create_glb(positions, normals, colors, indices, output_path):
    """Create a GLB file from mesh data."""
    positions = positions.astype(np.float32)
    normals = normals.astype(np.float32)
    colors = colors.astype(np.float32)
    indices = indices.astype(np.uint32)
    
    pos_bytes = positions.tobytes()
    norm_bytes = normals.tobytes()
    color_bytes = colors.tobytes()
    index_bytes = indices.tobytes()
    
    def pad_to_4(data):
        padding = (4 - (len(data) % 4)) % 4
        return data + b'\x00' * padding
    
    pos_padded = pad_to_4(pos_bytes)
    norm_padded = pad_to_4(norm_bytes)
    color_padded = pad_to_4(color_bytes)
    index_padded = pad_to_4(index_bytes)
    
    binary_data = pos_padded + norm_padded + color_padded + index_padded
    
    pos_view = BufferView(buffer=0, byteOffset=0, byteLength=len(pos_bytes), target=ARRAY_BUFFER)
    norm_view = BufferView(buffer=0, byteOffset=len(pos_padded), byteLength=len(norm_bytes), target=ARRAY_BUFFER)
    color_view = BufferView(buffer=0, byteOffset=len(pos_padded) + len(norm_padded), byteLength=len(color_bytes), target=ARRAY_BUFFER)
    index_view = BufferView(buffer=0, byteOffset=len(pos_padded) + len(norm_padded) + len(color_padded), byteLength=len(index_bytes), target=ELEMENT_ARRAY_BUFFER)
    
    pos_accessor = Accessor(
        bufferView=0,
        componentType=FLOAT,
        count=len(positions),
        type="VEC3",
        min=positions.min(axis=0).tolist(),
        max=positions.max(axis=0).tolist()
    )
    
    norm_accessor = Accessor(
        bufferView=1,
        componentType=FLOAT,
        count=len(normals),
        type="VEC3"
    )
    
    color_accessor = Accessor(
        bufferView=2,
        componentType=FLOAT,
        count=len(colors),
        type="VEC3"
    )
    
    index_accessor = Accessor(
        bufferView=3,
        componentType=UNSIGNED_INT,
        count=len(indices),
        type="SCALAR"
    )
    
    primitive = Primitive(
        attributes={"POSITION": 0, "NORMAL": 1, "COLOR_0": 2},
        indices=3,
        mode=TRIANGLES
    )
    
    mesh = Mesh(primitives=[primitive])
    node = Node(mesh=0)
    scene = Scene(nodes=[0])
    
    gltf = GLTF2(
        buffers=[Buffer(byteLength=len(binary_data))],
        bufferViews=[pos_view, norm_view, color_view, index_view],
        accessors=[pos_accessor, norm_accessor, color_accessor, index_accessor],
        meshes=[mesh],
        nodes=[node],
        scenes=[scene],
        scene=0
    )
    
    gltf.set_binary_blob(binary_data)
    gltf.save(output_path)
    
    tri_count = len(indices) // 3
    return tri_count


def main():
    output_dir = Path(__file__).parent.parent / "assets" / "meshes" / "items"
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print("=" * 60)
    print("DBD-Style Low Poly Item Model Generator")
    print("=" * 60)
    print()
    
    results = []
    
    print("[1/4] Creating Flashlight...")
    print("      - Bulky handheld, industrial hardware-store aesthetic")
    print("      - Muted yellow body, dark gray rim, reflective lens")
    p, n, c, i = create_flashlight()
    tris = create_glb(p, n, c, i, str(output_dir / "flashlight.glb"))
    status = "OK" if tris <= 800 else "OVER LIMIT"
    results.append(("Flashlight", tris, 800, status))
    print(f"      -> {tris} triangles [{status}]\n")
    
    print("[2/4] Creating Medkit...")
    print("      - Compact rectangular, rounded edges, survival-ready")
    print("      - Dark red plastic, white cross, matte look")
    p, n, c, i = create_medkit()
    tris = create_glb(p, n, c, i, str(output_dir / "medkit.glb"))
    status = "OK" if tris <= 900 else "OVER LIMIT"
    results.append(("Medkit", tris, 900, status))
    print(f"      -> {tris} triangles [{status}]\n")
    
    print("[3/4] Creating Toolbox...")
    print("      - Metal rectangular, industrial/mechanic vibe")
    print("      - Desaturated blue-gray, metal roughness, scratches")
    p, n, c, i = create_toolbox()
    tris = create_glb(p, n, c, i, str(output_dir / "toolbox.glb"))
    status = "OK" if tris <= 1000 else "OVER LIMIT"
    results.append(("Toolbox", tris, 1000, status))
    print(f"      -> {tris} triangles [{status}]\n")
    
    print("[4/4] Creating Map...")
    print("      - Folded paper, creased, fragile and worn")
    print("      - Dirty beige, faded print lines, fold shading")
    p, n, c, i = create_map()
    tris = create_glb(p, n, c, i, str(output_dir / "map.glb"))
    status = "OK" if tris <= 400 else "OVER LIMIT"
    results.append(("Map", tris, 400, status))
    print(f"      -> {tris} triangles [{status}]\n")
    
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"{'Item':<15} {'Tris':>8} {'Limit':>8} {'Status':>12}")
    print("-" * 45)
    for name, tris, limit, status in results:
        print(f"{name:<15} {tris:>8} {limit:>8} {status:>12}")
    print()
    print(f"Output: {output_dir}")
    print("=" * 60)


if __name__ == "__main__":
    main()
