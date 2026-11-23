//============================================================================================================================================
//                                                           TOLERANCEGUARANTEE.H
//============================================================================================================================================
// 🧩 Every tolerance and every capacity two units both read. Depends on nothing; depended on by everything.

#pragma once

#include "Foundation/Toolchain.h"

// 📝 🔴 This file is reached by all three of `Shared/`'s parameterisations and therefore by **every** shader the
//    engine compiles, so it is compiled once per toolchain like the shared source it feeds. Nothing below may be
//    spelled in a construct only the host has: `SLATE_CONSTANT` carries the declarations, `SLATE_STATIC_ASSERT`
//    carries the gates, and the widths come from `Toolchain.h` rather than from `<cstdint>` directly.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                CROSS-UNIT CAPACITIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A number declared inside one unit and read by another is a dependency edge wearing a disguise. Each
//    capacity below is read by at least two units, which is exactly why it is declared here and not there.
SLATE_CONSTANT Unsigned32 PhysicalTileTexels      = 128u;    // [texel] - texels per edge a tile covers, before apron
SLATE_CONSTANT Unsigned32 PhysicalTileApron       = 4u;      // [texel] - border carried by a resident tile
SLATE_CONSTANT Unsigned32 VirtualCellsPerEdge     = 64u;     // [-]     - cells per edge at the finest reduction level
SLATE_CONSTANT Unsigned32 MaximumWorkingEdge      = 8192u;   // [texel] - largest working extent per edge
SLATE_CONSTANT Unsigned32 TransmissionDepth       = 4u;      // [-]     - packed pairs per pixel, sorted
SLATE_CONSTANT Unsigned32 DirectOcclusionCapacity = 4u;      // [-]     - illuminants an RGBA8 target holds
SLATE_CONSTANT Unsigned32 RecordingSlotCount      = 2u;      // [-]     - cyclic recording slots — 🚧 open
SLATE_CONSTANT Unsigned32 DisplayExtentLimit    = 16384u;  // [px]    - largest display extent claimed
SLATE_CONSTANT Unsigned32 SubPixelSequenceLength  = 8u;      // [-]     - rotations before the offsets repeat
SLATE_CONSTANT Unsigned32 IlluminantReachCapacity = 16u;     // [-]     - illuminants one partition may carry
SLATE_CONSTANT Unsigned32 TilingNestingLimit    = 1u;      // [-]     - levels a tiling may nest — `54` §3

// 📝 🔴 `16` §1's partition extent. The host partitioner in `SlateCompute` fills against it, and `16`'s culling
//    and raster shaders read it as the specialisation constant `06`'s `ShaderCodec` fixes at stage construction
//    rather than recompiling per document. One number read by two toolchains is `00` §2's case without
//    exception, and the alternative — the host filling to 128 while a shader dispatched 64 lanes per partition —
//    drops half of every partition's triangles and reads as a hole in the surface rather than as a constant.
// 📝 A floor and a ceiling rather than one budget, because a partition grown across adjacency stops where the
//    surface or the material stops, and the last partition of a connected piece takes whatever remains.
SLATE_CONSTANT Unsigned32 PartitionTriangleFloor   = 64u;    // [-] - fewest triangles a filled partition carries
SLATE_CONSTANT Unsigned32 PartitionTriangleLimit = 128u;   // [-] - most triangles any partition carries

SLATE_STATIC_ASSERT(PartitionTriangleFloor <= PartitionTriangleLimit,
                    "The partition triangle floor may not exceed the partition triangle ceiling.");

