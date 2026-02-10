"""
Generate loop JSON assets from TileGenerator layout definitions.
Converts LocalPoint coordinates (0..15, center 7.5) to editor-relative coordinates.
"""
import json, os, math

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "loops")
TILE_CENTER = 7.5
WALL_HALF_HEIGHT = 1.0
WALL_HALF_THICK = 0.2
WIN_HALF = [0.8, 0.9, 0.2]
PAL_HALF = [0.8, 0.9, 0.2]

def lp(x, y):
    """Convert LocalPoint to editor-relative (x, z)."""
    return (x - TILE_CENTER, y - TILE_CENTER)

def wall_element(name, x1, y1, x2, y2):
    """Create a wall element from two LocalPoint endpoints."""
    cx, cz = lp((x1+x2)/2, (y1+y2)/2)
    dx = x2 - x1
    dy = y2 - y1
    if abs(dx) > abs(dy):  # horizontal wall
        half_len = abs(dx) / 2
        yaw = 0.0
    else:  # vertical wall
        half_len = abs(dy) / 2
        yaw = -90.0
    return {
        "type": "wall", "name": name,
        "position": [round(cx, 4), WALL_HALF_HEIGHT, round(cz, 4)],
        "half_extents": [round(half_len, 4), WALL_HALF_HEIGHT, WALL_HALF_THICK],
        "yaw_degrees": yaw, "pitch_degrees": 0.0, "roll_degrees": 0.0,
        "marker_tag": "", "transform_locked": False,
    }

def window_element(name, x, y, on_vertical_wall=False):
    cx, cz = lp(x, y)
    return {
        "type": "window", "name": name,
        "position": [round(cx, 4), WALL_HALF_HEIGHT, round(cz, 4)],
        "half_extents": list(WIN_HALF),
        "yaw_degrees": -90.0 if on_vertical_wall else 0.0,
        "pitch_degrees": 0.0, "roll_degrees": 0.0,
        "marker_tag": "", "transform_locked": False,
    }

def pallet_element(name, x, y, on_vertical_wall=False):
    cx, cz = lp(x, y)
    return {
        "type": "pallet", "name": name,
        "position": [round(cx, 4), WALL_HALF_HEIGHT, round(cz, 4)],
        "half_extents": list(PAL_HALF),
        "yaw_degrees": -90.0 if on_vertical_wall else 0.0,
        "pitch_degrees": 0.0, "roll_degrees": 0.0,
        "marker_tag": "", "transform_locked": False,
    }

def solid_element(name, x1, y1, x2, y2, half_height=1.0):
    """Create a solid box (wall type) from two corner LocalPoints."""
    cx, cz = lp((x1+x2)/2, (y1+y2)/2)
    hx = abs(x2 - x1) / 2
    hz = abs(y2 - y1) / 2
    return {
        "type": "wall", "name": name,
        "position": [round(cx, 4), half_height, round(cz, 4)],
        "half_extents": [round(hx, 4), half_height, round(hz, 4)],
        "yaw_degrees": 0.0, "pitch_degrees": 0.0, "roll_degrees": 0.0,
        "marker_tag": "", "transform_locked": False,
    }

def make_loop(loop_id, display_name, elements, footprint_w=1, footprint_h=1):
    return {
        "asset_version": 1,
        "id": loop_id,
        "display_name": display_name,
        "footprint": {"width": footprint_w, "height": footprint_h},
        "bounds": {"min": [-8.0, 0.0, -8.0], "max": [8.0, 2.0, 8.0]},
        "manual_bounds": True,
        "manual_footprint": True,
        "elements": elements,
    }

def save(loop):
    path = os.path.join(OUT_DIR, loop["id"] + ".json")
    with open(path, "w") as f:
        json.dump(loop, f, indent=2)
    print(f"  Created {path}")

# ── LongWall A ──────────────────────────────────────────────────
# Wall: (2,8)→(14,8) horizontal, Window at (8,8)
save(make_loop("long_wall_A", "Long Wall A", [
    wall_element("main_wall", 2, 8, 14, 8),
    window_element("window", 8, 8, on_vertical_wall=False),
]))

