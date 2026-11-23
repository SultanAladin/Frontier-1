# RESEARCH — Tone Mapping, State of the Art 2026

🧩 Deep-research pass on tone mapping operators for the Frontier viewport — fast, lightweight,
accurate, low-resource. Surveys credible sources 2020–2026, audits the two operators already live in
`SurfaceShade.frag` and `SkyDome.frag`, and resolves what Frontier should ship.

> **Headline:** the operator already in `SurfaceShade.frag` — **Khronos PBR Neutral** — is the correct
> choice and should NOT be replaced. The accuracy Frontier is losing is spent elsewhere: a **wrong sRGB
> transfer function** (~6 % error in shadows), a **double tone map** across the sky and surface passes,
> and **alpha blending in display space**. No operator swap recovers any of those.

---

## 1. What Frontier runs today

| Pass | File | Exposure | Operator | Transfer |
| ---- | ---- | -------- | -------- | -------- |
| Sky | `Atmosphere/Shaders/SkyDome.frag` | `Exposure = 8.0` | exposure tone map | sRGB in shader |
| Surface | `Visibility/Shaders/SurfaceShade.frag:241-260` | 🔴 **none** | Khronos PBR Neutral | `pow(x, 1/2.2)` |
| Matcap | `SurfaceShade.frag:365-375` | — | 🟢 bypassed (display-referred) | none |

⚠️ Three different transfer paths reach one framebuffer. The Matcap bypass is *correct* — a matcap is
already display-referred and must skip both BRDF and tone map or it double-corrects. The other two are
the problem.

---

## 2. 🐞 Defects found in the live pipeline

Ranked by impact on the accuracy the brief asks for. **Severity: 🔴 = highest.**

| # | Severity | Defect | Location | Consequence | Fix |
| - | -------- | ------ | -------- | ----------- | --- |
| ① | 🔴 | **Wrong sRGB transfer.** Comment says "the sRGB OETF"; code is pure 2.2 gamma | `SurfaceShade.frag:262-266` | Up to **~6 % error in the low end** — exactly where `AmbientColour` (0.10–0.16) lives. Shadow side is systematically wrong | Move encode to an `_SRGB` swapchain — **free, exact, and deletes two `pow` calls** |
| ② | 🔴 | **Double tone map.** Sky and surface each map independently, then composite | `SkyDome.frag` + `SurfaceShade.frag:484` | Two different roll-offs in one image; sky and geometry **cannot match at the horizon** | One HDR target, one resolve |
| ③ | 🔴 | **Blending in display space.** Glass `OutputAlpha` blends over an already-sRGB sky | `SurfaceShade.frag:476-485` | Alpha blending is only correct in **linear** light. Transparent materials composite wrong | Same fix as ② |
| ④ | 🚩 | **No exposure on the surface path.** Sky has `Exposure = 8.0`; surface has none | `SurfaceShade.frag` `ShadeConstants` | Tone maps are tuned around 1.0 = mid-grey. The only brightness control is `LightIntensity = 3.0`, so **re-lighting silently re-tunes the tone map's operating point** | Add scalar `Exposure`, applied *before* the operator |
| ⑤ | ✔️ | Comment/code mismatch — line 262 names a curve the code does not implement | `SurfaceShade.frag:262` | Misleads the next reader | Resolved by ① |

### 2.1 Why ① is not a rounding error

| | Pure 2.2 gamma (shipped) | True sRGB OETF (IEC 61966-2-1) |
| - | ------------------------ | ------------------------------ |
| Curve | `x^(1/2.2)` everywhere | linear `12.92·x` below `x = 0.0031308`, then `1.055·x^(1/2.4) − 0.055` |
| Deviation | **up to ~6 % in the low end** | reference |

💡 An `_SRGB` swapchain format is strictly better than fixing the maths in-shader: the ROP does it in
fixed-function hardware, exactly, for free, and it makes blending correct. ⚠️ `SkyDome.frag` shares the
in-shader idiom, so both passes must flip together or the sky double-encodes.

---

## 3. The operator field, 2020–2026

**Cost = GPU work per pixel; ✔️ low, 🚩 medium, 🔴 high. Hue / Accuracy: 🟢 = good, 🔴 = bad.**

