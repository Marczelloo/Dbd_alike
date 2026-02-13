# Asymmetric Horror Prototype (C++20 / OpenGL)

Modularny prototyp DBD-like z TRUE 3D ruchem, kamerą rolową (Survivor 3rd / Killer 1st), interakcjami (vault/pallet), chase, konsolą developerską i testowym multiplayerem host/join (ENet, server-authoritative).

## Build

### Windows (Ninja)

```powershell
cmake -S . -B build -G Ninja -DBUILD_IMGUI=ON -DUSE_GLFW_STATIC=ON
cmake --build build
.\build\asym_horror.exe
```

### Windows (Visual Studio / MSVC)

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_IMGUI=ON -DUSE_GLFW_STATIC=ON
cmake --build build-msvc --config Release
.\build-msvc\Release\asym_horror.exe
```

### Linux (Ninja)

```bash
cmake -S . -B build -G Ninja -DBUILD_IMGUI=ON -DUSE_GLFW_STATIC=ON
cmake --build build -j
./build/asym_horror
```

## Main Menu

Po starcie otwiera się Main Menu:
- `Play Solo`
- `Host Multiplayer`
- `Join Multiplayer`
- `Quit`

Opcje:
- Role: `Survivor` / `Killer`
- Map: `main_map` / `collision_test` / `test`
- Port + Join IP

W grze: `Esc` otwiera Pause Menu (`Resume`, `Return to Main Menu`, `Quit`).

## Controls

- `WASD`: move
- `Mouse`: look
- `Shift`: sprint (Survivor)
- `E`: interact (vault / drop pallet / break pallet / repair)
- `Space`: skill check hit (jump is N/A by default)
- `LMB`: short swing (Killer)
- `Hold LMB`: lunge (Killer)
- `` ` ``: toggle developer console
- `F1`: toggle HUD overlay
- `F2`: toggle debug draw (colliders/triggers/LOS/tile overlay)
- `F3`: toggle render mode (`wireframe` <-> `filled`)
- `F6`: toggle UI test panel
- `F7`: toggle loading screen test panel
- `F11`: fullscreen
- `Esc`: pause menu

## Console

HUD pokazuje hint: `Press ~ for Console`.

Przy pierwszym otwarciu konsoli automatycznie drukowane jest `help`.
`help` jest pogrupowane kategoriami (Gameplay/Debug/Network/System/General).
Pod polem input sa dynamiczne podpowiedzi; `TAB` autouzupelnia komendy.

### Key Commands

- `help`
- `host <port>`
- `join <ip> <port>`
- `disconnect`
- `set_role survivor|killer`
- `control_role survivor|killer`
- `cam_mode survivor|killer|role`
- `load map test|main|main_map|collision_test|benchmark`
- `regen_loops [seed]`
- `skillcheck start`
- `set_speed survivor|killer <percent>`
- `set_size survivor|killer <radius> <height>`
- `toggle_collision on|off`
- `toggle_debug_draw on|off`
- `physics_debug on|off`
- `noclip on|off`
- `set_vsync on|off`
- `set_fps <limit>`
- `set_tick 30|60`
- `render_mode wireframe|filled`
- `set_resolution <w> <h>`
- `toggle_fullscreen`
- `quit`
- `perf` — toggle performance profiler overlay
- `perf_pin on|off` — pin profiler to game window
- `perf_compact on|off` — compact FPS bar at corner

### Profiler Tabs

- **Overview**: Frame time graph + FPS histogram with auto-scaling
- **Systems**: Per-system timings (Update, Physics, Render, UI, FX, Audio, Swap) with frame budget indicator
- **Sections**: Named profile scopes with current/avg/peak times
- **Render**: Draw calls, vertices, triangles, culling stats, GPU memory
- **Distribution**: Frame time percentiles (P50, P90, P95, P99, 1% Low) + histogram
- **Benchmark**: Automated FPS benchmark with statistics

## Multiplayer (Host/Join)

### Host
1. W menu wybierz mapę i rolę.
2. Kliknij `Host Multiplayer`.
3. Drugi gracz używa twojego `IP:port`.

### Join
1. W menu wpisz `Join IP` i `Port`.
2. Kliknij `Join Multiplayer`.
3. Rola klienta jest przypisywana przez hosta (rola przeciwna do hosta).

Model sieci:
- client-server (host = serwer autorytatywny)
- klient wysyła input
- host symuluje i rozsyła snapshoty stanu
- klient interpoluje snapshoty

## How To Test (DEVTEST Checklist)

1. `Vault prompt reliability`:
- `load map test`
- podejdź do okna, prompt `Press E to Vault` musi być stabilny
- naciśnij `E` stojąc w triggerze: vault zawsze startuje (input buffer)

2. `Vault type`:
- sprint OFF -> `VaultType: Slow`
- sprint ON / wyższa prędkość -> `VaultType: Medium`

3. `Pallet visuals + interaction`:
- Survivor przy standing pallet: `Press E to Drop Pallet`
- po drop: paleta nisko (dropped) i blokuje przejście
- Killer przy dropped: `Press E to Break Pallet` + timer

4. `Render mode`:
- `F3` lub `render_mode wireframe|filled`
- `filled` ma Lambert lighting, wyraźnie inny wygląd od wireframe

