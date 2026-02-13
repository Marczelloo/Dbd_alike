#!/usr/bin/env python3
"""
Generate DBD-style Medkit model.
Compact rectangular first aid kit with rounded edges, top handle, medical cross.
"""

import numpy as np
from pathlib import Path

from pygltflib import GLTF2, Buffer, BufferView, Accessor, Mesh, Primitive, Node, Scene
from pygltflib import ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, UNSIGNED_INT
from pygltflib import TRIANGLES


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


def create_half_cylinder(radius, height, segments, color, origin=(0, 0, 0), axis='x'):
    """Create a half cylinder (180 degrees) for rounded edges."""
    ox, oy, oz = origin
    positions = []
    normals = []
    colors = []
    indices = []
    
    half_segments = segments // 2
    
    if axis == 'x':
        for i in range(half_segments + 1):
            angle = -np.pi/2 + np.pi * i / half_segments
            cos_a, sin_a = np.cos(angle), np.sin(angle)
            positions.append([ox, oy, oz + radius * sin_a])
            normals.append([0, 0, sin_a])
            colors.append(color)
            positions.append([ox, oy + height, oz + radius * sin_a])
            normals.append([0, 0, sin_a])
            colors.append(color)
        
        for i in range(half_segments):
            i0 = i * 2
            i1 = i * 2 + 1
            i2 = (i + 1) * 2
            i3 = (i + 1) * 2 + 1
            indices.extend([i0, i2, i1, i1, i2, i3])
    else:
        for i in range(half_segments + 1):
            angle = -np.pi/2 + np.pi * i / half_segments
            cos_a, sin_a = np.cos(angle), np.sin(angle)
            positions.append([ox + radius * cos_a, oy, oz])
            normals.append([cos_a, 0, 0])
            colors.append(color)
            positions.append([ox + radius * cos_a, oy + height, oz])
            normals.append([cos_a, 0, 0])
            colors.append(color)
        
        for i in range(half_segments):
            i0 = i * 2
            i1 = i * 2 + 1
            i2 = (i + 1) * 2
            i3 = (i + 1) * 2 + 1
            indices.extend([i0, i2, i1, i1, i2, i3])
    
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


