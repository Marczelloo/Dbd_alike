# Implementation Plan — Audio + Terror Radius + Scratch Marks + Blood Pools

## Short-Term Implementation Order (Next 1-2 Hours)

1. **FIX BUILD** (5 min)
   - Delete redundant `PlayLoop` declaration in AudioSystem.hpp:47
   - Verify clean build

2. **SMOKE TEST AUDIO** (15 min)
   - Run `audio_play tr_far music` - verify sound
   - Run `audio_loop tr_close music` - verify looping
   - Run `audio_stop_all` - verify silence
   - Test terror radius crossfade (move killer near survivor)

3. **IMPLEMENT TR DEBUG HUD** (30 min)
   - Add `App::RenderTerrorRadiusDebug()` overlay
   - Show: distance, intensity, layer volumes
   - Wire `tr_debug on|off` to toggle visibility

4. **STABILIZE & DOCUMENT** (20 min)
   - Verify Audio Settings UI works
   - Update README with any missing console commands
   - Commit working audio state

---

## Detailed Implementation Steps

### P0.1. Fix PlayLoop Ambiguity
**File:** `engine/audio/AudioSystem.hpp:47`
**Action:** Delete the redundant `PlayLoop` declaration
```cpp
// DELETE THIS LINE:
SoundHandle PlayLoop(const std::string& clipName, Bus bus, const PlayOptions& options);
```
**Rationale:** Line 46 already has `float loopDurationSeconds = 0.0F` as default parameter
**Test:** Rebuild, verify compilation succeeds
**Estimated time:** 1 minute

### P0.2. Verify Build
**Action:** Run `cmake --build build -j 8`
**Expected:** Clean build with no errors
**If fails:** Check for any other calls that need updating

---

## Phase A — Stabilize Audio + Terror Radius (Finish Baseline)

### A1. Audio File Setup
**Files:** `assets/audio/*.wav` (new)
**Action:**
- Create placeholder audio files or document required filenames
- Add `assets/audio/README.md` listing required terror radius clips:
  - `tr_far.wav` - distant terror sound
  - `tr_mid.wav` - mid-range tension
  - `tr_close.wav` - close proximity heartbeat/intensity
  - `tr_chase.wav` - chase music layer (chaseOnly)
**Test:**
- Run `audio_play tr_far music` - verify sound plays
- Run `audio_loop tr_close music` - verify looping
**Docs:** Update README.md with audio asset checklist

### A2. Verify tr_debug Command
**Files:** `engine/core/App.cpp`
**Lines to check:** ConsoleContext wiring for `setTerrorAudioDebug`
**Action:**
- Verify callback is properly wired in `BuildConsoleContext()`
- Add HUD overlay text when debug is enabled showing:
  - Current distance between killer/survivor
  - Intensity value (0-1)
  - Each layer's current volume
**Test:**
- Run solo session, move killer near survivor
- Type `tr_debug on`, observe HUD values
- Verify values change smoothly with distance
**Docs:** Document `tr_debug` in README console commands section

### A3. Audio Settings UI Verification
**Files:** `engine/core/App.cpp` (settings UI)
**Action:**
- Verify Audio tab in Settings UI is functional
- Test master/music/sfx/ui/ambience sliders
- Verify saving persists to `config/audio.json`
**Test:**
- Open Settings → Audio tab
- Adjust sliders, close settings, reopen - verify persisted
**Fix if needed:** Any issues with slider→audio→save flow

### A4. Add Audio Console Commands to README
**Files:** `README.md`
**Action:**
- Add audio commands to console commands list:
  - `audio_play <clip> [bus]`
  - `audio_loop <clip> [bus]`
  - `audio_stop_all`
  - `tr_debug on|off`
  - `tr_vis on|off` (already documented, add tr_debug nearby)
  - `tr_set <meters>` (already documented)

---

## Phase B — Scratch Marks + Blood Pools (Gameplay VFX)

### B1. Scratch Mark System Design

#### B1.1. Create Scratch Profile Asset
**Files:** `assets/fx/scratches_profile.json` (new)
**Schema:**
```json
{
  "asset_version": 1,
  "enabled": true,
  "spawn_interval_seconds": 0.35,
  "max_scratches_per_survivor": 24,
  "fade_duration_seconds": 45.0,
  "injured_multiplier": 1.8,
  "asset_id": "scratch_mark_trail"
}
```

#### B1.2. Create Scratch Trail FX Asset
**Files:** `assets/fx/scratch_mark_trail.json` (new)
**Based on:** Reference `dust_puff.json` or `chase_aura.json`
**Key parameters:**
- `type: Trail`
- `blendMode: Alpha`
- `lifetime: 45.0` (long fade)
- `colorOverLife`: Reddish/blood tint fading to transparent
- `sizeOverLife`: Start ~0.15, end ~0.05

#### B1.3. Implement Scratch Spawning in GameplaySystems
**Files:** `game/gameplay/GameplaySystems.hpp|.cpp`
**New members:**
```cpp
struct ScratchMarkState {
    glm::vec3 lastFootPosition{0.0F};
    double lastSpawnSeconds = 0.0F;
};
ScratchMarkState m_survivorScratches;
```

