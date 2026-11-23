# Important Notes — running notepad

Durable facts, decisions, reminders for the Authoring migration. Newest on top; prune when stale.

---

## 🔴 ANTI-DUPLICATION DIRECTIVE — mandatory gate on EVERY port (2026-07-27, user-issued)

The new codebase (`Projects\Frontier`) must stay **completely DRY**. Priority is **consolidation and
reuse**, not copying. Before porting ANY file, dependency, or utility from `RetiredProject\Engine\`,
run a **Code Existence Check** against the destination and follow this decision tree — no exceptions:

| Situation | Required action |
|---|---|
| Logic ALREADY EXISTS in the new project (even renamed/refactored) | 🔴 Do NOT port. Do NOT duplicate. Rewire the file being worked on — repoint `#include`s + call sites to the existing new implementation. |
| Does NOT exist, but belongs to an existing new file — **Option A** | Merge/add the ported logic INTO that existing file. No new file. |
| Does NOT exist, no logical home — **Option B** | Create a new file at its correct `FolderStructure.md` home, only after confirming zero overlap with existing systems. |

**Workflow per dependency:** (1) scan `Internal/` (Glob by name + Grep for the class/function/struct);
(2) if found, read it to confirm it's the same logic; (3) rehook instead of porting. Never drop an old
file into a new folder blindly. Verify, don't assume — renamed equivalents already caught: `VectorAlgebra`
→ `LinearAlgebra_Float64`, `WorkerPool`, `IntersectionSolver`.

---

## Migration rehook contract — retired math deps ALREADY EXIST (ported under new names) (2026-07-27)

🔴 Do NOT re-port `VectorAlgebra` or `IntersectionSolver` from the retired tree — the destination
already carries their ported equivalents. A first attempt copied a stale `VectorAlgebra.{h,cpp}` in and
it was a worse duplicate of the existing file; removed.

| Retired include (Authoring uses) | Destination equivalent (already present) | Type rename |
|---|---|---|
| `"VectorAlgebra.h"` / `"../../../EngineInfrastructure/Math/VectorAlgebra.h"` | `"LinearAlgebra_Float64.h"` (`EngineContext/Math/`) | `Vector2`→`Vector2d`, `Vector3`→`Vector3d` |
| `"IntersectionSolver.h"` | `"IntersectionSolver.h"` (`EngineContext/Math/`) — already ported, in `namespace Frontier`, uses `Vector3d` | — |
| `"WorkerPool.h"` | `"WorkerPool.h"` (`Platform/Concurrency/`) — already present | verify signature |

**Per-Authoring-file rehook transform for vector math:**
1. `#include "VectorAlgebra.h"` (any relative form) → `#include "LinearAlgebra_Float64.h"`
2. `Vector2` → `Vector2d`, `Vector3` → `Vector3d` (whole-word)
3. Function names are identical (`AddVector`, `CrossProduct`, `EvaluateVectorLength(Squared)`,
   `NormalizeVector`, `DotProduct`, `ScaleVector`, `ClampScalar`) — but now live in `namespace Frontier`,
   so either qualify (`Frontier::`) or add `using namespace Frontier;` matching each file's existing style.

**DeviceAssembly** genuinely had NO destination equivalent — ported verbatim to `Graphics/Render/Device/`
(`.h` + `.cpp`, self-contained includes, no rehook needed).

🔴 **General rule for the whole migration:** before porting ANY retired dependency, search the
destination first — it may already exist under a renamed/refactored form (as VectorAlgebra→LinearAlgebra_Float64
and WorkerPool did). Verify, don't assume.
