# CLAUDE_NOTES.md

## Current Status

As of 2026-02-12, the following refactoring has been completed:

### Completed (2026-02-12)
- **Scratch Marks (Phase 2 Refactor)**
  - Ring buffer pooling (64 max, no heap allocations)
  - Deterministic position-based RNG (no more std::mt19937)
  - Moved to FixedUpdate() for frame-rate independence
  - Distance threshold (0.3m) to prevent stacking
  - Removed RNG from render loop (no more visual flicker)

- **Blood Pools (Phase 3 Refactor)**
  - Ring buffer pooling (32 max, no heap allocations)
  - Deterministic position-based RNG
  - Moved to FixedUpdate()
  - Distance threshold (0.5m) to prevent stacking

- **Chase System (Phase 4 Refactor)**
  - Full state replication in Snapshot:
    - `chaseInCenterFOV`
    - `chaseTimeSinceLOS`
    - `chaseTimeSinceCenterFOV`
    - `chaseTimeInChase`
    - `bloodlustTier`
  - Server-authoritative for multiplayer
  - Assertions for negative timer values

- **Bug Fixes**
  - Fixed duplicate field declarations in DeveloperConsole.hpp
  - Fixed duplicate function definitions in GameplaySystems.cpp

### Previous (2025-02-11)
- **Chase State Machine (DBD-like)**
  - FOV: 87deg total (half 43.5deg), center FOV +-35deg
  - Start distance <= 12m, end distance >= 18m
  - Lost LOS timeout 8s, lost center FOV timeout 8s
  - Console: `chase_force on|off`, `chase_dump`

- **Stepped Terror Radius Audio**
  - Bands: Outside (>R), FAR (0.66R < dist <= R), MID (0.33R < dist <= 0.66R), CLOSE (0 <= dist <= 0.33R)
  - Default radius: 32m
  - Chase override: tr_chase ON, tr_close SUPPRESSED
  - Smoothing: 0.15-0.35s on transitions only
  - Console: `tr_dump`, `tr_radius <m>`

- **Bloodlust System (DBD-like)**
  - Tier 1: 15s -> 120% speed
  - Tier 2: 25s -> 125% speed
  - Tier 3: 35s -> 130% speed
  - Resets on: hit, stun, pallet break, chase end
  - Console: `bloodlust_reset`, `bloodlust_set <0-3>`, `bloodlust_dump`

### File Hotspots

| File | Purpose | Key Areas |
|------|---------|-----------|
| game/gameplay/GameplaySystems.hpp | Chase/Bloodlust/Scratch/Blood state structures | ChaseState, BloodlustState, ScratchMark, BloodPool |
| game/gameplay/GameplaySystems.cpp | Core gameplay logic | UpdateChaseState, UpdateBloodlust, UpdateScratchMarks, UpdateBloodPools |
| engine/core/App.cpp | Network serialization | SerializeSnapshot, DeserializeSnapshot |
| ui/DeveloperConsole.cpp | Console commands | chase_dump, tr_dump, bloodlust_dump |

## TODO List

- Test multiplayer with new chase state replication
- Verify scratch marks and blood pools look correct at different frame rates
- Add visual indicator for bloodlust tier (optional)

## Known Issues

- None currently

## Update (2026-02-13): Performance branch progress

### Completed runtime optimisations
- Screen vignette migrated from multi-rect UI draw to fullscreen shader path.
- Physics broadphase now uses spatial hash candidate filtering in collision/raycast/LOS.
- Reduced per-frame allocations in `StaticBatcher` and FX billboard/trail paths.
- Reduced repeated HUD state construction in app frame flow.

### Completed tooling optimisation (Blender)
- Added modular generation pipeline:
  - `tools/blender/scripts/core/*`
  - `tools/blender/scripts/generators/*`
  - `tools/blender/scripts/cli.py`
- Added JSON-driven generation config: `config/assets.json`.
- Added batch entry script: `tools/blender/generate_batch.ps1`.

### Next TODO
- Hook existing legacy scripts (`make_*.py`) into modular CLI wrapper path.
- Expand baking outputs (albedo/normal/roughness export in modular pipeline).
- Add cache manifest (`.cache/manifest.json`) with per-asset hash keys.

## Update (2026-02-13): FPS Limit Fix

