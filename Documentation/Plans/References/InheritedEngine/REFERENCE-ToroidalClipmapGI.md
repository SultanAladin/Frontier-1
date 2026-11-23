# Reference — Toroidal-Clipmap GI (EEVEE-Next-style, HiZ-backed)

Pointer + distilled architecture for the realtime global-illumination path. This lives in the CODE
repo (`Projects/Frontier`) as a findable stub; the **authoritative plan documents live in the DOCS
repo** (`C:\Users\OS\Documents\Frontier\EngineDocs`). Read those before implementing — this file is
the map to them, not a substitute.

---

## Authoritative sources (docs repo — `Documents\Frontier\`)

| Document | Owns |
|---|---|
| `EngineDocs/PLAN-VisibilityRenderer.md` | **The master plan.** §7 shadows (VSM+SMRT), §8 GI (horizon-scan + SH probe clipmap), §15 evidence ledger. |
| `EngineDocs/MAP-VisibilityRenderer.md` | Phase roadmap + status table (P5c → P6 → P7) and the shared-spine build order. |
| `Documentation/Research/POSTMORTEM-GI-FirstAttempt.md` | Why the first GI stack was retired (2026-07-21) — the dead ends NOT to re-walk. |
| `Documentation/Explainers/Explain-Clipmap-Toroidal-Scroll.html` | Interactive: how toroidal scroll addressing works. |
| `Documentation/Explainers/ToroidalClipmapVisualizer.html` / `…Visualizer3D.html` | 2D / 3D clipmap visualizers. |

Retired first-attempt source: `.retired/Attic-GI/` (copy-then-confirm; pull reference from there or
git history, not from memory).

---

## The recipe (distilled)

- **Model on EEVEE-Next, NOT UE5/Lumen.** The SDF-Lumen path was dropped entirely per user direction
  (no signed-distance-field GI). The chosen path is screen-space horizon-scan + world SH probes, all
  **compute**, **no hardware ray tracing**, on a **Pascal / GTX-1060** floor.
- **Toroidal clipmap = the shared adaptive-resolution addressing scheme** for BOTH the VSM shadow
  pages AND the GI probe volumes. Camera-tracked, nested cascades (fine-near + coarse-far). It is the
  shared primitive both consumers stand on — built ONCE before either.
- **HiZ** — `HierarchicalDepthPyramid` already exists in `RenderExtension` (max-reduce of the
  visibility depth, produced in the preamble). No cull/GI consumer wired yet; it is the depth spine
  the screen-space passes will read.

## Shared-spine build order (PLAN §13 / MAP)

```
P5c  ToroidalClipmapField   — the shared clipmap primitive (no dependency on cull or software raster)
 └─ P6  SunShadowClipmap + SMRT              — EEVEE-Next virtual shadow maps, soft ray-traced
 └─ P7  GLOBAL ILLUMINATION
        P7a  screen-space horizon-scan GI + AO   (SH-packed)
        P7b  GiProbeField  — world-space SH irradiance clipmap (far / off-screen indirect)
        P7c  composite
```

Both VSM and GI depend on the clipmap, so **P5c comes first**; the quartet can jump straight to it
since the clipmap has no dependency on cull or the software raster.

## First-attempt lessons (postmortem, do not repeat)

- A **single-resolution** world probe grid cannot serve object-scale near-field AND room-scale
  far-field at once → the camera-centred **clipmap cascades** (or screen-near + world-far split) is
  the correct answer, decided on paper BEFORE writing the grid.
- **Size the near cascade to the CAMERA, not to a scene-object union** — one large flat surface
  poisons a footprint-fit grid. Exclude nothing by footprint hacks.

---

_Status: all GI phases 🔴 not started. This is a saved reference, not active work._
