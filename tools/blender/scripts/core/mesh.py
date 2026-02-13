from __future__ import annotations

import bpy


def count_tris(obj: bpy.types.Object) -> int:
    return sum(max(0, len(poly.vertices) - 2) for poly in obj.data.polygons)


def apply_decimate(obj: bpy.types.Object, target_tris: int) -> None:
    current = max(1, count_tris(obj))
    ratio = max(0.02, min(1.0, float(target_tris) / float(current)))
    mod = obj.modifiers.new(name="auto_decimate", type="DECIMATE")
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def ensure_poly_budget(obj: bpy.types.Object, max_high_tris: int = 50000) -> int:
    current = count_tris(obj)
    if current > max_high_tris:
        apply_decimate(obj, max_high_tris)
    return count_tris(obj)