5. `Collision test map`:
- `load map collision_test`
- sprawdź wall sliding w korytarzu i corner snag test
- `F2` włącz collidery/triggery/tile overlay

6. `Benchmark map`:
- `load map benchmark`
- 100m x 100m arena z wieloma strefami testowymi
- Testuje: edge case kolizje, rendering pod obciążeniem, LOS, chase scenarios
- Strefy: tight corridors, spiral maze, pillar forest, slalom, dense grid, complex intersections, acute angles, multi-tier platforms, chaos scatter, concentric rings, bridge crossing, pallet gallery

6. `Wiggle`:
- ustaw survivor state na `carried`
- alternuj `A/D` (edge-based), progress ma rosnąć stabilnie
- spam jednego kierunku nie powinien dawać poprawnego wzrostu

7. `Skillcheck widget`:
- `skillcheck start`
- pojawia się ring + igła + success zone
- `Space` zalicza/obniża progres zależnie od timingu

8. `Multiplayer smoke test`:
- Host + Join (2 procesy)
- przetestuj ruch obu ról
- vault i pallet state widoczne po obu stronach
- killer hit aktualizuje stan survivor po stronie hosta i replikuje się

## Troubleshooting / Tuning

- Camera distance / shoulder / smoothing:
  - `game/gameplay/GameplaySystems.cpp` -> `UpdateCamera`
- Vault timing / arc / cooldown / type thresholds:
  - `game/gameplay/GameplaySystems.cpp` -> `DetermineWindowVaultType`, `BeginWindowVault`, `UpdateVaultState`
- Collision / step / penetration resolve:
  - `engine/physics/PhysicsWorld.cpp` -> `MoveCapsule`, `ResolveCapsulePosition`, `SphereIntersectsExpandedAabb`
- Multiplayer packet flow / snapshot apply:
  - `engine/core/App.cpp` (serialize/deserialize + network loop)
  - `engine/net/NetworkSession.cpp`

## Layout

- `engine/core`: app loop, time, events
- `engine/net`: ENet session wrapper
- `engine/platform`: window + input
- `engine/render`: wireframe + filled shading renderer
- `engine/physics`: 3D capsule-vs-box collision, raycast, triggers
- `engine/scene`: world + components
- `game/gameplay`: roles, cameras, interactions, chase, HUD/snapshot
- `game/maps`: test/main/collision_test generation + tile debug metadata
- `ui`: developer console + HUD rendering
- `docs`: architecture notes

## In-Engine Level Editor

Editor is built into the game (no external tools).

### Open Editor

From Main Menu:
- `Level Editor` -> map grid editing mode
- `Loop Editor` -> prefab loop editing mode

Inside editor:
- `Back To Main Menu` returns cleanly without restart
- `Playtest Current Map` saves current map and starts solo gameplay on it

### Asset Files

- Loop assets: `assets/loops/<loopId>.json`
- Map assets: `assets/maps/<mapName>.json`

Both formats are JSON and include `asset_version`.

### Editor Camera + Interaction

- `WASD`: move camera on plane
- `Q/E`: move camera down/up
- `RMB + Mouse`: look
- `Mouse Wheel`: camera speed
- `T`: top-down toggle

Selection/gizmos:
- `LMB`: select/place
- `RMB`: remove placement at hovered tile
- `1/2/3`: gizmo mode (translate/rotate/scale)
- `Arrows + PageUp/PageDown`: move selected
- `[` `]`: rotate selected
- `+` `-`: scale selected
- `Delete`: delete selected
- `Ctrl+D`: duplicate selected
- `Ctrl+Z`: undo
- `Ctrl+Y` / `Ctrl+Shift+Z`: redo
- `G`: toggle grid snap
- `R`: rotate pending loop placement
- `P`: prop placement mode

Mesh modeler (Loop Editor):
- `4/5/6`: mesh edit mode (`Face` / `Edge` / `Vertex`)
- `M`: toggle scene-edit picking for mesh
- `J`: edge extrude (when Edge mode)
- `B`: edge bevel (when Edge mode)
- click mesh directly in `Scene Viewport` to select face/edge/vertex
- drag mesh gizmo axis handles in viewport to move current mesh selection

Multi-select:
- `Ctrl + LMB` toggles object selection
- group ops supported: move/rotate/scale (gizmo), duplicate, delete

### Loop Editor Workflow

1. `New` loop.
2. Add elements: `Wall`, `Window`, `Pallet`, `Marker`.
3. Edit selected element in `Inspector`.
4. Use `Auto Compute Bounds/Footprint` or set manual bounds/footprint.
5. Check validation list (warnings/errors).
6. `Save Current` -> creates/updates `assets/loops/<id>.json`.

Loop library supports:
- search/filter
- load
- duplicate
- delete
- rename

### Map Editor Workflow

1. `New Map` or `Load Selected`.
2. Choose loop from `Loop Palette`.
3. Hover tile (green valid / red invalid), place with `LMB` or `Place At Hovered`.
4. Rotate placement with `R`.
5. Enable `Prop Placement Mode (P)` to place rocks/trees/obstacles.
6. Edit selected placement/prop in `Inspector`.
7. `Save Current` -> creates/updates `assets/maps/<name>.json`.
8. `Playtest Current Map` to run gameplay scene using saved map.