### Fixed
- **FPS limit not respected when VSync enabled**
  - Root cause: dual throttling (`VSync` + manual sleep limiter) caused uneven frame pacing and perceived stutter.
  - Fix: manual FPS limiter runs only when `VSync` is OFF; when `VSync` is ON, swap interval pacing is used.
  - Improvement: limiter uses a steadier `steady_clock` schedule (`sleep_until` + short yield window) to reduce jitter when uncapped by VSync.
  - Fix (follow-up): FPS/profiler timing now measures frame duration **after** limiter wait, so displayed FPS reflects actual presented cadence.
  - Fix (follow-up 2, Windows): enable high-resolution timer (`timeBeginPeriod(1)`) during app run and remove busy spin/yield tail from limiter. This reduces CPU usage and stabilizes cap accuracy.
  - File: `engine/core/App.cpp` main loop frame timing

## Update (2026-02-14): Profiler + High-Poly + Editor API follow-up

### Completed
- **Profiler threading metrics corrected**
  - Added frame-averaged worker utilization (`frameWorkerUtilizationPct`) and average active workers (`frameAverageActiveWorkers`) in `JobSystem::GetStats()`.
  - Profiler overlay now shows snapshot workers separately from frame-average metrics to avoid false "threading idle" reads.

- **High-poly benchmark rendering stabilized**
  - Removed high-poly edge-buffer fallback draws when meshes are outside frustum.
  - Added medium LOD geometry generation per high-poly spawn type.
  - Raised near full-detail distance and capped number of full-detail meshes rendered per frame (nearest-first).
  - Result: significantly lower off-screen high-poly cost and less aggressive detail collapse.

- **Model draw API unified for editor modes**
  - Added shared helper `DrawModelAssetInstance(...)` used by `PropType::MeshAsset` rendering.
  - Map/loop editor preview now uses one mesh-asset draw path (uniform scale + textured surface fallback behavior aligned).

### File hotspots
- `engine/core/JobSystem.hpp/.cpp`
- `engine/core/Profiler.hpp/.cpp`
- `engine/ui/ProfilerOverlay.cpp`
- `game/gameplay/GameplaySystems.hpp/.cpp`
- `game/editor/LevelEditor.hpp/.cpp`

## Update (2026-02-14): Deep Rendering Optimization Pass (34+ items)

Branch: `performance-optimisations`

### Profiler & Testing Infrastructure
- Complete ImGui profiler overlay (`engine/core/Profiler.hpp/cpp`, `engine/core/ProfilerOverlay.hpp/cpp`)
- Console: `perf`, `perf_pin`, `perf_compact`, `benchmark`, `benchmark_stop`
- Automated perf test commands: `perf_test`, `perf_report`
- VBO byte stats, draw call tracking, frustum culling stats

### Bug Fixes During Optimization Pass
- **Crash on map enter** — `operator*()` on empty `std::optional<HudState>`. `FinishLoading()` changed `m_appMode` to `InGame` mid-frame but `frameHudState` was never populated. Fixed with `has_value()` guard. (commit `9c263f9`)
- **Back-face culling broke all rendering** — `glEnable(GL_CULL_FACE)` with `GL_CCW` front face culled everything: UI quads (Y-flip in shader reverses winding), box geometry (cross products yield inward normals). Reverted entirely. A full winding order audit required before culling can be re-enabled. (commit `aa7f389`)

### GPU Optimizations
1. ~~**Back-face culling**~~ — REVERTED: geometry pipeline has mixed CW/CCW winding, culled all UI + most 3D faces
2. **Fragment shader distSq early-out** — `dot(toLight,toLight) < range*range` avoids sqrt for out-of-range lights
3. **Fragment shader fused inversesqrt** — single `inversesqrt(distSq)` replaces separate `length()+normalize()`
4. **Removed redundant normalize** — `-l` instead of `normalize(-toLight)` for spot lights; removed `normalize(spotDir)` (CPU pre-normalizes)
5. **VBO GL_STREAM_DRAW** — correct usage hint for per-frame data
6. **VBO buffer orphaning** — `glBufferData(nullptr)` every frame prevents GPU sync stalls
7. **Removed redundant glPolygonMode(GL_FILL)** — was called after line pass but fill was already active
8. **Removed GL state cleanup** — `glBindVAO(0)/glUseProgram(0)/glBindBuffer(0)` at end of frame not needed

### CPU Vertex Generation
9. **Capsule LOD by distance** — far capsules use fewer segments (huge vertex reduction)
10. **Inlined box normals** — eliminates 12 cross product + normalize per box
11. **Inlined oriented box normals** — same optimization with rotation matrix
12. **Inlined capsule normals** — analytical normals for cylinder body (radial) and hemispheres (sphere surface), pre-computed sin/cos tables, proper winding reversal for bottom hemisphere
13. **Blood pools: DrawBox** — replaced `DrawOrientedBox()` with zero rotation (eliminates rotation matrix computation)
14. **Cached scratch mark yaw** — `atan2()` computed once at spawn, not every frame