# ── LongWall B ──────────────────────────────────────────────────
# Wall: (1.5,7)→(13.5,7), Window(4,7), Solid(12,10)→(13.5,11.5)
save(make_loop("long_wall_B", "Long Wall B", [
    wall_element("main_wall", 1.5, 7, 13.5, 7),
    window_element("window", 4, 7, on_vertical_wall=False),
    solid_element("debris", 12, 10, 13.5, 11.5),
]))

# ── ShortWall A ─────────────────────────────────────────────────
# Split wall: (5,8)→(7,8) + (9,8)→(11,8), Pallet(8,8)
save(make_loop("short_wall_A", "Short Wall A", [
    wall_element("wall_left", 5, 8, 7, 8),
    wall_element("wall_right", 9, 8, 11, 8),
    pallet_element("pallet", 8, 8, on_vertical_wall=False),
]))

# ── ShortWall B ─────────────────────────────────────────────────
# Wall: (4.5,7)→(9.5,7) + (11.5,7)→(13,7), Pallet(10.5,7)
save(make_loop("short_wall_B", "Short Wall B", [
    wall_element("wall_left", 4.5, 7, 9.5, 7),
    wall_element("wall_right", 11.5, 7, 13, 7),
    pallet_element("pallet", 10.5, 7, on_vertical_wall=False),
]))

# ── LWallWindow A ──────────────────────────────────────────────
# Vert wall: (4,3)→(4,13), Horiz wall: (4,13)→(10,13), Window(4,8)
save(make_loop("L_wall_window_A", "L-Wall Window A", [
    wall_element("wall_vert", 4, 3, 4, 13),
    wall_element("wall_horiz", 4, 13, 10, 13),
    window_element("window", 4, 8, on_vertical_wall=True),
]))

# ── LWallWindow B ──────────────────────────────────────────────
# Vert wall: (12,3)→(12,13), Horiz wall: (6,3)→(12,3), Window(12,8)
save(make_loop("L_wall_window_B", "L-Wall Window B", [
    wall_element("wall_vert", 12, 3, 12, 13),
    wall_element("wall_horiz", 6, 3, 12, 3),
    window_element("window", 12, 8, on_vertical_wall=True),
]))

# ── LWallWindow C ──────────────────────────────────────────────
# Vert wall: (3,2)→(3,12), Horiz: (3,12)→(9,12), Window(3,7), Solid(10,5)→(11.5,7)
save(make_loop("L_wall_window_C", "L-Wall Window C", [
    wall_element("wall_vert", 3, 2, 3, 12),
    wall_element("wall_horiz", 3, 12, 9, 12),
    window_element("window", 3, 7, on_vertical_wall=True),
    solid_element("debris", 10, 5, 11.5, 7),
]))

# ── LWallPallet A ──────────────────────────────────────────────
# Vert: (4,3)→(4,12), Horiz split: (4,12)→(6,12)+(8,12)→(10,12), Pallet(7,12)
save(make_loop("L_wall_pallet_A", "L-Wall Pallet A", [
    wall_element("wall_vert", 4, 3, 4, 12),
    wall_element("arm_left", 4, 12, 6, 12),
    wall_element("arm_right", 8, 12, 10, 12),
    pallet_element("pallet", 7, 12, on_vertical_wall=False),
]))

# ── LWallPallet B ──────────────────────────────────────────────
# Vert: (12,4)→(12,13), Horiz split: (6,4)→(8,4)+(10,4)→(12,4), Pallet(9,4)
save(make_loop("L_wall_pallet_B", "L-Wall Pallet B", [
    wall_element("wall_vert", 12, 4, 12, 13),
    wall_element("arm_left", 6, 4, 8, 4),
    wall_element("arm_right", 10, 4, 12, 4),
    pallet_element("pallet", 9, 4, on_vertical_wall=False),
]))

# ── TWalls A ───────────────────────────────────────────────────
# Horiz split: (2,8)→(10,8)+(12,8)→(14,8), Vert stem: (8,8)→(8,14)
# Window(5,8), Pallet(11,8)
save(make_loop("T_walls_A", "T-Walls A", [
    wall_element("bar_left", 2, 8, 10, 8),
    wall_element("bar_right", 12, 8, 14, 8),
    wall_element("stem", 8, 8, 8, 14),
    window_element("window", 5, 8, on_vertical_wall=False),
    pallet_element("pallet", 11, 8, on_vertical_wall=False),
]))

