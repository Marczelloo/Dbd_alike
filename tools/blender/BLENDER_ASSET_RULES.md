# Blender Asset Generation Rules

## General

- Blender must be run headless only (`-b -P`).
- All assets are procedural (no external textures).
- Materials must be generated using shader nodes (Noise, Musgrave, Voronoi).
- Geometry must be generated procedurally.

## Mesh

- 1 Blender unit = 1 meter.
- Object name must be `ASSET`.
- Apply all transforms before export.
- Prefer low to medium poly unless specified.

## Materials & Textures

- Use Principled BSDF.
- Procedural shading first, then bake textures.
- Moss/lichen must be generated using:
  - Noise textures
  - Position or normal-based masks (top-facing surfaces)
- No image textures allowed before baking.

## Baking

- UV unwrap using Smart UV Project.
- Bake:
  - Albedo (color only)
  - Normal (OpenGL)
  - Roughness
- Texture size default: 1024x1024 PNG.
- Save baked textures to:
  `out/assets/<asset_name>/`

## Export

- Export as GLB.
- Y-up, -Z forward.
- Export only object named `ASSET`.

## Preview

- Render preview using Eevee.
- Resolution: 512x512.
- Save to `out/previews/<asset_name>.png`.

## Logging

- Print "OK:" messages after each major step.