**Action:** Add `UpdateScratchMarks(float fixedDt)` called from `FixedUpdate()`
- Track survivor position
- When sprinting and moving:
  - Calculate distance from `lastFootPosition`
  - If distance > threshold (~1.2m) and time interval passed:
    - Spawn scratch trail FX at foot position
    - Update `lastFootPosition`
- Use `SpawnGameplayFx("scratch_mark_trail", position, forward, FxNetMode::Local)`

#### B1.4. Scratch Console Commands
**Files:** `ui/DeveloperConsole.cpp`
**Add:**
- `scratches_debug on|off` - toggle debug visualization
- `scratches_clear` - remove all active scratches
- Update CommandCategoryForUsage to include these in Debug

### B2. Blood Pool System Design

#### B2.1. Create Blood Pool FX Asset
**Files:** `assets/fx/blood_puddle.json` (new)
**Based on:** Reference `blood_spray.json`
**Key parameters:**
- `type: Sprite`
- `blendMode: Alpha`
- `lifetime: 180.0` (3 minute fade)
- Burst of particles on ground plane
- Red/dark color gradient

#### B2.2. Implement Blood Pool Spawning
**Files:** `game/gameplay/GameplaySystems.cpp`
**Trigger:** In `ApplySurvivorHit()` or transition to `SurvivorHealthState::Injured`
**Action:**
- Spawn FX at survivor position: `m_fxSystem.Spawn("blood_puddle", position, ...)`
- Use `FxNetMode::ServerBroadcast` for multiplayer sync
- Limit max puddles (e.g., 12) - recycle oldest via FxSystem budget

#### B2.3. Blood Console Commands
**Files:** `ui/DeveloperConsole.cpp`
**Add:**
- `blood_debug on|off` - toggle debug info
- `blood_clear` - remove all blood pools

### B3. Verification Tests

**Multiplayer Test:**
1. Host session
2. Join from client
3. Host hits survivor
4. Verify blood pool appears on both host and client
5. Client sprints away
6. Host sees scratch marks appearing

**Performance Test:**
1. Spawn 20+ scratch marks rapidly
2. Spawn 12 blood pools
3. Verify FPS > 60
4. Check `fx_cpu_ms` in debug overlay

---

## Phase C — Documentation Updates (After Each Phase)

### C1. Update README.md
**Section to add: "Audio Assets"**
```markdown
## Audio Assets

Place audio files in `assets/audio/`:
- `tr_far.wav` - Terror radius far layer
- `tr_mid.wav` - Terror radius mid layer
- `tr_close.wav` - Terror radius close layer
- `tr_chase.wav` - Terror radius chase layer (only during chase)

Formats supported: .wav, .ogg, .mp3, .flac
```

**Section to add: "VFX Systems"**
```markdown
## Visual Effects

### Scratch Marks
- Survivor sprinting leaves red scratch marks on ground
- Client-side cosmetic (spawned locally per client)
- Config: `assets/fx/scratches_profile.json`

### Blood Pools
- Spawned when survivor is hit (Injured state)
- Server-authoritative, replicated to all clients
- Fade out after ~3 minutes

### Console Commands (VFX)
- `scratches_debug on|off` - Toggle scratch debug
- `scratches_clear` - Remove all scratches
- `blood_debug on|off` - Toggle blood debug
- `blood_clear` - Remove all blood pools
- `fx_spawn <assetId>` - Spawn FX at camera
- `fx_list` - List available FX assets
```

### C2. Update CLAUDE_NOTES.md
- Move completed items from TODO to COMPLETED section
- Add any new findings during implementation

### C3. Update TRACE.md
- Add any new issues discovered during testing
- Document reproduction steps for bugs found

---

## File Change Summary (Estimate)

| Phase | Files Modified | Files Created |
|--------|---------------|----------------|
| A1-A4 | `README.md`, `App.cpp` (minor) | `assets/audio/*.wav` or README |
| B1 | `GameplaySystems.hpp|.cpp`, `DeveloperConsole.cpp` | `assets/fx/scratches_profile.json`, `scratch_mark_trail.json` |
| B2 | `GameplaySystems.cpp`, `DeveloperConsole.cpp` | `assets/fx/blood_puddle.json` |
| C | `README.md`, `CLAUDE_NOTES.md`, `TRACE.md` | — |

---

## Testing Checklist (Before Considering Phase Complete)

### Audio / Terror Radius
- [ ] `audio_play tr_far music` produces sound
- [ ] `audio_loop tr_close music` loops indefinitely
- [ ] `audio_stop_all` silences all audio
- [ ] Terror radius crossfade smooth when killer approaches
- [ ] Chase layer activates only during chase
- [ ] `tr_debug on` shows debug values in HUD
- [ ] Settings → Audio sliders persist after restart

### Scratch Marks
- [ ] Scratch marks appear when survivor sprints
- [ ] Scratch marks fade over time
- [ ] `scratches_clear` removes all scratches
- [ ] Scratch marks visible to both players in multiplayer

### Blood Pools
- [ ] Blood pools appear when survivor is hit
- [ ] Blood pools fade after ~3 minutes
- [ ] Blood pools appear on both host and client
- [ ] `blood_clear` removes all blood pools

### Performance
- [ ] FPS > 60 with max effects
- [ ] `fx_cpu_ms` < 1.0ms typically
- [ ] No memory leaks (long runtime test)
