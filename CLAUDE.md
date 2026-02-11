# CLAUDE.md — Asymmetric Horror Prototype (C++20 / OpenGL) — Claude Code Instructions

You are working inside a C++20 OpenGL project: **Asymmetric Horror Prototype** — a modular DBD-like prototype with TRUE 3D movement, 3rd-person Survivor camera, 1st-person Killer camera, interactions (vault/pallet), chase, dev console, and a test multiplayer host/join (ENet; server-authoritative).  
Source of truth for features, controls, layout, editor, networking UX: see README. :contentReference[oaicite:2]{index=2}

This repo also contains an implementation plan/task spec for the next major gameplay systems: **Audio + Terror Radius + Scratch Marks + Blood Pools + Character Roster + Rig/Animation + Ragdoll**. :contentReference[oaicite:3]{index=3}

---

## 0) Operating Rules (MUST)

1. **Always start by understanding the current repo state**
   - Run:
     - `git status --short --branch`
     - `git log --oneline --decorate -n 15`
     - `cmake --build build -j 8` (or appropriate build folder)
   - If build fails: fix build first (no new features before green build).

2. **Work in small, verifiable steps**
   - Each step must compile and run.
   - Prefer small commits with clear messages:
     - `engine/audio: ...`
     - `gameplay: ...`
     - `ui: ...`
     - `net: ...`

3. **Do not regress existing core mechanics**
   - Vault/pallet/chase/multiplayer must remain functional after each change.

4. **Prefer data-driven config/assets**
   - Config in `config/*.json`
   - Assets in `assets/*` (follow existing patterns: `asset_version`, per-feature directories).

---

## 1) Repo Context (Quick)

### Build & Run

Follow README build instructions (Ninja/MSVC/Linux). :contentReference[oaicite:4]{index=4}

### Key Areas

- `engine/core`: app loop, time, events, network orchestration
- `engine/audio`: miniaudio backend (recently added / WIP)
- `engine/net`: ENet session wrapper + LAN discovery
- `engine/render`: wireframe + filled shading renderer
- `engine/physics`: capsule-vs-box collision, raycast, triggers
- `engine/scene`: world + components
- `engine/ui`: custom retained UI system (player-facing)
- `ui`: dev console + HUD rendering
- `engine/fx`: data-driven VFX system and editor
- `game/gameplay`: roles, cameras, interactions, chase, combat, states
- `game/maps`: map generation, loops, tile metadata
- `docs`: architecture notes

Source of truth for controls, console commands, and verification checklists: README. :contentReference[oaicite:5]{index=5}

---

## 2) Current Work-in-Progress Snapshot (What Codex Started)

A previous agent began implementing:

- `engine/audio/AudioSystem` using **miniaudio**
- Config persistence: `config/audio.json`
- Settings UI: new **Audio** tab inside in-game Settings (custom UI)
- Dev console commands:
  - `audio_play`, `audio_loop`, `audio_stop_all`
- Terror radius audio: layered looping tracks with crossfade based on distance, profile JSON:
  - `assets/terror_radius/<killerId>.json`
  - Layers FAR/MID/CLOSE/CHASE (chaseOnly), smoothing, handle-volume updates

Your job: **continue from repo state** (do not assume anything is perfect). Verify compilation and runtime. :contentReference[oaicite:6]{index=6}

---

## 3) Primary Objective (Continue Next Phase Tasks)

Use this phased plan (must remain testable after each phase): :contentReference[oaicite:7]{index=7}

### Phase A — Stabilize Audio + Terror Radius (Finish baseline)

1. Verify audio playback on Windows + Linux.
2. Ensure `config/audio.json` is created, loaded, saved, and applied reliably.
3. Validate miniaudio integration does not break CI/build.
4. Terror radius:
   - Ensure layers start looped at 0 volume
   - Smooth crossfade updates each frame
   - Support `tr_debug` and optional visualization toggle if present
5. Add a minimal audio asset checklist in README (what filenames to include under `assets/audio`). :contentReference[oaicite:8]{index=8}

### Phase B — Scratch Marks + Blood Pools + Killer Look Light (Gameplay VFX)

Implement:

- Scratch marks (sprinting; optional injured modifier)
- Blood pools for injured survivor
- Killer “look light” (spot light cone aligned with killer forward)

Requirements:

- Must be **multiplayer-safe**:
  - Prefer client-side cosmetic spawn based on replicated state, OR server broadcast via fx system if needed.
- Must be configurable and data-driven:
  - scratch profile JSON
  - blood profile JSON
- Should integrate with existing `engine/fx` where possible (preferred):
  - spawn `FX` assets for scratch/blood as either quads/trails/sprites.

### Phase C — Character Roster + Selection UI (Data-driven)

Implement:

- `assets/characters/survivors/<id>.json`
- `assets/characters/killers/<id>.json`
- Menu selection for survivor/killer (custom UI, not ImGui)
- Multiplayer: lobby/handshake replication (host authoritative for final assignment)

### Phase D — Rig / Animation / Ragdoll Test Foundation

- Skeletal animation playback for glTF
- Minimal blending (idle/walk/run; crouch/crawl; attack/vault)
- Ragdoll toggle for validation (console `ragdoll on/off`)
- Keep it modular; if full ragdoll is too large now, implement a test harness.

---

---

## 3.5) DBD-like Chase + Terror Radius (SOURCE OF TRUTH — NON-NEGOTIABLE)

This project aims to match the _feel_ of Dead by Daylight for CHASE + TERROR RADIUS audio.  
Do **not** reinterpret these rules. Implement exactly.

### A) Definitions (do not mix concepts)

- **CHASE**: a gameplay state (“In chase” / “Not in chase”), driven by LOS/FOV + distance + survivor sprinting.
- **TERROR RADIUS (TR)**: survivor warning audio system driven primarily by **distance** to killer, using **stepped layers**.
- **CHASE MUSIC**: an audio layer that plays only while `chaseActive == true`, replacing the _close_ TR feel.

`BuildHudState().chaseActive` must reflect **CHASE state machine only** (not TR intensity).

---

### B) Chase rules (DBD-like, simplified but strict)

#### Constants (FINAL)

- Killer total FOV: **87°** (half-angle = 43.5°)
- “Center FOV” constraint: **±35°** from killer forward
- Chase start distance: **<= 12.0m**
- Chase end distance: **>= 18.0m**
- Chase end timeouts:
  - LOS lost for **> 8.0s** → end chase
  - Center-FOV lost for **> 8.0s** → end chase
- Chase starts only if **Survivor is sprinting** (running)

Chase can last indefinitely if LOS and center-FOV keep being reacquired (that’s fine).

#### Required state machine

Implement as a clear state machine:

- `NotInChase`
- `InChase`

Track timers in chase system:

- `timeSinceSeenLOS`
- `timeSinceCenterFOV`
- `timeInChase`

#### Required inputs (minimum)

- Killer forward vector + position
- Survivor position
- Survivor running/sprinting flag (or reliable speed-based proxy)
- LOS test (raycast or existing visibility test)

---

### C) Terror Radius audio rules (DBD-like stepped layers)

#### Radius

Default TR radius: **32m** (data-driven via `assets/terror_radius/<killerId>.json` → `base_radius`)

Distance is Survivor → Killer distance (use XZ/horizontal if you must choose; be consistent).

#### Layer meaning (DO NOT INVERT)

These names have fixed meaning:

- `tr_far` = sound on the **OUTER EDGE** of TR (weakest)
- `tr_mid` = sound in the **MIDDLE** of TR
- `tr_close` = sound **CLOSE TO KILLER** (strongest)
- `tr_chase` = chase music, **only** when chase is active

#### Stepped bands (NO distance gradient)

No continuous “intensity 0..1 over whole radius”.

For base radius `R`:

- `distance > R` → **silence** (all TR layers volume = 0)
- `distance in (R * 0.66 .. R]` → `tr_far` ON (constant gain)
- `distance in (R * 0.33 .. R * 0.66]` → `tr_mid` ON (constant gain)
- `distance in [0 .. R * 0.33]` → `tr_close` ON (constant gain)

Only one band is dominant at a time (stepped feel like DBD).

#### Smoothing / crossfade (ONLY to prevent pops)

Even though the _logic_ is stepped, audio must not click.
Allow short smoothing:

- **0.15–0.35s** lerp when switching bands or entering/exiting TR
- This smoothing is NOT a “distance gradient”, only transition smoothing.

#### Chase override (critical)

When `chaseActive == true`:

