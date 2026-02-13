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

## Update (2026-02-14): Deep Rendering Optimization Pass (34+ items)

Branch: `performance-optimisations`

### Profiler & Testing Infrastructure
- Complete ImGui profiler overlay (`engine/core/Profiler.hpp/cpp`, `engine/core/ProfilerOverlay.hpp/cpp`)
- Console: `perf`, `perf_pin`, `perf_compact`, `benchmark`, `benchmark_stop`
- Automated perf test commands: `perf_test`, `perf_report`
- VBO byte stats, draw call tracking, frustum culling stats

### GPU Optimizations
1. **Back-face culling** — `glEnable(GL_CULL_FACE)`, halves fragment rasterization
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
