# CLAUDE_NOTES.md

## Current Status

As of 2025-02-11, the following systems have been implemented/rebuilt:

### Completed
- **Chase State Machine (DBD-like)**
  - FOV: 87deg total (half 43.5deg), center FOV +-35deg
  - Start distance <= 12m, end distance >= 18m
  - Lost LOS timeout 8s, lost center FOV timeout 8s
  - Console: `chase_force on|off`, `chase_dump`

- **Stepped Terror Radius Audio**
  - Bands: Outside (>R), FAR (0.66R < dist <= R), MID (0.33R < dist <= 0.66R), CLOSE (0 <= dist <= 0.33R)
  - Default radius: 32m
  - Chase override: tr_chase ON, tr_close SUPPRESSED
  - Smoothing: 0.15-0.35s on transitions only
  - Console: `tr_dump`, `tr_radius <m>`

- **Bloodlust System (DBD-like)**
  - Tier 1: 15s -> 120% speed
  - Tier 2: 25s -> 125% speed
  - Tier 3: 35s -> 130% speed
  - Resets on: hit, stun, pallet break, chase end
  - Console: `bloodlust_reset`, `bloodlust_set <0-3>`, `bloodlust_dump`

### File Hotspots

| File | Purpose | Key Areas |
|------|---------|-----------|
| game/gameplay/GameplaySystems.hpp | Chase/Bloodlust state structures | ChaseState, BloodlustState |
| game/gameplay/GameplaySystems.cpp | Chase/Bloodlust logic | UpdateChaseState, UpdateBloodlust |
| engine/core/App.hpp | TR band enum & profile | TerrorRadiusBand, TerrorRadiusProfileAudio |
| engine/core/App.cpp | TR stepped bands | UpdateTerrorRadiusAudio, DumpTerrorRadiusState |
| ui/DeveloperConsole.cpp | Console commands | chase_dump, tr_dump, bloodlust_dump |

## TODO List

- Network replication of bloodlust tier
- Test with actual audio assets
- Add visual indicator for bloodlust tier

## Known Issues

- None currently
