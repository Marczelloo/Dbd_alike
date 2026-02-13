# DECISIONS.md - Architecture Decisions Rationale

## Terror Radius: Stepped Bands vs Gradient

### Decision
Use stepped bands for TR audio instead of distance-based gradient.

### Rationale
1. **DBD Fidelity**: Dead by Daylight uses distinct "heart beat" stages that are stepped, not smooth
2. **Gameplay Clarity**: Players can clearly tell which band they're in
3. **Performance**: Simpler logic, no per-frame intensity calculations
4. **Audio Design**: Each layer can have its own character (Far = slow thump, Close = fast pounding)

### Trade-offs
- Pro: Clear gameplay feedback (players know exactly where they stand)
- Con: Less smooth audio transitions (mitigated by 0.15-0.35s smoothing)

## TR Smoothing Duration

### Decision
Use 0.15-0.35s crossfade only on band transitions.

### Rationale
1. **Audio Quality**: Prevents clicking/popping when switching bands
2. **Gameplay Accuracy**: Short smoothing keeps the stepped feel intact
3. **DBD-like**: Matches the "heart beat acceleration" feel

## Bloodlust: Immediate Reset vs Decay

### Decision
Reset bloodlust to tier 0 immediately on hit/stun/pallet break/chase end.

### Rationale
1. **DBD Mechanics**: In DBD, bloodlust resets completely when these events occur
2. **Killer Counterplay**: Survivors can deliberately reset bloodlust by forcing pallet breaks
3. **Simplicity**: No complex decay curves to tune

### Trade-offs
- Pro: Clear reset points, strategic for survivors
- Con: Punishes killer heavily (requires new 35s chase for max tier)

## Bloodlust: Server-Authoritative vs Local Compute

### Decision (Current Implementation)
Compute bloodlust locally for now; defer server replication.

### Rationale
1. **Simplicity**: Full networking overhaul not in scope for this phase
2. **Performance**: No extra network bandwidth
3. **Local Testing**: Allows immediate testing without multiplayer setup

### Future Work
- Make bloodlust tier server-authoritative
- Replicate tier in Snapshot
- Clients read replicated value, don't compute independently

## Chase: Center FOV Constraint (+-35deg)

### Decision
Require survivor to be within +-35deg of killer's forward to START chase.

### Rationale
1. **DBD Mechanics**: Killer must be "looking at" survivor to start chase
2. **Counterplay**: Survivors can break LOS without triggering chase by staying peripheral
3. **Clarity**: Clear visual feedback when chase actually starts

### Trade-offs
- Pro: Prevents accidental chase activation
- Con: Survivors can "orbit" killer just outside center FOV

## Chase: Timeout Values (8s)

### Decision
Use exactly 8.0s for both lost LOS and lost center FOV timeouts.

### Rationale
1. **DBD Reference**: Matches Dead by Daylight's chase lose timing
2. **Gameplay Balance**: Long enough for brief jukes, not so long that chase feels unfair
3. **Consistency**: Single timeout value for both conditions

## Constants Summary

| Constant | Value | Source |
|----------|--------|--------|
| Killer FOV | 87deg (half 43.5deg) | CLAUDE.md spec |
| Center FOV | +-35deg | CLAUDE.md spec |
| Chase start distance | <= 12m | CLAUDE.md spec |
| Chase end distance | >= 18m | CLAUDE.md spec |
| Lost LOS timeout | 8.0s | CLAUDE.md spec |
| Lost center FOV timeout | 8.0s | CLAUDE.md spec |
| TR base radius | 32m | CLAUDE.md spec |
| TR band thresholds | 0.33R, 0.66R | CLAUDE.md spec |
| Bloodlust tier 1 | 15s -> 120% | CLAUDE.md spec |
| Bloodlust tier 2 | 25s -> 125% | CLAUDE.md spec |
| Bloodlust tier 3 | 35s -> 130% | CLAUDE.md spec |

## Scratch Marks & Blood Pools: Deterministic RNG (2026-02-12)

### Decision
Use position-based deterministic hash function instead of std::mt19937 for VFX spawning.

### Rationale
1. **Multiplayer Consistency**: Same input (position) produces same output across all peers
2. **No Per-Frame RNG**: Eliminates visual flicker in rendering
3. **Ring Buffer**: Fixed-size array eliminates heap allocations
4. **FixedUpdate Integration**: Timing is frame-rate independent

### Implementation
```cpp
float DeterministicRandom(const glm::vec3& position, int seed) {
    // Hash-based deterministic random [0, 1)
    unsigned int hash = seed + position.x*1000 + position.y*1000*8 + position.z*1000*16;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    return (hash % 10000) / 10000.0f;
}
```

