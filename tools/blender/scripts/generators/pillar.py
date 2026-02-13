from __future__ import annotations

import bpy

from .base import AssetGenerator
from core.material import make_basic_material


class PillarGenerator(AssetGenerator):
    name = "pillar"
    category = "props"
    target_tris = (2500, 1400, 700)

    def create_high_mesh(self, entry):
        bpy.ops.mesh.primitive_cylinder_add(radius=0.3, depth=2.0, location=(0.0, 0.0, 1.0), vertices=24)
        obj = bpy.context.object
        obj.name = f"{entry.name}_HIGH"
        obj.scale = entry.scale
        bpy.ops.object.shade_smooth()
        return obj

    def create_material(self, obj, entry):
        mat = make_basic_material(f"{entry.name}_mat", (0.55, 0.55, 0.58, 1.0), 0.6)
        obj.data.materials.append(mat)
