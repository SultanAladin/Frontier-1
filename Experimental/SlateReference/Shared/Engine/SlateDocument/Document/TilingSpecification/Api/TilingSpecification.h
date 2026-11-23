//============================================================================================================================================
//                                                          TILINGSPECIFICATION.H
//============================================================================================================================================
// 🧩 A repeating pattern as plane symmetry plus cell content — declared, deterministic, and resolving nothing.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/LatticeProjection.slang.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LATTICE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The five plane symmetries `54` §2 declares, held as one specification.
/// note  🔴 These five **compose**. Herringbone is reflection on one axis combined with a rotation increment and
///        an offset progression; twill is a skew and an offset progression; basket weave is reflection on both.
///        None of them is a special case, and a design that enumerated named patterns could express exactly the
///        patterns somebody had already thought of.
/// tag   nonallocating, nonthrowing
struct LatticeSpecification
{
    double         CellExtentX         = 0.1;     // [-] - the repeating unit, in domain units
    double         CellExtentY        = 0.1;     // [-] - likewise
    double         OffsetProgressionX  = 0.0;     // [-] - row-to-row displacement, as a fraction of a cell
    double         OffsetProgressionY = 0.0;     // [-] - column-to-column; never declared beside the above
    double         SkewX               = 0.0;     // [-] - shear for a diagonal repeat
    double         SkewY              = 0.0;     // [-] - likewise
    std::uint32_t  ReflectionMask          = 0u;      // [-] - SlateReflectX and SlateReflectY, composed
    std::uint32_t  RotationIncrement       = 0u;      // [-] - quarter turns per step of the cell schedule

    /// 🧩 Whether the lattice can be classified at all.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive extent, for an extent finer than
    ///                     one texel of the maximum working extent, and for both offset progressions at once
    /// note  🔴 Both progressions together are rejected rather than resolved in a declared order. A row
    ///        displacement depending on the column and a column displacement depending on the row have no
    ///        consistent inverse, and a lattice that cannot be inverted cannot be sampled — which is exactly
    ///        what `70` does at every promotion.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Validate() const;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     CELL CONTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where one cell element's content comes from — `54` §3's four sources.
/// tag   guarantee
enum class CellContentSource : std::uint32_t
{
    VectorOutline = 0u,   // [-] - an outline from `52`, re-resolved at every reduction level
    Imagery       = 1u,   // [-] - a decoded image from `50`
    NestedTiling  = 2u,   // [-] - another tiling; one level of nesting only
    DeclaredColour = 3u,  // [-] - constant within its own coverage
    SourceCount   = 4u    // [-] - the closed count, never a source
};

/// 🧩 One element placed within the repeating cell.
/// note  🔴 The placing transform is **within the cell**, in the cell's own unit square. Held in domain units it
///        would have to be re-derived whenever the cell extents changed, and changing the cell extents is the
///        first thing an artist does to a pattern.
/// note  📝 Sources are carried by ordinal rather than by value. `54` §4 resolves nothing, so it needs no access
///        to the outline or the image — it needs to say which one, and `70` fetches it.
/// tag   nonallocating, nonthrowing
struct CellContent
{
    CellContentSource     Source           = CellContentSource::DeclaredColour;
    std::uint32_t         SourceIndex    = 0u;                             // [-] - into `52`, `50` or the index below
    double                PlacedX      = 0.0;                            // [-] - within the cell's unit square
    double                PlacedY     = 0.0;                            // [-]
    double                PlacedScale      = 1.0;                            // [-] - strictly positive
    double                PlacedRotation   = 0.0;                            // [deg]
    ColourSpecification   DeclaredColour   = {};                             // [-] - read at DeclaredColour
    CombineSpecification  Combination      = CombineSpecification::Over;     // [-] - `22` §3's, unamended
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      VARIATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How cells differ from one another, if at all — `54` §1's three rows.
/// note  🔴 `00` §5 declares continuous stochastic sources absent, and this enumeration is the substitution. The
///        third row is the one most likely to be implemented as noise and is the one that must not be: a
///        permutation of the cell ordinal is reproducible, and sampled noise makes the same document reopen
///        looking different.
/// tag   guarantee
enum class VariationSubject : std::uint32_t
{
    Uniform        = 0u,   // [-] - every cell identical; no variation is declared
    Progressive    = 1u,   // [-] - a declared progression indexed by cell position
    Permuted       = 2u,   // [-] - a deterministic permutation of the cell ordinal into a declared set
    VariationCount = 3u    // [-] - the closed count, never a variation
};

/// 🧩 What varies across cells, and over what interval.
/// tag   nonallocating, nonthrowing
struct VariationSpecification
{
    VariationSubject  Declared      = VariationSubject::Uniform;   // [-]
    std::uint32_t     DeclaredSpan  = 1u;                          // [-] - Permuted; the set it permutes into
    std::uint32_t     PatternSeed   = 1u;                          // [-] - Permuted; stored, never drawn
    double            LowerScale    = 1.0;                         // [-] - Progressive and Permuted
    double            UpperScale    = 1.0;                          // [-] - Progressive and Permuted
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A CELL RESOLVES TO
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One classified domain position — which cell, where inside it, and which variation the cell carries.
/// note  🔴 Not texels. `54` §4 and §5's last gate: this document declares a pattern and resolves nothing into a
///        domain. `70` takes this and produces the texels, at whatever level a tile was promoted to.
/// tag   nonallocating, nonthrowing
struct ClassifiedCell
{
    std::int32_t   CellX        = 0;     // [-] - the cell ordinal along, signed
    std::int32_t   CellY       = 0;     // [-] - the cell ordinal across, signed
    double         WithinX      = 0.0;   // [-] - after the declared reflections and turns
    double         WithinY     = 0.0;   // [-] - likewise
    std::uint32_t  VariationIndex = 0u;    // [-] - below the declared span; zero when Uniform
    double         VariationScale   = 1.0;   // [-] - within the declared interval
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE TILING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One repeating pattern — a lattice, an ordered run of cell content, and how cells vary.
/// note  🔴 Textiles are this. Herringbone, twill, houndstooth and basket weave are all a plane symmetry with
///        content placed in one cell of it, and none of them is noise. The mechanism is periodic and
///        deterministic, and saying so first is the point: pattern generation reaches for noise by habit.
/// tag   owning
class TilingSpecification
{
public:

    /// 🧩 Declares the lattice, validated before it is held.
    /// out   Result  [-]  carries the lattice's own refusal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareLattice(const LatticeSpecification& Declaring);

    /// 🧩 Appends one content element to the cell, at the end of the ordering.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive scale, for a colour declaring no
    ///                     space, and for a nested source in a tiling that is already nested
    /// note  🔴 `54` §3: nesting is bounded at `TilingNestingLimit`. A weave whose thread is itself a weave is
    ///        where the complexity artists want lives; unbounded nesting makes resolution cost unbounded, and
    ///        `20` §2.2's evaluation-cost budget cannot bound what it cannot predict.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareContent(const CellContent& Declaring);

    /// 🧩 Declares how cells differ.
    /// out   Result  [-]  refuses with ContentUnsupported for a declared span of zero, and for an inverted
    ///                     variation interval
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareVariation(const VariationSpecification& Declaring);

    /// 🧩 Declares this tiling as nested inside another, which bars it from nesting one itself.
    /// note  Recorded here rather than checked by the holder, so that a tiling accepted into a cell can refuse a
    ///        nested element afterwards rather than only at the moment it is accepted.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareNestingDepth(std::uint32_t Depth);

    /// 🧩 Classifies one domain position into its cell.
    /// in    PositionX   [-]  the domain's first axis
    /// in    PositionY  [-]  its second
    /// out   Result         [-]  refuses with ContentUnsupported while no valid lattice is declared
    /// note  🔴 Classification goes through `02` §5's `LatticeProjection` in `Shared/`, at Tier A, and never
    ///        through arithmetic written here. `54` §5's gate is that the host and the device agree about which
    ///        cell a position falls in, and two implementations of one boundary are two that will disagree.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ClassifiedCell> Classify(double PositionX, double PositionY) const;

    const LatticeSpecification&     Lattice() const;
    const std::vector<CellContent>& Content() const;
    const VariationSpecification&   Variation() const;

    /// 🧩 How deeply this tiling is nested; zero for one applied directly.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t NestingDepth() const;

    /// 🧩 Whether a lattice has been validly declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool LatticeDeclared() const;

private:

    LatticeSpecification      DeclaredLattice   = {};      // [-] - as validated
    std::vector<CellContent>  DeclaredContent;             // [-] - in declaration order; the ordering is the order
    VariationSpecification    DeclaredVariation = {};      // [-]
    std::uint32_t             Depth             = 0u;      // [-] - zero for a tiling applied directly
    bool                      LatticeHeld       = false;   // [-] - DeclareLattice delivered
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TILINGS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every declared tiling in the document, addressed by ordinal, with the nesting bound enforced across them.
/// note  🔴 The bound is enforced **here** rather than inside a tiling, because a tiling cannot see what it has
///        been nested inside. Accepting a nested reference is where the depth is known, so it is where the
///        refusal belongs.
/// tag   owning
class TilingIndex
{
public:

    /// 🧩 Declares one tiling and issues its ordinal.
    /// out   Result  [-]  refuses with ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare();

    /// 🧩 One declared tiling, for reading.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const TilingSpecification*> Resolve(std::uint32_t TilingIndex) const;

    /// 🧩 One declared tiling, for amending.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<TilingSpecification*> Amend(std::uint32_t TilingIndex);

    /// 🧩 Nests one tiling inside a cell of another, at the declared bound.
    /// in    EnclosingIndex  [-]  the tiling whose cell carries it
    /// in    NestedIndex     [-]  the tiling being nested
    /// out   Result           [-]  refuses with ContentUnsupported for an unknown ordinal, for a tiling nested
    ///                              inside itself, and for a nesting that would exceed `TilingNestingLimit`
    /// post  the nested tiling refuses a nested element of its own from this point
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Nest(std::uint32_t EnclosingIndex, std::uint32_t NestedIndex);

    std::uint32_t DeclaredCount() const;

private:

    static constexpr std::uint32_t TilingLimit = 4096u;   // [-] - tilings one document may declare

    std::vector<TilingSpecification>  Declared;   // [-] - by tiling ordinal
};

// 📐 The cell ordinals and the variation permutation are Exact; the within-cell coordinates and the variation
//    scale are Bounded. The component claims Bounded, per `00` §3's transitivity rule.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
