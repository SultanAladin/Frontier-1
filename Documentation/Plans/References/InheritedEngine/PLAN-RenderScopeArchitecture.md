# PLAN — RenderScope Architecture: Coexisting Renderers, Strippable per Target

🧩 Implementation plan for splitting Frontier's single render coordinator into **coexisting
`RenderScope`s** — PBR shading, CAD solid shading, overlay (grid/gizmo/curve), and selection — each a
separate static lib that a build target either links or does not. Verified against Blender's draw
manager (10 coexisting engines), Unreal Engine 5's module-type stripping, and O3DE Atom's pass tree.

> **Headline:** the mess is a **build-graph** defect, not a code-style defect.
> `Internal/Graphics/Build.bat:63` hardcodes `/DFRONTIER_POLYGON_AUTHORING` into the one globbed
> `Graphics.lib`, so every target — including a future game — compiles the authoring renderer in.
> `#ifdef` cannot fix that. Strip at the **lib boundary** (UE's model), order scopes in a **fixed list**
> (Blender's model, no dependency solver), and keep `#ifdef` at **4 declaration sites** and never inside
> a function body (Blender's `WITH_DRAW_DEBUG` template).

🟢 **The include graph is already clean.** Verified file-by-file: the Radiance group includes *nothing*
from Solid/Overlay/Select, and those three have *zero* edges between them. The subsystems were already
written to this layering — this plan makes the build system honour a separation the code respects. The
sole violator is `RenderExtension.h:23–36`, which is exactly the file being rewritten.

---

## 1. The three defects

| # | Defect | Evidence | Consequence |
| - | ------ | -------- | ----------- |
| 1 | One lib globs all Graphics `.cpp`, authoring baked in | `Internal/Graphics/Build.bat:63,74` | 🔴 A game target links CAD + gizmo + selection code |
| 2 | One ~400-line recorder lambda holds every renderer + ~30 key toggles | `RenderExtension.cpp:1537–1935` | 🔴 Scopes cannot be added or removed |
| 3 | `RenderSchedule` exists, is correct, and is switched **off** | `RenderSchedule.h:43`, `RenderExtension.cpp:1880` | 🚩 Only sky + grid registered of ~12 steps |

💡 Defect 3 is good news: `RenderSchedule` **is** Blender's `foreach_engine`, already written with the
right signature and a default-OFF A/B gate. The spine does not need designing, only populating.

---

## 2. Verified dependency graph

Established by scanning every `#include` in `Internal/Graphics/**` (71 files).

```text
              ┌─────────────┐
              │ COORDINATOR │  RenderExtension.{h,cpp}  ← the ONLY violator
              └──────┬──────┘     (includes all 4 top groups)
        ┌────────────┼────────────┬──────────────┐
     SOLID        OVERLAY       SELECT           │      all 3 independent,
   (CAD B-rep)  (Grid,Clipmap) (pick,outline)    │      zero edges between them
        └────────────┴────────────┴──────────────┤
                                             RADIANCE   ← clean: NONE
                                                 │
                                            SUBSTRATE   (VulkanHost, Diagnostics)
```

| Check | Result |
| ----- | ------ |
| Radiance → any top group | 🟢 **NONE** |
| Solid ↔ Overlay ↔ Select | 🟢 **NONE** — mutually independent already |
| Top groups → Substrate / Radiance only | 🟢 correct direction (`VulkanHost`, `VisibilityDepth`, `VisibilityImage`) |
| Anything outside Solid includes `ParametricSketch*` | 🟢 **NONE** |
| Sole violator | 🚩 `RenderExtension.h:23–36` (the coordinator) |

⚠️ Select depends on `VisibilityImage` / `VisibilityDepth` — selection reads the id buffer the shading
raster already wrote. So **Select layers on Radiance; it is not a peer.** This reproduces Blender's
finding independently.

---

## 3. Concept map and naming

Names derived by the `SKILL-Naming.md` §0 procedure (mechanism → verb + substrate noun → 6-gate test),
not recalled.

| Concept | Name | Mechanism |
| ------- | ---- | --------- |
| One coherent renderer | `RenderScope` | owns its targets + pipelines; declares its own record step |
| Lazily-built handle to one | `ScopeSlot` | `nullptr` until first engaged — Blender's `DrawEngine::Pointer` |
| Ordered spine | `RenderSchedule` ✔️ **exists** | walk front-to-back; order **is** append order |
| Which scopes are live this frame | `ScopeEngagementTable` | mutually-exclusive shading + additive overlay |
| A stripped scope's substitute target | `BypassResolution` | O3DE Atom's `FallbackConnection` |

**Rejected names:** `Product` (commercial, not a mechanism) · `Pass`, `Stage` (banned, §1 2026-07-26)
· `Engine` (borrowed from known products, §0 gate 3) · `Renderer`/`Manager` (OO trope, gate 4).

📝 Pre-existing `GroundGridPass` / `DeferredShadePass` keep their names for now — both are already on
the SKILL-Naming deferred-rename list. New code adds no further `Pass`.

---

## 4. The five libs

| Lib | Guard define | Editors that link it | Game |
| --- | ------------ | -------------------- | ---- |
| `GraphicsSubstrate` | — | all | ✔️ |
| `GraphicsRadiance` | — | all | ✔️ |
| `GraphicsSolid` | `FRONTIER_SOLID_SCOPE` | Sketch | ✗ |
| `GraphicsOverlay` | `FRONTIER_OVERLAY_SCOPE` | all editors | ✗ |
| `GraphicsSelect` | `FRONTIER_SELECT_SCOPE` | Model, Paint, Sketch | ✗ |

File assignment (from the §2 scan):

| Lib | Contents |
| --- | -------- |
| Substrate | `RenderExtension/Device/*`, `RenderExtension/Diagnostics/*`, `RenderSchedule/*`, `Render/Resources/*` |
| Radiance | `Render/Radiance/*`, `Atmosphere/*`, `HierarchicalDepth/*`, `Shadow/*`, `Scene/*`, `Visibility/{VisibilityDepth,VisibilityImage,VisibilityRasterization,VisibilityInscription,SurfaceShadeInscription,InstanceCullSubmission,SoftwareRasterization,SurfacePartition}` |
| Solid | `Render/Surface/ParametricSketch*` |
| Overlay | `Grid/*`, `Clipmap/*` |
| Select | `Visibility/{ObjectPickReadback,SelectionOutlineInscription,ComponentOverlayInscription}` |

🔴 `Internal/Graphics/Build.bat:63` must stop hardcoding `/DFRONTIER_POLYGON_AUTHORING`. That single
line is what makes stripping impossible today.

---

## 5. Four architectural rules (each from a verified source)

| Rule | Source | Why it matters here |
| ---- | ------ | ------------------- |
| Shading scopes **mutually exclusive**; overlay **additive** | Blender `DRWContext::enable_engines` | Radiance *or* Solid wins the scene target; grid/gizmo always composites on top |
| Overlay owns a **separate target + colour space** | Blender `overlay_fb` (display-linear vs scene-linear) | ✔️ Already discovered independently — `RenderExtension.h:102` documents the same split |
| Selection runs **exclusively**; ID 0 = nothing; per-object `{vert,edge,face}` subranges | Blender `SELECTID_Context`, `ElemIndexRanges` | `NoSelectionSentinel` already matches; component subranges are the piece to add |
| A disabled scope declares a **bypass** per output | O3DE Atom `FallbackConnection` | Makes "CAD compiled out" provably safe rather than a dangling target |

**Cost of a scope compiled in but inactive: one null pointer.** That is what makes "all editors in one
application, still fast" true — three independent axes:

```text
compiled-in    ← lib linked?              (build target)
instantiated   ← ScopeSlot non-null?      (first engagement, ever)
active         ← engaged this frame?      (ScopeEngagementTable)
```

---

## 6. The 4 guard sites — Blender's `WITH_DRAW_DEBUG` template

```text
① Build.bat     if defined X → append .cpp sources AND add /DX   ← together, never apart
② declaration   #ifdef X   SolidScope Solid;        (the slot member)
③ schedule      #ifdef X   callback(Solid);         (the ordered list)
④ activation    #ifdef X   EngageScope(Solid, …);   (per-frame enable)
```

Function bodies stay clean because a stripped scope's **entire implementation lives in translation
units that are not compiled**. Blender guards `edit_select_debug` at exactly these four sites and
nowhere else.

⚠️ `if constexpr` **cannot** replace `#ifdef` here: the false branch must still parse and resolve
names, so a missing header or absent type still breaks the build. It is unusable for subsystem removal.

---

## 7. Requirements traceability

| Requirement | Mechanism |
| ----------- | --------- |
| 2 renderers, not always both on | Radiance + Solid are mutually-exclusive shading scopes; `ScopeEngagementTable` picks one per viewport |
| PBR shared by model / paint / bake, same scene + camera | One `GraphicsRadiance`; those editors differ only in which overlay/select scopes engage |
| CAD needs a dedicated pass | `GraphicsSolid` — already isolated, zero cross-edges ✔️ |
| Gizmo / grid / 2D curves / outliner-like | `GraphicsOverlay`, own display-referred target |
| Use only 1 / 2 / 3 … or all editors | Per-target `Build.bat` selects libs; unlinked = absent from the exe |
| Game with no bloat, "as if it doesn't know about CAD" | Game links Substrate + Radiance only — enforced by the **linker**, not by discipline |
| Selection for modelling + paint (+ maybe CAD) | `GraphicsSelect`; ID 0 = nothing, per-object `{vert,edge,face}` subranges |
| Texture paint edits materials on GPU, strokes back to CPU | Paint = Radiance + Select + Overlay; stroke readback reuses the `ObjectPickReadback` ring idiom |
| All editors at once, still fast | Inactive scope = one null pointer; see §5's three axes |
| Code not messy | `#ifdef` at 4 declaration sites only, never inside a function body |

---

## 8. Phase order and gates

| Ph | Work | Gate | Risk |
| -- | ---- | ---- | ---- |
| 1 | Split into 5 `Build.bat`; de-hardcode the define at `Build.bat:63` | each lib compiles and links alone | ✔️ |
| 2 | Lift ~30 key toggles out of the recorder → `ScopeEngagementTable` | pixel-identical to today | ✔️ |
| 3 | Register every scope as a `RenderSchedule` step; flip `EnabledCondition` ON | A/B vs the legacy inline path | 🚩 |
| 4 | Delete the ~400-line legacy recorder | — | ✔️ |
| 5 | `GameProfileValidation` target: Substrate + Radiance only | 🔴 must run in `Automation/ValidationRun.ps1` | 🚩 |

Phases 1–2 are mechanical moves behind a pixel-identity check. Phase 3 is where it becomes the new
architecture — and the A/B gate it needs is the one already built into `RenderSchedule`.

🔴 **Phase 5 is load-bearing, not an afterthought.** Godot's `disable_3d` has been broken continuously
from 2015 to 2025 (114 unresolved externals; still failing in 4.5). *Every* failure was a reverse
dependency from debugger / serialization / script-binding glue — never the renderer itself. A
compile-time toggle that CI does not build **rots**.

---

## 9. Rejected alternatives

| Rejected | Why |
| -------- | --- |
| Render graph / dependency solver (RDG, FrameGraph) | Frostbite justified it at **54 features, hundreds of passes** (BF4). Frontier has 4 scopes. Blender ships a hand-ordered list with `/* IMPORTANT: Order here defines the draw order. */` and no solver |
| `if constexpr` instead of `#ifdef` | Cannot remove a subsystem — the false branch must still parse and name-lookup (§6) |
| Runtime-virtual `IRenderer` | Pays indirection in the game build to buy flexibility only editors need |
| One lib + per-target defines | Requires rebuilding the lib per configuration; a stale lib silently links authoring code into the game |

Worth adopting later, independent of this plan: Frostbite's **transient resource aliasing** — the one
render-graph benefit available without a graph (reported >50% of resource allocation space).

---

## 10. Follow-ups

- `EngineDocs/FolderStructure.md` — update when the Graphics tree splits into 5 libs (CLAUDE.md requires it).
- Deferred renames this work touches: `GroundGridPass` → `GroundGridRasterization`,
  `DeferredShadePass` → `…Inscription`. Fold into phase 1 only on instruction.
- Component-selection subranges (`{vert,edge,face}` within one flat ID space) are additive to
  `GraphicsSelect` and can follow phase 3.

## 11. Evidence quality

| Claim | Basis |
| ----- | ----- |
| Include graph is clean; sole violator is the coordinator | 🟢 Verified — scanned all 71 files in `Internal/Graphics` |
| `Build.bat:63` hardcodes the authoring define into the shared lib | 🟢 Verified — read directly |
| `RenderSchedule` exists, default-OFF, 2 steps registered | 🟢 Verified — `RenderSchedule.h:43`, `RenderExtension.cpp:1060–1078,1880` |
| Blender: fixed ordered list, 4-site `WITH_DRAW_DEBUG` guard, separate overlay framebuffer, exclusive selection | 🟢 Verified — `draw_view_data.hh`, `draw_context.cc`, `select_engine.hh`, `draw/CMakeLists.txt` |
| Frostbite scale (54 features / hundreds of passes) | 🟢 GDC 2017, O'Donnell |
| Godot `disable_3d` breakage history | 🟢 Issues #1701, #89185, #103516, #103315 |
| UE editor-primitive internals (`AddEditorPrimitivePass`, `FEditorPrimitiveInputs` bodies) | 🚩 Symbol names corroborated by secondary sources; source is auth-gated and was **not** read |