// 📝 🔴 The three resident atmosphere extents are read by `08` §2's shared-target table in `SlateVulkan` and by
//    `28` in `SlateCompute`. Two units, one set of numbers, so `00` §2 places them here without exception —
//    `08` §2 previously carried them as comments beside the table and `28` §1 as prose, which is the same
//    number written twice in two units and is exactly the disguised edge conflict 30 was recorded to remove.
SLATE_CONSTANT Unsigned32 TransmittanceExtentX  = 256u;   // [px] - altitude
SLATE_CONSTANT Unsigned32 TransmittanceExtentY = 64u;    // [px] - sun zenith angle
SLATE_CONSTANT Unsigned32 MultiScatterExtentX   = 32u;    // [px] - altitude
SLATE_CONSTANT Unsigned32 MultiScatterExtentY  = 32u;    // [px] - sun zenith angle
SLATE_CONSTANT Unsigned32 SkyViewExtentX        = 192u;   // [px] - view azimuth
SLATE_CONSTANT Unsigned32 SkyViewExtentY       = 108u;   // [px] - view zenith
SLATE_CONSTANT Unsigned32 AtmosphereComponentCount  = 4u;     // [-]  - RGBA
SLATE_CONSTANT Unsigned32 AtmosphereComponentBytes  = 2u;     // [B]  - half precision; Tier D by definition

// 📝 The widening is a functional cast rather than `static_cast`, which the shader toolchain does not spell. It is
//    applied to the first operand of each product alone: the widened operand carries the rest of its own product
//    up with it, and each of the three products is well inside the narrow width regardless.
SLATE_CONSTANT Unsigned64 AtmosphereResidentBytes =
    Unsigned64(TransmittanceExtentX) * TransmittanceExtentY
        * AtmosphereComponentCount * AtmosphereComponentBytes
  + Unsigned64(MultiScatterExtentX)  * MultiScatterExtentY
        * AtmosphereComponentCount * AtmosphereComponentBytes
  + Unsigned64(SkyViewExtentX)       * SkyViewExtentY
        * AtmosphereComponentCount * AtmosphereComponentBytes;   // [B]

// 🔴 `28` §7's first gate, as a build failure rather than as a review remark. The figure was arithmetically
//    wrong once already — `00` §10 conflict 42 records 217 KB against a true 298 KiB — and prose review is
//    what failed to catch it. 128 + 8 + 162 = 298.
// 📝 The gate is checked by the host translation alone, because the shader toolchain has no compile-time
//    assertion to check it with. That costs nothing: this file is compiled by both toolchains on every build from
//    one source, so a figure that fails the gate fails the build before any shader reads it.
SLATE_STATIC_ASSERT(AtmosphereResidentBytes == Unsigned64(298) * Unsigned64(1024),
                    "The three resident atmosphere surfaces must total the declared 298 KiB.");

// 📝 🔴 `54` §3 bounds nesting at one level and `70` resolves what that bound accepts, so two units read the
//    number and `00` §2 places it here. A weave whose thread is itself a weave is where the complexity artists
//    want lives; a second level makes resolution cost unbounded, and `20` §2.2's evaluation-cost budget cannot
//    bound what it cannot predict.

// 📝 🚧 `64` §9 leaves the offset sequence length open and it blocks convergence quality alone. It is declared
//    here rather than in `64` because `46` applies the offset, `64` accumulates across it and `82` replays it —
//    three units reading one number, which `00` §2 places in `Foundation/` without exception.

// 📝 🔴 `44` §5's reach capacity is read by `44` and by `60` §3.1, which truncates again at the narrower packed
//    capacity of `DirectOcclusionSurface`. Two units, one number, so `00` §2 places it here. Exceeding it is a
//    truncation reported through `86`, never a silent drop.

// 📝 🔴 `MaximumWorkingEdge` is `20` §1's largest working extent per edge, and `68` §5 sizes its inter-chart gap
//    against it together with the apron. Two units read it, so `00` §2 places it here rather than in either —
//    which is precisely the edge conflict 30 was recorded to remove, closed at the declaration rather than by
//    a comment promising the read is "only a constant".

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DEPTH CONVENTION
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 Depth is **reversed** — the nearest plane maps to one and the furthest to zero. `46` §3 declares it a
//    repository-wide convention rather than one document's private choice: `16`'s comparison, `30`'s ray march,
//    `60`'s occlusion comparison and `80` ⑩'s depth test all read it. Reversed depth with a floating-point target
//    distributes precision where perspective takes it away; the forward arrangement spends its precision near the
//    camera, where the perspective divide has already supplied precision for free, and starves the distance.
// ⚠️ One document reversing its own test in isolation produces geometry that **vanishes** rather than geometry
//    that sorts wrongly, which is why the two ordinates are constants and not literals at each comparison.
SLATE_CONSTANT Real64 NearPlaneDepth = 1.0;   // [-] - clip depth at the nearest plane
SLATE_CONSTANT Real64 FarPlaneDepth  = 0.0;   // [-] - clip depth at the furthest plane

