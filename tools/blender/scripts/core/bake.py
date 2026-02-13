from __future__ import annotations

import bpy


def setup_bake_device(use_gpu_requested: bool) -> str:
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"

    if not use_gpu_requested:
        scene.cycles.device = "CPU"
        return "CPU"

    addon = bpy.context.preferences.addons.get("cycles")
    if addon is None:
        scene.cycles.device = "CPU"
        return "CPU"

    prefs = addon.preferences
    backend_names = ("OPTIX", "CUDA", "HIP", "METAL", "ONEAPI")
    for backend in backend_names:
        try:
            prefs.compute_device_type = backend
            prefs.get_devices()
            if any(device.type == backend and getattr(device, "use", True) for device in prefs.devices):
                scene.cycles.device = "GPU"
                return f"GPU_{backend}"
        except Exception:
            continue

    scene.cycles.device = "CPU"
    return "CPU"


def configure_bake_settings(samples: int) -> None:
    scene = bpy.context.scene
    scene.cycles.samples = max(1, samples)
    scene.cycles.use_denoising = True
    scene.render.bake.use_pass_direct = False
    scene.render.bake.use_pass_indirect = False
    scene.render.bake.margin = 4
