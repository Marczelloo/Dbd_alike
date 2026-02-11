# TRACE — Reproduction Steps for Current Known Issues

## Issue: Audio Files Missing

### Expected
- Running `audio_play tr_far` should play a sound
- Terror radius should produce layered audio based on distance

### Actual
- Audio system initializes but no files exist in `assets/audio/`
- `ResolveClipPath()` returns empty path, `PlayOneShot()` returns handle 0

### Reproduction Steps
1. Build and run the game
2. Press `~` to open console
3. Type `audio_play tr_far music`
4. Observe: No audio plays

### Root Cause
- `assets/audio/` directory is empty
- No default audio files included in repo

### Fix Status
- PENDING: Need to either:
  1. Add placeholder audio files to repo
  2. Document required audio files in README
  3. Provide generation script for test tones

---

## Issue: tr_debug Command Not Verified

### Expected
- `tr_debug on|off` should toggle debug visualization for terror radius audio
- Should show layer volumes, distance, intensity values

### Actual
- Command is registered in DeveloperConsole (line 715-729)
- `ConsoleContext::setTerrorAudioDebug` callback exists
- Wiring to App `m_terrorAudioDebug` not confirmed

### Reproduction Steps
1. Run game, start solo session
2. Press `~` to open console
3. Type `tr_debug on`
4. Observe: No visible feedback of debug state

### Fix Status
- PENDING: Need to verify callback wiring in App.cpp
- May need HUD overlay for TR debug values

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