| Operator | Year | Cost | Hue behaviour | Invertible | Fit for CAD | Rank |
| -------- | ---- | ---- | ------------- | ---------- | ----------- | ---- |
| **Khronos PBR Neutral** | 2024 | ✔️ ~13 lines, ALU-only, no LUT | 🟢 **preserved by construction** | 🟢 analytic | 🟢 built for exactly this | 🥇 🏆 |
| Reinhard | 2002 | ✔️ cheapest | 🔴 per-channel skew | 🟢 | 🔴 washed out | 🏁 |
| Uncharted 2 / Hable | 2010 | ✔️ | 🔴 skews | 🔴 | 🔴 artistic, not neutral | 🏁 |
| ACES 1.x (Narkowicz fit) | 2016 | ✔️ curve fit | 🔴 known hue twist | 🔴 | 🔴 **crushes saturated colour** | 🏴 |
| AgX | 2023 | 🚩 matrix + curve + inverse matrix | 🔴 **deliberate rotation** | 🔴 | 🔴 disqualified — §3.1 | 🏴 |
| ACES 2.0 | 2024/25 | 🔴 **~3–5× AgX** | 🟢 | 🔴 | 🔴 disqualified — §3.2 | 🏴 |

### 3.1 🔴 Why AgX is disqualified for a modelling tool

AgX applies a **gamut inset** — a matrix into a smaller gamut, tone curve, then the inverse matrix out.
Displacing the primaries intentionally rotates hue to compensate for the **Abney effect**. That is a
*film look*, and it carries documented costs:

- It shifts pure blue toward cyan **even when the colour is not bright enough to need desaturating**.
- The compensation is **fixed and not user-adjustable**.
- For highlights to roll off smoothly, the **most saturated sRGB colours become unreachable under any
  lighting**.

💡 Khronos built PBR Neutral precisely because filmic operators "restrict access to certain colours,
such as bright shades of yellow, green, and cyan." A CAD user picked a material colour; it must survive
to the screen.

### 3.2 🔴 Why ACES 2.0 is disqualified on cost

ACES 2.0 abandons ACES 1's direct AP1-RGB curve for a round trip through **JMh**, a perceptual space
from a simplified Hellwig 2022 colour appearance model:

```
ACES --> JMh --> tonescale (J) --> chroma compression (M) --> gamut compression (J,M) --> JMh --> RGB
```

Measured at **~3–5× the cost of AgX** (~150 lines of per-pixel maths plus a 360-entry hue table built at
init). Nothing in a modelling viewport justifies that.

---

## 4. Peer-reviewed sources, 2020–2026

| Work | Venue | Finding that bears on Frontier |
| ---- | ----- | ------------------------------ |
| Ou, Ambalathankandy, Takamaeda, Motomura, Asai, Ikebe — *Real-Time Tone Mapping: A Survey and Cross-Implementation Hardware Benchmark* | **IEEE TCSVT 32(5):2666–2686, 2022** | ~60 hardware TMOs (GPU/FPGA/ASIC). 🔴 **Global TMOs cost strictly less memory than local TMOs** — local operators pay for line buffers and Gaussian pyramids. Throughput is the governing metric |
| *HDR Image Tone Mapping: Literature review and performance benchmark* | **Digital Signal Processing, 2023** | 17 quality metrics over two large datasets. 🔴 **No TMO wins universally — the best is content-dependent.** Argues against chasing a "best" operator |
| Todorov et al. — *TGTM: TinyML-based Global Tone Mapping for HDR Sensors* | **arXiv:2405.05016, 2024** | 9 000 FLOPS, resolution-independent (histogram-domain). ⚠️ **ISP/ADAS sensor technique, not a rendering one** — cs.CV/eess.IV, no shader or frame-rate framing |
| *Learning Differential Pyramid Representation for Tone Mapping* | arXiv:2412.01463, 2024 | Photographic. Useful survey of LUT accelerations (AttentionLUT ICASSP'24, 3D-LUT ICCV'21) |

### 4.1 ⚠️ An honest gap in the literature

There is **no 2020–2026 peer-reviewed survey of tone mapping for real-time 3D rendering.** Academia
treats tone mapping as a *photographic / video / ISP* problem — mapping **captured** HDR to a display.
Rendering has a different problem: the scene radiance is authored, so **exposure is a decision, not an
estimate**. The authoritative rendering sources are industry specifications (Khronos, Filament, GDC),
not journals. The ISP papers above are recorded for completeness, not because they transfer.

---

## 5. Khronos PBR Neutral — the specification, verified

Read from the Khronos specification, not from a blog. Constants match `SurfaceShade.frag:243-244`. 🟢

| Symbol | Value | Role | In code |
| ------ | ----- | ---- | ------- |
| `F90` | 0.04 | dielectric offset source | inline `0.04` |
| `Ks` | `0.8 − F90` = 0.76 | where highlight compression starts | `StartCompression` |
| `Kd` | 0.15 | desaturation speed | `Desaturation` |

