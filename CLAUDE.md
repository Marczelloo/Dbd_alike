# Blender automation in this repo (Claude Code)

## Goal

Generate simple procedural assets in Blender headless mode and export them to GLB + preview PNG.

## Rules

- Never use Blender GUI; always run headless (`-b`) via `tools/blender/run_blender.ps1`.
- All Blender scripts live in `tools/blender/scripts/`.
- All outputs go to `out/`:
  - `.blend` and `.glb` -> `out/assets/`
  - previews -> `out/previews/`
- Scripts must accept args only after `--` (handled by `run_blender.ps1`).
- Scripts should be idempotent: start from factory settings or open provided `.blend`.

## Commands

Generate an asset (.blend):

- `.\tools\blender\run_blender.ps1 -Script .\tools\blender\scripts\make_asset.py -Args @("<type>","out\assets\<name>.blend")`

Export GLB:

- `.\tools\blender\run_blender.ps1 -Script .\tools\blender\scripts\export_glb.py -Args @("out\assets\<name>.blend","out\assets\<name>.glb")`

Render preview:

- `.\tools\blender\run_blender.ps1 -Script .\tools\blender\scripts\render_preview.py -Args @("out\assets\<name>.blend","out\previews\<name>.png")`

## Asset types

- crate
- pillar
- rock
  (Add more in `make_asset.py`.)

  ## Asset Optimization (MANDATORY)

All Blender-generated assets MUST be optimized for real-time rendering.

### Geometry Rules

- NEVER generate sculpt-level or ultra-high-density meshes.
- Geometry is for silhouette and major forms ONLY.
- Fine surface detail MUST come from textures (normal / height maps).

### Polycount Targets

- Environment props (large): 3,000 – 8,000 triangles (LOD0)
- Medium props: 1,000 – 4,000 triangles
- Small props: < 1,500 triangles

Exceeding these limits is NOT allowed unless explicitly requested.

### Displacement Workflow

- Displacement modifiers may be used ONLY for shaping.
- After shaping:
  - Apply modifiers.
  - Reduce geometry using Decimate or equivalent optimization.
- Displacement must NOT remain active on final mesh.

### Optimization Techniques (Required)

- Apply all transforms.
- Use Decimate modifier (or equivalent) to reduce polycount.
- Preserve silhouette and large shape readability.
- Do NOT add geometry to fix surface detail issues — rebake textures instead.

### Iteration Rule

If the asset lacks detail or looks flat:

- Adjust procedural materials.
- Improve normal / height / roughness maps.
- DO NOT increase geometry density.

### Validation

Before export, assets should be:

- Visually correct at target polycount.
- Suitable for real-time OpenGL rendering.

  ## LOD & Collision Rules (MANDATORY)

All environment assets MUST include LODs and a collision mesh
unless explicitly disabled.

### LOD Generation

- Assets must include at least 3 LOD levels:
  - LOD0: base mesh (full quality, optimized)
  - LOD1: ~50% triangle reduction
  - LOD2: ~20–25% of LOD0 triangles

### LOD Rules

- LODs must be generated automatically using:
  - Decimate modifier or equivalent.
- LODs must preserve silhouette as much as possible.
- No material or UV changes between LODs.
- LOD meshes must be named:
  - ASSET_LOD0
  - ASSET_LOD1
  - ASSET_LOD2

### Triangle Budgets (Guideline)

- LOD0: 3,000 – 8,000 tris (large environment prop)
- LOD1: ~1,500 – 4,000 tris
- LOD2: ~600 – 1,500 tris

---

## Collision Mesh Rules

### Collision Mesh

- Every environment asset must include a collision mesh.
- Collision mesh must be:
  - Simple
  - Low-poly
  - Convex where possible

### Collision Creation

- Generate collision mesh by:
  - duplicating LOD2 or LOD1
  - further decimating and simplifying
- Remove small details, overhangs, and noise.

### Collision Naming

- Collision object must be named:
  - ASSET_COLLIDER

### Collision Constraints

- Collision mesh must:
  - NOT use materials
  - NOT be exported as visible geometry
  - Be suitable for real-time physics in OpenGL

---

## Export Rules (LOD & Collision)

- Export format: GLB only.
- Export must include:
  - ASSET_LOD0
  - ASSET_LOD1
  - ASSET_LOD2
- Collision mesh (ASSET_COLLIDER) may be exported if needed by the engine
  or excluded if collision is generated engine-side.
- Apply all transforms before export.
- Y-up, forward = -Z.

---

## Asset Validation & Reporting (REQUIRED)

Before final export, Claude must print a validation report:

- Triangle count per mesh:
  - LOD0
  - LOD1
  - LOD2
  - COLLIDER
- Texture resolution and count
- Total texture memory estimate
- Confirmation that optimization rules were followed

Example:

Asset Validation Report:

- LOD0: 6,842 tris
- LOD1: 3,214 tris
- LOD2: 1,102 tris
- Collider: 248 tris
- Textures: 5 × 2048x2048 PNG
- Status: OK (real-time safe)

Speed optimization required.

Right now the mesh is ~245k faces which is unacceptable and makes baking extremely slow.
Update the pipeline:

1. Create two meshes:
   - ASSET_HIGH: used only as bake source (can be detailed but keep it reasonable, avoid >200k faces)
   - ASSET_LOD0: the real-time mesh (target 6k–10k tris for this large rock)

2. Do NOT UV unwrap ASSET_HIGH. UV unwrap ONLY ASSET_LOD0.

3. Bake using Cycles "selected to active":
   - Select ASSET_HIGH then ASSET_LOD0 (active)
   - Bake maps to ASSET_LOD0 images:
     - Albedo (diffuse color only)
     - Normal (OpenGL)
     - Roughness
     - Height (optional)
   - Use low bake samples (16–32) and print progress heartbeats.

4. Add a hard poly budget clamp:
   - After shaping, decimate ASSET_LOD0 until it is under target tris.
   - Log final tri counts for HIGH and LOD0.

5. For iteration runs, default bake resolution to 1024 unless user explicitly requests 2048.
   Provide an option/arg --res 1024/2048.

Do not increase geometry density to fix quality; improve textures instead.
