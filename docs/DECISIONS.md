# DECISIONS.md - Architecture Decisions Rationale

## Terror Radius: Stepped Bands vs Gradient

### Decision
Use stepped bands for TR audio instead of distance-based gradient.

### Rationale
1. **DBD Fidelity**: Dead by Daylight uses distinct "heart beat" stages that are stepped, not smooth
2. **Gameplay Clarity**: Players can clearly tell which band they're in
3. **Performance**: Simpler logic, no per-frame intensity calculations
4. **Audio Design**: Each layer can have its own character (Far = slow thump, Close = fast pounding)

### Trade-offs
- Pro: Clear gameplay feedback (players know exactly where they stand)
- Con: Less smooth audio transitions (mitigated by 0.15-0.35s smoothing)

## TR Smoothing Duration

### Decision
Use 0.15-0.35s crossfade only on band transitions.

### Rationale
1. **Audio Quality**: Prevents clicking/popping when switching bands
2. **Gameplay Accuracy**: Short smoothing keeps the stepped feel intact
3. **DBD-like**: Matches the "heart beat acceleration" feel

## Bloodlust: Immediate Reset vs Decay

### Decision
Reset bloodlust to tier 0 immediately on hit/stun/pallet break/chase end.

### Rationale
1. **DBD Mechanics**: In DBD, bloodlust resets completely when these events occur
2. **Killer Counterplay**: Survivors can deliberately reset bloodlust by forcing pallet breaks
3. **Simplicity**: No complex decay curves to tune

### Trade-offs
- Pro: Clear reset points, strategic for survivors
- Con: Punishes killer heavily (requires new 35s chase for max tier)

## Bloodlust: Server-Authoritative vs Local Compute

### Decision (Current Implementation)
Compute bloodlust locally for now; defer server replication.

### Rationale
1. **Simplicity**: Full networking overhaul not in scope for this phase
2. **Performance**: No extra network bandwidth
3. **Local Testing**: Allows immediate testing without multiplayer setup

### Future Work
- Make bloodlust tier server-authoritative
- Replicate tier in Snapshot
- Clients read replicated value, don't compute independently

## Chase: Center FOV Constraint (+-35deg)

### Decision
Require survivor to be within +-35deg of killer's forward to START chase.

### Rationale
1. **DBD Mechanics**: Killer must be "looking at" survivor to start chase
2. **Counterplay**: Survivors can break LOS without triggering chase by staying peripheral
3. **Clarity**: Clear visual feedback when chase actually starts

### Trade-offs
- Pro: Prevents accidental chase activation
- Con: Survivors can "orbit" killer just outside center FOV

## Chase: Timeout Values (8s)

### Decision
Use exactly 8.0s for both lost LOS and lost center FOV timeouts.

### Rationale
1. **DBD Reference**: Matches Dead by Daylight's chase lose timing
2. **Gameplay Balance**: Long enough for brief jukes, not so long that chase feels unfair
3. **Consistency**: Single timeout value for both conditions

## Constants Summary

| Constant | Value | Source |
|----------|--------|--------|
| Killer FOV | 87deg (half 43.5deg) | CLAUDE.md spec |
| Center FOV | +-35deg | CLAUDE.md spec |
| Chase start distance | <= 12m | CLAUDE.md spec |
| Chase end distance | >= 18m | CLAUDE.md spec |
| Lost LOS timeout | 8.0s | CLAUDE.md spec |
| Lost center FOV timeout | 8.0s | CLAUDE.md spec |
| TR base radius | 32m | CLAUDE.md spec |
| TR band thresholds | 0.33R, 0.66R | CLAUDE.md spec |
| Bloodlust tier 1 | 15s -> 120% | CLAUDE.md spec |
| Bloodlust tier 2 | 25s -> 125% | CLAUDE.md spec |
| Bloodlust tier 3 | 35s -> 130% | CLAUDE.md spec |