```
f   = x - x²/(4·F90)   for x ≤ 2·F90 ,  else F90        where x = min(R,G,B)
p   = max(R-f, G-f, B-f)
pn  = 1 - (1-Ks)² / (p + 1 - 2·Ks)
g   = 1 / (Kd·(p - pn) + 1)

out = c - f                                     for p ≤ Ks
    = (c-f)·(pn/p)·g + [pn,pn,pn]·(1-g)         for p > Ks
```

**Properties the spec guarantees:**

- 🟢 **Exact colour reproduction.** For all channels in `[0.08, 0.8]` the map reduces to exactly
  `c − 0.04`. A base colour authored as sRGB renders back as that colour.
- 🟢 **No hue shift, geometrically.** Adjustments occur only in the plane containing the input and the
  white axis `[1,1,1]` — a structural fact, not a tuning claim.
- 🟢 **C¹ continuous**, derivatives vanishing at the boundaries.
- 🟢 **Analytically invertible**, 1:1.
- ⚠️ **No gamut mapping.** Assumes Rec. 709 input for both textures and lighting (as glTF does). That
  assumption is load-bearing; Frontier satisfies it today.

---

## 6. Target architecture

The fix for ②, ③, and ④ is one structural change, not a new operator:

```
sky ------\
           +--> HDR target --> [exposure] --> [PBR Neutral] --> _SRGB swapchain
geometry -/    R16G16B16A16_SFLOAT   ^ one place    ^ one place      ^ free, exact
               linear: blending          (fixes ④)     (fixes ②)        (fixes ①)
               correct here (fixes ③)
```

💡 This is what Filament and Unreal both do. One resolve pass collapses four of the five defects.

---

## 7. Recommendation

**Effort: ✔️ low, 🚩 medium, 🔴 high. Value: 🔴 = highest.**

| Priority | Action | Effort | Value | Why |
| -------- | ------ | ------ | ----- | --- |
| 🥇 | **Keep PBR Neutral. Do not switch operators.** | ✔️ none | 🔴 | Already best-in-class on all four criteria in the brief. The research confirms what is shipped |
| 🥈 | Move sRGB encode → `_SRGB` swapchain format (both passes together) | ✔️ | 🔴 | Removes ~6 % shadow error **and** two `pow` calls — accuracy *and* speed |
| 🥉 | Add HDR intermediate + single resolve pass | 🚩 | 🔴 | Fixes double tone map ②, display-space blending ③, missing exposure ④ |
| 🏅 | Add scalar `Exposure` to `ShadeConstants`, applied before the operator | ✔️ | 🚩 | Tone maps are tuned around 1.0 = mid-grey; that control is currently unreachable |
| 🎖️ | Keep the operator behind one function | ✔️ done | ✔️ | `TonemapPbrNeutral` already is. Mirrors the `IntegrateBxDF` lesson in `RESEARCH-MaterialModels2026.md` §1.2 |

⭐ **Bottom line:** the operator question is already answered correctly. The accuracy is being lost in
the transfer function and the composite, not in the curve.

🏷️ *Defects ①–④ tagged for the render-pipeline pass; see `EngineDocs/Backlog.md`.*

---

## 8. Sources

**Specifications**
- Khronos PBR Neutral specification — `github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/README.md`
- Khronos press release, 16 May 2024 — `khronos.org/news/press/khronos-pbr-neutral-tone-mapper-released-for-true-to-life-color-rendering-of-3d-products`
- model-viewer rationale + interactive comparison — `modelviewer.dev/examples/tone-mapping`
- ACES 2.0 Output Transforms — `docs.acescentral.com/system-components/output-transforms/`
- OCIO ACES 2.0 optimization notes (2025) — `github.com/AcademySoftwareFoundation/OpenColorIO/wiki/ACES-2.0-optimization`

**Peer-reviewed**
- Ou et al., IEEE TCSVT 32(5):2666–2686, 2022 — DOI `10.1109/TCSVT.2021.3060143`
- *HDR Image Tone Mapping: Literature review and performance benchmark*, Digital Signal Processing, 2023 — `doi.org/10.1016/j.dsp.2023.104015`
- Todorov et al., *TGTM*, arXiv:2405.05016, 2024
- *Learning Differential Pyramid Representation for Tone Mapping*, arXiv:2412.01463, 2024

**Industry / practitioner**
- Lottes, *Advanced Techniques and Optimization of HDR Color Pipelines*, GDC 2016 — cross-talk parameter
- Narkowicz, *ACES Filmic Tone Mapping Curve*, 2016 — the widely-used ACES 1.x fit
- darktable AgX module reference — documents the gamut inset and its side effects
- *Qualities of Good Tonemappers*, CG Meerkat — AgX Abney-compensation critique
- Bruop, *Tone Mapping* — operator survey with shader listings
