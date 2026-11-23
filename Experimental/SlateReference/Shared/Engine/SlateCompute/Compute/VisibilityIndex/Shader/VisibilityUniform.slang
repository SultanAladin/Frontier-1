//============================================================================================================================================
//                                                          VISIBILITYUNIFORM.SLANG
//============================================================================================================================================
// 🧩 The one uniform block both visibility entry points read, the spans they read geometry from, and the word a fragment writes.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

// 📝 🔴 The block, the carried word and the projection are declared **once**, here, and included by both entry
//    points. The vertex entry point resolves a position and the fragment entry point writes what it carried; a
//    declaration spelled separately at each is the same layout written twice, and a member inserted into one of
//    the two is not a compilation failure — it is the fragment reading the vertex's fields at an offset, which
//    reaches the artist as an image where every pixel names a partition that is nearly the right one.
//
// 🔴 The two entry points sit in **separate** files rather than in this one. The build lowers each `.slang`
//    carrying an entry point attribute on its own and passes no entry point name, and the toolchain names a
//    lone entry point `main` — which is what `ShaderCodec::Stage` declares. A second entry point in one file
//    makes the toolchain emit both under their own names instead, and the vendor then declines a program whose
//    declared entry point no module carries. One entry point per file is what keeps that spelling true.
//
// ⚠️ This file spells no entry point attribute **anywhere**, comments included. The build decides what is a
//    translation and what is an include by searching each `.slang` for that attribute's opening, so a file
//    naming it in prose is one the build hands to the toolchain — which then refuses a stream that exports
//    nothing. An include declares and never enters.
//
// ⚠️ The block is **32-bit** and `46` derives its projection at 64. That is the same concession `28` makes and
//    for the same reason: `06` negotiates no 64-bit uniform storage. What keeps the narrowing affordable is
//    where it happens — the host rebases against the view origin at 64 bits and folds the result into this
//    transform's fourth column, so every coefficient that crosses the edge is already view-relative even though
//    the positions it multiplies are not. The width would only buy precision in a product whose operands all
//    sit within one view of the origin once the transform has been applied.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The composed projection, the display extent and the partition run the recording is drawing.
/// note  📝 Every member is scalar and ordered widest-first so the layout carries no implicit padding under any
///        of the three declared layout rules. A matrix member here would carry a row stride the host mirror
///        would agree with under exactly one of them.
/// note  🔴 The coefficients are column-major, as `02` declares `ProjectedTransform`. The projection is applied
///        as a column-major product below and nowhere else, so the convention is read at one site rather than
///        transposed at whichever site last needed it the other way round.
/// tag   guarantee
struct VisibilityUniform
{
    Real32      ComposedCoefficient00 ;   // [-]  - `46`'s Composed, column-major, first column
    Real32      ComposedCoefficient01 ;   // [-]
    Real32      ComposedCoefficient02 ;   // [-]
    Real32      ComposedCoefficient03 ;   // [-]
    Real32      ComposedCoefficient04 ;   // [-]  - second column
    Real32      ComposedCoefficient05 ;   // [-]
    Real32      ComposedCoefficient06 ;   // [-]
    Real32      ComposedCoefficient07 ;   // [-]
    Real32      ComposedCoefficient08 ;   // [-]  - third column
    Real32      ComposedCoefficient09 ;   // [-]
    Real32      ComposedCoefficient10 ;   // [-]
    Real32      ComposedCoefficient11 ;   // [-]
    Real32      ComposedCoefficient12 ;   // [-]  - fourth column; the rebased translation
    Real32      ComposedCoefficient13 ;   // [-]
    Real32      ComposedCoefficient14 ;   // [-]
    Real32      ComposedCoefficient15 ;   // [-]
    Unsigned32  DisplayX          ;   // [px] - the extent this rotation is recorded against
    Unsigned32  DisplayY         ;   // [px]
    Unsigned32  RegistrationBase      ;   // [-]  - the document-wide ordinal this registration's partitions start at
    Unsigned32  DrawnPartitionCount   ;   // [-]  - partitions the recording draws, from the base
    Unsigned32  SurvivingResolved     ;   // [-]  - non-zero selects the compacted triangle indirection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS RESOLVED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One vertex position in its owner's own object space, narrowed at the upload.
/// note  🔴 Object space and **not** rebased, which is what lets the span persist. `16` §1 makes the residency
///         survive every rotation and the view origin moves every rotation, so a span carrying view-relative
///         positions would have to be rewritten whenever the camera moved — the one cost `16` §1 exists to avoid.
/// note  🔴 The rebasing is therefore in the **transform** rather than in the position. The host composes the
///         owner's placement with `46`'s projection at 64 bits, with `02`'s `Rebase` applied to the placement's
///         translation before anything narrows, and only the composed coefficients cross at 32 bits. That ordering
///         is the whole point: subtracting the view origin after a truncation is what `02` calls jitter with a
///         plausible-looking cause, and folding it into the translation places the subtraction before the narrowing.
/// note  ⚠️ Narrowing an object-space position is safe where narrowing a document one is not. An object extent is
///         bounded by the owner and a 32-bit real resolves it far below `38`'s welding tolerance; a document
///         position carries the placement as well, and that is what exhausts the width.
/// tag   guarantee
struct ObjectPosition
{
    Real32  PositionX ;   // [mm] - in the owner's own object space
    Real32  PositionY ;   // [mm]
    Real32  PositionZ ;   // [mm]
};

/// 🧩 One drawn triangle — which partition it belongs to and its ordinal within that partition.
/// note  🔴 The triangle ordinal counts **within the partition** and never within the document, per `16` §4.
///        A partition carries 64 to 128 triangles, so the ordinal fits a narrow range and the pair fits the two
///        components `08` §2 claims `VisibilityIndex` as. Counting document-wide would exhaust the component on
///        a document of a few million triangles and leave nothing for the partition.
/// tag   guarantee
struct DrawnTriangle
{
    Unsigned32  CornerVertex0    ;   // [-] - into the object-space positions
    Unsigned32  CornerVertex1    ;   // [-]
    Unsigned32  CornerVertex2    ;   // [-]
    Unsigned32  PartitionIndex ;   // [-] - document-wide, as `16` §4's first component
    Unsigned32  TriangleIndex  ;   // [-] - within the partition
    Unsigned32  Unoccupied       ;   // [-] - pads the record to an even word count; never read
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS CARRIED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the vertex entry point resolves and the fragment entry point writes.
/// note  🔴 Both ordinals are carried **flat**. They are ordinals and not quantities: interpolating a partition
///        ordinal across a triangle produces a fractional ordinal at every pixel but the corners, which resolves
///        to whichever partition the rounding reaches and reads as an image speckled with a neighbour's surface.
/// tag   guarantee
struct ResolvedCorner
{
    Real32x4                     ClipPosition      : SV_Position ;   // [-] - the projection's own output
    nointerpolation  Unsigned32  PartitionIndex  : PARTITION    ;   // [-] - document-wide, flat
    nointerpolation  Unsigned32  TriangleIndex   : TRIANGLE     ;   // [-] - within the partition, flat
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects one object-space position through the composed transform.
/// in    Projecting  [-]   the uniform block, for its coefficients
/// in    Positioned  [mm]  the position, in the owner's own object space
/// out   Clipped     [-]   the clip position, the perspective divide not yet applied
/// note  📐 Column-major, matching `02`'s `ProjectedTransform`. The fourth column is the translation and it is
///         applied with an implicit unit fourth coordinate rather than with a stored one — the position is a point
///         and never a direction, so the alternative is a component that is unity at every call site.
/// note  🔴 The transform carries the owner's placement, the rebasing and `46`'s projection, composed at 64
///         bits by the host and narrowed once. One product per corner and no second transform: a shader applying
///         a placement and then a projection has narrowed the intermediate, and that intermediate is the document
///         position whose width `02` rebases precisely to avoid spending.
/// note  🔴 The reversed depth convention is `46`'s and lives in the projection itself, not here. A shader
///         negating a depth after the projection has reversed it has reversed it twice, and the image is the
///         inside of every object rather than an image that sorts wrongly.
/// cost  ✔️
/// tag   nonallocating, nonthrowing
Real32x4 ProjectObjectPosition(VisibilityUniform Projecting, ObjectPosition Positioned)
{
    Real32x4 Clipped;

    Clipped.x = Projecting.ComposedCoefficient00 * Positioned.PositionX
              + Projecting.ComposedCoefficient04 * Positioned.PositionY
              + Projecting.ComposedCoefficient08 * Positioned.PositionZ
              + Projecting.ComposedCoefficient12;

    Clipped.y = Projecting.ComposedCoefficient01 * Positioned.PositionX
              + Projecting.ComposedCoefficient05 * Positioned.PositionY
              + Projecting.ComposedCoefficient09 * Positioned.PositionZ
              + Projecting.ComposedCoefficient13;

    Clipped.z = Projecting.ComposedCoefficient02 * Positioned.PositionX
              + Projecting.ComposedCoefficient06 * Positioned.PositionY
              + Projecting.ComposedCoefficient10 * Positioned.PositionZ
              + Projecting.ComposedCoefficient14;

    Clipped.w = Projecting.ComposedCoefficient03 * Positioned.PositionX
              + Projecting.ComposedCoefficient07 * Positioned.PositionY
              + Projecting.ComposedCoefficient11 * Positioned.PositionZ
              + Projecting.ComposedCoefficient15;

    return Clipped;
}

}   // namespace Slate