// 📝 The clip coordinate is inverted for the device's downward-increasing display coordinate. Declared here because
//    `46` derives the projection and `16` reconstructs a position from a pixel; a sign the two spell separately
//    is a sign they will eventually spell differently.
SLATE_CONSTANT Real64 ClipCoordinateSignum = -1.0;   // [-] - applied to the projection's second row

// 📐 Frustum planes are pushed outward by this fraction of their own distance, matching `38` §6's extents and
//    `40` §6's subdivision. An inward-rounded plane culls geometry the camera can see, and the artist meets it as
//    a surface that disappears along one edge of the display.
SLATE_CONSTANT Real64 FrustumOutwardMargin = 1.0e-6;   // [-] - relative, with an absolute floor at the origin

//------------------------------------------------------------------------------------------------------------------------
//                                                      TOLERANCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A tolerance is scale-relative, expressed against the extent of the operand rather than as an absolute
//    distance, because an absolute tolerance is correct at exactly one scene scale.
SLATE_CONSTANT Real64 WeldTolerance          = 1.0e-5;   // [-] - below this two positions are one position
SLATE_CONSTANT Real64 ImpressionSpacingFloor = 0.01;     // [-] - finest spacing, as a fraction of the extent
SLATE_CONSTANT Real64 CollinearityTolerance  = 1.0e-9;   // [-] - filtered only; the exact path decides
SLATE_CONSTANT Real64 QuaternionRenormalise  = 1.0e-12;  // [-] - below this a rotation is left untouched

// 📐 🔴 The fraction of one spacing by which a walked distance may fall short of the next impression and still be
//    counted as having reached it — `58` §3's resampling. Both operands are accumulated sums, so an exact
//    comparison decides the last impression of a segment on the residue of the additions rather than on the
//    geometry: a path of four tenths at a spacing of one fortieth is sixteen spacings exactly and 5.6e-17 short of
//    sixteen in binary, and the artist meets that as a stroke ending one impression before they released.
// 📝 Relative to the spacing rather than absolute, because the spacing is a domain distance and the domain is the
//    unit square at one working extent and something else at the next. Above the accumulation of a full stroke —
//    65536 additions carry roughly 7e-12 of relative error — and far below a residue that means anything.
SLATE_CONSTANT Real64 SpacingArrivalTolerance = 1.0e-9;   // [-] - as a fraction of one spacing

// 📐 The Newton criterion the Gauss–Legendre abscissae are derived against, and the ceiling that bounds the
//    derivation. `02` §8 gates that no tolerance literal appears outside `Foundation/`, so neither may sit at the
//    derivation site — a criterion written there is one that is tuned there, and a rule derived to two different
//    criteria in two builds integrates to two different numbers.
// 📝 Newton on the Legendre recurrence converges quadratically from the standard initial estimate, so the
//    ceiling is reached only by an implementation defect rather than by a hard input.
SLATE_CONSTANT Real64     QuadratureConvergence      = 1.0e-15;   // [-] - Newton step below which it settles
SLATE_CONSTANT Unsigned32 QuadratureIterationLimit = 128u;      // [-] - iterations before it stops

// 📐 🔴 The magnitude, relative to the greatest coefficient the system supplied, below which a pivot no longer
//    carries a trustworthy sign. `02` §5's `LinearSolver` refuses at it rather than dividing by it. The floor is
//    relative for the reason every tolerance here is relative: an absolute one is correct at exactly one scaling
//    of the system, and a system scaled in millimetres and the same system scaled in metres would then be called
//    singular in one spelling and solvable in the other.
// ⚠️ It sits here rather than at the factorisation because `02` §8 gates that no tolerance literal appears
//    outside `Foundation/`, and because `24` and `68` both read the solver — a floor tuned at the call site is one
//    tuned twice, and the two consumers would then disagree about which systems exist.
SLATE_CONSTANT Real64 FactorisationPivotFloor = 1.0e-14;   // [-] - relative to the greatest supplied magnitude

