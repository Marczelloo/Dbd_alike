from __future__ import annotations

import math

import bpy


def reset_scene() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)


def setup_preview_scene() -> None:
    bpy.ops.object.light_add(type="AREA", location=(3, -3, 4))
    light = bpy.context.object
    light.data.energy = 800

    bpy.ops.object.camera_add(location=(3, -3, 2), rotation=(math.radians(65), 0, math.radians(45)))
    bpy.context.scene.camera = bpy.context.object
