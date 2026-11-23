# PLAN — ChainAuthoring (native-speed operation chains)

🧩 A visual authoring surface where each unit is a **preexisting native C++ function**. The wired
chain is **transpiled to C++ and compiled to a DLL** — never interpreted — so evaluation runs at
hand-written speed. Measured: **−2.8 % vs hand-written C++**, **~312 ms** rebuild, hot swap with
live records intact.

Every figure in this document was measured on this machine. Probes:
`_ClaudeScratch/build/CompileProbe/` (git-ignored — reproduce, do not cite as source).

---

## 1. Measured position

| Question | Result | Probe |
|---|---|---|
| Speed vs hand-written C++ | **−2.8 %** (88.6 ms vs 91.2 ms, 4.2 M elements) | `SpeedComparison.cpp` |
| Do the functions inline? | **Yes — 0 `call` in the loop, 4× unrolled** | `FusedGraph.asm` (`/FAs`) |
| Cost of per-operation dispatch | **+144 %** here; **9×** on cheap-arithmetic chains | `SpeedComparison.cpp` |
| Rebuild latency | **~312 ms** (PCH) · ~474 ms thin · ~2258 ms with 4 STL headers | `compilespeed/` |
| Hot swap with live records | **Works** — 3 revisions, accumulator 3.411 → 20.467 → 21.319 | `ReloadProbe.cpp` |
| DLL/PDB locking | **Real failure; shadow-copy resolves it** | `PdbLockProbe.cpp` |
| Metadata drift vs signature | **Compile error**, not runtime surprise | `DriftProbe.cpp` |
| Token resolve cost | **1.06×** hoisted · 1.43× per-access · 1.91× with generation check | handle probe |

💡 Two results drive the whole design:

- **`/Od` is not faster than `/O2`** — header parsing is the entire compile cost, so optimization
  is free and a precompiled thin header is the only latency lever that matters.
- **Fusion's win is unlocking SIMD**, not saving call overhead. An opaque call per operation blocks
  the vectorizer outright.

---

## 2. 🔴 The gate — resolve before writing any code

**Crossover sits between 1 K and 10 K elements per evaluation.**

| Elements | Evaluations to repay a 312 ms compile | Verdict |
|---|---|---|
| 10 | 1,560,000 | 🔴 never pays |
| 100 | 120,000 | 🔴 never pays |
| 1 K | 16,596 | 🔴 never pays |
| 10 K | 1,098 | 🟢 pays within ~1 min |
| 100 K | 144 | 🟢 pays within seconds |
| 1 M | 10 | 🟢 pays immediately |
| 4 M | 2 | 🟢 pays immediately |

⚠️ Speedup is **flat (~1.6–2.0×) at every batch size**. What scales is the *absolute* time saved
per evaluation — that is what must repay the one-off compile. SideFX documents the same effect for
Houdini compiled blocks: the benefit targets "iterations over large numbers of pieces," otherwise
"the reward is often not worth the effort."

| If chains sweep… | Then |
|---|---|
| ≥ 10 K elements | 🟢 build it — payoff is large |
| a handful of values | 🔴 **do not build this** — a direct evaluator is correct |
| both | 🚩 build it, gate compilation on an element-count threshold |

**Step 0 answers this against real `Authoring/Geometry` functions. Nothing else starts first.**

---

## 3. Structure

```
┌──── Editor .exe ─────────────────── never reloaded ────┐
│  ChainAuthoringPanel    (imgui-node-editor, vendored)  │
│  OperationCatalogue     RecordToken store              │
└─────────────────────┬──────────────────────────────────┘
             ONE indirect call per chain evaluation
                      ▼
┌──── GeneratedChain.dll ──────────── hot swapped ───────┐
│  fused entry point; operations INLINED from headers    │
└────────────────────────────────────────────────────────┘
```

Reload granularity is the **whole chain**, not the operation. That is what resolves the tension
between inlining (wants static linkage) and hot reload (wants an ABI boundary): one indirect call
per evaluation amortizes to nothing, while everything inside the DLL inlines.

