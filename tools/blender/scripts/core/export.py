from __future__ import annotations

from pathlib import Path

import bpy


def export_glb(out_path: str, selected_only: bool = True) -> None:
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(out),
        export_format="GLB",
        use_selection=selected_only,
        export_apply=True,
        export_yup=True,
    )
