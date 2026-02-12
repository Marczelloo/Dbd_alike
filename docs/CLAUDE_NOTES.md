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
