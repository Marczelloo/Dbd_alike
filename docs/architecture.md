# Architecture

## 1. Engine modules

- `engine/core/App`:
  - app state machine (`MainMenu` / `InGame`)
  - fixed timestep loop (`Time`) with selectable tick (`30/60 Hz`)
  - split fixed simulation and rendering
  - host/client packet serialization + snapshot replication
- `engine/core/EventBus`:
  - lightweight pub/sub event queue
- `engine/net/NetworkSession`:
  - ENet wrapper (host/client/connect/disconnect/send/poll)
- `engine/platform/Window`, `Input`:
  - GLFW lifecycle
  - resolution/fullscreen/vsync handling
  - input snapshots (`down` / `pressed` / `released`)
- `engine/render/Renderer`:
  - wireframe and filled rendering
  - minimal Lambert directional light in filled mode
  - primitives: line, box, capsule, grid
- `engine/physics/PhysicsWorld`:
  - collision layers (`Player`, `Environment`, `Interactable`)
  - capsule movement with wall sliding and step handling
  - trigger volumes (`Vault`, `Interaction`, `Chase`)
  - LOS and ray tests
- `engine/scene/World`:
  - lightweight component storage (entity -> component maps)

## 2. Gameplay systems

`game/gameplay/GameplaySystems` manages:

- scene loading (`test` / `main` / `collision_test`)
- survivor + killer movement and role-based camera mode
- interaction resolver (vault/pallet/hook/generator/pickup)
- interaction input buffering (`E` buffered in fixed tick)
- chase state machine
- vault logic with type selection (`Slow` / `Medium` for windows)
- pallet state machine (`Standing` / `Dropped` / `Broken`) and break timer
- killer swing cone + hit validation
- survivor FSM, carry wiggle, hook stages, skill checks
- snapshot build/apply for multiplayer replication

## 3. Multiplayer model

- model: client-server (listen server)
- host:
  - authoritative fixed simulation
  - receives input packets from client
  - simulates gameplay and sends snapshots
- client:
  - sends input packets
  - applies authoritative snapshots with interpolation

Replicated minimum state:
- survivor/killer transforms + velocity
- survivor health FSM state
- chase debug state
- pallet states/transforms
- map type + generation seed

## 4. Map & loop generation

`game/maps/TileGenerator` provides:

- `GenerateTestMap()` sandbox
- `GenerateCollisionTestMap()` for wall sliding/snag tests
- `GenerateMainMap(seed)` procedural 8x8 tile placement

Main map now uses local-tile geometry prefabs (`0..15` local coordinates per tile):
- `Killer Shack`
- `L-T Walls`
- `JungleGymLong`
- `JungleGymShort`
- `FourLane`
- `Filler`

Each tile has:
- `staticMeshes` (wall segments / obstacle boxes)
- `interactables` (window / pallet)
- `Rotate(int degrees)` for `90/180/270` transforms

Generation constraints:
- deterministic seed for archetype + rotation selection
- checker `GetDistanceToNearestMaze(currentTile)`:
  if distance `< 2 tiles`, prefab is replaced with `Filler`

Generator output includes tile debug metadata (`center`, `bounds`, `loopId`, `archetype`) used by debug overlay.

## 5. Debug/UI/Console

`ui/DeveloperConsole`:
- command registry + autocomplete + history
- first-open discoverability (`help` auto print)
- HUD overlay with:
  - role/camera/render mode
  - controls hint
  - chase + interaction + physics debug
  - nearest loop tile id/archetype

Networking and simulation commands include:
- `host`, `join`, `disconnect`
- `set_tick 30|60`
- `regen_loops [seed]`
- `skillcheck start`

## 6. Add a new mechanic

1. Add component data in `engine/scene/Components.hpp`.
2. Extend fixed logic in `GameplaySystems`.
3. Extend collision/triggers in `PhysicsWorld` if needed.
4. Add debug draw + HUD output for verification.
5. Expose optional dev commands in `DeveloperConsole`.

## 7. Add a new tile/archetype

1. Implement emitter in `game/maps/TileGenerator.cpp`.
2. Register prefab in `EmitTile(...)` and generation rules.
3. Push tile debug metadata for overlay.
4. Validate with `load map main` + `regen_loops [seed]`.