Mesh asset rendering note (Map + Loop editor runtime preview):
- `PropType::MeshAsset` now uses one shared model draw path (`DrawModelAssetInstance`) for both editor modes.
- Surface albedo textures and fallback mesh draw are handled by the same helper for consistent visuals.

Placement rules:
- loop footprints cannot overlap
- placement must stay in map bounds
- props can be set `solid` on/off

### Runtime Integration

- Main Menu includes `Play Saved Map` list.
- Saved map loading instantiates loop references by ID.
- Windows/pallets/markers are converted into runtime interactables/triggers.

## Update: Multiplayer UX + LAN Discovery

### Console Commands (network)
- `host [port]` (default `7777`)
- `join <ip> <port>`
- `disconnect`
- `net_status`
- `net_dump`
- `lan_scan`
- `lan_status`
- `lan_debug on|off`

### LAN Multiplayer (Automatic Discovery)
1. Host: click `Host Multiplayer` (default port `7777`).
2. Join: in Main Menu check `LAN Games` list and click `Join` on discovered server.
3. Fallback manual join still works (`join <ip> <port>` or Join IP/Port fields).
4. Incompatible builds are shown as `Incompatible Version` and cannot be joined.

### Runtime Verification Checklist (short)
1. Host session -> verify host panel shows LAN IP(s) and port.
2. Second PC (same LAN) -> host appears in `LAN Games` in ~1-3 seconds.
3. Click `Join` -> role/map assignment arrives, state becomes `CONNECTED`.
4. Press `F4` -> overlay shows network state, peers, RTT/ping (if available), LAN discovery status.
5. Use `net_status`, `net_dump`, `lan_status` in console for diagnostics.
6. Check `logs/network.log` for connection transitions and errors.

### Multiplayer Troubleshooting
- Not visible in LAN list:
  - allow app in firewall (game + discovery UDP/TCP ports)
  - verify same subnet / disable AP isolation
  - run `lan_scan` and check `lan_status`
- Internet play:
  - LAN discovery is LAN-only
  - for internet, use manual `join` + port forwarding or VPN
- Quick sanity:
  - ping host machine first
  - test on LAN before WAN

## Update: Combat + States + TR + Carry (Latest)

### Controls (current)
- Survivor:
  - `WASD` move
  - `Mouse` look
  - `Shift` sprint
  - `Ctrl` crouch
  - `E` interact (vault/pallet/heal/repair)
  - `RMB` use equipped item
  - `R` drop equipped item
  - `LMB` pick up nearby ground item
  - `Space` skill check
- Killer:
  - `WASD` move
  - `Mouse` look
  - `LMB click` short swing
  - `Hold LMB` lunge (instant start + acceleration while held)
  - `RMB` use killer power
  - `E` secondary power action / interact (pickup/reset trap, break/hook)
- Global:
  - `~` console
  - `F1` debug HUD
  - `F2` collider/trigger debug
  - `F3` wireframe/filled
  - `F4` network debug
  - `F5` terror radius visualization

### New Console Commands
- `tr_vis on|off`
- `tr_set <meters>`

### Quick Test Checklist
1. Terror radius:
- press `F5`.
- ring around killer should stay readable through walls.
- test `tr_set 24` and `tr_set 32`.

2. Killer attacks:
- `LMB click` => short swing.
- `Hold LMB` => lunge starts immediately and accelerates while held.
- in debug, wedge triangle should be visible and highlight during attack.

3. Weapon visibility:
- control killer in FP.
- weapon box should be visible and move during charge/swing/lunge.

4. Survivor states + movement:
- healthy/injured/downed/carried color changes are visible.
- `Ctrl` crouch slows movement.
- downed survivor can crawl slowly.

5. Healing:
- injure survivor, then hold `E` for self-heal.
- progress should advance; skill checks appear; completion returns to Healthy.

6. Multiplayer carry fix:
- host picks up downed survivor.
- survivor should stay carried (no immediate drop).
- check runtime logs for carry validation/replication/drop reason.

7. Overhead debug labels (F1/F2):
- labels above players show ID, state, movement, speed, chase, and killer attack state.
- forward direction line should be visible.

## Update: Real In-Game Settings Menus

Player-facing menus and HUD now use the engine custom retained UI system (`engine/ui/UiSystem`).
ImGui is kept for developer/debug windows only (console/debug overlays/editor internals).

Quick UI debug toggles:
- `F6` -> custom UI Test Panel (buttons, dropdown, sliders, input, progress, keybind capture)

Settings are now available directly from:
- `Main Menu -> Settings`
- `Pause Menu -> Settings`

### Tabs
- `Controls`
  - action-based keybinds (primary + secondary)
  - rebind capture with conflict detection/override
  - survivor and killer mouse sensitivity
  - invert Y
- `Graphics`
  - display mode: `Windowed` / `Fullscreen` / `Borderless`
  - resolution list from OS
  - VSync
  - FPS cap
  - render mode
  - quality placeholders: shadow quality/distance, AA, texture quality, fog
  - `Apply` / `Cancel`
  - auto-revert safety (10s) for risky display changes