# ── TWalls B ───────────────────────────────────────────────────
# Vert split: (8,2)→(8,10)+(8,12)→(8,14), Horiz stem: (8,8)→(14,8)
# Window(8,5), Pallet(8,11)
save(make_loop("T_walls_B", "T-Walls B", [
    wall_element("bar_top", 8, 2, 8, 10),
    wall_element("bar_bottom", 8, 12, 8, 14),
    wall_element("stem", 8, 8, 14, 8),
    window_element("window", 8, 5, on_vertical_wall=True),
    pallet_element("pallet", 8, 11, on_vertical_wall=True),
]))

# ── TWalls C ───────────────────────────────────────────────────
# Horiz split: (3,6)→(9.5,6)+(11.5,6)→(13,6), Vert: (8,2)→(8,6)
# Window(5.5,6), Pallet(10.5,6), Solid(2,10)→(3.5,12)
save(make_loop("T_walls_C", "T-Walls C", [
    wall_element("bar_left", 3, 6, 9.5, 6),
    wall_element("bar_right", 11.5, 6, 13, 6),
    wall_element("stem", 8, 2, 8, 6),
    window_element("window", 5.5, 6, on_vertical_wall=False),
    pallet_element("pallet", 10.5, 6, on_vertical_wall=False),
    solid_element("debris", 2, 10, 3.5, 12),
]))

# ── GymBox A ──────────────────────────────────────────────────
# Left: (4,4)→(4,12), Right split: (12,4)→(12,7)+(12,9)→(12,12)
# Top split: (4,12)→(8,12)+(10,12)→(12,12), Bottom: (4,4)→(12,4)
# Window(4,8), Pallet(12,8)
save(make_loop("gym_box_A", "Gym Box A", [
    wall_element("wall_left", 4, 4, 4, 12),
    wall_element("wall_right_top", 12, 4, 12, 7),
    wall_element("wall_right_bot", 12, 9, 12, 12),
    wall_element("wall_top_left", 4, 12, 8, 12),
    wall_element("wall_top_right", 10, 12, 12, 12),
    wall_element("wall_bottom", 4, 4, 12, 4),
    window_element("window", 4, 8, on_vertical_wall=True),
    pallet_element("pallet", 12, 8, on_vertical_wall=True),
]))

# ── GymBox B ──────────────────────────────────────────────────
# Left: (5,5)→(5,11), Right: (11,5)→(11,11)
# Top: (5,11)→(11,11), Bot split: (5,5)→(7,5)+(9,5)→(11,5)
# Window(11,8), Pallet(8,5)
save(make_loop("gym_box_B", "Gym Box B", [
    wall_element("wall_left", 5, 5, 5, 11),
    wall_element("wall_right", 11, 5, 11, 11),
    wall_element("wall_top", 5, 11, 11, 11),
    wall_element("wall_bot_left", 5, 5, 7, 5),
    wall_element("wall_bot_right", 9, 5, 11, 5),
    window_element("window", 11, 8, on_vertical_wall=True),
    pallet_element("pallet", 8, 5, on_vertical_wall=False),
]))

# ── DebrisPile A ──────────────────────────────────────────────
# Solid: (3,6)→(5,10), (7,4)→(9,7), (10,9)→(13,11)
save(make_loop("debris_pile_A", "Debris Pile A", [
    solid_element("rock1", 3, 6, 5, 10),
    solid_element("rock2", 7, 4, 9, 7),
    solid_element("rock3", 10, 9, 13, 11),
]))

# ── DebrisPile B ──────────────────────────────────────────────
# Solids: (4,3)→(6.5,5.5), (5.5,7)→(6.5,11), (8.5,7)→(10,11), (11,3)→(13,5)
# Pallet(7.5,9) in corridor between the two tall solids
save(make_loop("debris_pile_B", "Debris Pile B", [
    solid_element("rock_nw", 4, 3, 6.5, 5.5),
    solid_element("corridor_left", 5.5, 7, 6.5, 11),
    solid_element("corridor_right", 8.5, 7, 10, 11),
    solid_element("rock_ne", 11, 3, 13, 5),
    pallet_element("pallet", 7.5, 9, on_vertical_wall=False),
]))

print("\nDone! All v2 loop JSON assets generated.")
