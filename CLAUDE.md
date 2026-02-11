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