### Trade-offs
- Pro: Fully deterministic, no desync between peers
- Pro: Zero heap allocations (ring buffer)
- Con: Less "random" than true RNG (but sufficient for VFX)

## Scratch Marks: Distance Threshold (2026-02-12)

### Decision
Require minimum distance between scratch mark spawns.

### Rationale
1. **DBD Accuracy**: Scratch marks shouldn't stack in same location
2. **Performance**: Fewer marks to update/render
3. **Visual Quality**: Better spacing, more readable trail

### Constants
- `minDistanceFromLast = 0.3m` - minimum horizontal distance before next spawn

## Blood Pools: Distance Threshold (2026-02-12)

### Decision
Require minimum distance between blood pool spawns.

### Rationale
1. **DBD Accuracy**: Blood pools shouldn't stack excessively
2. **Performance**: Fewer pools to update/render
3. **Visual Quality**: More natural bleeding trail

### Constants
- `minDistanceFromLast = 0.5m` - minimum horizontal distance before next spawn

## Chase: Full State Replication (2026-02-12)

### Decision
Replicate complete `ChaseState` in `Snapshot` for multiplayer.

### Rationale
1. **Server Authority**: Only server computes chase state
2. **Consistency**: All clients see identical chase behavior
3. **Audio/UI Sync**: Terror radius audio and HUD elements use replicated state

### Replicated Fields
- `chaseActive` (was already replicated)
- `chaseDistance` (was already replicated)
- `chaseLos` (was already replicated)
- `chaseInCenterFOV` (NEW)
- `chaseTimeSinceLOS` (NEW)
- `chaseTimeSinceCenterFOV` (NEW)
- `chaseTimeInChase` (NEW)
- `bloodlustTier` (NEW)

### Trade-offs
- Pro: Full determinism, no client-side desync
- Pro: Simplifies client logic (just read replicated state)
- Con: Slightly larger network packets (negligible)

## VFX: Ring Buffer Pooling (2026-02-12)

### Decision
Use fixed-size `std::array` with head pointer instead of `std::vector`.

### Rationale
1. **Zero Heap Allocations**: No push_back/erase during gameplay
2. **Cache Friendly**: Contiguous memory layout
3. **Predictable Memory**: Fixed upper bound on VFX count

### Constants
- `kScratchMarkPoolSize = 64` - max simultaneous scratch marks
- `kBloodPoolPoolSize = 32` - max simultaneous blood pools

## Items/Add-ons/Powers: Data-Driven Catalog + Modifier Context (2026-02-12)

### Decision
Use JSON-driven catalog (`items/addons/powers/characters`) with aggregated modifier contexts instead of hardcoding IDs in gameplay flow.

### Rationale
1. **Scalability**: New item/power/add-on does not require editing central gameplay switch blocks.
2. **Separation of concerns**: Definitions live in assets; runtime logic reads parameters and hook modifiers.
3. **Multiplayer safety**: Host snapshot carries selected IDs and runtime state (charges/traps), clients render/apply replicated state.
4. **Extensibility**: Same pattern can later be reused for perks/status effects/events.

### Implementation Notes
- `game/gameplay/LoadoutSystem.*`:
  - `GameplayCatalog` (load + default asset bootstrap)
  - `LoadoutSurvivor` / `LoadoutKiller`
  - `AddonModifierContext` (stat/hook aggregation)
- `GameplaySystems`:
  - loadout selection APIs
  - runtime item/power update loops
  - bear trap entity/state lifecycle

### Trade-offs
- Pro: Minimal ifology in core gameplay loops.
- Pro: Add-ons can combine add/mul/override consistently.
- Con: More runtime validation needed for mismatched add-on targets.

## Bear Trap Power: Server-Authoritative Trap State (2026-02-12)

### Decision
Trap trigger/escape logic runs in gameplay authority loop; trap state replicated in snapshot (`Armed/Triggered/Disarmed`, attempts/chance/trapped entity).

### Rationale
1. **Correctness**: Avoids divergent escape outcomes between peers.

## Profiler Threading Metrics: Frame-Averaged Utilization (2026-02-14)

### Decision
Keep instantaneous worker count for debugging, but treat frame-averaged utilization as the primary threading metric in profiler UI.

### Rationale
1. **Snapshot bias**: End-of-frame sampling often reports near-zero active workers even during heavy parallel work.
2. **Operator clarity**: Frame-average metrics better match user-observed behavior and benchmark results.
3. **Low overhead**: Cumulative busy-time accounting per worker job is simple and cheap.

### Trade-offs
- Pro: Much more trustworthy threading telemetry.
- Con: Adds tiny timing bookkeeping around each job execution.