// 📐 `28` §4's "materially". A rebuild on any camera movement at all makes a precomputed surface an expensive
//    way to compute what it was meant to precompute; a strict inequality makes every rebuild condition true.
SLATE_CONSTANT Real64 SunDirectionMateriality   = 0.0035;   // [rad] - about a fifth of a degree
SLATE_CONSTANT Real64 CameraAltitudeMateriality = 10.0;     // [m]

// 📐 🔴 `60` §7 places this here by name, and it is the single most-tuned tolerance in any renderer — one
//    written at the comparison site is one that is tuned in six places and agrees in none. It is a **depth**
//    offset under the reversed convention, so it is subtracted from the occluder's recorded coordinate rather
//    than added: near is one, so pushing an occluder further means lowering it.
// ⚠️ Slope-scaled rather than constant. A constant offset large enough to clear a surface at a grazing angle
//    detaches every contact shadow on a surface facing the illuminant, and the artist reads the gap as the
//    occlusion being wrong rather than as the offset being one number doing two jobs.
SLATE_CONSTANT Real64 ShadowComparisonOffset      = 2.0e-4;   // [-] - constant term, in reversed depth
SLATE_CONSTANT Real64 ShadowComparisonSlopeFactor = 3.0e-3;   // [-] - scaled by the receiver's grazing tangent

// 📐 `60` §5's depth-aware upsample. The ambient term is resolved at half extent and read at display extent, so
//    a bilinear read crosses depth discontinuities and pulls a background surface's occlusion onto a foreground
//    silhouette — visible as a dark fringe around every object. The bound is relative to the centre coordinate so
//    that it means the same thing at every distance; an absolute one rejects every tap in the distance.
SLATE_CONSTANT Real64 AmbientUpsampleDepthBound = 0.02;   // [-] - relative departure a tap may carry

// 📝 🔴 The spacing floor is `58` §5's and is read by `22`, which resamples a path at it and whose impression
//    count it therefore bounds. Two units, one number, so `00` §2 places it here. It is applied **and said** —
//    `58` reports reaching it through `86` rather than coarsening a stroke silently.

//------------------------------------------------------------------------------------------------------------------------
//                                      FLOATING-POINT CONSTANTS OF THE EXACT PATH
//------------------------------------------------------------------------------------------------------------------------

// 📐 ε is the unit roundoff of the 64-bit representation, 2⁻⁵³. The orientation filter constant is
//    (3 + 16ε)ε, which is the bound below which the sign of the filtered determinant cannot be trusted.
SLATE_CONSTANT Real64 MachineEpsilon         = 1.1102230246251565e-16;                         // [-] - ε
SLATE_CONSTANT Real64 OrientationErrorFactor = (3.0 + 16.0 * MachineEpsilon) * MachineEpsilon;  // [-]
SLATE_CONSTANT Real64 IncircleErrorFactor    = (10.0 + 96.0 * MachineEpsilon) * MachineEpsilon; // [-]
SLATE_CONSTANT Real64 ExpansionSplitter      = 134217729.0;                                     // [-] - 2²⁷+1

//------------------------------------------------------------------------------------------------------------------------
//                                          MATHEMATICAL AND SAMPLING CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 Declared once, here, for the reason `02` §8 declares tolerances here: a constant transcribed a second
//    time is transcribed to a second precision, and two subsystems that disagree in the sixteenth place produce
//    a seam nobody attributes to a literal.
SLATE_CONSTANT Real64 Pi = 3.14159265358979323846;   // [-] - the circle constant

// 📐 A Bounded shared entry point is compared against this many units in the last place rather than for
//    equality. A direction that departs from unit length by more has not merely rounded — it has taken a
//    different path through its own arithmetic, which is what parity exists to catch.
SLATE_CONSTANT Real64 SampleUnitPlaceLimit = 8.0;   // [-] - units in the last place

}   // namespace Slate
