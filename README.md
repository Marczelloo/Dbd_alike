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
- `load map test|main|main_map|collision_test`
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
  - `Space` skill check
- Killer:
  - `WASD` move
  - `Mouse` look
  - `LMB click` short swing
  - `Hold LMB` lunge (instant start + acceleration while held)
  - `E` interact (pickup/break/hook)
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
- `F10` -> toggle legacy ImGui menus/HUD fallback

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
