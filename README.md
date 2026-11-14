# Frontier

General Purpose Editor — **physics-bearing rebuild**.

This repository is the clean restart of the Frontier engine and its authoring workspaces. It currently
contains plans only; code lands phase by phase, starting with the Jolt-backed physics floor.

---

## Plan set

| Document | Scope |
| :--- | :--- |
| [Documentation/Plans/PhysicsEnginePlan.md](Documentation/Plans/PhysicsEnginePlan.md) | Master plan: Jolt boundary, determinism tiers, tick model, fracture, deformables (3D FEM + XPBD), cloth, soft body, particles, fluid, networking, phases, validation gates |
| [Documentation/Plans/PhysicsFolderStructure.md](Documentation/Plans/PhysicsFolderStructure.md) | Canonical annotated folder tree for `Engine/SimulationSpace/**` and its integration points, with phase tags |
| [Documentation/Plans/ClothAuthoringPlan.md](Documentation/Plans/ClothAuthoringPlan.md) | Cloth workspace: pattern sheet → grid sampling → sewing → XPBD small-steps drape → bake and 1:1 DXF/SVG export |

---

## Non-negotiables (from the Frontier ruleset)

- **Jolt isolation:** `JPH/*` headers appear only inside `Engine/SimulationSpace/PhysicsSpace/Internal/`.
- **Fixed step only:** one clock (`FixedStepClock`), no variable delta time anywhere in simulation.
- **Authority is small:** deformables are authoritative on a 48–200 point CPU proxy; the dense surface is GPU-skinned and cosmetic.
- **Determinism is declared per domain** (tiers A/B/C) and verified by `SimulationHashRegister`.
- **No float atomics** on an authoritative path; colour-batched constraints, fixed index order, counter-based RNG.
- **Every validation emits `.png` visual proof** into `Exhibits/Images/<Domain>/`.
- **Identifiers** follow `Documentation/BannedWordsVocabulary.md` (`No Manager / System / Record / mesh / frame / node / table / stage / pipeline`).
