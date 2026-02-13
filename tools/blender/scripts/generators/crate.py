from __future__ import annotations

import bpy

from .base import AssetGenerator
from core.material import make_basic_material


class CrateGenerator(AssetGenerator):
    name = "crate"
    category = "props"
    target_tris = (2000, 1200, 500)

    def create_high_mesh(self, entry):
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.5))
        obj = bpy.context.object
        obj.name = f"{entry.name}_HIGH"
        obj.scale = entry.scale

        bevel = obj.modifiers.new(name="Bevel", type="BEVEL")
        bevel.width = 0.03
        bevel.segments = 2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=bevel.name)
        return obj

    def create_material(self, obj, entry):
        mat = make_basic_material(f"{entry.name}_mat", (0.38, 0.25, 0.14, 1.0), 0.7)
        obj.data.materials.append(mat)
