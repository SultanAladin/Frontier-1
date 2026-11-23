# TextureWorkspace — owner complaint report & audit notes (2026-10-22)

**Status: 📋 documented only.** Per owner instruction nothing in TextureWorkspace /
Pattern Studio is changed yet; this report is the fix-phase backlog. Complaints are
the owner's, from hands-on use of the Windows build (`c177821` / `db6524f0` title
bars). Evidence: six owner screenshots in `Docs/TextureWorkspaceEvidence/` plus a
read-only code audit of `SultanAladin/Frontier` @ `c177821` (this repo does not
contain that code; findings below cite it by path).

**Recorded decision (deferred):** the current flake model will be replaced by
**Glinty** (constant-time glinty NDFs — see `Docs/MaterialAndFlakesReport.md`),
but **not now**: the workspace must be stabilised first (this report), because the
code "has too many bugs" to swap shading models on top of.

## Defect register

| # | Area | Complaint (owner) | Evidence | Severity |
|---|---|---|---|---|
| C1 | Stability | crashes randomly, esp. while editing sliders (both studios) | Shot 4230: "Vulkan UI failure: -1000069000"; shots 4228/4231 during zoom/edit | 🔴 blocker |
| C2 | Material library | keeps re-rendering all thumbnails — must be a baked atlas, re-render only on save | shots 4227–4232 (131 materials re-rendered) | 🔴 perf + likely C1 driver |
| C3 | Viewport | renders **two** preview shapes, one wrong/ghosted | shots 4228 (ghost ball right), 4229 (plane + stray cube), 4231 (second ball top-right) | 🔴 correctness |
| C4 | Viewport camera | rotate does not orbit the object; object tilts in Weird directions; scene feels wrong | owner-observed; shot 4229 plane orientation | 🟠 usability |
| C5 | Materials | sliders exist but shaders aren't editable: "procedural shader family is not ported yet" | shot 4232 tooltip; `UI/TextureStudio/MaterialLibraryPanel.cpp:924` | 🟠 completeness |
| C6 | Material fidelity | not audited 1:1 vs Alloy HTML: LED not 100%; strawberry flat; orange flat; cactus, petals, leather, leaf wrong/flat | owner-observed vs HTML; profiles exist (`MaterialRecipeProfiles.cpp`: petal 33, berry/strawberry 34, cactus 35) but profiles ≠ finished shaders | 🟠 fidelity |
| C7 | Perf | Scratches material/shader extremely slow to evaluate; HTML version is fast | owner-observed; bake path `Engine/Render/MaterialProceduralBaking.cpp` to audit | 🟠 perf |
| C8 | Pattern Studio | not a true SVG pattern generator; low-resolution output (SVG must be crisp); shapes evaluated incorrectly | code audit below | 🔴 design |
| C9 | Pattern Studio | layer-stack redo + needed functions were requested previously, not delivered (2 days → broken prototype); crashes on edit | `PatternWorkspacePanel.cpp:1248` stored groups "not ported yet", `:2536` paint/image masks "not ported yet" | 🟠 completeness |

## C1 — crash analysis (hypothesis, to confirm from the diagnostic log)

`-1000069000` is `VK_ERROR_OUT_OF_POOL_MEMORY`. The ImGui Vulkan backend is
initialised with `DescriptorPoolSize = 128` (`Engine/Interface/VulkanPresentation.cpp:188`)
while the library holds **131** materials (screenshot counters) plus viewport and
inspector targets. If thumbnails/preview targets consume pool descriptor sets and
are re-created on re-render (C2) instead of being baked once into an atlas, the
pool exhausts exactly the way the owner experiences it: fine at start, fatal while
scrubbing sliders. **Confirm against** `C:\Users\OS\AppData\Local\Frontier\Logs\`
(the crash dialogue names the file) before fixing. Fix directions for the phase:
bake thumbnails to an atlas (one descriptor), size pools from content counts,
and treat pool exhaustion as a recoverable diagnostic, never a "Severe failure".

## C2 — library re-rendering

Partial mitigation exists for the *inspected* material only: procedural gestures
set `ProceduralDirty` and rebake once when the mouse is released
(`MaterialLibraryPanel.cpp:1127–1132`). The owner observes the whole 131-item
library re-rendering — the thumbnail path (`PatternThumbnailRaster.*`,
`Engine/Render/MaterialLibraryRendering.cpp`) must be audited for invalidation
scope. Requirement: thumbnails are a **baked atlas**; a cell re-renders only when
its material is saved.

## C8/C9 — Pattern Studio is not SVG (code-confirmed)

ThorVG is linked but used **only for icons** (`Engine/Interface/IconArt.cpp`,
`Internal/EngineContext/Interface/Icons/SvgRasterizer.cpp`). The pattern canvas
draws shapes itself with immediate `ImDrawList` calls (`AddPolyline` at
`PatternWorkspacePanel.cpp:979`, rects/cards throughout) and thumbnails via its
own `PatternThumbnailRaster`. That is a home-grown tessellating renderer:
curves are polyline approximations at a fixed segment count, so output is neither
resolution-independent nor crisp when scaled — exactly the owner's complaint.
Owner's standing requirement (re-stated for the fix phase):

- patterns are **true SVG**, rendered by **ThorVG** on the canvas;
- one shape is evaluated **once**, then arrayed/scaled/rotated as instances —
  evaluation must be correct at every scale, edges always crisp;
- composition is a **layer stack** with the functions a pattern generator needs
  (the previously requested redo), and it must be able to generate **any** pattern.

## C3/C4 — viewport

Screenshots show the preview mesh twice (a correct one + a ghost at the right
edge / a stray cube beside the plane) and orbit that tilts instead of orbiting
the object. Fix-phase audit: single preview instance; orbit pivots on the object
centre with a stable up axis; plane/cloth/ball swaps must not leave the previous
mesh in the scene (shot 4229 suggests the swapped-out cube remains). Shot 4227's
"Draped cloth" also reads as a blobby drape rather than cloth — include in the
fidelity audit (C6).

## Fix-phase acceptance criteria (recorded, not started)

1. Slider-fuzz every inspector control in both studios for minutes: **zero crashes**;
   Vulkan pool/allocation failures surface as log diagnostics, never process death.
2. Material library renders its atlas **once**; only a saved material's cell updates.
3. Exactly **one** preview shape; orbit/zoom/pan behave like the HTML (orbit about
   the object, stable up).
4. Every recipe parameter backed by a ported shader family — no "not ported yet"
   survivors; per-material screenshot audit vs Alloy HTML signed off (LED,
   strawberry, orange, cactus, petal, leather, leaf explicitly).
5. Scratches evaluation at HTML-class speed (profile first; no silent quality cut).
6. Pattern Studio: ThorVG-rendered true SVG, single-shape evaluation arrayed at
   any scale with crisp edges, layer stack + full generator functions.
7. Only after 1–6: swap flakes to Glinty (decision above).