## High-Poly Benchmark Visibility Policy (2026-02-14)

### Decision
Use strict frustum culling for high-poly benchmark meshes (no edge-buffer fallback), with 3 distance tiers and capped full-detail draw count.

### Rationale
1. **Off-screen cost control**: Edge fallback was still spending CPU/GPU budget when object not visible.
2. **Near-camera quality**: Raised full-detail range prevents overly aggressive detail drop.
3. **Scalability**: Full-detail cap (nearest-first) avoids catastrophic frame spikes in dense clusters.

### Trade-offs
- Pro: Better worst-case FPS stability in stress scenes.
- Con: Potentially more visible LOD transitions at extreme camera sweeps (mitigated by medium LOD tier).
2. **Debuggability**: Host can inspect trap states (`power_dump`, HUD debug fields).
3. **Future ready**: Can later move to dedicated net messages if packet budget needs optimization.

### Add-on Examples
- `serrated_jaws`: hook modifier on trap escape (`bleed_multiplier`).
- `tighter_springs`: stat modifiers (`escape_chance_step`, `max_escape_attempts`).

## Blender Asset Generation: Modular CLI + Generator Registry (2026-02-13)

### Decision
Move toward a modular Blender pipeline with a single CLI entrypoint and generator registry, while keeping legacy scripts for compatibility.

### Rationale
1. **Scalability**: Adding a new asset type means adding one generator class instead of cloning large scripts.
2. **Maintainability**: Shared concerns (scene setup, decimation, bake setup, export, validation) are centralized in `core/*`.
3. **Incremental builds**: Timestamp-based skip avoids full regeneration on unchanged configs.
4. **Hardware compatibility**: Bake setup attempts GPU backends then falls back to CPU safely.

### Trade-offs
- Pro: Faster iteration and clearer ownership of pipeline responsibilities.
- Con: Initial migration period with both legacy and modular scripts coexisting.

### Follow-up
- Add hash-based cache manifest for stronger invalidation than mtime.
- Expand modular baking path to output full texture sets and metadata.

## Benchmark Map: Zone-Based Design vs Monolithic (2026-02-13)

### Decision
Use 15 distinct testing zones with clear purposes instead of a single heterogeneous map.

### Rationale
1. **Debuggability**: When a collision bug occurs, the zone immediately indicates what geometry pattern is problematic.
2. **Benchmarking**: FPS/performance can be correlated to specific zone types (pillar forest = instancing, grid = broadphase).
3. **Testing**: Each zone tests a specific game mechanic (step-up, acute angles, LOS, pallet cycling).
4. **Visual variety**: Different colored zones make the map more interesting and easier to navigate.

### Zone Design Principles
- **Corner Corridors**: 1.2m width = minimum viable for capsule navigation
- **Spiral Maze**: Continuous wall curvature tests collision response smoothness
- **Pillar Forest**: ~160 pillars tests broadphase efficiency and draw call batching
- **Acute Corners**: 30° V-angle is the extreme case where collision response can fail
- **Concentric Rings**: Cardinal gaps test radial LOS with multiple occluders

### Trade-offs
- Pro: Clear test boundaries, easy to isolate problems
- Pro: Good visual benchmark with varied geometry complexity
- Con: Not representative of actual gameplay maps (intentionally extreme)

## Threading: Scoped Waits via JobCounter (2026-02-13)

### Decision
Use per-task-group `JobCounter` waits (e.g. culling pass) instead of global `JobSystem::WaitForAll()` in frame-critical paths.

### Rationale
1. **Isolation**: Frame culling no longer blocks on unrelated async jobs (asset loader, debug jobs, etc.).
2. **Latency**: Reduces long-tail stalls and frame time spikes under queue contention.
3. **Determinism**: Wait scope is explicit and tied to the exact jobs created by that subsystem.

### Trade-offs
- Pro: Better frame pacing and less cross-system interference.
- Con: Slightly more bookkeeping (`JobCounter` creation/passing) per parallel pass.

## High-Poly Render LOD Proxy (2026-02-13)

### Decision
For benchmark high-poly meshes, render full geometry only within a near distance threshold; use oriented-box proxy farther away.

### Rationale
1. **CPU reduction**: Avoids per-frame CPU vertex transform/push for distant heavy meshes.
2. **GPU reduction**: Cuts fragment/triangle load in high-coverage scenes.
3. **Visual stability**: Maintains clear scene silhouette with cheap proxy geometry.

### Trade-offs
- Pro: Significant perf gain on stress map and camera sweeps.
- Con: Distant geometry fidelity is intentionally reduced.