### CPU Data Handling
15. **Frustum culling** — all dynamic objects culled against view frustum
16. **Light uniform deduplication** — computed once, lambda uploads to both shader programs
17. **Spot lights vector caching** — avoid per-frame heap allocation
18. **Dirty-flag physics rebuild** — skip redundant `RebuildPhysicsWorld` when nothing changed
19. **Eliminated double BuildHudState** — was called twice per frame
20. **HudState move semantics** — `std::move` instead of copy for toolbar state
21. **QueryCapsuleTriggers buffer reuse** — output param avoids per-call allocations
22. **BuildId: string concat** — replaced `std::ostringstream` (30-60 heap allocs/frame) with `std::string::clear()+append()`
23. **SetSpotLights/SetPointLights move overloads** — avoid vector copy at call site
24. **FX curve 2-key fast path** — direct interpolation for the most common case (fade-out curves)

### Rendering Pipeline
25. **UI single VBO upload** — merged batch uploads into one
26. **Line pass merged** — shared shader/VAO binding for regular + overlay lines
27. **Line+overlay single VBO upload** — concatenated at different offsets, single orphan call
28. **Billboard dead code removed** — cleaned up unused billboard rendering paths
29. **FX system skip emitter copy** — no param copy when no overrides

### Physics & Gameplay
30. **SphereCastTriggers buffer reuse** — output-param overload avoids per-call vector alloc+sort
31. **Interaction resolve dedup** — reuse resolved candidate instead of computing 3× per tick
32. **Spatial grid no full-scan fallback** — removed O(n) fallback when spatial query returns empty
33. **LOS distance guard** — skip expensive HasLineOfSight raycast when distance > 20m
34. **Incremental physics update** — eliminated per-tick full `RebuildPhysicsWorld()` (~200+ entity rebuild); killer chase trigger updated in-place via `UpdateTriggerCenter()`; full rebuild only on structural changes (pallet drop/break, trap placement)
35. **Aspect ratio compute once** — single computation before render branches

### Deferred (diminishing returns)
- UBO for light uniforms: ~36 glUniform calls per frame → ~1% frame budget, high std140 complexity
- Grid static VBO caching: 484 line verts/frame, negligible impact
- `glMapBufferRange`: driver-dependent vs current cross-platform approach

## Audit: Items/Add-ons/Killer Powers (2026-02-12)

### Findings

- There is an existing, modular `PerkSystem` in:
  - `game/gameplay/PerkSystem.hpp`
  - `game/gameplay/PerkSystem.cpp`
- Existing perk architecture already provides:
  - loadout struct (`PerkLoadout`)
  - active runtime states (`ActivePerkState`)
  - effect aggregation (`PerkEffect`)
  - role separation (survivor/killer)
- Gameplay integration already consumes modifier-like effects from perks (speed/terror/heal/repair etc.).
- There is no dedicated data model yet for:
  - survivor items with 0..2 add-ons
  - killer power with 0..2 add-ons
  - character roster assets (survivor/killer definitions)
- Asset folders currently present do not include:
  - `assets/items/`
  - `assets/addons/`
  - `assets/powers/`
  - `assets/characters/survivors/`
  - `assets/characters/killers/`

### Gaps

- Missing contracts/interfaces:
  - `IItemBehaviour`
  - `IPowerBehaviour`
  - `IAddonModifier`
- Missing loadout contracts:
  - `LoadoutSurvivor {itemId, addonAId, addonBId}`
  - `LoadoutKiller {powerId, addonAId, addonBId}`
- Missing runtime systems:
  - item use lifecycle (charges, active state, cooldown)
  - killer power lifecycle (place/use/state machine)
  - addon modifier pipeline (parameter + hooks) without id-switch logic spread in gameplay
- Missing multiplayer replication for killer power state (trap entities/state + trapped survivor values).
- Missing console surface for item/power setup and debugging.

### Decision

- Reuse the existing perk system style (registry + loadout + active state + aggregated modifiers),
  but implement a separate data-driven framework for items/powers/add-ons to avoid mixing concerns.

## Update (2026-02-13): Benchmark Map Implementation

### New Feature: Comprehensive Benchmark Scene

**Command**: `load map benchmark`

**Purpose**: 
1. Visual benchmark for rendering performance under load
2. Collision edge case testing for physics validation
3. AI/chase scenario stress testing

**Map Statistics**:
- Arena size: 100m x 100m (2.5x larger than main map)
- 15 distinct test zones
- ~400+ collision boxes
- 16 windows, 16+ pallets
- 4 corner spawn positions

**Test Zones**:

