# Frontier Cloth Authoring — Restart Research (Modern, 2024–2026)

**Status:** Old prototypes scrapped (`Engine/SimulationSpace/ClothSpace/` + `Experimental/ClothDemo/` deleted, viewer rebuilt, pushed to `arena/c2de3250-frontier-1` as `fd979c9`). This document is the fresh research basis for the restart.
**Target:** Authoring-grade cloth in the class of **Marvelous Designer / CLO** (pattern → drape → bake), not runtime game cloth and not offline film FEM.
**Rule:** No outdated research. Everything below is 2023–2026 production or SIGGRAPH/Eurographics/CVPR work. Pre-XPBD mass-spring and plain-PBD references are excluded except where noted as superseded.

---

## 1. What "authoring-grade" means (Marvelous Designer class)

Marvelous Designer is the reference: **2D pattern drafting → virtual sewing → real-time 3D drape on an avatar**, with fabric presets (weight, stretch, shear, bend, damping), collision/friction/wind, avatar morphs, and `DXF`/`FBX`/`Alembic` export [1](https://www.marvelousdesigner.com/explore/guide/best-3d-cloth-simulation-clothing-design-software-2026) [2](https://www.style3d.ai/blog/what-makes-marvelous-designer-essential-for-3d-clothing-design/). Its solver is proprietary and tuned for **expressive real-time drape** (speed + visual fidelity) rather than production fit accuracy — the opposite trade-off from Browzwear's fit-validation solver [4](https://aifashion.tech/blog/browzwear-vs-marvelous-designer-simulation-architecture-compared). Recent releases add **GPU trim (2026.0)** and **GPU soft body (2025.2)** [1](https://www.marvelousdesigner.com/explore/guide/best-3d-cloth-simulation-clothing-design-software-2026).

Frontier restart requirements derived from this:

| Requirement | Why |
|---|---|
| 2D pattern ↔ 3D drape sync (SVG/DXF) | Core Marvelous workflow; Frontier already has UVWorkspace split-viewport |
| Sewing constraints (seams, pleats, darts) | Garment construction, not just a draped sheet |
| Fabric presets with physical params | Silk vs denim vs wool must drape differently |
| Avatar morphs + pinning | Body presets already exist (preserved mannequin) |
| Real-time drape feedback (60 fps) | Authoring iteration, not offline bake |
| Body + self collision | Multi-layer garments, wrap overlaps |
| Bake + export (Alembic/FBX/DXF) | Handoff to engine/DCC |
| GPU acceleration | 20k-tri garments at interactive rates |

---

## 2. Modern solver landscape (2023–2026)

### 2.1 XPBD "small steps" — the production standard

**Extended Position-Based Dynamics (XPBD)** is the current industry standard for authoring and real-time cloth. It decouples results from timestep/iteration count and allows stiffness in the `100–1000+` range, aligning with Marvelous/Style3D behavior [5](http://apjcriweb.org/content/vol11no6/30.pdf).

- **Houdini Vellum** — unified **XPBD-based** solver for cloth/hair/softbody/grains (since H17). Adaptive substepping + optional GPU. Typical tuning: **substeps 4–8** (10+ for thin cloth), constraint iterations for stiffness [1](https://www.artivoxa.com/how-to-use-houdinis-vellum-solver-for-cloth-fabric-animations/) [2](https://tryskilly.app/learn/houdini-vellum-cloth-soft-body-and-hair/). Production VFX standard.
- **Unreal Chaos Cloth** — mass-spring → **XPBD since UE 5.3 (2023)**, Panel Cloth Editor, node-based solver, GPU-accelerated, up to 5 layers [5](http://apjcriweb.org/content/vol11no6/30.pdf) [5](https://www.unrealengine.com/blog/unreal-engine-5-3-is-now-available). Real-time multi-layer reference.
- **Blender 5.3 Cloth Dynamics** — **XPBD Solver node** with substeps + constraint iterations + residual-error metric (self-collision still missing, experimental) [4](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/simulation/cloth_dynamics.html).
- **MagicaCloth2 (Unity)** — **XPBD on GPU compute** (DOTS/Burst → GPU). PT/EE self-collision, backstop, edge collision. Reports **dozens of times faster on GPU** than CPU; self-collision (EE contacts) is the bottleneck [3](https://80.lv/articles/impressive-cloth-simulations-with-collisions-on-gpu-in-unity). Closest analogue to Frontier's WebGPU/.slang target.
- **dxge cloth (Go)** — XPBD "small steps" with long-range attachments, graph-coloured constraints, CPU + Vulkan GPU backends, Chaos Cloth parity [2](https://pkg.go.dev/go.digitalxero.dev/dxge/physics/cloth/clothgpu) [3](https://pkg.go.dev/go.digitalxero.dev/dxge/physics/cloth).

**2025 research confirmation:** Small timesteps with single iterations beat large timesteps with many iterations for stability; Energy-Aware XPBD improves stiffness behavior; differentiable XPBD enables material/force estimation from motion [1](https://diglib.eg.org/server/api/core/bitstreams/24bb67a8-a638-40ec-b14b-66023f04c87b/content). **DiffAvatar (CVPR 2024)** uses differentiable XPBD to recover simulation-ready garments from scans [2](https://openaccess.thecvf.com/content/CVPR2024/papers/Li_DiffAvatar_Simulation-Ready_Garment_Optimization_with_Differentiable_Simulation_CVPR_2024_paper.pdf).

### 2.2 AVBD / VBD — newest research (SIGGRAPH 2024–2025)

- **VBD (SIGGRAPH 2024)** — Vertex Block Descent: block-descent structure, millions of points, highly parallel.
- **AVBD (SIGGRAPH 2025, Utah)** — **Augmented** VBD with augmented Lagrangian for **hard constraints + extreme stiffness ratios**. Unconditional stability, parallel Gauss-Seidel + spatial hashing, **millions of objects at 100 fps on a consumer GPU**, open source [1](https://sciencereader.com/physics-simulation-avbd-method/) [2](https://github.com/LuckyIYI/avbd-metal) [3](https://github.com/tatsuya-ogawa/MetalAVBD) [5](https://bsky.app/profile/cemyuksel.com/post/3lsao4okiwm24).

AVBD is the strongest candidate for a **final bake** solver (hard constraints, high stiffness, scale). It is newer than XPBD and less proven for garment *authoring* UX, so the restart uses it as Phase 2, not Phase 1.

### 2.3 What we are NOT using (outdated / wrong trade-off)

| Approach | Why excluded |
|---|---|
| Mass-spring (pre-PBD) | Unstable, iteration-dependent, superseded by XPBD everywhere (Vellum/Chaos/Blender) |
| Plain PBD (no XPBD) | Stiffness depends on iteration count; superseded by XPBD small-steps |
| Global implicit Newton-PCG (our scrapped prototype) | Accurate but slow (~2,000 dispatches/frame at 1.75×); wrong trade-off for authoring iteration. Kept only as a possible offline bake reference, not the authoring solver |
| Offline implicit FEM (film) | High fidelity, hours per shot; impractical for real-time authoring |

---

## 3. Restart architecture proposal

### Phase 1 — XPBD small-steps authoring core (Marvelous-class drape)

- **Solver:** XPBD, **substeps 8–16, 1–2 iterations/substep** (Vellum/Chaos tuning). Constraints: stretch, shear, bend (Bergou quadratic), long-range attachments for inextensibility.
- **Host:** GPU compute — **WebGPU (WGSL) for Experimental studio + `.slang` for native `Engine/`** (matches Frontier's existing GPU direction). CPU reference for parity/verification only.
- **Collision:** Body SDF/grid + **PT/EE self-collision via spatial hash** (MagicaCloth2 bottleneck — budget EE carefully, use swept-AABB culling). Friction, wind, pinning.
- **Authoring:** 2D pattern (SVG/DXF) ↔ 3D drape sync in UVWorkspace split view; sewing constraints; fabric presets (digitized weight/stretch/shear/bend/damping); avatar = preserved mannequin (`Experimental/Shared/Mannequin/`).
- **Targets:** 20k-tri garment at 60 fps on discrete GPU; drape settles in <2 s; `0` body penetration; self-clip telemetry.

### Phase 2 — Bake + AVBD option

- **Bake:** Cache drape to Alembic/FBX; DXF pattern export (CLO parity).
- **AVBD bake:** Optional high-stiffness final bake (hard constraints, extreme ratios) using AVBD formulation. Only if Phase 1 XPBD proves insufficient for stiff garments (denim/leather).

### Phase 3 — Differentiable / AI-assisted (optional, 2025–2026 direction)

- Differentiable XPBD for material estimation from motion (Eurographics 2025); DiffAvatar-style garment recovery. Explicitly out of scope for Phase 1.

---

## 4. Preserved assets for restart

| Asset | Location | Notes |
|---|---|---|
| Mannequin body + engine | `Experimental/Shared/Mannequin/` (`FrontierMannequin.bin` 799K, `MannequinEngine.js`) | 10,764 verts / 21,524 tris, 5 body presets. Reusable avatar for new cloth. Not deleted. |
| Physics architecture plan | `Documentation/Plans/PhysicsArchitecturePlan.md` | Broader Jolt/FEM/ClothSpace plan. ClothSpace section to be rewritten per this research. Kept. |
| Old cloth docs | Deleted (`ClothAuthoringBenchmarkReport.md`) | Obsolete after scrap. Viewer rebuilt (38 docs). |

---

## 5. Immediate next steps (proposed)

1. Scaffold new `Experimental/ClothAuthoring/` studio (WebGPU XPBD core, 1.0× default, Studio preset).
2. Implement XPBD small-steps + stretch/shear/bend + body collision; verify drape smoothness (avg dihedral <5°) at 1.0×.
3. Add pattern/sewing + fabric presets; verify 2D↔3D sync.
4. Add PT/EE self-collision; verify multi-layer drape.
5. Native `.slang` port + bake/export.

*Awaiting go-ahead to scaffold Phase 1.*
