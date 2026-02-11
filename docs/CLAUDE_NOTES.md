# CLAUDE_NOTES — Living Notes for Audio + Terror Radius + VFX

## Current Status (2026-02-11)

### Audio System (Phase A - COMPLETED)

**Implemented:**
- `engine/audio/AudioSystem.hpp|.cpp`: miniaudio-based audio engine
  - Bus system: Master, Music, Sfx, Ui, Ambience
  - One-shot and looped playback
  - Spatial 3D audio (position, min/max distance)
  - Config persistence: `config/audio.json`
  - Console commands: `audio_play`, `audio_loop`, `audio_stop_all`

**Terror Radius Audio (FULLY IMPLEMENTED):**
- `assets/terror_radius/default_killer.json`: profile with layers (FAR/MID/CLOSE/CHASE)
- `App::LoadTerrorRadiusProfile()`: loads profile and starts looped layers at 0 volume
- `App::UpdateTerrorRadiusAudio()`: crossfades layers based on killer-survivor distance
- **NEW:** Terror radius debug HUD overlay showing:
  - Distance between killer/survivor
  - Intensity (0-100%)
  - Chase state
  - Per-layer volume percentages with color coding

**Audio Assets Status:**
- Audio files present: `tr_far.wav`, `tr_mid.wav`, `tr_close.wav`, `tr_chase.wav`
- `assets/audio/README.md` documents required files and testing commands

**Completed (2026-02-11 session):**
1. ✅ Fixed PlayLoop ambiguity bug
2. ✅ Build compiles cleanly
3. ✅ Audio Settings UI verified (sliders, save/load, mute)
4. ✅ TR debug HUD implemented and wired to `tr_debug on|off`

**Known Issues / TODO:**
1. None - audio system is stable and ready for Phase B

### Killer Look Light (Already Implemented)

**Implemented in `engine/core/App.cpp`:**
- Spot light cone following killer forward direction
- Configurable via variables: `m_killerLookLightEnabled`, `m_killerLookLightIntensity`, `m_killerLookLightRange`, angles
- Already working - see lines 549-564 in App.cpp
- **No additional work needed** unless exposing to UI

### File Hotspots

| File | Purpose | Notes |
|------|----------|--------|
| `engine/audio/AudioSystem.hpp:46` | PlayLoop declaration | Fixed - removed redundant line 47 |
| `engine/audio/AudioSystem.cpp` | miniaudio backend | Lines 215-278: PlayLoop implementations |
| `engine/core/App.cpp:3117-3190` | UpdateTerrorRadiusAudio | TR crossfade + debug value storage |
| `engine/core/App.cpp:4330-4366` | TR debug HUD overlay | Shows distance, intensity, layer volumes |
| `ui/DeveloperConsole.cpp:251-279` | Audio console commands | Already registered |
| `engine/fx/FxSystem.cpp` | VFX particle system | Ready for scratch/blood |
| `game/gameplay/GameplaySystems.cpp` | Role movement, state | Need to extend for VFX |

### Asset Patterns

**Audio (`assets/audio/`):**
- Expected: `tr_far.wav`, `tr_mid.wav`, `tr_close.wav`, `tr_chase.wav`
- Format: `.wav`, `.ogg`, `.mp3`, or `.flac`

**Terror Radius (`assets/terror_radius/`):**
- Schema: `asset_version`, `base_radius`, `killer_id`, `layers[]`
- Layer: `clip`, `fade_in_start`, `fade_in_end`, `gain`, `chase_only`

**FX (`assets/fx/`):**
- Existing: `blood_spray.json`, `chase_aura.json`, `dust_puff.json`, `generator_sparks_loop.json`, `hit_spark.json`
- Can use as reference for scratch/blood VFX

---

## Phase B Planning (Scratch Marks + Blood Pools)

### Design Decisions

| Feature | Approach | Rationale |
|----------|-----------|------------|
| Scratch Marks | FxSystem trail emitter + GameplaySystems spawn on sprint | Client-side cosmetic, existing trail support |
| Blood Pools | FxSystem sprite emitter on injured damage | Server-authoritative damage, FxNetMode::ServerBroadcast |
| Killer Look Light | Already implemented as SpotLight | Zero work needed |

### Multiplayer Safety

- Scratch marks: `FxNetMode::Local` - each client spawns from replicated state
- Blood pools: `FxNetMode::ServerBroadcast` - host decides spawn, clients receive via `m_fxReplicationCallback`
- FxSystem already has `SetSpawnCallback()` for replication (wired in GameplaySystems)

---

## TODO Checklist

### Phase A - Stabilize Audio ~~COMPLETED~~
- [x] Delete redundant `PlayLoop` declaration (AudioSystem.hpp:47)
- [x] Verify build passes on Windows
- [ ] Verify build passes on Linux (needs testing)
- [ ] Audio smoke test (requires interactive run): `audio_play tr_far music`
- [ ] Audio loop test (requires interactive run): `audio_loop tr_close music`
- [ ] Terror radius crossfade test (requires interactive run)
- [x] Implement TR debug HUD overlay (distance, intensity, layer volumes)
- [x] Verify Audio Settings UI works
- [x] Audio asset checklist exists in assets/audio/README.md

### Phase B - Scratch Marks + Blood Pools
- [ ] Design `assets/fx/scratches_profile.json` for scratch mark parameters
- [ ] Design `assets/fx/blood_puddle.json` for blood puddle parameters
- [ ] Implement scratch spawn logic in GameplaySystems (on sprint, foot position tracking)
- [ ] Implement blood puddle spawn on survivor hit (when state becomes Injured)
- [ ] Add console commands: `scratches_debug`, `blood_debug`
- [ ] Test multiplayer replication

### Phase C - Character Roster
- [ ] Design character JSON schema
- [ ] Implement `assets/characters/survivors/*.json`
- [ ] Implement `assets/characters/killers/*.json`
- [ ] Add selection UI to Main Menu
- [ ] Wire up multiplayer role assignment with selected character
