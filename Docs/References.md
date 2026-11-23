# References — orientation for this checkout

> **Provenance:** this file is this checkout's orientation/reference index.
> `CLAUDE.md` in the repo root is a **verbatim sync** of upstream
> `SultanAladin/Frontier` (branch `Frontier` @ `fd2a957`, synced 2026-10-22 —
> PR #6 merged upstream into that tip, and the tip merged into this checkout;
> the owner moved the code there from `arena/db2f9953-frontier` @ `c177821`, which
> is an ancestor — CLAUDE.md verified unchanged at the new tip)
> per owner instruction — do not edit it here; update by re-syncing.


This repository is the **portable overlay**: engine code that compiles anywhere
(`Frontier/Engine`), the editor host (`Frontier/Applications/Editor`), projects
(`Frontier/Projects`), proofs (`VisualProof`) and the approved engine plan
(`Docs/VehicleHmiEnginePlan.md`). The windowed interface SDK (dockspace, notch,
vendored ImGui/GLFW/ThorVG, `ProjectInterchange.h`) is supplied by the upstream
checkout below — never re-implement it here.

## Rules live upstream — do not diverge

Authoritative rules are `CLAUDE.md` + `SKILL-*.md` in
[SultanAladin/Frontier](https://github.com/SultanAladin/Frontier/tree/Frontier)
(branch `Frontier`; SDK contracts inspected at `676eceb`, byte-identical at
`c177821`, at the moved tip `9ec758d`, and at `fd2a957` (PR #6: preview
stage/lighting fixes under `Engine/Render`, no Interface changes) — verified by
diff; the only Interface delta remains `VulkanPresentation.cpp`, unused by
our panel). This checkout merged `fd2a957` on 2026-10-22 (unrelated
histories; overlaps resolved: upstream README, union `.gitignore`). PR #5
reclaimed the inherited `Applications/`, `Internal/`, `Executables/` trees
upstream — our overlay keeps its own `Frontier/Applications`; do not blindly
sync those deletions. Highlights that bind this repo:
Vulkan primary · Slang shaders · GLFW · TOML via toml++ · Windows binaries by
default · ask before pushing / GitHub Actions · 140-wide file banners, `//`+120-dash
rulers · banned identifier stems (Graph, Frame, Pass, Entity, Manager, … — see
SKILL-naming) · compact delivery with executed proof, never faked results.

## Full reference recovered — 2026-10-27

The VehicleHMI branch `arena/e9bd7db5-frontier` resolves to `6afb6f00b5f25c45f7cda92c7da65b64845768f3`.
Selected byte-identical sources and a blob manifest now live in `Experimental/VehicleHmiReference/`.
Unlike the local SlateReference copies, it includes independent warm/blue flare components, a lunar texture atlas,
160-particle XPBD and solid/curved-surface continuum tyre implementations. Luna's original JPEG is retained in
`EngineContent/CelestialMaps`. Do not repeat the earlier claim that those files do not exist in the owner's demo.
The existing C++ force/vehicle port matches the older spring-model source; the full continuum port is still open.
See `Exhibits/VehiclePhysics/Continuation.md` for executed source audits and limits.

## References (owner-supplied)

| Reference | Where | What it governs |
|---|---|---|
| Upstream SDK + TextureWorkspace UI | `SultanAladin/Frontier` @ branch `Frontier` (`fd2a957`, incl. PR #6) | panel dockspace + notch + ControlKit contracts; UI is **reused**, copied 1:1, never reinvented |
| VehicleHMI demo (HTML copied only) | `SultanAladin/Frontier` @ `arena/e9bd7db5-frontier`, `Experimental/VehicleHmiPrototype/` | the ReSTIR demo being ported; local copy: `Frontier/Projects/Project-VehicleHmiDemo/Reference/` |
| **Previous project (Windows)** | [`SultanAladin/Frontier-`](https://github.com/SultanAladin/Frontier-/tree/arena/9440f4d0-frontier) @ `arena/9440f4d0-frontier` (inspected `3382938`) | the owner's earlier full engine. Patterns/code may be ported **when judged good enough** (see `Docs/MaterialAndFlakesReport.md`). Holds: `Engine/Shaders/MaterialEvaluation.slang` (OpenPBR lobe library), `Engine/Shaders/AutomotiveFlakePaint.slang` (current flakes), `Engine/PhysicalDynamics/` (C++ rigid-body solver) |
| **Vehicle physics (Phase 7 copy source)** | previous project, `Experimental/VehicleHmiPrototype/Source/{TyreSlipSolver,VehicleSolver,VehicleSpecification}.js` | **Pacejka Magic Formula MF6.1** (pure-slip Fx/Fy + cosine combined-slip), itself a unit-for-unit port of a Frontier `PacejkaMagicFormula.cpp` / GRIT `ComputePacejka*`. No C++ copy survives — the JS is the reference. Do not port before the Phase 7 gate (plan §9); D6 is informed by this (custom Frontier forces over Jolt bodies) |

Owner standing preferences: **avoid Python tooling** (the previous project leaned
on ~143 exhibit runner scripts; here builds are native `.bat` / CMake / C++),
UI elements copied **1:1** with a parity checklist (`Exhibits/LogStream/PARITY.md`),
and blocked work reported as blocked — never simulated as done.