- `Gameplay (Tuning)`
  - movement/capsule/terror radius/vault/combat/heal values
  - loop generation weights and limits
  - networking tuning fields (tick/interpolation)
  - in multiplayer client mode values are read-only (`Server Values`)

### Config Files
- `config/controls.json`
- `config/graphics.json`
- `config/gameplay_tuning.json`
- `ui/theme.json` (UI colors/sizing)
- `ui/layouts/hud.json` (HUD placement + scale)

### Reset Settings
- Use reset buttons in Settings tabs, or
- delete files in `config/` and restart.

### Multiplayer Tuning Authority
- Host/server is authoritative for gameplay tuning.
- On connect, host sends current gameplay tuning to client.
- Clients cannot override gameplay tuning while connected.

### Default Action Bindings
- `MoveForward/MoveBackward/MoveLeft/MoveRight`: `W/S/A/D`
- `LookX/LookY`: mouse axis
- `Sprint`: `Left Shift`
- `Crouch`: `Left Ctrl` (`Right Ctrl` secondary)
- `Interact`: `E`
- `AttackShort`: `Mouse Left`
- `AttackLunge`: `Mouse Left` (hold)
- `ToggleConsole`: `` ` ``
- `ToggleDebugHUD`: `F1`

## Update: Real Level Editor Upgrade (Phase Foundation)

Editor now includes a real data-driven authoring foundation:
- `engine/assets/AssetRegistry` for import/cache/metadata and file operations
- Content Browser panel (import/create folder/rename/delete/list/drag source)
- Material assets (`assets/materials/*.json`)
- Transform animation clips (`assets/animations/*.json`)
- Environment assets (`assets/environments/*.json`)
- Map asset now references environment (`environment_asset`) and richer prop data

### Content Browser
- Open `Level Editor`.
- In `Content Browser`:
  - set `Import Path` to file path (`.glb/.gltf/.obj/.fbx`, `.png/.jpg/...`)
  - click `Import File`
  - imported files are copied to:
    - `assets/meshes`
    - `assets/textures`
  - sidecar metadata is generated: `<asset>.<ext>.meta.json`
- You can:
  - `Create Folder`
  - `Rename Selected`
  - `Delete Selected`

### Asset Placement
- In Map Editor:
  - choose/hover tile
  - drag file entry from Content Browser and drop on `Scene Drop Target`
  - or use `Place Selected Asset At Hovered`
- This creates a `MeshAsset` prop with serialized mesh path.

### Materials
- Use `Materials & Environment -> Material Editor`.
- Set:
  - shader type (`Lit/Unlit`)
  - base color
  - roughness/metallic/emissive
  - optional texture paths
- Save as JSON in `assets/materials`.
- Assign material asset path to prop in `Inspector`.

### Animation Preview (Transform Clips)
- Animation clips are JSON assets in `assets/animations`.
- Assign clip path in prop inspector (`Animation Clip`).
- Use `Play Preview Animation` / `Stop Preview Animation`.
- Preview affects editor rendering and is data-driven in map asset.

### Environment (Sky/Fog/Clouds/Directional Light)
- Use `Materials & Environment -> Environment`.
- Editable settings:
  - sky colors
  - cloud coverage/density/speed
  - directional light dir/color/intensity
  - fog color/density/start/end
  - shadow/tone-mapping/bloom hooks
- Save environment to `assets/environments`.
- Map references environment via `environment_asset`.
- Runtime loads map environment when starting saved map sessions.

### New/Extended Serialized Data
- `assets/maps/<map>.json` now includes:
  - `environment_asset`
  - per-prop:
    - `name`
    - `mesh_asset`
    - `material_asset`
    - `animation_clip`
    - collider setup (`type/offset/half_extents/radius/height`)

## Update: Next Phase (Mesh + Prefabs)

This phase adds two major editor/runtime-ready systems:
- real mesh preview in editor for imported mesh props (`.obj` currently)
- prefab workflow for reusable prop groups

### Mesh Preview (OBJ + glTF/glb)
- Module: `engine/assets/MeshLibrary` (cached loader)
- Renderer now supports `DrawMesh(...)` and draws mesh geometry in editor viewport.
- For `MeshAsset` props:
  - if `mesh_asset` points to valid OBJ/glTF/GLB in `assets/meshes`, mesh is rendered
  - fallback is still oriented box if mesh missing/unsupported

Current mesh import support:
- full preview support: `.obj`, `.gltf`, `.glb`
- accepted by content import but not yet parsed for rendering: `.fbx`

### Prefab System
- New asset type: `assets/prefabs/<prefabId>.json`
- In Map Editor:
  - `Save Selected Props As Prefab`
  - select prefab from list
  - `Instantiate At Hovered`
  - `Reapply Selected Prefab Instance`
- Each instantiated prop stores:
  - `prefab_source`
  - `prefab_instance`

This enables basic update flow after prefab changes.

### Quick Verify
1. Import OBJ into Content Browser (`Import File`).
2. Place asset on map (`Place Selected Asset At Hovered` or drag/drop target).
3. In Inspector set prop type `MeshAsset` and verify rendered mesh.
4. Select multiple props and save prefab.
5. Instantiate prefab in another tile.
6. Modify prefab file via editor and use `Reapply Selected Prefab Instance`.

## Update: Next Phase (Material/Animation Authoring)

Editor now has production-style authoring panels for reusable materials and transform animation clips.

### Material Library
- Window: `Materials & Environment -> Material Editor`
- New capabilities:
  - material asset library list (`assets/materials/*.json`)
  - `New / Load / Save / Delete` material directly in editor
  - `Assign Material To Selected Props` for bulk assignment
  - unsaved-change indicator
- Runtime/editor viewport now uses cached material loads for prop rendering color.
- Lit shader now uses material parameters in runtime/editor rendering:
  - `roughness`
  - `metallic`
  - `emissive_strength`
  - `shader_type = Unlit/Lit`

### Material Lab (Shader Ball Preview)
- Open `Materials & Environment -> Material Editor -> Material Lab Controls`.
- This enables a dedicated in-scene preview sphere near editor camera.
- You can tune:
  - lighting ON/OFF
  - directional and point lights separately
  - render mode (wireframe/filled)
  - sphere size/distance/auto-rotation
- Use `Align Camera To Lab` for fast setup and `Reset Lab Defaults` to restore preview rig.

### Animation Clip Library
- Window: `Materials & Environment -> Animation Clip Editor`
- New capabilities:
  - clip asset library list (`assets/animations/*.json`)
  - `New / Load / Save / Delete` clip
  - keyframe editing (time, position, rotation, scale)
  - keyframe operations: add/remove/sort
  - `Assign Clip To Selected Props` for bulk assignment
  - unsaved-change indicator
- Editor rendering now uses cached clip loads and supports:
  - per-prop loop override
  - per-prop animation speed multiplier

## Update: Next Phase (Map Lights: Point/Spot)

Map assets now include authored light instances used by both editor viewport and runtime gameplay.

### What is new
- New map light data in `assets/maps/<map>.json`:
  - `type`: `point` / `spot`
  - `position`, `rotation_euler`
  - `color`, `intensity`, `range`
  - `spot_inner_angle`, `spot_outer_angle`
  - `enabled`
- Renderer supports dynamic:
  - directional (environment)
  - point lights (up to 8)
  - spot lights (up to 8)
- Runtime (`Play Map` / solo-host flow) loads map lights and applies them automatically.

### Editor usage
- In `Map Editor` window use:
  - `Add Point Light`
  - `Add Spot Light`
- For intuitive placement:
  - enable `Light Placement Mode (LMB)`
  - choose `Light Type`
  - click on hovered tile to place light exactly at preview marker
- Manage list in `Lights`.
- Edit selected light in `Inspector`:
  - type, on/off, color, position
  - intensity, range
  - spot rotation + inner/outer cone angles
- Important:
  - light shading is visible in `Filled` viewport mode
  - `Auto Lit Preview` in editor forces `Filled` when lights are active

## Update: Loadouts (Items/Add-ons/Powers) + Bear Trap

New data-driven gameplay catalog is now available:

- `assets/items/*.json`
- `assets/addons/*.json`
- `assets/powers/*.json`
- `assets/characters/survivors/*.json`
- `assets/characters/killers/*.json`

Default assets are auto-generated on startup if missing.

### Survivor Item Loadout

- 1 item max + up to 2 add-ons.
- Base items implemented:
  - `medkit`
  - `toolbox`
  - `flashlight`
  - `map`

### Killer Power Loadout

- 1 power max + up to 2 add-ons.
- Implemented power:
  - `bear_trap`
- Example power add-ons:
  - `serrated_jaws`
  - `tighter_springs`

### Character Roster

- Survivors and killers are selected by character IDs from JSON assets.
- Survivors share gameplay rules; killers can define `power_id`.
- Main menu now includes character and loadout selectors (custom UI).

### Multiplayer Replication

- Snapshot now includes:
  - selected survivor/killer character IDs
  - survivor item/power loadout IDs
  - survivor item runtime (charges/active)
  - bear trap state list (position/state/escape data)

### New Console Commands

- `item_set <id|none>`
- `item_addon_a <id|none>`
- `item_addon_b <id|none>`
- `power_set <id|none>`
- `power_addon_a <id|none>`
- `power_addon_b <id|none>`
- `addon_set_a <id|none>` (alias for current role)
- `addon_set_b <id|none>` (alias for current role)
- `item_dump`
- `power_dump`
- `set_survivor <characterId>`
- `set_killer <characterId>`
- `item_respawn_near [radius]`
- `trap_spawn [count]`
- `trap_clear`
- `trap_debug on|off`

## Update: FX System MVP (VFX + Gameplay Hooks)

Nowy moduł `engine/fx` dodaje data-driven system efektów:
- assety FX: `assets/fx/*.json`
- runtime manager: `engine/fx/FxSystem`
- pooling instancji/cząstek (bez alokacji per-frame)
- parametryzacja assetu przez overrides przy spawnie

### Dostępne domyślne FX
- `hit_spark` (burst)
- `blood_spray` (burst kierunkowy)
- `dust_puff` (burst)
- `chase_aura` (loop)
- `generator_sparks_loop` (loop)

### Console (FX)
- `fx_list`
- `fx_spawn <assetId>`
- `fx_stop_all`

### FX Editor (in-engine)
- W `Level Editor` otwórz okno `FX Editor`.
- Workflow:
  - `New FX` tworzy nowy asset w edycji.
  - `Load Selected FX` ładuje asset z biblioteki.
  - `Save FX` zapisuje do `assets/fx/<id>.json`.
  - `Delete Selected FX` usuwa asset z dysku.
- Emittery:
  - lista emitterów + `Add Emitter` / `Remove Emitter`
  - edycja typu (`Sprite`/`Trail`), blend, lifetime/speed/size, velocity, curves, gradient
- Preview:
  - `Spawn Editing FX At Camera`
  - `Spawn Editing FX At Hovered`
  - `Stop Preview`

### Gameplay Hooks (MVP)
- killer hit: `hit_spark` + `blood_spray`
- drop/break pallet: `dust_puff` (+ spark przy starcie break)
- vault window/pallet: `dust_puff` (+ `hit_spark` dla fast vault)
- skillcheck success/fail: FX feedback
- chase start/active: `chase_aura` podążające za killerem

### Multiplayer Hook (Host -> Client)
- hostowe FX z `FxNetMode::ServerBroadcast` są serializowane i wysyłane do klienta
- klient odbiera packet i spawnuje efekt lokalnie
- to jest hook MVP pod dalszą rozbudowę filtrów (`OwnerOnly`, priorytety, LOD per-peer)

### FX Profiling
HUD/debug udostępnia:
- `FX Instances`
- `FX Particles`
- `FX CPU (ms)`

### Szybki test
1. Uruchom mapę i wpisz `fx_list`.
2. Sprawdź ręczny spawn: `fx_spawn hit_spark`, `fx_spawn blood_spray`, `fx_spawn chase_aura`.
3. W gameplay:
- uderz survivor -> hit FX
- zrzuć/rozwal paletę -> dust FX
- zrób vault -> vault FX

## Update: Audio System + Terror Radius

### What is new
- `engine/audio/AudioSystem`: miniaudio-based audio backend
- Bus system: Master, Music, Sfx, Ui, Ambience
- 3D spatial audio with distance attenuation
- Terror radius layered audio crossfade based on distance
- Audio settings UI in Settings menu
- Config persistence: `config/audio.json`

### Audio Assets (required for terror radius)
Place audio files in `assets/audio/`:

| Filename | Purpose |
|----------|---------|
| `tr_far.wav` | Distant terror sound (low ambience) |
| `tr_mid.wav` | Mid-range tension |
| `tr_close.wav` | Close proximity heartbeat/intensity |
| `tr_chase.wav` | Chase music (only during active chase) |

Supported formats: `.wav`, `.ogg`, `.mp3`, `.flac`

### Terror Radius Profile (DBD-like Stepped Bands)
- **Audio routing** (Phase B1):
  - **Survivor hears**: TR bands (far/mid/close) + chase override rules
  - **Killer hears**: ONLY chase music when actively chasing
  - Debug overlay shows: `localRole`, `tr_enabled`, `chase_enabled_for_killer`
- Profile: `assets/terror_radius/default_killer.json`
- Profile: `assets/terror_radius/default_killer.json`
- **Stepped bands** (NOT gradient):
  - **OUTSIDE** (distance > radius): All layers silent
  - **FAR** (0.66R < dist <= R): tr_far ON, others OFF
  - **MID** (0.33R < dist <= 0.66R): tr_mid ON, others OFF
  - **CLOSE** (0 <= dist <= 0.33R): tr_close ON, others OFF
- **Chase override**: When chase active, tr_chase ON, tr_close SUPPRESSED
- Smoothing: 0.15-0.35s crossfade on transitions only
- Base radius: 32m (configurable via `tr_radius <meters>`)
- Layer files: `tr_far.wav`, `tr_mid.wav`, `tr_close.wav`, `tr_chase.wav`

### Chase System (DBD-like)
- **Start conditions**: Survivor sprinting + distance <= 12m + LOS + in center FOV (+-35deg)
- **End conditions**: Distance >= 18m OR lost LOS > 8s OR lost center FOV > 8s
- Killer FOV: 87deg total (half 43.5deg)
- Center FOV: +-35deg from killer forward
- **Chase can persist indefinitely** if LOS/center-FOV keeps being reacquired

### Bloodlust System (DBD-like)
- **Tier 1**: At 15s in chase -> 120% speed multiplier
- **Tier 2**: At 25s in chase -> 125% speed multiplier
- **Tier 3**: At 35s in chase -> 130% speed multiplier
- **Reset triggers** (immediate): Hit survivor, stunned by pallet, break pallet, chase ends

### Console Commands (Audio/Chase/Bloodlust)
- `audio_play <clip> [bus]` - Play one-shot (bus: music|sfx|ui|ambience)
- `audio_loop <clip> [bus]` - Play looping clip
- `audio_stop_all` - Stop all audio
- `tr_debug on|off` - Toggle terror radius audio debug
- `tr_vis on|off` - Toggle terror radius visualization (F5)
- `tr_set <meters>` - Set terror radius distance
- `tr_radius <m>` - Alias for tr_set
- `tr_dump` - Print TR state, band, per-layer volumes
- `chase_force on|off` - Force chase state
- `set_chase on|off` - Alias for chase_force
- `chase_dump` - Print chase state, timers, conditions
- `bloodlust_reset` - Reset bloodlust to tier 0
- `bloodlust_set <0|1|2|3>` - Set bloodlust tier directly
- `bloodlust_dump` - Print bloodlust state and speed info
---
## Phase B Fixes (2026-02)

### Fixes Applied:
1. **Blood Pools** — Now spawn on **Injured OR Downed** (was only Downed)
2. **TR Audio Routing** — Killer never hears TR bands, Survivor-only
3. **Killer Look Light** — Debug visualization, survivor-only rendering, config commands
4. **Scratch Marks** — Thinner multi-segment streaks, faster spawn rate (0.08-0.15s)

### B1: Audio Routing
**Bug**: Killer heard TR bands after role switch
**Fix**: TR audio now checks local role every frame
**Result**: TAB role switch → Killer instantly hears silence

### B3: Blood Pools
**Bug**: Only spawned when Downed, not Injured
**Fix**: Spawn condition now `Injured || Downed`
**Visibility**: Killer-only, debug overlay support

### B4: Killer Look Light
**Bug**: Barely visible, no debug mode
**Fix**: Light now disabled for killer view, cone debug visualization
**Commands**: `killer_light on|off`, `killer_light_range <m>` (0-100m)

### B2: Scratch Marks
**Fix**: Thinner 3-segment streaks, faster spawn (0.08-0.15s interval)
**Visibility**: Killer-only (debug mode shows to survivor too)

- `scratch_debug on|off` - Toggle scratch marks debug overlay
- `scratch_profile <name>` - Load scratch profile (future)
- `blood_debug on|off` - Toggle blood pools debug overlay
- `blood_profile <name>` - Load blood profile (future)
- `killer_light on|off` - Toggle killer look light
- `killer_light_range <m>` - Set killer light range (0–100m)
- `killer_light_debug on|off` - Toggle killer light debug overlay


### Audio Test Checklist
1. Place audio files in `assets/audio/`
2. Run game, open console with `~`
3. Test: `audio_play tr_far music`
4. Test: `audio_loop tr_close music`
5. Start solo session, move killer near survivor
6. Observe stepped audio transitions (FAR->MID->CLOSE)
7. Test `tr_dump` for band/volume info
8. Test chase: start chase, verify tr_chase ON, tr_close SUPPRESSED
4. W MP (Host/Join) sprawdź, że FX hosta pojawiają się też u klienta.

---

## Update: Modular Blender Asset Pipeline (Optimisation Phase)

### What is new
- New modular Blender generation stack in `tools/blender/scripts/`:
  - `core/*` for config/scene/mesh/bake/export/validation helpers
  - `generators/*` for asset-specific generators (`rock`, `crate`, `pillar`)
  - `cli.py` as a single entry point (`list`, `generate`)
- New config file: `config/assets.json`
- New batch helper: `tools/blender/generate_batch.ps1`

### Why this was added
- Removes monolithic script coupling and enables incremental extension.
- Introduces automatic regenerate/skip behavior based on config modification time.
- Reduces initial high-poly rock generation pressure (uses lower subdivision baseline in modular generator).

### Config schema (minimal)
`config/assets.json`:
- `defaults.texture_size` (int)
- `defaults.bake_samples` (int)
- `defaults.use_gpu` (bool)
- `defaults.output_root` (string)
- `assets.<assetId>.generator` (`rock|crate|pillar`)
- `assets.<assetId>.variant` (string, optional)
- `assets.<assetId>.seed` (int, optional)
- `assets.<assetId>.scale` (`[x,y,z]`, optional)

### Quick smoke test
1. Run Blender CLI list mode and verify generators are listed.
2. Run generate mode with `config/assets.json`.
3. Verify outputs under `out/assets/`:
   - `<asset>.blend`
   - `<asset>.glb`
4. Re-run generate; unchanged assets should print `SKIP ... up-to-date`.

### Multiplayer impact
- None (tooling-side change only, no runtime netcode behavior change).

---

## Update: VFX System (Phase B)

### What is new
- **Scratch Marks** (DBD-like): Visual trail when survivor sprints
- **Blood Pools**: Spawn when survivor is injured/downed
- **Killer Look Light**: Spotlight cone for killer

### Scratch Marks (Phase B2)
- **Behavior**: Spawns behind survivor when sprinting
- **Visibility**: Killer-only (DBD-like); survivors cannot see own marks
- **Spawn interval**: 0.15–0.25s (data-driven)
- **Lifetime**: 30s default, fades over time
- **Pooled**: Ring buffer cap (default 64 marks)

Config: `assets/vfx/scratch_profiles/default.json` (future: from JSON)

Console commands:
- `scratch_debug on|off` - Toggle debug overlay
- `scratch_profile <name>` - Load profile (future)

### Blood Pools (Phase B3)
- **Behavior**: Spawns under injured/downed survivor
- **Visibility**: Killer-only (DBD-like)
- **Spawn interval**: 2s default (data-driven)
- **Lifetime**: 120s default, fades quadratically
- **Pooled**: Ring buffer cap (default 32 pools)

Config: `assets/vfx/blood_profiles/default.json` (future: from JSON)

Console commands:
- `blood_debug on|off` - Toggle debug overlay
- `blood_profile <name>` - Load profile (future)

### Killer Look Light (Phase B4)
- **Behavior**: Spotlight cone aligned with killer's forward
- **Range**: 14m default (configurable)
- **Angle**: 28° outer cone, 16° inner full-brightness
- **Color**: Reddish tint (1.0, 0.15, 0.1)

Console commands:
- `killer_light on|off` - Toggle killer look light
- `killer_light_range <m>` - Set light range (0–100m)
- `killer_light_debug on|off` - Toggle debug overlay

### VFX Test Checklist
1. Sprint as survivor, switch to killer → verify scratch marks visible
2. Get injured, move around → verify blood pools spawn
3. Use `killer_light on|off` to verify spotlight appears/disappears
4. Use debug overlays to confirm counts and values

---

## Update: Visibility / LOD Optimization (Current)

Render path now uses:

- **Dynamic actors**: 3-zone policy
  - inside frustum → full capsule
  - outside frustum but inside +10% edge buffer → low proxy box
  - outside buffer → culled
- **Benchmark high-poly meshes**: strict frustum culling + distance tiers
  - outside frustum → fully culled (no edge-buffer fallback)
  - visible + near → full geometry
  - visible + mid-range → medium LOD geometry
  - visible + far → oriented-box proxy
  - full-detail draws are capped per frame (nearest meshes first)

This prevents out-of-view high-poly cost while keeping nearby quality high.

---

## Update: Multithreading System (Job System)

### What is new
- **JobSystem**: Thread pool with N workers (auto-detected from CPU cores - 1)
- **RenderThread**: Command buffer for async render data preparation
- **AsyncAssetLoader**: Background asset loading using JobSystem

### Architecture
The game now uses a job-based multithreading system:

1. **JobSystem** (`engine/core/JobSystem.hpp`)
   - Thread pool with worker threads
   - Priority-based job queue (High/Normal/Low)
   - `ParallelFor` for data-parallel tasks
   - `JobCounter` for synchronization

2. **RenderThread** (`engine/render/RenderThread.hpp`)
   - Command buffer for render data
   - Double-buffered frame data
   - Allows async preparation of render commands

3. **AsyncAssetLoader** (`engine/assets/AsyncAssetLoader.hpp`)
   - Background file loading
   - Uses JobSystem for parallel loading
   - Callback-based completion notification

### Console Commands (Threading)
- `job_stats` - Show job system statistics (workers, pending jobs, completed)
- `job_enable on|off` - Enable/disable job system processing
- `job_test <iterations>` - Run parallel test (default: 10000 iterations)
- `asset_stats` - Show async asset loader statistics

### Usage Example
```cpp
// Schedule a job
engine::core::JobSystem::Instance().Schedule([]() {
    // Background work
}, engine::core::JobPriority::Normal, "my_job");

// Parallel for loop
engine::core::JobSystem::Instance().ParallelFor(count, batchSize, [](size_t i) {
    processItem(i);
});

// Async asset loading
engine::assets::AsyncAssetLoader::Instance().LoadAsync(
    "textures/character.png",
    engine::assets::AssetType::Texture,
    [](const engine::assets::AssetLoadResult& result) {
        if (result.state == engine::assets::AssetState::Loaded) {
            // Use loaded asset
        }
    }
);
```

### Performance Impact
- Parallel work distributed across all available CPU cores
- No synthetic per-frame CPU burn jobs; worker threads execute only real scheduled work
- Parallel culling: HighPolyMesh (for >256 meshes), StaticBatcher (for >256 chunks)
- Frame culling waits are scoped to local job groups via `JobCounter` (no global `WaitForAll()` stalls)
- Asset loading no longer blocks the main thread
- Maintains deterministic gameplay (fixed timestep physics on main thread)

### 2026-02-13 Regression Fix (CPU 80% -> normal)
- Removed test-only background workload from in-game frame loop.
- `ParallelFor` now captures callable safely (no dangling references) and falls back to sequential mode for tiny workloads.
- `JobCounter` wait path uses atomic wait/notify (no spin-yield busy loop).
- High-poly rendering now uses strict frustum culling + distance tiers (full / medium / proxy).
- Textured draw batches with adjacent same texture are merged to reduce draw calls.
- Profiler threading now reports frame-averaged worker utilization and average active workers (snapshot value kept only as instantaneous reference).

### Limitations
- OpenGL context is single-threaded (GPU submit on main thread only)
- Vertex buffer building (Renderer::DrawMesh) remains sequential
- For full parallel rendering, consider Vulkan/DX12 migration

### Verification
1. Run the game and start a solo session
2. Open Task Manager → Details → asym_horror.exe
3. Right-click → Set affinity → Observe multiple cores are active
4. Check Profiler overlay (F1) → Systems tab → Threading section
  - `Workers (snapshot)` = instantaneous sample (can be low between frames)
  - `Avg active (frame)` + `Worker utilization (frame)` = reliable load indicators
5. Use `job_stats` console command to see worker activity
6. Run benchmark map with high-poly meshes to see parallel culling
