//============================================================================================================================================
//                                                         LATTICEPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 Periodic plane symmetry — the cell one domain position falls in, and where inside that cell it lands.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"

// 📐 `02` §5 places this at Tier A and `54` §2 gives the reason from the consuming side: `82` classifies a
//    position on the host and `70` classifies it on the device, and a cell boundary the two disagree about
//    produces a pattern that does not meet itself across a tile edge. One boundary, one implementation.
//
// 🔴 The classification is in two halves and they are separated deliberately. Which cell a position falls in is
//    integer flooring over an unskewed coordinate and is Exact. Where inside that cell it lands after the
//    declared reflections and turns reads the cell ordinals but no transcendental, and is Exact as well — the
//    Bounded part of `54` is the content resolution `70` performs afterwards, not this.

// 📝 The reflections are named as constants rather than as an enumeration because an enumeration declared on the
//    host has no spelling the shader toolchain shares without a second declaration that must be kept identical.
#define SlateReflectX  (1u)   // [-] - the first axis mirrors on alternate cells along it
#define SlateReflectY (2u)   // [-] - the second axis mirrors on alternate cells across it

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CELL ORDINAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The greatest ordinal not above a coordinate.
/// in    Coordinate  [-]  a continuous cell coordinate, of either sign
/// out   Index     [-]  floored, never truncated toward zero
/// note  🔴 Truncation and flooring agree above the origin and disagree below it, so a lattice that truncated
///        would repeat its first cell twice across the origin and shift every negative cell by one. The artist
///        meets that as a pattern whose repeat breaks along exactly one row and one column of the domain.
/// cost  ✔️
/// note  Exact — a comparison and an integer decrement; identical on the host and on the device.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Signed32 FlooredIndex(Real64 Coordinate)
{
    const Signed32 Truncated = Signed32(Coordinate);

    return Coordinate < Real64(Truncated) ? Truncated - 1 : Truncated;
}