def create_medkit():
    """
    DBD-style Medkit - compact rectangular first aid kit.
    Red/dark red base, white cross, rounded edges, top handle.
    Matte plastic look with dirt wear.
    Target: ~700-900 tris
    """
    meshes = []
    
    # === COLORS ===
    # Dark red plastic base
    red_primary = [0.58, 0.12, 0.10]       # Primary dark red
    red_dark = [0.42, 0.08, 0.06]          # Darker red (shadows, edges)
    red_light = [0.68, 0.18, 0.15]         # Lighter red (highlights)
    
    # White/off-white cross
    white_cross = [0.92, 0.90, 0.85]       # Off-white
    
    # Gray plastic handle
    handle_gray = [0.38, 0.36, 0.34]       # Dark gray handle
    handle_dark = [0.28, 0.26, 0.24]       # Darker gray
    
    # Metal latches
    metal_silver = [0.55, 0.53, 0.50]      # Silver metal
    
    # Dirt/wear
    dirt_brown = [0.32, 0.18, 0.12]        # Dirt marks
    
    # === DIMENSIONS ===
    # Box size: compact first aid kit
    box_w = 0.14   # Width (X)
    box_h = 0.08   # Height (Y)
    box_d = 0.09   # Depth (Z)
    corner_r = 0.018  # Corner rounding radius
    
    # === MAIN BODY ===
    # Center rectangular section
    center_w = box_w - corner_r * 2
    center_p, center_n, center_c, center_i = create_box(
        center_w, box_h, box_d, red_primary, origin=(0, box_h/2, 0)
    )
    meshes.append((center_p, center_n, center_c, center_i))
    
    # === ROUNDED CORNERS ===
    # Left side rounded edge (half cylinder along Y)
    left_edge_p, left_edge_n, left_edge_c, left_edge_i = create_half_cylinder(
        corner_r, box_h, 8, red_primary, origin=(-center_w/2, 0, 0), axis='z'
    )
    meshes.append((left_edge_p, left_edge_n, left_edge_c, left_edge_i))
    
    # Right side rounded edge
    right_edge_p, right_edge_n, right_edge_c, right_edge_i = create_half_cylinder(
        corner_r, box_h, 8, red_primary, origin=(center_w/2, 0, 0), axis='z'
    )
    meshes.append((right_edge_p, right_edge_n, right_edge_c, right_edge_i))
    
    # === LID ===
    # Slightly raised lid on top
    lid_h = 0.012
    lid_p, lid_n, lid_c, lid_i = create_box(
        box_w + 0.004, lid_h, box_d + 0.004, red_dark, origin=(0, box_h + lid_h/2, 0)
    )
    meshes.append((lid_p, lid_n, lid_c, lid_i))
    
    # Lid edge highlight
    lid_edge_p, lid_edge_n, lid_edge_c, lid_edge_i = create_box(
        box_w + 0.006, 0.004, box_d + 0.006, red_light, origin=(0, box_h + lid_h + 0.002, 0)
    )
    meshes.append((lid_edge_p, lid_edge_n, lid_edge_c, lid_edge_i))
    
    # === MEDICAL CROSS ON LID ===
    # Horizontal bar
    cross_w = 0.055
    cross_h_bar = 0.015
    cross_thick = 0.003
    cross_h_p, cross_h_n, cross_h_c, cross_h_i = create_box(
        cross_w, cross_h_bar, cross_thick, white_cross,
        origin=(0, box_h + lid_h + cross_thick/2, box_d/2 - 0.01)
    )
    meshes.append((cross_h_p, cross_h_n, cross_h_c, cross_h_i))
    
    # Vertical bar
    cross_v = 0.055
    cross_v_p, cross_v_n, cross_v_c, cross_v_i = create_box(
        cross_h_bar, cross_v, cross_thick, white_cross,
        origin=(0, box_h + lid_h + cross_thick/2, box_d/2 - 0.01)
    )
    meshes.append((cross_v_p, cross_v_n, cross_v_c, cross_v_i))
    
    # === HANDLE ===
    # Handle base mount
    handle_base_w = 0.045
    handle_base_d = 0.022
    handle_base_h = 0.015
    handle_base_p, handle_base_n, handle_base_c, handle_base_i = create_box(
        handle_base_w, handle_base_h, handle_base_d, handle_gray,
        origin=(0, box_h + lid_h + handle_base_h/2, 0)
    )
    meshes.append((handle_base_p, handle_base_n, handle_base_c, handle_base_i))
    
    # Handle arc (curved bar) - simplified as bent boxes
    handle_arc_r = 0.028
    handle_arc_w = 0.012
    handle_arc_segments = 5
    
    for i in range(handle_arc_segments):
        t = i / (handle_arc_segments - 1)
        angle = -np.pi/2 + np.pi * t
        y_offset = box_h + lid_h + handle_base_h + handle_arc_r * np.sin(angle) * 0.4
        z_offset = handle_arc_r * np.cos(angle) * 0.3
        
        seg_p, seg_n, seg_c, seg_i = create_box(
            handle_arc_w, 0.01, 0.012, handle_gray,
            origin=(0, y_offset, z_offset)
        )
        meshes.append((seg_p, seg_n, seg_c, seg_i))
    
    # Handle support posts
    for side in [-1, 1]:
        post_p, post_n, post_c, post_i = create_box(
            0.01, 0.025, 0.01, handle_dark,
            origin=(side * 0.022, box_h + lid_h + 0.012, 0)
        )
        meshes.append((post_p, post_n, post_c, post_i))
    
    # === LATCHES ===
    # Front latches (left and right)
    latch_w = 0.018
    latch_h = 0.032
    latch_d = 0.012
    
    for side in [-1, 1]:
        # Latch body
        latch_p, latch_n, latch_c, latch_i = create_box(
            latch_w, latch_h, latch_d, metal_silver,
            origin=(side * 0.05, box_h * 0.6, box_d/2)
        )
        meshes.append((latch_p, latch_n, latch_c, latch_i))
        
        # Latch clasp (small protrusion)
        clasp_p, clasp_n, clasp_c, clasp_i = create_box(
            latch_w - 0.004, 0.012, 0.006, metal_silver,
            origin=(side * 0.05, box_h * 0.75, box_d/2 + 0.008)
        )
        meshes.append((clasp_p, clasp_n, clasp_c, clasp_i))
    
    # === CORNER PROTECTORS ===
    # Metal corners for wear/protection
    corner_prot_w = 0.015
    corner_prot_h = box_h * 0.6
    
    corners = [
        (-box_w/2 + corner_prot_w/2, box_h/2, box_d/2),
        (box_w/2 - corner_prot_w/2, box_h/2, box_d/2),
        (-box_w/2 + corner_prot_w/2, box_h/2, -box_d/2),
        (box_w/2 - corner_prot_w/2, box_h/2, -box_d/2),
    ]
    
    for cx, cy, cz in corners:
        corner_p, corner_n, corner_c, corner_i = create_box(
            corner_prot_w, corner_prot_h, corner_prot_w, red_dark,
            origin=(cx, cy, cz)
        )
        meshes.append((corner_p, corner_n, corner_c, corner_i))
    
    # === DIRT/WEAR DETAILS ===
    # Small dirt marks on surface
    dirt_positions = [
        (-0.04, box_h + lid_h + 0.002, 0.02),
        (0.03, box_h + lid_h + 0.002, -0.03),
        (0.05, box_h * 0.5, box_d/2 + 0.002),
    ]
    
    for dx, dy, dz in dirt_positions:
        dirt_p, dirt_n, dirt_c, dirt_i = create_box(
            0.012, 0.002, 0.008, dirt_brown,
            origin=(dx, dy, dz)
        )
        meshes.append((dirt_p, dirt_n, dirt_c, dirt_i))
    
    # === SIDE RIDGES (molded plastic detail) ===
    for side in [-1, 1]:
        ridge_p, ridge_n, ridge_c, ridge_i = create_box(
            0.003, box_h * 0.7, 0.003, red_dark,
            origin=(side * (box_w/2 + 0.001), box_h/2, 0)
        )
        meshes.append((ridge_p, ridge_n, ridge_c, ridge_i))
    
    # === BOTTOM FEET ===
    for fx, fz in [(-0.04, -0.03), (0.04, -0.03), (-0.04, 0.03), (0.04, 0.03)]:
        foot_p, foot_n, foot_c, foot_i = create_box(
            0.015, 0.004, 0.015, handle_dark,
            origin=(fx, 0.002, fz)
        )
        meshes.append((foot_p, foot_n, foot_c, foot_i))
    
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
        bufferView=0, componentType=FLOAT, count=len(positions), type="VEC3",
        min=positions.min(axis=0).tolist(), max=positions.max(axis=0).tolist()
    )
    norm_accessor = Accessor(bufferView=1, componentType=FLOAT, count=len(normals), type="VEC3")
    color_accessor = Accessor(bufferView=2, componentType=FLOAT, count=len(colors), type="VEC3")
    index_accessor = Accessor(bufferView=3, componentType=UNSIGNED_INT, count=len(indices), type="SCALAR")
    
    primitive = Primitive(attributes={"POSITION": 0, "NORMAL": 1, "COLOR_0": 2}, indices=3, mode=TRIANGLES)
    mesh = Mesh(primitives=[primitive])
    node = Node(mesh=0)
    scene = Scene(nodes=[0])
    
    gltf = GLTF2(
        buffers=[Buffer(byteLength=len(binary_data))],
        bufferViews=[pos_view, norm_view, color_view, index_view],
        accessors=[pos_accessor, norm_accessor, color_accessor, index_accessor],
        meshes=[mesh], nodes=[node], scenes=[scene], scene=0
    )
    
    gltf.set_binary_blob(binary_data)
    gltf.save(output_path)
    return len(indices) // 3


def main():
    output_dir = Path(__file__).parent.parent / "assets" / "meshes" / "items"
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print("Generating DBD-style Medkit...")
    print("  - Compact rectangular with rounded edges")
    print("  - Dark red plastic, white cross")
    print("  - Top handle, metal latches")
    print("  - Matte plastic with dirt wear")
    
    p, n, c, i = create_medkit()
    tris = create_glb(p, n, c, i, str(output_dir / "medkit.glb"))
    
    status = "OK" if tris <= 900 else "OVER LIMIT"
    print(f"\nCreated: {output_dir / 'medkit.glb'}")
    print(f"Triangles: {tris} / 900 [{status}]")


if __name__ == "__main__":
    main()
