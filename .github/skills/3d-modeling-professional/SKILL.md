---
name: 3d-modeling-professional
description: Create precise, game-ready low-poly 3D models in Blender with clean topology, professional shading, correct UVs, real-time PBR materials, and (if required) production-ready rigs. Use this skill when the user asks to create props, environment assets, or character models for games or real-time engines.
license: Complete terms in LICENSE.txt
---

This skill guides the creation of **professional, production-grade low-poly 3D assets** that are technically correct, visually clean, and ready for export to real-time engines (GLB/glTF preferred).

The user provides the asset requirements: prop, environment object, or character. They may include style, poly budget, engine target, or references.

Your job is not just to “model something.”  
Your job is to model it like a senior game artist.

---

# Modeling Thinking Framework

Before opening Blender, define:

## 1. Purpose

- Where will this asset be used?
- What is the camera distance?
- Is it gameplay-critical or background filler?
- Is it static or animated?

## 2. Constraints

- Target poly budget (triangles).
- Texture resolution budget (512 / 1k / 2k).
- Export format (GLB preferred).
- Real-time PBR compatibility required.

## 3. Visual Intent

Commit to a modeling direction:

- Clean stylized low-poly
- Semi-realistic low-poly
- Chunky exaggerated stylization
- Hard-surface technical precision
- Organic simplified realism

Clarity of intention > random detailing.

---

# Professional Modeling Principles

## Silhouette > Detail

The silhouette must read clearly at gameplay distance.

If removing an edge does not affect silhouette or deformation:
→ Remove it.

Low-poly quality is about decision-making, not polygon count.

---

## Big → Medium → Small

Model in layers:

1. Primary form (blockout proportions)
2. Secondary forms (cuts, straps, panels)
3. Tertiary detail (mostly textures, not geometry)

Never start with micro detail.

---

## Intentional Topology

Good topology is:

- Clean
- Efficient
- Predictable
- Purposeful

For static props:

- Add geometry only where shape changes.
- Large flat areas stay low density.
- Curves get more segments.

For characters:

- Edge flow must support deformation.
- Extra loops around elbows, knees, shoulders, hips.
- Avoid triangles directly in bending zones.

Topology decisions affect:

- Shading
- UV quality
- Rigging
- Animation

---

# Professional Workflow

---

## Step A — Reference & Planning

- Gather 3–8 reference images.
- Identify key silhouette and defining features.
- Decide material separation.
- Decide if asset should be atlas-friendly.

Never model blindly.

---

## Step B — Blockout

Goal: proportions only.

Rules:

- Use primitives.
- No bevels yet.
- No unnecessary loops.
- Focus on scale and silhouette accuracy.

Fix proportion issues here — not later.

---

## Step C — Topology Pass

Use tools correctly:

- Extrude
- Inset
- Loop Cut
- Knife (only when needed)
- Bevel (controlled)

Rules:

- Avoid long skinny triangles.
- Keep edge spacing relatively consistent.
- Remove redundant loops.
- No internal faces.
- Ensure triangulation-safe export.

---

## Step D — Shading & Edge Control

Low-poly models often fail because of shading, not geometry.

Use:

- Auto Smooth
- Mark Sharp edges intentionally
- Small bevels to catch light

Goal:

- Clean highlights
- No random gradients
- No melted shading
- No unexpected faceting

Shading must look correct before texturing.

---

## Step E — UV Unwrap

Requirements:

- At least one UV map.
- Clean seam placement.
- No severe stretching.
- Proper island padding.
- Consistent texel density within asset set.

If seams are visible in gameplay → reposition them.

---

## Step F — PBR Materials

Use real-time friendly setup:

- Principled BSDF
- BaseColor
- Normal
- ORM (or AO / Rough / Metal separate)

Rules:

- No complex shader tricks incompatible with glTF.
- Validate look in viewport.
- Avoid accidental “plastic everywhere” look.

If baking:

- Validate cage distance.
- Check for projection artifacts.
- Verify normal orientation.

---

# Characters (If Required)

## Mesh Requirements

- Proper edge flow in joints.
- No collapsing geometry when bending.
- No unnecessary high density in static zones.

## Rig Requirements

- Clean hierarchy:
  - root
  - pelvis
  - spine
  - clavicles
  - arms
  - legs
- Only deform bones influence mesh.
- Normalize weights.
- No stray vertex groups.

## Deformation Test

Before export:

- Test extreme bends.
- Fix collapsing areas.
- Adjust topology or weights.

Include at minimum:

- idle animation
- walk animation

If deformation breaks → asset is not finished.

---

# Definition of Done

Asset is complete only if:

- Silhouette reads clearly.
- Topology is efficient.
- Shading is clean.
- UVs are packed correctly.
- Materials are real-time compatible.
- GLB export works correctly.
- (If character) Rig deforms properly.

If any of these fail → return to fix.

---

# Mandatory Self-Review Loop

After each stage (Blockout, Topology, Shading, UV, Materials), answer:

1. What reads well?
2. What looks wrong?
3. Where is geometry wasted?
4. Where is geometry missing?
5. What are the next 3 concrete fixes?

No skipping this step.

---

This skill produces assets that are:

- Engine-ready
- Cleanly optimized
- Technically correct
- Visually intentional
- Professionally structured

Never rush.
Never guess.
Model with purpose.