/// 🧩 Classifies one domain position into its lattice cell and its position within that cell.
/// in    PositionX            [-]  the domain's first axis
/// in    PositionY           [-]  its second
/// in    CellExtentX          [-]  the repeating unit, strictly positive — `54` §2
/// in    CellExtentY         [-]  likewise
/// in    OffsetProgressionX   [-]  row-to-row displacement, as a fraction of a cell
/// in    OffsetProgressionY  [-]  column-to-column; never declared beside the above
/// in    SkewX                [-]  shear for a diagonal repeat
/// in    SkewY               [-]  likewise
/// out   CellX                [-]  the cell ordinal along, signed
/// out   CellY               [-]  the cell ordinal across, signed
/// out   WithinX              [-]  in the half-open unit interval, before reflection and turning
/// out   WithinY             [-]  likewise
/// pre   the lattice validated — a vanishing extent or a unit skew product is rejected at declaration
/// note  🔴 The two offset progressions are resolved in opposite orders and never both at once. A row
///        displacement depending on the column ordinal needs the column resolved first and a column displacement
///        depending on the row needs the row; declaring both leaves each waiting on the other, which is why
///        `LatticeSpecification::Validate` refuses the pair rather than picking an order here.
/// cost  ✔️
/// note  Exact — the unskewing is one correctly-rounded division per axis and the flooring is integral.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ClassifyLatticeCell(Real64 PositionX,
                                      Real64 PositionY,
                                      Real64 CellExtentX,
                                      Real64 CellExtentY,
                                      Real64 OffsetProgressionX,
                                      Real64 OffsetProgressionY,
                                      Real64 SkewX,
                                      Real64 SkewY,
                                      SLATE_OUT(Signed32) CellX,
                                      SLATE_OUT(Signed32) CellY,
                                      SLATE_OUT(Real64)   WithinX,
                                      SLATE_OUT(Real64)   WithinY)
{
    // 📐 The shear is inverted rather than applied. A position arrives in the domain and the lattice is what is
    //    sheared, so classifying it means carrying the position back through the shear the lattice declares.
    const Real64 Determinant    = 1.0 - SkewX * SkewY;
    const Real64 UnskewedX  = (PositionX  - SkewX  * PositionY) / Determinant;
    const Real64 UnskewedY = (PositionY - SkewY * PositionX)  / Determinant;

    const Real64 CoordinateX  = UnskewedX  / CellExtentX;
    const Real64 CoordinateY = UnskewedY / CellExtentY;

    if (OffsetProgressionY != 0.0)
    {
        CellX   = FlooredIndex(CoordinateX);
        WithinX = CoordinateX - Real64(CellX);

        const Real64 DisplacedY = CoordinateY - OffsetProgressionY * Real64(CellX);

        CellY   = FlooredIndex(DisplacedY);
        WithinY = DisplacedY - Real64(CellY);

        return;
    }

    CellY   = FlooredIndex(CoordinateY);
    WithinY = CoordinateY - Real64(CellY);

    const Real64 DisplacedX = CoordinateX - OffsetProgressionX * Real64(CellY);

    CellX   = FlooredIndex(DisplacedX);
    WithinX = DisplacedX - Real64(CellX);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 WITHIN ONE CELL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Applies the declared reflections and quarter turns to a position within its cell.
/// in    CellX          [-]  the cell ordinal along; its parity selects the reflection
/// in    CellY         [-]  the cell ordinal across; likewise
/// in    WithinX        [-]  in the unit interval, as classified
/// in    WithinY       [-]  likewise
/// in    ReflectionMask     [-]  SlateReflectX and SlateReflectY, composed
/// in    RotationIncrement  [-]  quarter turns per step of the cell schedule
/// out   ProjectedX     [-]  in the unit interval, after reflection and turning
/// out   ProjectedY    [-]  likewise
/// note  🔴 `54` §2: the five symmetries **compose** and none of them is a named pattern. Herringbone is one
///        reflection with a rotation increment and an offset progression, and twill is a skew with an offset
///        progression. A form that enumerated named patterns could express exactly the patterns already listed.
/// note  📝 The turn count is taken modulo four over the sum of the two ordinals, in unsigned arithmetic, so a
///        cell at a negative ordinal turns the same way as the positive cell four steps from it rather than
///        turning by a negative count that has no meaning.
/// cost  ✔️
/// note  Exact — reflection is one subtraction from unity and a quarter turn exchanges two coordinates.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectWithinCell(Signed32   CellX,
                                    Signed32   CellY,
                                    Real64     WithinX,
                                    Real64     WithinY,
                                    Unsigned32 ReflectionMask,
                                    Unsigned32 RotationIncrement,
                                    SLATE_OUT(Real64) ProjectedX,
                                    SLATE_OUT(Real64) ProjectedY)
{
    Real64 X  = WithinX;
    Real64 Y = WithinY;

    if ((ReflectionMask & SlateReflectX) != 0u && (Unsigned32(CellX) & 1u) != 0u)
    {
        X = 1.0 - X;
    }

    if ((ReflectionMask & SlateReflectY) != 0u && (Unsigned32(CellY) & 1u) != 0u)
    {
        Y = 1.0 - Y;
    }

    const Unsigned32 Turns = (RotationIncrement * (Unsigned32(CellX) + Unsigned32(CellY))) & 3u;

    for (Unsigned32 TurnIndex = 0u; TurnIndex < Turns; ++TurnIndex)
    {
        const Real64 Turned = X;

        X  = Y;
        Y = 1.0 - Turned;
    }

    ProjectedX  = X;
    ProjectedY = Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE CELL ORDINAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Carries one signed ordinal onto the unsigned ordinals, order-preserving about the origin.
/// in    Index   [-]  a cell ordinal, of either sign
/// out   Folded    [-]  a non-negative ordinal; zero maps to zero and −1 to one
/// note  📝 The negative branch is written against `Index + 1` rather than against `Index`, because the most
///        negative representable ordinal has no representable negation and negating it directly is undefined.
/// cost  ✔️
/// note  Exact — integer arithmetic; a bijection onto the unsigned ordinals.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 ZigzagIndex(Signed32 Index)
{
    return Index < 0 ? ((Unsigned32(-(Index + 1)) << 1) | 1u) : (Unsigned32(Index) << 1);
}

/// 🧩 Folds a cell's two ordinals into the one ordinal its variation is indexed by.
/// in    CellX   [-]  the cell ordinal along, signed
/// in    CellY  [-]  the cell ordinal across, signed
/// out   Folded      [-]  the ordinal `54` §1's variation is a function of
/// note  🔴 Variation is a function of **this ordinal and nothing else** — `54` §1. That is what makes a pattern
///        survive a reopen, agree between a coarse reduction level and the finer one that replaces it, and agree
///        between `82`'s host preview and `70`'s device resolution.
/// note  ⚠️ Two cell ordinals carry more information than one, so the fold cannot be injective and two distant
///        cells will eventually share a variation. That is a repeat at a period far beyond any declared lattice,
///        not a collision worth resolving — resolving it would need an ordinal wider than the sequence consuming
///        it accepts.
/// cost  ✔️
/// note  Exact — two products and one sum, all wrapping in 32 bits by construction.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 FoldedCellIndex(Signed32 CellX, Signed32 CellY)
{
    return ZigzagIndex(CellX) * 0x9E3779B9u + ZigzagIndex(CellY) * 0x85EBCA6Bu;
}

}   // namespace Slate