🔴 **Hard constraint:** operations must be `inline` in headers (or LTO'd). Exposed only as DLL
exports, nothing inlines and the dispatch penalty returns in full.

---

## 4. Naming

🔴 `Node`, `Graph`, `Kernel`, `Module`, `Frame`, `Data` are all banned (`SKILL-Naming.md` §1).
Names below were constructed via the §0 procedure (mechanism → verb + substrate → domain register).

| Concept | Name | Source |
|---|---|---|
| Pillar folder | `Authoring/ChainAuthoring/` | — |
| One registered function | `OperationEntry` | `RecordEntry` precedent (§4) |
| The registry | `OperationCatalogue` | name already in use under `Menus/PolygonMutation/` |
| Wiring between operations | `AdjacencyTable` | §4 approved connectivity store |
| Emits C++ | `ChainTranslation` | `Translate` (§6) |
| Compiles it | `ChainCompilation` | `Compile` (§6) |
| Loads / swaps the DLL | `RevisionActivation` | `Activate` (§6 lifecycle) |
| Canvas panel | `ChainAuthoringPanel` | §10 panel convention |

---

## 5. Build order

### Step 0 — Answer the gate
Benchmark fusion against real geometry functions at realistic element counts.
**Deliverable: go / no-go.**

### Step 1 — `OperationCatalogue`
Registration with the compile-time-checked form (verified in `ReflectionProbe.cpp`):

```cpp
Register("Derive", &DeriveEquation)
    .In("InputX").In("InputY")
    .Out("OutputX").Out("OutputY").Out("OutputZ");
```

Templates deduce arity and reject by-value parameters as outputs; the author supplies names and
direction. A mismatched count is a build error — verified diagnostic:

```
error C2338: static assertion failed: 'pin count does not match the function signature'
```

📝 Templates **cannot** recover parameter names, nor separate write-only `out` from read-write
`in-out`. Those two facts are the only hand-supplied metadata; everything else is deduced and
cross-checked against the signature.

Independently useful — unblocks the rest and survives any gate outcome.

### Step 2 — `ChainTranslation`
Emit the fused `.cpp`. Diff output against the hand-written probe that measured −2.8 %; that probe
is the reference implementation.

### Step 3 — `ChainCompilation` + `RevisionActivation`

| Measure | Effect |
|---|---|
| Cache the vcvars environment **once at startup** | 7–16 s → ~1 s |
| **PCH a thin interface header**; generated code includes only that | ~1 s → **~312 ms** |
| **Shadow-copy the DLL before `LoadLibrary`** | the verified fix for path locking |
| Swap only after the new revision proves loadable | a broken chain leaves the previous one live |
| Keep `DllMain` an **empty stub** | it runs under the loader lock |

⚠️ Writing over a mapped DLL fails outright (`FAILED — path locked`). Shadow-copy succeeded with
the *same* PDB path, so the DLL mapping is what locks, not the PDB.

### Step 4 — `ChainAuthoringPanel`
Wire up `ExternalPackages/imgui-node-editor` (already vendored).

---

## 6. Design rules

**Resolve tokens once, in a prologue.** `MicroUtils/RecordToken.h` is already the index + generation
slotmap this needs — canonical across Scene, Revision, and Instrumentation. Resolve at the top of
the generated function, then loop on the raw pointer.

| Access pattern | Cost |
|---|---|
| Resolved once, hoisted | 1.06× |
| Per-access resolve | 1.43× |
| Per-access + generation check | 1.91× |

⚠️ Per-access indirection blows the budget. The generation counter is what keeps a token safe
across a reload — a stale token fails a comparison instead of dereferencing freed memory.

**Permit non-compilable operations, and mark them.** Every surviving system does this — Houdini
ships a badge for non-compilable SOPs; `torch.compile` falls back with a graph break. Blueprint
nativization promised near-total coverage of a legacy set and broke precisely at the seams.

**Never translate partially.** The dominant reported Blueprint failure was the boundary between
nativized and non-nativized classes — cook failures, packaging errors, runtime crashes. Compiling a
whole chain behind a C ABI means no such boundary exists.

**Reload cycle after RuntimeCompiledCPlusPlus** (2,291 ★, zlib): a stable identity preserved across
the swap, then `serialize → recreate → deserialize → notify`. Live++ and Unreal Live Coding
converge on the same sequence independently — the strongest signal available on this design.

⚠️ RCC++ carries an `_isRuntimeDelete` flag because destructors that cascade deletes will destroy
the surrounding record set on reload. Design that out rather than debug it later.

---

## 7. Risks

| Risk | Status | Handling |
|---|---|---|
| Batch size below crossover | 🔴 **open — the gate** | Step 0 |
| Debuggability of generated code | 🚩 intrinsic | `#line` directives; operation identity in symbol names |
| Preview vs compiled divergence | 🚩 intrinsic | make the compiled path the only path |
| Maintenance burden | 🚩 | Epic deleted theirs rather than maintain it — keep scope narrow |
| Speed, latency, reload, locking, drift | 🟢 measured | §1 |

### On the Blueprint precedent

Epic's UE5 Migration Guide lists Blueprint Nativization under Gameplay Framework → **Removals**:
*"Blueprint Nativization will not exist in UE5."* **No reason was ever published**, and no credible
speedup figure for the feature exists publicly.

Of 14 identified failure modes, ~11 stem from reproducing full UObject/gameplay semantics under
*partial* translation — reflection metadata, GC, replication, delegates, latent nodes, cook
targets, bug-for-bug parity across a legacy node set. None of those exist for pure functions over
plain records. The three that do carry over are listed in the risk table above.

💡 The four traits shared by systems that survived — Houdini compiled blocks, Nuke Blink, Unity
Shader Graph, `torch.compile`: a closed pure-data domain; willingness to declare an operation
non-compilable; a compiled region with no external references; batch dispatch over a large
iteration domain. This design satisfies the first three by construction. **The fourth is the gate.**

---

## 8. Unverified

Stated plainly rather than asserted: Epic's rationale for the removal (never published); UE-xxxxx
issue identifiers (tracker returns 403); *Mortal Shell* / *Valorant* anecdotes (third-hand forum
reports); Nuke Blink's LLVM backend; Bifrost's execution model beyond "Amino JIT, native ARM64 and
x86-64"; Blender's multi-function architecture; Substance Designer entirely.
