# Material system & flakes — inspection report (2026-10-22)

Scope: `Engine/Shaders/MaterialEvaluation.slang` (1,088 lines) and
`Engine/Shaders/AutomotiveFlakePaint.slang` from the owner's previous project
([SultanAladin/Frontier-](https://github.com/SultanAladin/Frontier-/tree/arena%2F9440f4d0-frontier)
@ `3382938`), read in full. The VehicleHMI demo (e9bd7db5) carries flake/paint
*parameters* only (`MaterialSpecification.js`); its `MaterialResolve.wgsl` is a
visibility-buffer resolve, not the BSDF. Glint references fetched and cited below.

## 1. MaterialEvaluation.slang — good or excellent?

**Verdict: 🏆 excellent** — research-grade, keep as the porting source of truth.
The concerns are about *real-time usage cost*, not quality.

| Aspect | Rating | Evidence from the code |
|---|---|---|
| Model (OpenPBR coverage) | ⭐⭐⭐⭐⭐ | full §5 lobe stack, equation numbers cited inline (eq. 42/60/72/73…) |
| Diffuse | ⭐⭐⭐⭐⭐ | EON (Portsmouth/Kutz/Hill 2025) incl. CLTC sampling — current state of the art |
| Specular | ⭐⭐⭐⭐⭐ | anisotropic GGX, Dupuy–Benyoub spherical-cap VNDF, Kulla–Conty/Turquin multi-scatter via E(μ,α) LUT |
| Fresnel | ⭐⭐⭐⭐⭐ | F82-tint metals, exact dielectric, Schlick only where appropriate |
| Thin film | ⭐⭐⭐⭐⭐ | Belcour & Barla spectral Airy series with Gaussian CIE sensitivity fit |
| Coat | ⭐⭐⭐⭐ | roughening (r⁴+2r_c⁴)^¼, view-dependent absorption, base darkening (§5.6) |
| Fuzz/sheen | ⭐⭐⭐⭐⭐ | Zeltner LTC sheen with table + Charlie albedo correction |
| Transmission | ⭐⭐⭐⭐ | Walter BTDF, exact R/T branch split, thin-wall + solid medium walk |
| Sampling/PDF | ⭐⭐⭐⭐⭐ | full mixture pdf, per-lobe masses (`LobeWeights`), CLTC/VNDF samplers |
| Proofs | 🏆 | same text compiles as C++ (`FRONTIER_CPU_PORT`): white-furnace, reciprocity, pdf tests |
| Architecture | ⭐⭐⭐⭐ | `ResolvedLayers` hoists every per-(material, wo) constant out of per-wi eval; exact-zero gates keep opaque paths bit-identical |
| Real-time eval cost | ⚠️ ⭐⭐ | path-tracer priced: per-wi `pow(vec3,vec3)` coat absorption, 2× multi-scatter LUT fetches, spectral thin-film, 11 LUT call sites. ReSTIR evaluates f per reservoir candidate (dozens×/pixel) — that multiplier is the real bill, and it is what "evaluates it so many times" meant |
| Bytes per record | ⚠️ | `ShadingRecord` ≈ 35 floats ≈ 140 B — fine in registers, fatal if ever stored per pixel (the Unreal GBuffer trap). Store material id + compact params; reconstruct |
| Flakes/glints | ❌ | absent by design ("glints (M6)" slot reserved); the flake file re-implements approximate GGX/Smith instead of reusing the library |
| Code hygiene | ⭐⭐⭐⭐ | dense but disciplined; GLSL dialect inside .slang + fixed LUT bindings (13/14) to normalise at port |

**Port plan implication:** bring it in as Tier A verbatim (Slang→Vulkan), add a
Tier B real-time eval (RGB thin-film approximation, lobe-set already gated by
weights) for reservoir candidate weighting, keep the CPU proof harness — it is
the strongest verification asset in the previous project.

## 2. Flakes / glints — can we do better than the current flakes?

**Verdict: ✔️ yes — adopt the Glinty model as the M6 lobe.** The shader the owner
pasted ([Shadertoy tcdGDl](https://www.shadertoy.com/view/tcdGDl)) is the paper's
official standalone implementation.

| | Current flakes (`AutomotiveFlakePaint`) | **Glinty** (Kemppinen et al., SIGGRAPH Asia 2025) | Loubet 2020 slope-space | Chermain real-time glints |
|---|---|---|---|---|
| Idea | finite jittered ellipse facets in UV cells; one winner facet + population mean | 4D point process (spatial × slope-disk) on implicit multiscale grid | analytic slope-space integrals over triangles for specular NEE | explicit facet search / NDF filtering line of work |
| Cost model | ≤ 16 layers × 3×3 cells = **≤144 candidate facets** per shading point (hashes + trig), scales with `Density` | **constant**: 2 LODs × 4×4 4D cells = 32 Gaussian kernel evals + 2 compensation terms, independent of flake count N | per-triangle integral setup; path-tracing machinery | search cost grows with facet density/footprint |
| Filtering / far field | smoothstep blend to population Beckmann (plausible, not exact) | ✔️ converges to the base GGX/Beckmann NDF exactly as footprint grows (`compensation()` term) | ✔️ analytic | approximate |
| Importance sampling / PDF | ❌ evaluate-only → under-sampled fireflies in a path/ReSTIR context | ✔️ provided (paper title: evaluating **and sampling**) | ✔️ (its purpose) | partial |
| Per-facet colour | ✔️ 8 weighted colour families (nice, keep the concept) | ✔️ per-facet colour control | via normal maps | ✔️ |
| Anisotropy | ❌ isotropic Beckmann | ✔️ | ✔️ | ✔️ |
| Memory / GBuffer bytes | ✔️ fully procedural, 0 textures | ✔️ fully procedural, 0 textures, 0 precompute | normal-map based | varies |
| Real-time | ⚠️ direct-light OK; cost rides on density setting | ✔️ designed for it (single fragment shader) | ❌ offline | ✔️ (page currently HTTP 500; superseded by Glinty for our use) |
| Honesty of our current file | 🏆 every approximation is labelled ("NOT an implementation of DB23 binomial glints", "Poisson coverage approximation"…) | — | — | — |
| Fit here | already integrated over clearcoat in the paint exhibit | needs UV Jacobian — we already pass `dx, dy` **in metres** ✔️; reimplement in Slang from the paper (don't paste Shadertoy text — licence), CPU-proof like Tier A | keep as offline ground-truth reference for validating M6 | prior art only |

Notes: the Zsolnai page is PSSMLT *automatic parameter control* (2013) — not a
flakes technique; its value here is later, for adaptive ReSTIR/reservoir tuning.
Both current flakes and Glinty are procedural, so the bytes-per-pixel fear
doesn't apply to either — the difference is purely ALU scaling: Glinty's cost is
fixed whether the paint has 10³ or 10⁹ flakes; ours is fixed only after the
density cap, and 144 hash/trig candidates precede any lighting.

**Recommendation (Phase 5/M6):** reimplement Glinty as the glints lobe inside
`MaterialEvaluation` (shared Fresnel/GGX, palette colours via per-facet colour),
keep `AutomotiveFlakePaint`'s clearcoat-over-flakes structure and parameter
surface (artists keep their controls), retire the 16-layer cell search.
