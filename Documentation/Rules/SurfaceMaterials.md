# Authored surface material contract

The implemented material contract is **opaque GGX/diffuse surfaces and absorbing dielectric coatings**, ordered
bottom-to-top. It is not a general shader-expression language or a claim that every PBR transport model exists.
The single progress tracker remains `Documentation/Tracker.md`.

## Ownership and execution

- `Engine/Render/SurfaceMaterial.{h,cpp}`: material definition, strict TOML codec, texture compilation.
- `Engine/Render/Shader/ScatteringEvaluation.slang`: shared angular evaluation and display conversion.
- `MaterialSurface.slang`: authored-UV/tangent material rasterization, one shared optional pipeline.
- `SurfaceMaterialResidence`: immutable typed textures; `SurfaceScatteringResidence`: immutable material uniform.
- `Exhibits/Renderer/Materials`: authored validation specimens, not project creation presets.
- `Exhibits/Renderer/index.html`: separate native-renderer review UI, not native editor material assignment.
- `Engine/ContentInterchange/MaterialLibrary`: shared catalogue and source/apply/save operations for Texture Studio.

```text
HTML material TOML → same-origin Python bridge → native C++ TOML/texture compilation
    → Vulkan + Slang/SPIR-V → SwiftShader software device → native image readback → PNG in HTML
```

This path does not use WebGPU. The browser is not executing the Vulkan shaders. Existing original browser samples
remain WebGL references. SwiftShader executes Vulkan in software on the server CPU; hardware acceleration and
Windows/native-editor execution are not implied.

## Authored TOML

The `.material` extension contains TOML, decoded with the existing pinned toml++. Unknown keys, unsupported versions,
wrong types, nonfinite/out-of-range coefficients and unsupported categories are errors, not silently ignored features.
The native decoder accepts at most 1 MiB. The HTML transport uses a slightly smaller source limit to bound JSON overhead.

```toml
Format = 1
Name = "Coated copper"
UvScaleOffset = [2.0, 2.0, 0.0, 0.0]
NormalStrength = 1.0
Radiance = [0.0, 0.0, 0.0]

[Textures]
Albedo = "Textures/Albedo.png"
Orm = "Textures/Orm.png"
Normal = "Textures/Normal.png"

[[Strata]]
Category = "Opaque"
Albedo = [0.95, 0.48, 0.22]
Roughness = 0.35
Metallic = 1.0
Ior = 1.5
Coverage = 1.0
TextureMask = 7

[[Strata]]
Category = "Coating"
Roughness = 0.15
Ior = 1.5
Coverage = 0.8
Absorption = [1.0, 3.0, 8.0]
Thickness = 0.01
```

| Property | Meaning / range |
| --- | --- |
| Name | 1–128 bytes |
| UvScaleOffset | XY scale, ZW offset; finite, each magnitude ≤ 1024; zero/negative scales allowed |
| Radiance | Nonnegative scene-linear self-radiance RGB, each ≤ 64; does not illuminate other surfaces |
| NormalStrength | Root and per-stratum tangent-normal XY multipliers, each 0–4; multiplied together |
| Strata | 1–8 entries, bottom-to-top; first must be opaque with coverage 1 |
| Albedo | Scene-linear RGB, each 0–1; opaque diffuse colour or metal reflectance |
| Roughness | Perceptual roughness 0.045–1; GGX alpha is roughness squared |
| Metallic | 0–1, opaque only; coatings must be zero |
| Ior | 1–3; normal-incidence dielectric reflectance derives from IOR |
| Coverage | 0–1; mixes evaluated angular scattering, not roughness/material parameters |
| Absorption | RGB absorption coefficients in inverse metres, 0–100; coatings only |
| Thickness | Coating thickness in metres, 0–1; opaque strata require zero absorption/thickness |
| TextureMask | Integer 0–15; bit 0 albedo, bit 1 ORM, bit 2 layer normal, bit 3 material-wide radiance |

Each stratum may author its own `UvScaleOffset`, `NormalStrength`, and `[Strata.Textures]` overrides
(`Albedo`, `Orm`, `Normal`). Omitted source overrides fall back to the root texture paths. The root UV transform
is applied first, then the stratum transform. Zero scales safely sample a constant coordinate; negative scales
also reverse the corresponding tangent-normal XY direction. Coatings use IOR and absorption, not opaque albedo.
Self-radiance remains material-wide when any active stratum selects bit 3.

| Directional property | Meaning / range |
| --- | --- |
| Anisotropy | Signed −0.9..0.9; zero restores isotropic GGX, negative swaps tangent/bitangent spreads |
| Rotation | Tangent-space orientation in radians, −100..100; default 0 |
| Fresnel | `Schlick` (default) or `Dielectric` (exact unpolarized Fresnel, requires Metallic = 0) |

`AnisotropicReflection.h` contains independent scalar references. The shader evaluates the anisotropic GGX
normal distribution and height-correlated Smith visibility in each stratum's rotated tangent basis. Exact
Fresnel includes equal-IOR and total-internal-reflection cases in its scalar reference; material shading currently
uses the air-to-material reflection case only. This is not refracted transport or complex-IOR conductor Fresnel.

```toml
# Inside an existing [[Strata]] entry, before its optional texture table:
Anisotropy = 0.7
Rotation = 0.4
Fresnel = "Dielectric"
UvScaleOffset = [0.5, 2.0, 0.13, 0.0]
NormalStrength = 0.5

[Strata.Textures]
Normal = "Textures/CoatNormal.png"
Orm = "Textures/CoatOrm.png"
```

## Texture interpretation

