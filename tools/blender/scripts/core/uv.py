from __future__ import annotations

import bpy


def smart_unwrap(obj: bpy.types.Object, angle_limit: float = 1.3, island_margin: float = 0.02) -> None:
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=angle_limit, island_margin=island_margin)
    bpy.ops.object.mode_set(mode="OBJECT")
