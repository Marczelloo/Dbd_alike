"""
Generate the V2 map JSON with v2 loop placements on an 8x8 tile grid.
Map is 128x128 world units (8 tiles * 16 units/tile).
"""
import json, os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "maps", "map_v2.json")

def p(loop_id, tx, ty, rot=0):
    return {"loop_id": loop_id, "tile": [tx, ty], "rotation_degrees": rot, "transform_locked": False}

# Layout plan (8x8 grid):
# FEW pallets, more window/stone loops for variety
# Only 1-2 pallets total, more gym_box and L_wall_window loops
# ~42 placements for good density.
placements = [
    # Row 0 — top edge: strong/medium border
    p("gym_box_A",       0, 0, 0),
    p("T_walls_A",       2, 0, 0),
    p("L_wall_window_A", 3, 0, 0),
    p("long_wall_A",     4, 0, 0),
    p("T_walls_B",       6, 0, 0),
    p("gym_box_B",       7, 0, 90),

    # Row 1
    p("L_wall_window_B", 0, 1, 0),
    p("short_wall_A",    1, 1, 0),
    p("T_walls_C",       3, 1, 0),
    p("short_wall_B",    5, 1, 90),
    p("L_wall_window_C", 7, 1, 0),

    # Row 2
    p("gym_box_B",       1, 2, 0),
    p("long_wall_B",     2, 2, 0),
    p("L_wall_pallet_A", 5, 2, 0),  # ONLY PALLET 1
    p("debris_pile_B",   6, 2, 0),

    # Row 3 — mid area: gym + windows
    p("L_wall_window_A", 0, 3, 90),
    p("gym_box_A",       3, 3, 0),
    p("long_wall_A",     7, 3, 0),

    # Row 4 — mid area mirror
    p("debris_pile_A",   0, 4, 0),
    p("gym_box_B",       4, 4, 180),
    p("L_wall_window_B", 7, 4, 180),

    # Row 5
    p("debris_pile_B",   1, 5, 270),
    p("long_wall_B",     2, 5, 180),
    p("T_walls_C",       5, 5, 270),
    p("T_walls_A",       6, 5, 180),

    # Row 6
    p("L_wall_window_C", 0, 6, 0),
    p("gym_box_A",       2, 6, 180),
    p("short_wall_A",    4, 6, 270),
    p("short_wall_B",    6, 6, 270),
    p("long_wall_A",     7, 6, 180),

    # Row 7 — bottom edge: strong/medium border
    p("gym_box_B",       0, 7, 180),
    p("T_walls_C",       1, 7, 180),
    p("L_wall_window_A", 3, 7, 270),
    p("long_wall_B",     4, 7, 270),
    p("T_walls_B",       5, 7, 270),
    p("gym_box_A",       7, 7, 180),
]

# Count elements for report
from collections import Counter
counts = Counter(p_["loop_id"] for p_ in placements)
print(f"Total placements: {len(placements)} / 64 tiles")
print("Loop type distribution:")
for k, v in sorted(counts.items()):
    print(f"  {k}: {v}")

# Spawns at opposite ends of the map
# Map center is (0,0), extends ±64 in each direction
# Survivor in top-left area, killer in bottom-right
map_data = {
    "asset_version": 1,
    "name": "map_v2",
    "grid": {
        "width": 8,
        "height": 8,
        "tile_size": 16.0
    },
    "placements": placements,
    "props": [
        # Scattered rocks/trees for visual interest in empty tiles
        {"type": "rock", "position": [8.0, 0.85, 8.0],
         "half_extents": [0.9, 0.9, 0.9], "yaw_degrees": 0.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "rock", "position": [-24.0, 0.85, -8.0],
         "half_extents": [1.1, 0.8, 1.0], "yaw_degrees": 30.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "tree", "position": [24.0, 0.85, 24.0],
         "half_extents": [0.6, 1.6, 0.6], "yaw_degrees": 0.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "tree", "position": [-40.0, 0.85, 32.0],
         "half_extents": [0.6, 1.6, 0.6], "yaw_degrees": 0.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "obstacle", "position": [32.0, 0.85, -24.0],
         "half_extents": [1.2, 1.0, 0.7], "yaw_degrees": 45.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "tree", "position": [-8.0, 0.85, -24.0],
         "half_extents": [0.6, 1.6, 0.6], "yaw_degrees": 0.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "rock", "position": [40.0, 0.85, -40.0],
         "half_extents": [0.8, 0.7, 0.9], "yaw_degrees": 15.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
        {"type": "obstacle", "position": [-32.0, 0.85, 8.0],
         "half_extents": [1.0, 0.8, 0.6], "yaw_degrees": 0.0,
         "pitch_degrees": 0.0, "roll_degrees": 0.0,
         "solid": True, "transform_locked": False},
    ],
    "spawns": {
        "survivor": [-48.0, 1.05, -48.0],
        "killer":   [48.0, 1.05, 48.0]
    }
}

with open(OUT, "w") as f:
    json.dump(map_data, f, indent=2)
print(f"\nMap saved to {OUT}")
