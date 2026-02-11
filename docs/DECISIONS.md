# DECISIONS — Architecture and Design Decisions Log

## Build Error Fix (2026-02-11) - COMPLETED

### Decision: Remove redundant PlayLoop declaration
**Issue:** Ambiguous overload between two `PlayLoop` declarations
**Action:** Deleted line 47 from AudioSystem.hpp (redundant declaration)
**Rationale:** Line 46 already has `loopDurationSeconds = 0.0F` default parameter
**Files affected:** `engine/audio/AudioSystem.hpp`, `engine/audio/AudioSystem.cpp`

---

## Terror Radius Debug HUD (2026-02-11) - COMPLETED

### Decision: Integrate TR debug into main HUD overlay
**Approach:** Add debug panel directly in `DrawInGameHudCustom()` when `m_terrorAudioDebug == true`
**Rationale:** Keeps debug info visible during gameplay without separate panel
**Display:**
- Distance (meters) and base radius
- Intensity percentage (0-100%)
- Chase state (ON/OFF)
- Per-layer volumes with color coding

**Alternative rejected:**
- Separate ImGui debug window: Would break custom UI consistency

---

## Audio System

### Decision: Use miniaudio instead of other libraries
**Date:** 2026-02-11 (by previous agent)
**Rationale:**
- Single-header library, easy integration
- Cross-platform (Windows, Linux, macOS)
- Supports 3D spatial audio out of the box
- Permissive license (public domain/Unlicense)

**Alternatives considered:**
- SDL_mixer: Extra dependency, we already use GLFW
- OpenAL: More verbose API, requires additional loader for common formats
- FMOD: Commercial license needed for distribution

**Tradeoffs:**
- miniaudio is lower-level - more code for features we might need (e.g. custom effects)
- We accept this for simplicity of integration

---

## Terror Radius Audio Design

### Decision: Layered crossfade approach
**Rationale:**
- Multiple layers (FAR/MID/CLOSE/CHASE) allow smooth transitions
- Crossfade based on distance prevents audio "pops"
- Chase-only layer adds tension when killer is actively pursuing

**Implementation:**
- Distance-based intensity: `clamp(1 - distance/baseRadius, 0, 1)`
- Each layer has `fadeInStart` and `fadeInEnd` for crossfade ranges
- Chase layer only active when `chaseActive == true`

**Tradeoffs:**
- Requires 4 audio files per killer
- Memory: 4 simultaneous looped streams (acceptable for modern hardware)

---

## Scratch Marks Implementation

### Decision: FxSystem trail emitter for scratch marks
**Rationale:**
- Trail emitter already supports continuous line segments
- Client-side cosmetic: no server bandwidth cost
- Fade-out built into FxSystem alphaOverLife curve

**Multiplayer Safety:**
- `FxNetMode::Local` - each client spawns from survivor position
- Position is replicated in snapshot, so scratch marks appear roughly correct
- Small desync acceptable for cosmetic effect

**Alternative rejected:**
- Server-authoritative scratch marks: Would add packet spam for cosmetic-only effect

---

## Blood Pools Implementation

### Decision: Server broadcast for blood pools
**Rationale:**
- Blood pools indicate damage state - gameplay-relevant information
- Must be synchronized across all clients
- FxSystem already supports `FxNetMode::ServerBroadcast`

**Implementation:**
- Spawn on survivor state change: Healthy → Injured
- On survivor hit, trigger Fx spawn at position
- FxSystem calls `m_spawnCallback` which App forwards to network

**Tradeoffs:**
- Adds network traffic per hit
- Acceptable: hit events are infrequent (~1-2 per second max)

---

## Character Roster Design

### Decision: Data-driven JSON characters
**Rationale:**
- Consistent with existing asset patterns (loops, maps, terror_radius)
- Easy to add new characters without code changes
- `asset_version` allows future schema migration

**Schema (planned):**
```json
{
  "asset_version": 1,
  "id": "survivor_meg",
  "display_name": "Meg Thomas",
  "model_path": "assets/models/survivor_meg.gltf",
  "role": "survivor",
  "perks": [...],
  "cosmetics": {...}
}
```

**Multiplayer:**
- Host authoritative for final assignment
- Client sends preferred character in handshake
- Host can override (e.g., duplicate character selection)

---

## Killer Look Light

### Decision: Implemented as SpotLight in renderer
**Date:** Already implemented before 2026-02-11
**Rationale:**
- Renderer already supports SpotLight type
- Reuses existing lighting system
- Performance: GPU-based, no extra draw calls

**Configuration:**
- `m_killerLookLightEnabled`: toggle
- `m_killerLookLightIntensity`: brightness multiplier
- `m_killerLookLightRange`: max distance
- `m_killerLookLightInnerDeg`/`OuterDeg`: cone angles

**Future consideration:**
- Expose to Gameplay Tuning UI for player configuration