- `tr_chase` must be audible (constant within chase)
- `tr_close` must be suppressed/replaced by `tr_chase` (DBD-like)
- `tr_far` and `tr_mid` remain distance-based (or can be reduced—keep simple and consistent)
  When chase ends:
- fade out `tr_chase` quickly
- return to correct distance band

---

### D) Debug & verification (MANDATORY)

#### Viewport overlay must show:

- distance_to_killer
- in_LOS (bool)
- in_center_FOV (bool)
- survivor_sprinting (bool)
- chaseActive (bool)
- timeInChase
- bloodlustTier (if implemented)
- TR band active: FAR/MID/CLOSE/OUTSIDE
- current per-layer volumes: far/mid/close/chase

#### Console commands (must exist)

- `tr_dump` → print TR state, band, per-layer volumes
- `tr_radius <meters>` → set radius live
- `tr_debug on|off` → overlay toggle (or extra debug)
- `chase_force on|off` → force chaseActive for testing
- `chase_dump` → print chase timers/state
- `bloodlust_reset` → reset bloodlust (if bloodlust is implemented)

---

### E) Acceptance tests (must pass)

1. TR:

- Outside radius → silence
- Enter radius → `tr_far` (outer edge) ON
- Move closer → switch to `tr_mid`
- Very close → switch to `tr_close`
- No “smooth gradient” based on every meter; only stepped bands + short smoothing.

2. Chase:

- Survivor sprinting + within 12m + killer has LOS + in center FOV → chase starts
- Break LOS > 8s OR break center FOV > 8s OR distance >= 18m → chase ends

3. Chase/TR integration:

- In chase → `tr_chase` ON, `tr_close` replaced
- After chase ends → `tr_chase` OFF and TR returns to correct band

4. Debug:

- Overlay and console commands fully confirm behavior without guessing.

---

---

## 3.6) Bloodlust System (DBD-like — Strict Rules)

Bloodlust is a **Killer-only mechanic** active only during an active chase.

It exists to prevent infinite looping and rewards longer chases.

This implementation must be deterministic and fully debug-visible.

---

### A) Bloodlust Tiers (FINAL VALUES)

Bloodlust activates only when:

- `chaseActive == true`
- Killer is NOT stunned
- Killer is NOT in attack cooldown
- Killer is NOT breaking pallet

#### Tier thresholds (time spent continuously in chase)

- Tier 0: default (no bonus)
- Tier 1: after **15 seconds** in chase → **120% base movement speed**
- Tier 2: after **25 seconds** in chase → **125% base movement speed**
- Tier 3: after **35 seconds** in chase → **130% base movement speed**

Movement bonus applies multiplicatively to base killer speed.

Example:
If killer base speed = 4.6 m/s:

- Tier 1 → 4.6 × 1.20
- Tier 2 → 4.6 × 1.25
- Tier 3 → 4.6 × 1.30

---

### B) Bloodlust Reset Conditions (MANDATORY)

Bloodlust resets to Tier 0 when:

- Killer successfully hits survivor
- Killer is stunned (pallet stun)
- Killer breaks a pallet
- Chase ends (state transitions to NotInChase)

Reset must:

- Immediately clear tier
- Reset `timeInChase` tracking for bloodlust

---

### C) Time Tracking

Bloodlust uses:

- `timeInChase` (continuous time inside `InChase`)
- Do NOT accumulate across multiple chases
- Do NOT accumulate while not in chase

When chase ends:

- Bloodlust tier = 0
- timer resets

---

### D) Multiplayer Authority

Bloodlust tier must be:

- Server-authoritative
- Replicated to clients
- Used for:
  - killer movement speed
  - debug display
  - HUD if needed

Clients must NOT compute their own bloodlust independently.

---

### E) Debug Requirements (MANDATORY)

Overlay must display:

- bloodlustTier (0/1/2/3)
- timeInChase
- killerBaseSpeed
- killerCurrentSpeed
- speedMultiplier

Console commands:

- `bloodlust_reset` → force reset to Tier 0
- `bloodlust_set <0|1|2|3>` → manually force tier for testing
- `bloodlust_dump` → print current values

---

### F) Acceptance Tests

1. Enter chase and keep LOS:
   - At 15s → Tier 1
   - At 25s → Tier 2
   - At 35s → Tier 3

2. Hit survivor:
   - Tier resets immediately

3. Break pallet:
   - Tier resets immediately

4. Lose chase:
   - Tier resets immediately