| Slot | Encoding | Interpretation |
| --- | --- | --- |
| Albedo | sRGB RGB | Hardware sRGB decode **before** filtering; alpha ignored for opaque output |
| ORM | Linear RGB | R: environment occlusion, G: roughness multiplier, B: opaque metallic multiplier |
| Normal | Linear RGB | Tangent XYZ encoded from −1..1 to 0..1; +Y follows the authored bitangent |
| Radiance | sRGB RGB | Decoded RGB modulates scene-linear material self-radiance |

PNG/JPEG sources are resolved relative to the supplied `ContentDirectory`, not the process working directory or an
arbitrary material-file directory. Absolute, drive-prefixed and backslash paths are rejected. Canonical containment
also rejects symlink escapes. Missing/unsupported files fail compilation. Image dimensions are bounded to 4096²,
encoded files to 64 MiB. Missing slots have explicit neutral 1×1 defaults; unused texture-mask bits stay disabled.

Four fixed descriptors hold 2D texture arrays (no descriptor indexing requirement). Each albedo/ORM/normal
slot deduplicates its source paths into at most eight slices; radiance has one slice. Sources within a slot must
share dimensions, except a 1×1 constant may expand. Mismatched nontrivial sizes fail rather than silently resample.
Storage is mip-major, then slices; mip offsets include every slice. Combined resident arrays are bounded to 256 MiB,
with expansion checked before allocation. Draw recording rejects parameter slice indices beyond resident capacity.

All textures receive complete mip chains. Colour mip averages operate in linear light and re-encode sRGB; normal
mips renormalize the averaged direction; ORM is linear. Odd dimensions use weighted full-footprint box integration.
The shared material sampler is repeat/trilinear, without mandatory anisotropy. PNG top row corresponds to V = 0;
UV importers must follow that convention. Inverted-Y normal maps must be converted before this API.

## Geometry and lifetime

Material geometry explicitly enables `MaterialCoordinatesEnabled`. `SurfaceCorner` is 48 bytes: position at 0,
normal at 12, UV at 24 and tangent XYZW at 32. Tangents must be finite, nonzero, normal-orthogonal and carry handedness
±1. Authored seams require split corners. CAD/MatCap/old preview callers need not enable material coordinates.

Normals use inverse transpose; tangents use the forward view transform and fragment orthogonalization. Reflections
adjust tangent handedness. The material uniform is 1504 bytes; root UV starts at 672,
radiance/normal at 688, uint4 texture mask/slices at 704, layer UVs at 832, layer normal strengths at 960 and
anisotropy/rotation/Fresnel tuples at 1088, automotive paint at 1216 (272 bytes), and runtime-only preview configuration at 1488. The original 1216-byte prefix is unchanged. Shader reflection and C++ assertions check these contracts.

Recorded targets retain textures, uniforms, geometry and programs even after source finalization. The caller owns
queue serialization, submission confirmation, fences and safe replacement. Cancelled recordings do not confirm uploads.
The HTML executable waits for readback completion and reuses unchanged texture pyramids across parameter changes;
there is no production asynchronous retirement scheduler or filesystem watcher.

## Explicit rendering limits

- Ordered opaque/coating BRDFs use independent shading normals, single-scattering anisotropic GGX and Lambert diffuse.
- Coatings add reflection and attenuate underlying scattering with entrance/exit selected Fresnel and Beer absorption.
  Refracted path bending and full multilayer multiple scattering are not implemented.
- Environment illumination uses bounded deterministic analytical-sky quadrature, not a converged HDR reference.
  Its mixture combines a geometric-normal cosine proposal and each rotated anisotropic GGX half-vector proposal,
  with the matching full-mixture PDF. Layer BRDFs include their own light cosine; no shared cosine is applied twice.
  ORM occlusion affects that layer's environment reflection, not direct illumination.
- No transmission, refraction, SSS, arbitrary shader expressions, transparency, GI, RT or ReSTIR.
- No native editor/document material assignment, painting-app integration or hardware/Windows runtime verification.
- No energy-convergence/performance claim is inferred from scalar-reference pixel tests.

Source changes must pass local CPU/Vulkan/sanitizer regressions and shader reflection checks before delivery.
Windows cross-compilation and native Windows execution are separate acceptance gates. See `Exhibits/Renderer/README.md`.

### Executable automotive paint

`[AutomotivePaint]` enables derivative-filtered, object-space cellular particles with 1–4 front-to-back buried layers.
`Distribution` is coverage, particle size in µm, repetition and binder depth. `Finish` stores roughness and metallic
minimum/maximum pairs. `Relief` stores particle tilt, burial tint, orange peel and peel frequency. `Palette` contains
1–12 linear RGB/ramp-position vectors. `ColourMode` is Palette, Ramp or Single; `Enabled` is a Boolean. The strict
codec rejects unknown keys, invalid modes, non-finite/out-of-range values, reversed ranges and unordered ramps.
Compilation derives the unresolved tint from the same 48 linear-space samples as the HTML. The native shader
uses derivative footprints to fade unresolved particles to that mean, not to an arbitrary palette entry. Coating
normal relief is independent of the particle-tilted binder normal. Paint data is authored in `.material`; saving or
duplicating it must not strip procedural parameters. Draft/source failure retains the last valid applied material.

The runtime-only preview configuration classifies the library's combined ball/pedestal/foot/ring geometry and
selects bounded analytic studio-panel environment illumination. It is not a new authored TOML field and does not
relax the normal-projection matrix contract. Other material shader families and full HTML lighting parity are not
implied by implementing automotive paint.
