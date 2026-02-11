# CLAUDE_NOTES — Living Notes for Audio + Terror Radius + VFX

## Current Status (2026-02-11)

### Audio System (Phase A - WIP)

**Implemented:**
- `engine/audio/AudioSystem.hpp|.cpp`: miniaudio-based audio engine
  - Bus system: Master, Music, Sfx, Ui, Ambience
  - One-shot and looped playback
  - Spatial 3D audio (position, min/max distance)
  - Config persistence: `config/audio.json`
  - Console commands: `audio_play`, `audio_loop`, `audio_stop_all`

**Terror Radius Audio (Partially Implemented):**
- `assets/terror_radius/default_killer.json`: profile with layers (FAR/MID/CLOSE/CHASE)
- `App::LoadTerrorRadiusProfile()`: loads profile and starts looped layers at 0 volume
- `App::UpdateTerrorRadiusAudio()`: crossfades layers based on killer-survivor distance
- Console: `tr_debug on|off` exists but not fully wired to App

**Known Issues / TODO:**
1. No actual audio files in `assets/audio/` - cannot test playback yet
2. `tr_debug` toggle registered in console but not verified working
3. Need to add default audio asset documentation to README
4. Settings UI: Audio tab exists in settings but needs verification

### Killer Look Light (Already Implemented)

**Implemented in `engine/core/App.cpp`:**
- Spot light cone following killer forward direction
- Configurable via variables: `m_killerLookLightEnabled`, `m_killerLookLightIntensity`, `m_killerLookLightRange`, angles
- Already working - see lines 549-564 in App.cpp
- **No additional work needed** unless exposing to UI

### File Hotspots

| File | Purpose | Notes |
|------|----------|--------|
| `engine/audio/AudioSystem.cpp` | miniaudio backend | New, WIP |
| `engine/core/App.cpp:2719-3137` | Audio config + TR logic | Main integration |
| `ui/DeveloperConsole.cpp:251-279` | Audio console commands | Already registered |
| `engine/fx/FxSystem.cpp` | VFX particle system | Ready for scratch/blood |
| `game/gameplay/GameplaySystems.cpp` | Role movement, state | Need to extend |

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

### Phase A - Stabilize Audio
- [ ] Verify audio playback on Windows (need audio files)
- [ ] Verify Linux audio (if cross-platform testing available)
- [ ] Test `tr_debug on|off` console command
- [ ] Verify Audio Settings UI tab works
- [ ] Add audio asset checklist to README
- [ ] Save default `assets/audio/` placeholder files or document required filenames

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