5. Debug overlay reflects correct tier and speed multiplier.

---

### G) Important Design Constraint

Bloodlust logic must be separated from:

- Terror Radius audio
- Animation logic
- VFX

It should only influence:

- Killer movement speed
- Debug/HUD display

No hidden coupling.

---

## 4) Non-Negotiable UX / Debuggability

### Provide clear runtime diagnostics

- If hosting multiplayer:
  - show active LAN IPs + port in UI
  - show connection state transitions
- If joining:
  - show “connecting/connected/error” with reason
- Add logs to `logs/network.log` where appropriate (existing pattern in README). :contentReference[oaicite:9]{index=9}

### Keep player-facing UI in custom UI system

- Custom UI (`engine/ui/UiSystem`) for menus/HUD/settings
- ImGui allowed only for deep debug/editor internals

---

## 5) Conventions & Quality Bar

### Coding

- Prefer small headers, clear naming, no mega-functions.
- Avoid hidden global state; route through `App` / systems.

### Data-driven assets

- Always include `asset_version` and validate.
- Provide a “save default if missing” behavior for new JSON assets.

### Performance

- No per-frame allocations in hot loops for effects (use pooling in FX system).
- For scratch/blood: fixed-cap ring buffer + reuse.

### Multiplayer

- Server-authoritative for gameplay state.
- Cosmetics can be client-side derived if consistent.

---

## 6) Immediate Next Actions (Start Here)

1. **Audit state**
   - Build and run.
   - Test:
     - `audio_play <clip>`
     - `audio_loop <clip>`
     - `audio_stop_all`
     - Terror radius crossfade (move killer close to survivor).
2. **Create helper notes for yourself**
   - Create `docs/CLAUDE_NOTES.md` with:
     - current findings
     - file hot-spots
     - TODO list
   - (Optional) Create `docs/TRACE.md` where you write down how to reproduce issues.

3. **If something is inconsistent**
   - Add short debug overlays or `F-key` toggles rather than spam logs.
   - Keep debug options discoverable via `Help` / console `help`.

---

## 7) Acceptance Criteria (Per Phase)

### Audio / TR

- Audio backend works, no crashes, bus volumes persist.
- TR profile loads from JSON and layers crossfade smoothly.
- Console commands work and do not leak handles.

### Scratch / Blood / Light

- Scratch marks visible and fade.
- Blood pools spawn only when injured and fade.
- Killer look light is clearly visible and configurable.
- Does not break multiplayer.

### Character & Anim

- Can choose at least 2 survivors + 2 killers (different models).
- Selection spawns correct model; camera modes remain correct.
- Animation plays and blends; ragdoll toggle works for test.

---

## 8) References

- README (build, menu, controls, console, editor, networking UX). :contentReference[oaicite:10]{index=10}
- Task spec / prompt with phased requirements. :contentReference[oaicite:11]{index=11}

## Documentation & Change Log (MUST)

Every time you add or change functionality, you MUST update documentation so the repo stays self-explanatory (like the current README style). Documentation is part of the deliverable.

### Required updates for EACH feature/change

- Update `README.md`:
  - What was added/changed (short, user-facing)
  - How to use it (controls / menus / console commands)
  - Where configs/assets live (paths + example filenames)
  - How to test it quickly (smoke checklist)
  - Multiplayer notes if relevant (host/client behavior)
- If you add new config or asset types:
  - Document schema (keys, types, defaults)
  - Add a minimal example JSON snippet (short)
- If you add new debug commands:
  - Add them to README’s “Console / Debug Commands” section

### Required project notes files (create if missing)

- `docs/CLAUDE_NOTES.md` — living notes:
  - current status, found issues, decisions, TODO
  - file hotspots / pointers
- `docs/TRACE.md` — reproduction steps:
  - exact steps to reproduce bugs
  - expected vs actual
  - how to verify fixes
- `docs/DECISIONS.md` — decision log (short):
  - key architecture decisions and rationale
  - tradeoffs (e.g. client-side cosmetic vs server broadcast)

### Commit discipline

- Each major chunk should be a separate commit.
- Commit messages must be clear and scoped:
  - `engine/audio: ...`
  - `gameplay/vfx: ...`
  - `ui: ...`
  - `docs: ...`
- After implementing a feature, include a follow-up docs commit if needed.

Acceptance:

- After each new feature, README and docs reflect the current reality of the project.
