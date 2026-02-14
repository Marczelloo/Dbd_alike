# Wraith Cloak Shader System

This document describes the Wraith cloak shader implementation for the Dead by Daylight-style "watery invisible" effect.

## Render Order

```
1. Render world normally (BeginFrame -> Render -> EndFrame)
2. Capture backbuffer to texture (glCopyTexImage2D)
3. Render cloaked Wraith with refraction shader
   - Samples captured scene texture
   - Applies distortion, rim lighting, noise-based transition
   - Uses dithered alpha for transparency
```

## Required Textures

| Texture | Unit | Format | Description |
|---------|------|--------|-------------|
| `uSceneColor` | 0 | RGBA8 | Captured backbuffer (scene behind Wraith) |
| `uNoiseTex` | 1 | R8 | 256x256 grayscale noise for breakup/transition |
| `uDistortTex` | 2 | RG8 | 256x256 distortion vectors for refraction |

## Shader Uniforms

### Matrices & Transform
- `uViewProj` (mat4) - View-projection matrix
- `uModel` (mat4) - Model matrix for Wraith

### Camera & Position
- `uCameraPos` (vec3) - World-space camera position
- `uWraithPos` (vec3) - World-space Wraith position
- `uScreenSize` (vec2) - Screen resolution in pixels

### Capsule Dimensions
- `uCapsuleHeight` (float) - Height of Wraith capsule
- `uCapsuleRadius` (float) - Radius of Wraith capsule

### Tuning Parameters (Recommended Defaults)

| Uniform | Default | Description |
|---------|---------|-------------|
| `uCloakAmount` | 0.0-1.0 | Cloak state (0=visible, 1=cloaked) |
| `uRimStrength` | 0.8 | Fresnel rim intensity |
| `uRimPower` | 3.0 | Fresnel falloff exponent |
| `uDistortStrength` | 0.015 | UV distortion amount |
| `uNoiseScale` | 2.0 | Tiling scale for noise texture |
| `uNoiseSpeed` | 0.4 | Animation speed of noise |
| `uBaseCloakOpacity` | 0.10 | Minimum opacity when fully cloaked |
| `uTransitionWidth` | 0.15 | Width of noise-based transition edge |
| `uTime` | - | Current time for animation |

## Tuning Guide

### More Visible Cloak
- Increase `uBaseCloakOpacity` (e.g., 0.2)
- Increase `uRimStrength` (e.g., 1.2)
- Decrease `uRimPower` for broader rim (e.g., 2.0)

### More Invisible Cloak
- Decrease `uBaseCloakOpacity` (e.g., 0.05)
- Decrease `uRimStrength` (e.g., 0.5)
- Increase `uRimPower` for sharper rim (e.g., 4.0)

### Stronger Distortion
- Increase `uDistortStrength` (e.g., 0.025)

### Slower/Faster Transition
- Adjust `uTransitionWidth` (smaller = sharper edge)

## Dithered Alpha (Transparency Strategy)

The shader uses **Option A: Dithered Alpha** to avoid sorting artifacts:

```glsl
float bayer4x4(vec2 screenPos)
{
    // 4x4 Bayer matrix lookup
    // Returns threshold 0.0-1.0
}

// In main():
if (cloakAlpha < bayer4x4(gl_FragCoord.xy))
{
    discard;
}
```

This allows:
- Depth write ON (no sorting issues)
- Blending OFF (no transparency artifacts)
- Approximates transparency via screen-door pattern

## Debug Controls

| Key | Action |
|-----|--------|
| F8 | Toggle cloak debug mode |
| F9 | Toggle cloak state (when debug enabled) |

When debug mode is ON, a test capsule appears at position (0, 1, 3) with the cloak effect.

## Files

| File | Purpose |
|------|---------|
| `engine/render/SceneCaptureFBO.hpp/cpp` | Framebuffer object for scene capture |
| `engine/render/WraithCloakRenderer.hpp/cpp` | Cloak shader renderer |
| `engine/core/App.cpp` | Integration and debug toggles |

## Known Limitations

1. **Frame latency**: Scene texture is captured from previous frame position. For fast-moving cameras, there may be slight misalignment.

2. **No depth-based intersection**: The cloak effect doesn't properly intersect with geometry in front of the Wraith. Future improvement: sample scene depth texture and adjust alpha based on depth difference.

3. **Single Wraith only**: Currently designed for one cloaked entity. Multiple cloaked entities would need sorted rendering.

4. **Capsule geometry only**: Current implementation renders a capsule mesh. For actual Wraith character model, replace capsule with character mesh.

## Future Improvements

1. **Depth-based soft intersection** - Sample `uSceneDepth` and compare with Wraith depth for softer edges
2. **Motion blur** - Add velocity-based blur during fast movement
3. **Character mesh** - Replace capsule with actual Wraith 3D model
4. **Integration with power system** - Connect to existing Wraith cloak power state
