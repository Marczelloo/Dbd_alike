# TRACE — Reproduction Steps for Current Known Issues

## ~~CRITICAL: Build Failure (2026-02-11)~~ RESOLVED

### Error Summary
**Status:** RESOLVED - Build now compiles cleanly
**Error:** Ambiguous function overload in `PlayLoop`
**Locations:**
- `engine/audio/AudioSystem.hpp:46-47`
- `engine/audio/AudioSystem.cpp:217`
- `engine/core/App.cpp:889, 3085`

### Error Message (Previously)
```
error: call of overloaded 'PlayLoop(const std::string&, Bus, const PlayOptions&)' is ambiguous
note: candidate 1: 'SoundHandle PlayLoop(const std::string&, Bus, const PlayOptions&, float)'
note: candidate 2: 'SoundHandle PlayLoop(const std::string&, Bus, const PlayOptions&)'
```

### Fix Applied
1. Deleted redundant line 47 from AudioSystem.hpp
2. Updated AudioSystem.cpp implementation signature to match header
3. Added `(void)loopDurationSeconds;` to suppress unused warning

### Verification
- Build succeeds: `cmake --build build -j 8`
- All audio console commands functional
- TR debug HUD displays correctly

---

## Issue: Audio Files Missing ~~RESOLVED~~

### Expected
- Running `audio_play tr_far` should play a sound
- Terror radius should produce layered audio based on distance

### Actual (Previous State)
- Audio system initializes but no files exist in `assets/audio/`
- `ResolveClipPath()` returns empty path, `PlayOneShot()` returns handle 0

### Current Status (2026-02-11)
**RESOLVED:** Audio files now present:
- `assets/audio/tr_far.wav`
- `assets/audio/tr_mid.wav`
- `assets/audio/tr_close.wav`
- `assets/audio/tr_chase.wav`
- `assets/audio/README.md` documents usage

### Reproduction Steps (After Build Fix)
1. Fix build error (see above)
2. Build and run the game
3. Press `~` to open console
4. Type `audio_play tr_far music`
5. Verify: Audio plays

---

## Issue: tr_debug Command Not Verified ~~RESOLVED~~

### Expected
- `tr_debug on|off` should toggle debug visualization for terror radius audio
- Should show layer volumes, distance, intensity values

### Actual (Previous State)
- Command is registered in DeveloperConsole (line 715-729)
- `ConsoleContext::setTerrorAudioDebug` callback exists
- No HUD overlay to display debug values

### Fix Applied (2026-02-11)
- Added debug HUD overlay in `DrawInGameHudCustom()`
- Shows: distance, intensity, chase state, per-layer volumes
- Color-coded volumes (muted/accent/danger)

### Verification Steps
1. Run game, start solo session
2. Press `~` to open console
3. Type `tr_debug on`
4. Observe: HUD panel at top-center showing TR debug info

---

## Issue: Killer Look Light Configuration Not Exposed

### Expected
- User should be able to configure killer light (range, angle, intensity)
- Settings panel should include these options

### Actual
- Light works hardcoded (App.cpp lines 549-564)
- Variables exist: `m_killerLookLightEnabled`, etc.
- Not exposed to settings UI

### Reproduction Steps
1. Start game as killer
2. Observe light cone in front of killer
3. Try to change light range via settings
4. Result: No UI option exists

### Fix Status
- PENDING: Add Killer Light settings to Gameplay Tuning UI
- LOW PRIORITY: Feature works, just not user-configurable

---

## Smoke Test Checklist (Multiplayer)

### After Each Audio/FX Change

1. Host session on port 7777
2. Join from second instance (127.0.0.1:7777)
3. Verify:
   - [ ] Killer approaches survivor - terror radius audio changes
   - [ ] Survivor sprinting - scratch marks appear on both clients
   - [ ] Survivor hit - blood pools appear on both clients
   - [ ] Killer look light visible only to killer player
4. Check console: `net_status`, `net_dump`
5. Check `logs/network.log` for FX replication events

---

## Performance Verification

### After FX Implementation

1. Enable FPS counter (F1 overlay)
2. Spawn 20+ scratch marks rapidly
3. Spawn 10+ blood pools
4. Verify:
   - FPS stays above 60
   - No per-frame allocations in FxSystem::Update()
   - `fx_cpu_ms` in HUD remains under 1.0ms
