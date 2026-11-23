# RockFormation — working notes and deferred work

Running notes for the node-based 3D rock/erosion editor. **What works, what is deliberately deferred, and
why.** Kept beside the code rather than in `EngineDocs/` (which holds only `FolderStructure.md`,
`Backlog.md` and `AgenticInstuctions/`).

---

## 🟢 Confirmed working — do not regress these

Author feedback from the 2026-07-31 review, against the current build:

| Species | Verdict | What specifically works |
|---|---|---|
| `JointNetwork` | 🟢 **strong** | Genuinely drives erosion. **Changing Spacing yields different erosion *types*** — a discovery worth protecting: one dial reaches distinct weathering regimes, not just a finer or coarser version of one look. |
| `StratumBand` | 🟢 working | Alternating hard/soft beds express themselves through differential erosion rate as intended. |
| `TunnelMass` | 🟢 working | Bores the arch. GPU-probed open across 3928 grid lanes. |
| `HullMass` + `HullRelief` | 🟢 verified | Blender-generator port. 26% occupancy, faceted (support CoV 0.111), unclipped by the domain wall. |
| erosion chain | 🟢 "significantly better" | Thermal → hydraulic → aeolian → spheroidal → saltfreeze. |
| `StratumTint` | 🟢 "not bad" | Munsell-derived palettes indexed by resistance. |

📝 **Why Spacing changing erosion *type* matters.** Joint spacing sets how many low-resistance planes
intersect a given volume, so it controls whether weathering proceeds as isolated pitting, as slab release
along widely-spaced joints, or as granular disintegration where joints are dense enough to overlap. That
is three distinct mechanisms from one dial, which is exactly the leverage a node-based editor wants. Any
future change to `EvaluateJointNetwork` should re-check that the *range* of behaviours survives, not just
that a single default still looks right.

---

## 🚧 Deferred — agreed, not yet started

### ① Strata warping — the banding is too uniform

**Author:** *"they can be put through a node that warps/distorts the banding (of strata + colour) as it is
too uniform"*

`StratumBand` and `StratumTint` both take an optional `warp` intake already, so the plumbing exists — what
is missing is a warp species shaped for **bedding** rather than for general noise.

Real strata are not parallel planes. They carry:

| Structure | What it does to the banding |
|---|---|
| folding | long-wavelength sinusoidal bending of the whole stack |
| pinch-and-swell | thickness varying *along* a bed, so a band fattens and thins |
| cross-bedding | internal laminae at an angle to the bed boundary |
| unconformity | a truncation surface cutting older beds off at an angle |
| faulting | offset — a band steps vertically across a plane |

⚠️ **The existing `DomainWarp` is the wrong tool.** It applies isotropic fractal displacement, which
distorts a band's *boundary* but also smears its *thickness* incoherently, so beds stop reading as beds. A
bedding warp wants displacement that is **strong along the bed normal and weak within the bed plane** —
anisotropic by construction.

🔴 **Applies to colour too, and via the same probe.** `StratumTint` reads its own `warp` intake, so if the
band and the tint are fed warps that do not agree, the colour will slide off the rock it belongs to. The
warp must be authored once and linked to **both** — worth an explicit note on the cards, because a tree
that links it to only one still transcribes and still compiles.

📝 Cheapest credible first version: fold (low-frequency sinusoid along the dip direction) + pinch (bed
thickness modulated by a 2-D noise sampled *in* the bed plane). Both are pure functions of position, so
they cost one extra `let` binding and no new architecture.

### ② `TunnelMass` needs more control + a visual debug

**Author:** *"tunnel mass too, but we need more control — perhaps add a visual debug so we can see the
shape in editor"*

The control problem and the visibility problem are the same problem: the tunnel is **invisible until it
has already cut something**, so dialling it is guesswork.

🔴 **This is why the units bug went unnoticed for a whole session.** The hull was seeded ~2.4× too small
and the 0.72 m tunnel swallowed it entirely — no arch could ever have appeared — and the shader compiled
perfectly. A carver that renders as a wireframe in the viewport would have made that obvious in one glance
instead of needing a GPU probe.

Wanted:
- **Carver visualisation** — draw `Distance` species that feed a boolean's `carver` intake as a
  translucent hull or wireframe, so the author sees the cutting volume *in place* before it cuts.
