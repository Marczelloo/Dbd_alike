from __future__ import annotations

from pathlib import Path

import bpy

from .bake import configure_bake_settings, setup_bake_device
from .export import export_glb
from .mesh import apply_decimate, ensure_poly_budget
from .uv import smart_unwrap
from .validation import build_report, print_report


def run_asset_pipeline(generator, entry, defaults) -> dict:
    asset_name = entry.name
    out_root = Path(defaults.output_root)
    out_root.mkdir(parents=True, exist_ok=True)

    texture_size = entry.texture_size if entry.texture_size is not None else defaults.texture_size
    bake_samples = entry.bake_samples if entry.bake_samples is not None else defaults.bake_samples
    use_gpu = entry.use_gpu if entry.use_gpu is not None else defaults.use_gpu

    high = generator.create_high_mesh(entry)
    ensure_poly_budget(high, max_high_tris=50000)
    generator.create_material(high, entry)

    bpy.ops.object.select_all(action="DESELECT")
    high.select_set(True)
    bpy.context.view_layer.objects.active = high
    bpy.ops.object.duplicate()
    lod0 = bpy.context.object
    lod0.name = f"{asset_name}_LOD0"

    smart_unwrap(lod0)

    device = setup_bake_device(use_gpu_requested=use_gpu)
    configure_bake_settings(samples=bake_samples)
    _ = texture_size  # reserved for expanded baking implementation

    bpy.ops.object.select_all(action="DESELECT")
    lod0.select_set(True)
    bpy.context.view_layer.objects.active = lod0

    bpy.ops.object.duplicate()
    lod1 = bpy.context.object
    lod1.name = f"{asset_name}_LOD1"
    apply_decimate(lod1, target_tris=max(3000, int(generator.target_tris[1])))

    bpy.ops.object.duplicate()
    lod2 = bpy.context.object
    lod2.name = f"{asset_name}_LOD2"
    apply_decimate(lod2, target_tris=max(1000, int(generator.target_tris[2])))

    bpy.ops.object.duplicate()
    collider = bpy.context.object
    collider.name = f"{asset_name}_COLLIDER"
    apply_decimate(collider, target_tris=450)
    collider.hide_render = True

    glb_path = out_root / f"{asset_name}.glb"
    bpy.ops.object.select_all(action="DESELECT")
    lod0.select_set(True)
    lod1.select_set(True)
    lod2.select_set(True)
    collider.select_set(True)
    export_glb(str(glb_path), selected_only=True)

    blend_path = out_root / f"{asset_name}.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), compress=True)

    report = build_report(asset_name, [lod0, lod1, lod2, collider])
    print_report(report)

    return {
        "asset": asset_name,
        "blend": str(blend_path),
        "glb": str(glb_path),
        "device": device,
        "lod0_tris": report.lod0_tris,
    }
