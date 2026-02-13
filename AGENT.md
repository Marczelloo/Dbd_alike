Asymmetric Horror Prototype (C++20 / OpenGL)
Project Overview

Asymmetric Horror Prototype is a modular Dead by Daylight–like prototype featuring:

TRUE 3D movement

Survivor (3rd-person) / Killer (1st-person)

Vault / pallet interactions

Combat system (short swing + lunge)

Chase system (DBD-like)

Terror Radius (stepped audio)

Bloodlust system (DBD-like tiers)

Scratch marks + blood pools

Killer look light

ENet multiplayer (server-authoritative)

In-engine Level Editor

Data-driven assets (maps, loops, materials, animations, FX, prefabs)

Custom retained UI (no ImGui for player-facing UI)

README is the source of truth for:

Controls

Console commands

Multiplayer UX

Editor workflow

Testing checklists

README

0. OPERATING CONTRACT (MANDATORY)
   0.1 Always Audit Repo State First

Before implementing anything:

git status --short --branch
git log --oneline --decorate -n 20
cmake --build build -j 8

Rules:

If build fails → FIX BUILD FIRST.

No new features on broken build.

Never assume previous agent finished correctly.

0.2 Small, Verifiable Steps

Every change must:

Compile

Run

Not regress multiplayer

Not regress vault/pallet/combat/chase

Commit discipline:

engine/audio: ...
gameplay/chase: ...
gameplay/bloodlust: ...
gameplay/vfx: ...
ui/settings: ...
editor: ...
net: ...
docs: ...

0.3 Multiplayer Authority Model (STRICT)

Host = authoritative server.

Client sends input only.

Host simulates gameplay.

Host replicates snapshots + replicated state.

Bloodlust, chase, movement speed modifiers MUST be server-authoritative.

Cosmetic FX may be client-derived if deterministic.

0.4 Data-Driven First

All gameplay-configurable systems must support JSON config:

config/\*.json

assets/_/_.json

Each asset must:

Include asset_version

Validate on load

Auto-create default if missing

No hard-coded gameplay constants unless explicitly marked FINAL in this file.

1. Core Systems — Behavioral Contract
2. Chase System (DBD-like — NON-NEGOTIABLE)
   Constants

Killer FOV: 87° total

Center FOV: ±35°

Chase start distance: ≤ 12m

Chase end distance: ≥ 18m

LOS timeout: 8s

Center-FOV timeout: 8s

Survivor must be sprinting to START chase

Chase may persist indefinitely if LOS/center-FOV are reacquired.

Required State Machine

States:

NotInChase
InChase

Track:

timeSinceSeenLOS

timeSinceCenterFOV

timeInChase

chaseActive must reflect state machine ONLY (not TR band).

3. Terror Radius (Stepped, Not Gradient)
   Base Radius

Default: 32m
Runtime changeable:

tr_set <meters>
tr_radius <meters>

Stepped Bands (STRICT)

For radius R:

distance > R → silence

0.66R–R → tr_far

0.33R–0.66R → tr_mid

0–0.33R → tr_close

Only one dominant band active.

Smoothing allowed only 0.15–0.35s to avoid audio pops.

Chase Override

When chaseActive == true:

tr_chase ON

tr_close SUPPRESSED

tr_far/tr_mid remain distance-based

Audio Routing (STRICT)

Survivor hears TR bands.

Killer hears ONLY chase music.

Role switch must update routing immediately.

4. Bloodlust (Server Authoritative)

Tiers:

15s → 120%

25s → 125%

35s → 130%

Reset immediately on:

Survivor hit

Killer stunned

Pallet break

Chase end

Applied multiplicatively to base speed.

Must replicate to clients.

5. Combat System Contract

LMB click → short swing

Hold LMB → lunge (instant start + acceleration)

Killer hit triggers:

hit_spark FX

blood_spray FX

Survivor states:

Healthy

Injured

Downed

Carried

Movement modifiers must match state.

6. Scratch Marks (DBD-like)

Spawn when survivor sprinting.

Killer-only visibility.

Spawn interval ~0.08–0.15s.

Lifetime ~30s.

Ring buffer pooled.

No per-frame allocations.

7. Blood Pools

Spawn when Injured OR Downed.

Killer-only visibility.

Interval ~2s.

Lifetime ~120s.

Quadratic fade.

Ring buffer pooled.

8. Killer Look Light

Spotlight aligned to killer forward.

Default range ~14m.

Debug visualization supported.

Disabled in killer FP view.

Survivor-only visibility.

9. FX System Contract

Data-driven assets: assets/fx/\*.json

Pooled instances.

Host can broadcast FX (ServerBroadcast).

Clients spawn locally on receive.

No per-frame heap allocations.

Console:

fx_list
fx_spawn <id>
fx_stop_all

10. Multiplayer + LAN Discovery

Must support:

host [port]
join <ip> <port>
disconnect
lan_scan
lan_status
net_status
net_dump

Overlay (F4) must show:

connection state

peers

ping/RTT

LAN discovery state

Logs:

logs/network.log

11. Editor Contract

Editor must remain stable:

Map Editor

Loop Editor

Prefabs

Mesh preview (.obj, .gltf, .glb)

Materials

Animation clips

Environment assets

Map lights (point/spot)

FX Editor

Editor must:

Save JSON assets with asset_version

Not require restart to return to Main Menu

Not corrupt map files

12. Debug Requirements (MANDATORY)

Overlay (F1/F2/F4/F5) must show:

distance_to_killer

chaseActive

timeInChase

bloodlustTier

TR band

per-layer volumes

player states

attack state

Console debug commands must reflect real state (no fake values).

13. Documentation Discipline

Every feature must update:

README (user-facing explanation)

Console command list

Asset path documentation

Test checklist

Multiplayer notes (if relevant)

Maintain:

docs/AGENT_NOTES.md
docs/TRACE.md
docs/DECISIONS.md
