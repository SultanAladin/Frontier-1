//============================================================================================================================================
//                                                          TRANSFORMPROJECTION.H
//============================================================================================================================================
// 🧩 Decomposed transforms, their composition, and the rebasing that precedes every narrowing to 32-bit.

#pragma once

#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                               POSITIONS AND ROTATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A position in document space, at 64-bit. The document's own origin is what it is measured from.
/// note  Scene extents exceed 32-bit relative precision, which is why document space is never 32-bit.
/// tag   nonallocating, nonthrowing
struct DocumentPosition
{
    double  PositionX = 0.0;   // [mm] - along the document x axis
    double  PositionY = 0.0;   // [mm] - along the document y axis
    double  PositionZ = 0.0;   // [mm] - along the document z axis
};

/// 🧩 A position in view-relative space — `02` §3's second space, at 64-bit and measured from the view origin.
/// note  🔴 A **distinct** structure rather than a second reading of `DocumentPosition`, and the distinctness is
///       the whole of what it buys. The two carry identical members and mean different things, so one structure
///       serving both makes a view-relative position passable wherever a document position is expected — which
///       is `00` §10 conflict 15's defect exactly, and it survived the whole series the first time.
/// note  📐 Still 64-bit. The narrowing is a separate act with its own routine, because the subtraction and the
///       narrowing are two decisions and a caller that wants the first without the second is `46` at every
///       projection it derives. Fusing them is what makes the intermediate unnameable.
/// tag   nonallocating, nonthrowing
struct ViewPosition
{
    double  PositionX = 0.0;   // [mm] - relative to the view origin
    double  PositionY = 0.0;   // [mm] - relative to the view origin
    double  PositionZ = 0.0;   // [mm] - relative to the view origin
};

/// 🧩 A position after rebasing, narrowed for the device.
/// note  🔴 Only ever produced by Narrow or Rebase. A 32-bit position that did not pass through one of them is
///       jitter with a plausible-looking cause.
/// tag   nonallocating, nonthrowing
struct DevicePosition
{
    float  PositionX = 0.0f;   // [mm] - relative to the view origin
    float  PositionY = 0.0f;   // [mm] - relative to the view origin
    float  PositionZ = 0.0f;   // [mm] - relative to the view origin
};

/// 🧩 A rotation as a unit quaternion.
/// note  📐 Compounding quaternions renormalises and multiplying matrices does not, which is what lets the
///       containment relation in `12` compound to unbounded depth without drift.
/// tag   nonallocating, nonthrowing
struct RotationQuaternion
{
    double  ImaginaryX = 0.0;   // [-] - 𝑖 coefficient
    double  ImaginaryY = 0.0;   // [-] - 𝑗 coefficient
    double  ImaginaryZ = 0.0;   // [-] - 𝑘 coefficient
    double  Real       = 1.0;   // [-] - scalar coefficient; identity rotation as declared
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A transform stored decomposed — never as a general matrix.
/// note  Matrix form is derived at the point of use and never stored back. `Project` derives it; nothing
///       caches it, because a cached matrix is a second representation that drifts from the first.
/// tag   nonallocating, nonthrowing
struct DecomposedTransform
{
    DocumentPosition    Translation = {};                     // [mm] - the origin this transform places
    RotationQuaternion  Rotation    = {};                     // [-]  - unit quaternion
    double              ScaleX      = 1.0;                    // [-]  - along the local x axis
    double              ScaleY      = 1.0;                    // [-]  - along the local y axis
    double              ScaleZ      = 1.0;                    // [-]  - along the local z axis
};

/// 🧩 A transform as sixteen coefficients, column-major, derived for one use and discarded.
/// tag   nonallocating, nonthrowing
struct ProjectedTransform
{
    double  Coefficient[16] = { 1.0, 0.0, 0.0, 0.0,
                                0.0, 1.0, 0.0, 0.0,
                                0.0, 0.0, 1.0, 0.0,
                                0.0, 0.0, 0.0, 1.0 };   // [-] - column-major
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     COMPOUNDING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Compounds two rotations and renormalises the result.
/// in    OuterRotation  [-]  applied second
/// in    InnerRotation  [-]  applied first
/// out   Compounded     [-]  a unit quaternion
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
RotationQuaternion Compound(RotationQuaternion OuterRotation, RotationQuaternion InnerRotation);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Compounds two decomposed transforms without ever forming a matrix.
/// in    OuterTransform [-]  applied second — the containing transform
/// in    InnerTransform [-]  applied first — the contained transform
/// out   Compounded     [-]  decomposed, ready to compound again
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
DecomposedTransform Compound(const DecomposedTransform& OuterTransform,
                             const DecomposedTransform& InnerTransform);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Derives the matrix form of a decomposed transform at the point of use.
/// in    Source     [-]  the decomposed transform
/// out   Projected  [-]  column-major coefficients; never stored back into Source
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ProjectedTransform Project(const DecomposedTransform& Source);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                       REBASING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Carries a document position into view-relative space. The subtraction, and nothing else.
/// in    Subject     [mm]  a position in document space
/// in    ViewOrigin  [mm]  the current camera position, in document space
/// out   Relative    [mm]  the same position, measured from the view origin, still at 64-bit
/// note  📐 This is the half of `02` §3.2 that carries the precision claim. The difference of two 64-bit
///       quantities is exact to within one unit in the last place of the **larger operand's** exponent, so a
///       position ten metres from a camera a kilometre from the document origin retains micrometre resolution
///       here. Narrowing that difference afterwards costs the 32-bit relative precision of ten metres, which is
///       far below one micrometre; narrowing before the subtraction costs the relative precision of a kilometre.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ViewPosition Relative(DocumentPosition Subject, DocumentPosition ViewOrigin);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Narrows a view-relative position for the device. The narrowing, and nothing else.
/// in    Subject   [mm]  a position already measured from the view origin
/// out   Narrowed  [mm]  the same position at 32-bit
/// note  🔴 Takes a `ViewPosition` and not a `DocumentPosition`, so a caller cannot narrow an unrebased
///       position by mistake. `02` §8's gate — *every position narrowing to 32-bit is rebased in 64-bit first* —
///       is discharged by this signature rather than by a review, because the only way to obtain the argument is
///       to have called `Relative`.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
DevicePosition Narrow(ViewPosition Subject);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Rebases a document position to the view origin and narrows it for the device — both halves, at once.
/// in    Subject    [mm]  a position in document space
/// in    ViewOrigin [mm]  the current camera position, in document space
/// out   Rebased    [mm]  relative to the view origin, at 32-bit
/// note  🔴 The subtraction happens in 64-bit, before the narrowing. Every position crossing into
///       `SlateCompute` passes through here; `02` §8 gates it and the failure it prevents reads as a
///       driver defect rather than as the arithmetic it is.
/// note  📝 Exactly `Narrow(Relative(Subject, ViewOrigin))` and is implemented as that composition rather than
///       as a third arithmetic. It is kept because the fused act is what most callers want and because every
///       caller in the engine already spells it this way; the two halves exist for the callers — `46` deriving a
///       projection, `78` measuring a grip — that want the difference before it is narrowed.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
DevicePosition Rebase(DocumentPosition Subject, DocumentPosition ViewOrigin);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
