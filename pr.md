# TASK (NON-NEGOTIABLE): Make CHASE + TERROR RADIUS behave like Dead by Daylight (DBD)

Stop asking clarifying questions and implement exactly what is specified below.  
Do NOT claim “DBD has no terror radius” — it does. Implement TR + chase music like DBD.

We already have:

- AudioSystem with buses and handle volumes (miniaudio)
- Terror radius profile JSON with layered loops and crossfade logic
- chaseActive boolean available via `BuildHudState().chaseActive` (but current chase detection may be wrong)

Your job:

1. Fix chase state logic to match DBD-style rules (simplified but faithful).
2. Fix terror radius audio layering so it matches DBD behavior (distance layers + chase override).
3. Add debug visibility (UI overlay + console commands) so we can verify it instantly.

---

## 0) Definitions (DBD-Like Expected Behavior)

### A) Terror Radius (TR)

Terror Radius is an audio warning system for Survivors:

- It depends on **distance to Killer** (use horizontal distance in XZ; ignore vertical for now).
- It has **non-chase layers** that intensify with distance.
- In **chase**, the normal highest layer is replaced by **chase music**.

### B) Chase

Chase begins when:

- Killer has Survivor in line of sight / within FOV cone,
- AND Survivor is running,
- AND distance is within the chase-start threshold.

Chase ends when:

- distance is too large OR
- line of sight is lost for a timeout OR
- Survivor exits FOV cone for a timeout.

We will implement a simplified but close DBD model with explicit thresholds (below).

---

## 1) Implement Correct CHASE Detection (CRITICAL)

### 1.1 Use the Killer's FOV cone and distance

Use these values (hardcode first, then expose to config):

- Start distance: **<= 12.0m**
- End distance: **>= 18.0m**
- Killer total FOV: **87°** (so half-angle = 43.5°)
- “Center requirement”: if Survivor leaves **±35°** from center, chase begins to decay/end.
- LOS lost timeout to end: **8.0s**
- “Out of center FOV” timeout to end: **8.0s**
- Locker hide end: ignore for now unless already implemented.

### 1.2 Running requirement

Chase start requires:

- Survivor movement state == running (sprint).
  (If we don't have it reliably, approximate: survivor speed > walk threshold.)

Chase continues while:

- The chase is active until end conditions trigger (distance end OR LOS/FOV timeouts).

### 1.3 State machine

Create a robust state machine:

- `NotInChase`
- `InChase`
  Track timers:
- `timeSinceSeenInLOS`
- `timeSinceInCenterFOV`
- `timeInChase`

### 1.4 Update chaseActive

`BuildHudState().chaseActive` must reflect this corrected logic.
Do NOT mix it with terror radius intensity. It must be a pure chase state.

### Acceptance

- You can reproduce: killer looks at running survivor within 12m => chase starts.
- If survivor breaks LOS for >8s OR leaves center FOV for >8s OR goes beyond 18m => chase ends.

---

## 2) Implement Bloodlust Timers (DBD-Like) (MUST)

Bloodlust increases Killer speed during long chase:

- Tier1 after **15s** in chase (120% speed multiplier)
- Tier2 after **25s** (125%)
- Tier3 after **35s** (130%)

Bloodlust resets when:

- Killer hits survivor OR
- Killer breaks a pallet OR
- Killer gets stunned
  (If these events exist: hook into them. If not, expose a debug console command to trigger resets.)

### Acceptance

- When chase continues, the tier increases at the correct times.
- Reset works when the event fires (or via debug command).

---

## 3) Fix Terror Radius Audio to Match DBD (CRITICAL)

### 3.1 TR radius values

Default TR radius depends on killer speed archetype. For now:

- Use **32m** as default TR radius.
  Expose in JSON: `base_radius`.

### 3.2 Distance layers (non-chase)

We want **three non-chase layers** that intensify with distance.

Implement either:

- A) true stepped layers (more DBD-like), OR
- B) smooth fades but matching the stepped feel.

Use this model (simple and testable):

- `tr_far`: active when distance within [radius * 1.0 .. radius * 0.66]
- `tr_mid`: active when distance within [radius * 0.66 .. radius * 0.33]
- `tr_close`: active when distance within [radius * 0.33 .. 0]
  Map intensity in [0..1] where 1 = killer on top of survivor.