| Zone | Name | Purpose | Elements |
|------|------|---------|----------|
| 1 | Corner Corridors | Tight corner collision | 4 L-corridors (NW, NE, SW, SE), 1.2m wide |
| 2 | Spiral Maze | Continuous collision checks | 4-ring spiral with segmented walls |
| 3 | Staircase Pyramid | Step-up/gravity test | 6-tier pyramid with 0.5m height steps |
| 4 | Pillar Forest | Rendering + broadphase | 4 rings × 16-64 pillars = ~160 pillars |
| 5 | Narrow Slalom | Capsule slide test | 10 gates, 1.8m width |
| 6 | Density Grid | Worst-case broadphase | 12×12 grid, ~100 small obstacles |
| 7 | Complex Intersection | Multi-vault scenario | Central hub, 4 windows, 4 pallets |
| 8 | Edge Case Corners | Acute angle collision | V-shaped walls at 30° angle |
| 9 | Multi-Tier Platforms | Elevation changes | 4 platforms with 1.2m height differences |
| 10 | Chaos Scatter | Random navigation | 40 random debris pieces |
| 11 | Tunnel Gallery | LOS calculations | 30m tunnel with side passages |
| 12 | Concentric Rings | Radial LOS test | 3 rings with 4 cardinal gaps each |
| 13 | Biased Steps | Slanted surfaces | 8 stepped platforms |
| 14 | Bridge Crossing | Precision movement | 20m bridge with 1.5m width |
| 15 | Pallet Gallery | Rapid pallet cycling | 3×3 pallet grid |

**Key Files Modified**:
- `game/maps/TileGenerator.hpp`: Added `GenerateBenchmarkMap()` declaration
- `game/maps/TileGenerator.cpp`: ~700 lines of zone generation code
- `game/gameplay/GameplaySystems.hpp`: Added `MapType::Benchmark`
- `game/gameplay/GameplaySystems.cpp`: Map name routing, `MapToName()` support
- `ui/DeveloperConsole.cpp`: Updated command help

**Debug Features**:
- Tile debug markers for zone visualization
- 5 generators distributed across zones
- F2 shows colliders/triggers overlay

**Testing Checklist**:
1. `load map benchmark` - Verify map loads without errors
2. Test collision in each zone (especially spiral, acute corners)
3. Verify FPS with full scene rendered (filled vs wireframe)
4. Test all windows and pallets function correctly
5. Verify LOS calculations around concentric rings and tunnel
6. Test chase scenarios through complex zones

## Update (2026-02-13): Multithreading System

### New Feature: Job-Based Threading

**Purpose**: Utilize multiple CPU cores for parallel work

**Architecture**:
- **JobSystem** (`engine/core/JobSystem.hpp/cpp`)
  - Thread pool with N workers (auto: CPU cores - 1)
  - Priority queues: High, Normal, Low
  - `ParallelFor` for data-parallel tasks
  - `JobCounter` for synchronization
  
- **RenderThread** (`engine/render/RenderThread.hpp/cpp`)
  - Command buffer for async render preparation
  - Double-buffered frame data
  
- **AsyncAssetLoader** (`engine/assets/AsyncAssetLoader.hpp/cpp`)
  - Background asset loading
  - Callback-based completion

**Actual Usage (per frame)**:
1. **No synthetic background workload** in the main game loop.
2. **Parallel culling**: HighPolyMesh culling uses `ParallelFor` only for large sets (>256 meshes).
3. **Static batch culling**: `StaticBatcher::Render` uses `ParallelFor` only for large sets (>256 chunks).
4. **Frame-local synchronization**: culling waits use dedicated `JobCounter` objects (no global `WaitForAll()` stall against unrelated jobs).

### Regression fix (2026-02-13): CPU spike + FPS drop on benchmark map

Root cause:
- Test-only per-frame `bg_work` jobs were still scheduled from `App::Run()`.
- `WaitForAll()` in render culling could block on unrelated jobs in shared queues.
- `ParallelFor` captured callable by reference, unsafe for async execution.

Fixes:
- Removed `bg_work` scheduling from game loop.
- Added counter-aware scheduling and scoped wait (`WaitForCounter`) for culling passes.
- Reworked `JobCounter` to atomic wait/notify (no spin/yield loop).
- Fixed `ParallelFor` callable lifetime (`shared_ptr` capture) and added sequential fallback for small workloads.
- Added high-poly distance LOD fallback (full mesh near camera, oriented-box proxy farther away).
- Merged contiguous textured batches with same texture in renderer.

**Console Commands**:
- `job_stats` - Show worker count, pending jobs, completed jobs
- `job_enable on|off` - Enable/disable job processing
- `job_test <N>` - Run parallel test with N iterations
- `asset_stats` - Show asset loader statistics

