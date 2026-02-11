# TRACE.md - Reproduction Steps

## Chase System Testing

### Test 1: Chase Start Conditions
**Expected**: Chase starts only when all conditions are met
1. Load main map with survivor and killer spawned
2. Move survivor within 12m of killer
3. Have killer face survivor (within +-35deg center FOV)
4. Survivor must be sprinting
5. **Result**: Chase starts (HUD shows "Chase: ON")

### Test 2: Chase End - Distance
**Expected**: Chase ends when survivor moves too far
1. Start chase (Test 1)
2. Move survivor to >= 18m from killer
3. **Result**: Chase ends immediately (HUD shows "Chase: OFF")

### Test 3: Chase End - Lost LOS
**Expected**: Chase ends after 8s of no LOS
1. Start chase
2. Break LOS (hide behind wall)
3. **Result**: Chase ends after 8s (timeSinceLOS reaches 8.0)

### Test 4: Chase End - Lost Center FOV
**Expected**: Chase ends after 8s outside center FOV
1. Start chase
2. Move to killer's side (outside +-35deg cone)
3. **Result**: Chase ends after 8s (timeSinceCenterFOV reaches 8.0)

### Test 5: Console Commands
1. Run `chase_force on` - chase starts immediately
2. Run `chase_dump` - prints all state values

## Terror Radius Testing

### Test 1: Stepped Bands
**Expected**: Distinct bands, no gradient
1. Place killer at origin
2. Move survivor to 35m (outside 32m radius)
3. **Result**: Silence (all volumes 0)
4. Move to 25m (FAR band: 21.12m < dist <= 32m)
5. **Result**: tr_far fades in over 0.15-0.35s
6. Move to 20m (still FAR band)
7. **Result**: tr_far stays ON
8. Move to 12m (MID band: 10.56m < dist <= 21.12m)
9. **Result**: tr_far fades out, tr_mid fades in
10. Move to 8m (CLOSE band: 0 <= dist <= 10.56m)
11. **Result**: tr_mid fades out, tr_close fades in

### Test 2: Chase Override
1. Start chase (tr_chase should be ON)
2. **Result**: tr_close volume = 0 (suppressed)
3. End chase
4. **Result**: tr_chase fades out, tr_close returns

### Test 3: Console Commands
1. Run `tr_radius 40` - radius changes to 40m
2. Run `tr_dump` - prints band and all layer volumes

## Bloodlust Testing

### Test 1: Tier Progression
**Expected**: Tiers at exactly 15s, 25s, 35s
1. Start chase
2. Run `bloodlust_dump` - tier 0, speed 100%
3. Wait 15s in chase
4. **Result**: Tier 1, speed 120%
5. Wait another 10s (total 25s)
6. **Result**: Tier 2, speed 125%
7. Wait another 10s (total 35s)
8. **Result**: Tier 3, speed 130%

### Test 2: Reset on Hit
1. Reach bloodlust tier 2 or 3
2. Hit survivor
3. **Result**: Bloodlust immediately resets to tier 0

### Test 3: Reset on Stun
1. Reach bloodlust tier 2 or 3
2. Get stunned by pallet
3. **Result**: Bloodlust immediately resets to tier 0

### Test 4: Reset on Pallet Break
1. Reach bloodlust tier 2 or 3
2. Break pallet while in chase
3. **Result**: Bloodlust immediately resets to tier 0

### Test 5: Reset on Chase End
1. Reach bloodlust tier 2 or 3
2. End chase (distance >= 18m or LOS lost >8s)
3. **Result**: Bloodlust immediately resets to tier 0

## Verification Checklist

- [ ] Chase starts only with sprinting + LOS + center FOV + <=12m
- [ ] Chase ends when >=18m OR lost LOS >8s OR lost center FOV >8s
- [ ] TR: stepped bands with silence outside radius
- [ ] TR: chase suppresses tr_close
- [ ] Bloodlust: tiers at 15/25/35s
- [ ] Bloodlust: resets on hit/stun/pallet/chase end
- [ ] Overlay: shows all debug info
- [ ] Console: all commands work