Important:

- Outside radius => all TR layers should be ~0 volume (silent).

### 3.3 Chase layer override

When `chaseActive == true`:

- `tr_chase` must be audible (fade in quickly; e.g. 0.25s smoothing).
- `tr_close` should be reduced or replaced (DBD replaces highest non-chase feel).
  Simplified rule:
- In chase:
  - Keep `tr_far` and `tr_mid` low/optional (either 0 or small)
  - Replace `tr_close` with `tr_chase`
    Minimum expected behavior:
- `tr_chase` ONLY plays when chaseActive is true.
- `tr_chase` must NOT play outside chase.

### 3.4 Bus usage

- TR layers should be on **Music** bus (or a dedicated `Ambience/Music` choice) but consistent.
- Heartbeat SFX (if added later) would be SFX bus.

### Acceptance

- At distance > radius: silence.
- Approaching into radius: far->mid->close intensifies.
- Starting chase: chase music kicks in; close layer is suppressed/replaced.
- Ending chase: chase music fades out; return to distance layers.

---

## 4) Debug & Verification (MUST, no excuses)

### 4.1 Viewport overlay (Survivor view)

Add an overlay showing:

- `distance_to_killer`
- `in_LOS` (true/false)
- `in_center_FOV` (true/false)
- `chaseActive` (true/false)
- `bloodlust_tier` (0/1/2/3)
- current volumes for: far/mid/close/chase

### 4.2 Console commands

Add commands:

- `tr_dump` => print current TR state (distance, intensity, chase, volumes)
- `chase_force on|off` => force chaseActive for testing
- `bloodlust_reset` => reset bloodlust timers/tier
- `tr_radius <meters>` => set TR radius live

### Acceptance

- We can validate everything without guessing by reading overlay/console.

---

## 5) Implementation Constraints

- Keep allocations out of per-frame loops (no per-frame vector growth).
- Keep code modular:
  - chase system lives in gameplay logic
  - TR audio mixing in App/Audio integration
- After implementation:
  - update README: controls + console commands + how to test chase/TR quickly
  - update docs/TRACE.md with reproduction steps

---

## 6) Definition of Done (DO NOT STOP EARLY)

This task is done ONLY when:

- Chase starts/ends correctly with thresholds and timeouts.
- Bloodlust tiering works and resets.
- Terror radius layers behave like DBD with chase override.
- Debug overlay and console commands exist.
- README + docs updated with how to test.

## IMPORTANT: Terror Radius Volume Behavior (DBD Accurate)

Terror Radius layers must NOT use full continuous gradient over distance.

Instead:

- Each TR layer has a mostly constant volume within its distance band.
- Only small smoothing (0.2–0.4s fade) is allowed when:
  - entering TR radius
  - exiting TR radius
  - switching between distance bands
  - entering/exiting chase

### Correct behavior:

Outside TR radius:

- All TR layers volume = 0.

Inside TR radius:

- Exactly one distance band is dominant at a time:
  - FAR band
  - MID band
  - CLOSE band

Volume inside band:

- ~constant (e.g. 0.8–1.0 multiplier)
- no linear interpolation across whole radius

Only smooth fade when switching bands.

### Chase Override

When chaseActive == true:

- CLOSE band must be replaced by CHASE layer.
- CHASE volume constant while chaseActive.
- On chase end: fade back to proper distance band.

DO NOT use full-distance intensity gradient (0..1 mapping).
This must feel stepped and layered like DBD.
📌 Jak to powinno działać matematycznie
Załóżmy TR radius = 32m.

Podział:

21m – 32m → FAR

10m – 21m → MID

0m – 10m → CLOSE

Logika:

cpp
Skopiuj kod
if (distance > 32) -> silence
else if (distance > 21) -> FAR = 1.0
else if (distance > 10) -> MID = 1.0
else -> CLOSE = 1.0
I tylko:

ini
Skopiuj kod
currentVolume = lerp(previous, target, delta \* 6.0f)
🧠 Dlaczego to ważne?
Bo gradient:

brzmi jak indie horror

nie brzmi jak DBD

daje „ambient horror vibe”

A DBD ma:

wyraźne warstwy

skokowe odczucie intensyfikacji

mocne przejście w chase
