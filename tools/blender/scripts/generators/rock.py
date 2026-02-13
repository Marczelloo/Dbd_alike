from __future__ import annotations

import random

import bpy

from .base import AssetGenerator
from core.material import make_basic_material


class RockGenerator(AssetGenerator):
    name = "rock"
    category = "environment"
    target_tris = (7000, 3500, 1200)

    def create_high_mesh(self, entry):
        random.seed(entry.seed)
        bpy.ops.mesh.primitive_ico_sphere_add(
            subdivisions=5,
            radius=0.9,
            location=(0.0, 0.0, 0.75),
        )
        obj = bpy.context.object
        obj.name = f"{entry.name}_HIGH"
        obj.scale = entry.scale
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

        tex = bpy.data.textures.new(f"{entry.name}_noise", type="CLOUDS")
        tex.noise_scale = 1.5
        dis = obj.modifiers.new(name="RockDisplace", type="DISPLACE")
        dis.texture = tex
        dis.strength = 0.2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=dis.name)
        bpy.ops.object.shade_smooth()
        return obj

    def create_material(self, obj, entry):
        color = (0.34, 0.32, 0.30, 1.0) if entry.variant == "default" else (0.30, 0.35, 0.28, 1.0)
        mat = make_basic_material(f"{entry.name}_mat", color, 0.88)
        obj.data.materials.append(mat)