- **More shape control.** Currently radius / span / elevation / bearing — a straight capsule of constant
  radius. Missing: taper along the sweep, curvature (an arch bore is rarely straight), and elliptical
  cross-section.
- ⚠️ Extra shape parameters need lanes, and an entry has exactly **four**. A tapered curved elliptical
  tunnel exceeds that, so this likely splits into two species the way `HullMass`/`HullRelief` did — or
  takes a second slot. Decide before implementing, not during.

### ③ Rock colouring research — explicitly deferred by the author

**Author:** *"you may need to conduct research on rock colouring… but rock colour leave for later"*

Not started, by instruction. When it resumes, the open question is how non-sedimentary rocks get their
colour — the author noted sedimentation is the only mechanism they know. Igneous/metamorphic colour comes
from mineral assemblage rather than cement and bedding, so it will not reuse `StratumTint`'s
resistance-indexed palette directly.

### ④ Surface cracks

Research complete, nothing implemented. **The key constraint, which changes the whole approach:**

🔴 **Crack aperture cannot be voxel geometry.** At 256³ a real crack is **0.05–0.5 cells wide** — one to
two orders below Nyquist. A sub-voxel carve collides the inside and outside narrow bands and produces
**sign noise, not a crack**.

So cracks enter as **three decoupled layers sharing one pattern generator**:

| Layer | Resolution | What it represents |
|---|---|---|
| resistance field | sim | *on a joint* — aperture irrelevant, only weakness matters |
| emergent geometry | sim | widening that appears once erosion opens a joint past ~2 cells |
| shading detail | render | the visible line, below sim resolution |

Numbers worth keeping:
- joint spacing `s ≈ 0.77t` (t/s ≈ 1.3, Monterey Fm), **log-normal** distributed; cross-joint spacing ×1.81
- the master-vs-cross **"ladder pattern" termination rule** buys most of the realism
- ⚠️ **mudcracks are 90° T-junctions, four-sided.** Voronoi's 120°/hexagonal is **wrong** here — it is
  right for *columnar* jointing. Mudcrack rule = recursive midpoint subdivision, length halving each
  generation, angular jitter 5° (thick) → 20° (thin).
- columnar jointing needs **partial** Lloyd relaxation (2–4 iterations, *not* convergence — full
  relaxation over-produces hexagons)
- **exfoliation is the cheapest and best grid fit**: `frac(SDF/spacing(depth))` bands + a curvature gate
  (curvature = divergence of the normalised density gradient, a pure gather). Slabs 1–10 m, spacing
  growing to 50–100 m with depth.
- erodibility changes **3–4×** with fracture state, while intact strength differs **<2×** — fracture is
  the dominant term, which is why a resistance field is the right vehicle
- 💡 **spheroidal weathering falls out of a resistance field for free** (corners see more low-resistance
  neighbours) but would *not* from subtracted geometry

### ⑤ Erosion rate control

The timeline module exposes `Rate`, but no UI speed control is wired.

📝 **Root cause of "erodes too fast" is not the per-step aggression.** `RoundsPerFrame = 2` against
`RoundCeiling = 260` means the entire run completes in **~2 s at 60 fps** — 260 rounds elapse faster than
a person can watch, with no mechanism to hold it anywhere. It is a transport problem, which is why the
timeline was built before touching any erosion constant.

### ⑥ Timeline wiring

`Simulation/ErosionTimeline.js` is complete and unit-verified (27/27 assertions) but **nothing calls it**.
Needs: wiring into `DeviceHost.js` / `RockFormationEntry.js`, and a scrub bar + play/pause + speed control
in the viewport footer.

---

## 🚩 Divergences from the approved plan

- **`Resolve/SphereTrace.js` was NOT deleted** as the plan directed. The per-card preview thumbnails depend
  on its analytic march; only the main viewport marches the voxel grid. Deleting it would have broken every
  card preview.
- **`HullMass` lost its `Jitter` dial** to fund a metre-valued `Extent`. Jitter perturbed plane
  *directions*, `Spread` perturbs their *distances*, and at usable amplitudes the two are visually near
  indistinguishable — so the duplicate was spent rather than the mechanism. Jitter is fixed at its former
  default (0.55) inside the shader.