**Profiler Integration**:
- JobSystem stats visible in Profiler → Systems tab
- Shows: workers total, workers active, pending jobs, utilization %

**Files Modified/Added**:
- `engine/core/JobSystem.hpp/cpp` (new)
- `engine/render/RenderThread.hpp/cpp` (new)
- `engine/assets/AsyncAssetLoader.hpp/cpp` (new)
- `engine/core/App.cpp` - Initialize/shutdown, submit jobs per frame
- `engine/core/Profiler.hpp/cpp` - Threading stats in FrameStats
- `engine/ui/ProfilerOverlay.cpp` - Display threading stats
- `engine/render/StaticBatcher.cpp` - Parallel culling
- `game/gameplay/GameplaySystems.cpp` - Parallel high-poly mesh culling
- `ui/DeveloperConsole.cpp` - Threading console commands
- `CMakeLists.txt` - Added new source files

**Known Limitations**:
- OpenGL context is single-threaded (all GPU submit on main thread)
- ~~Vertex buffer building (Renderer::DrawMesh) is still sequential~~ — Solved by GPU mesh cache (see below)
- Full parallel rendering would require Vulkan/DX12 or thread-local GL contexts

## Update (2026-02-14): GPU Mesh Cache + Memory Leak Fix

Branch: `performance-optimisations`

### Root Cause Analysis

**111ms Render Submit bottleneck**: `DrawMesh()` transformed every vertex CPU-side every frame:
`position = pos + rotation * (p * scale)` per vertex, pushed into `m_solidVertices`, then
re-uploaded ~489K vertices via `glBufferSubData` each frame.

**Memory leak on map change**: `std::vector::clear()` retains allocated capacity.
High-poly MeshGeometry data (hundreds of thousands of vec3/u16 elements) stayed allocated.

**0/27 threading**: JobSystem only triggers for >256 items; real CPU work was per-vertex
transforms in DrawMesh which is inherently sequential. Eliminated entirely by GPU cache.

### GPU Mesh Cache System

New API in `engine/render/Renderer`:
- `UploadMesh(geometry)` → uploads vertex data to persistent GPU VBO+VAO (GL_STATIC_DRAW), returns `GpuMeshId`
- `DrawGpuMesh(id, modelMatrix)` → queues draw with per-instance model matrix
- `FreeGpuMesh(id)` / `FreeAllGpuMeshes()` → deletes GL resources

Solid vertex shader now uses `uniform mat4 uModel`:
- `gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0)`
- `vNormal = mat3(uModel) * aNormal`
- Identity matrix set for immediate-mode draws (backward compatible)

### High-Poly Mesh Rendering Rewrite

`RenderHighPolyMeshes()` now uses lazy GPU upload:
1. First frame: uploads geometry via `UploadMesh()`, then clears CPU-side geometry data
2. Subsequent frames: issues `DrawGpuMesh()` with model matrix (translate/rotate/scale)
3. No per-frame vertex transforms, no CPU-side geometry retained after upload

### Memory Leak Fixes

1. **Swap-to-empty pattern** in `BuildSceneFromGeneratedMap()`:
   `{ std::vector<HighPolyMesh> empty; m_highPolyMeshes.swap(empty); }` — forces RAM release
2. **GPU resource cleanup**: `FreeGpuMesh()` called for all uploaded meshes before map change
3. **PhysicsWorld::Clear()**: Added `shrink_to_fit()` for m_solids, m_triggers, scratch buffers
4. **Transient buffer shrink**: BeginFrame reclaims `m_solidVertices` capacity when >256K and >4× last frame usage

### Other Optimizations
- **DrawGrid**: halfSize reduced 60→40 (fewer grid lines drawn)
- **StaticBatcher**: passes model matrix location, sets identity for static geometry

### Files Modified
- `engine/render/Renderer.hpp` — GPU mesh cache API, GpuMeshId/GpuMeshInfo/GpuMeshDraw types
- `engine/render/Renderer.cpp` — Shader uModel uniform, GPU cache implementation, buffer shrink
- `engine/render/StaticBatcher.hpp/.cpp` — Model matrix uniform support
- `engine/physics/PhysicsWorld.cpp` — shrink_to_fit in Clear()
- `game/gameplay/GameplaySystems.hpp` — GpuMeshId fields, upload flag, renderer pointer
- `game/gameplay/GameplaySystems.cpp` — Lazy GPU upload, swap-to-empty cleanup, DrawGrid reduction
- `game/maps/TileGenerator.cpp` — Removed GridPlane "grass" from benchmark Zone 16