---

## 🐞 Hazards worth not rediscovering

- 🔴 **Carry units explicitly.** The hull's first version used a dimensionless extent (`1.0`) while every
  neighbouring species dialled metres. Result: a body 2.4× too small at 1.58% grid occupancy, with the
  tunnel engulfing it. **All 8 pipelines compiled and validated.** Only a dispatch-and-measure probe found
  it — and only after adding a *span-against-the-domain* assertion, because occupancy, connectivity,
  faceting and proportion checks all pass happily on a pebble.
- 🔴 **A downstream entry cannot rewrite its upstream's probe.** The transcriber emits each upstream entry
  to a `let` binding *before* the consumer's expression runs. Anisotropy therefore has to live where the
  probe *enters* (`HullMass`), fed through the `warp` intake. Scaling `Probe` in `HullRelief` would sample
  hull and noise at different points — relief sliding across the surface as the dial turns, which presents
  as a correlation bug.
- ⚠️ **One `vec4f` slot per entry, strictly.** `SlotCeiling = 48`; the host asserts against more than four
  dials. Eight hull parameters is what forced the `HullMass`/`HullRelief` split.
- ⚠️ **Probe the tunnel off-centre.** It sweeps at elevation −0.42 m, so a single centre-lane scan reports
  "no bore" on a perfectly good seed. Scan all lanes.
- 🔴 **Erosion has no inverse**, so backward scrubbing is *remembered*, never computed. Determinism is what
  legitimises restore-nearest-snapshot + re-step: a round depends only on the density it reads plus the
  profile, whose only time-varying term derives from the round number, not a wall clock. **If any process
  ever takes a genuinely random input, this breaks** and every round must be stored.
- ⚠️ **Snapshot budget is a BYTE ceiling, not a count** — the same count costs 8× at bake tier (128³ =
  8 MiB/buffer vs 256³ = 64 MiB).
- ⚠️ **WGSL has no f32 atomics** → voxel transfer is gather + ping-pong, never scatter.
- ⚠️ **A density grid is not a distance field** → fixed-step march, step ≤ 0.7 cell. And `SmoothMaximum`
  yields a *bound*, not a distance: safe for a fixed-step seed dispatch and a clamped preview, **not** for
  an unclamped sphere trace.
- ⚠️ **`mid_level 0` vs `0.5` is not cosmetic.** 0 is additive only (inflates into lumps); 0.5 is signed
  (cuts as much as it adds). Running the coarse displacement stage signed eats through thin hull parts and
  opens unrequested holes.

---

## 📝 Blender rock generator — what the source actually says

Verified against `add_mesh_rocks/rockgen.py`, `randomize_texture.py`, `utils.py`, `factory.xml`. Three of
the six assumptions carried into this port were **wrong**:

| Assumed | Actually |
|---|---|
| starts from an icosphere | **12 hand-authored 7/8/10-vertex hulls**, chosen at random |
| Musgrave type is configurable | **MULTIFRACTAL hardcoded** (fBm / hetero-terrain / ridged sit commented out) |
| `mat_hard` drives displacement | **no material code exists at all** — `mat_*` in `factory.xml` is vestigial; `mat_hard` was Blender-Internal *specular hardness*, which never touched geometry |

🔴 **The transferable idea is crease-then-subdivide, not the mesh.** Creasing edges in three jittered tiers
and *then* subdividing twice is what makes the output read as rock: some arrises survive as facets, others
round away. The SDF analogue is a plane-intersection hull where **each plane carries its own blend radius**
— which is why `DistanceToPlaneHull` smooth-maxes plane by plane instead of taking one hard max and
rounding uniformly. One global round makes every arris identically soft: the machined look the whole
exercise exists to escape.

📝 **Multifractal MULTIPLIES octaves** (unlike fBm's sum), giving position-dependent amplitude — some
regions smooth, others jagged, from one texture. A good part of why the addon's output does not look
uniformly noisy.

📝 **Fibonacci sphere for plane normals, not random.** Random normals clump; two near-parallel planes cut
nearly the same slab, and with enough planes it degenerates toward a sphere. Golden angle = 2.39996323.
