//============================================================================================================================================
//                                                         TILINGSPECIFICATION.CPP
//============================================================================================================================================
// 🧩 Lattice validation, the per-cell variation that is a permutation rather than a sample, and the nesting bound.

#include "SlateDocument/Document/TilingSpecification/Api/TilingSpecification.h"

#include "Shared/SampleProjection.slang.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   LATTICE VALIDATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> LatticeSpecification::Validate() const
{
    if (CellExtentX <= 0.0 || CellExtentY <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a cell of no extent repeats nothing" });

    // 📝 A cell finer than one texel of the maximum working extent can never be resolved distinctly at any level
    //    `20` will promote, so it is rejected where it is declared rather than discovered as a grey smear.
    const double FinestExtent = 1.0 / static_cast<double>(MaximumWorkingEdge);

    if (CellExtentX < FinestExtent || CellExtentY < FinestExtent)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a cell finer than one texel of the maximum working extent" });
    }

    // 🔴 `54` §2: the two progressions have no consistent inverse together. Rejected rather than resolved in some
    //    declared order, because whichever order was chosen would be invisible in the declaration and decisive
    //    in the result.
    if (OffsetProgressionX != 0.0 && OffsetProgressionY != 0.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "two offset progressions at once cannot be inverted" });
    }

    // 📐 A skew product reaching unity collapses the lattice onto a line, and the unskewing above then divides
    //    by a vanishing determinant.
    if (SkewX * SkewY >= 1.0 || SkewX * SkewY <= -1.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the declared skew collapses the lattice" });

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TilingSpecification::DeclareLattice(const LatticeSpecification& Declaring)
{
    const Deliver<bool> Validated = Declaring.Validate();

    if (!Validated.Resolved)
        return Validated;

    DeclaredLattice = Declaring;
    LatticeHeld     = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> TilingSpecification::DeclareContent(const CellContent& Declaring)
{
    if (Declaring.PlacedScale <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a content element of no scale" });

    if (Declaring.Source == CellContentSource::DeclaredColour && !Declaring.DeclaredColour.ColourDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a declared colour carries no space" });

    // 🔴 A tiling already nested may not nest another — `54` §3's one level, enforced at the element rather than
    //    at the reference, so a tiling that is nested afterwards still refuses.
    if (Declaring.Source == CellContentSource::NestedTiling && Depth >= TilingNestingLimit)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "nesting is bounded at one level — `54` §3" });
    }

    DeclaredContent.push_back(Declaring);

    return Deliver<bool>::Result(true);
}

Deliver<bool> TilingSpecification::DeclareVariation(const VariationSpecification& Declaring)
{
    if (Declaring.Declared == VariationSubject::Permuted && Declaring.DeclaredSpan == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a permutation into an empty set" });

    if (Declaring.UpperScale < Declaring.LowerScale)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the variation interval is inverted" });

    DeclaredVariation = Declaring;

    return Deliver<bool>::Result(true);
}

void TilingSpecification::DeclareNestingDepth(std::uint32_t IncomingDepth)
{
    Depth = IncomingDepth;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ClassifiedCell> TilingSpecification::Classify(double PositionX, double PositionY) const
{
    if (!LatticeHeld)
        return Deliver<ClassifiedCell>::Refuse({ RefusalReason::ContentUnsupported, "no lattice is declared" });

    ClassifiedCell Classified;

    std::int32_t CellX    = 0;
    std::int32_t CellY   = 0;
    double       WithinX  = 0.0;
    double       WithinY = 0.0;

    ClassifyLatticeCell(PositionX,                    PositionY,
                        DeclaredLattice.CellExtentX,  DeclaredLattice.CellExtentY,
                        DeclaredLattice.OffsetProgressionX,
                        DeclaredLattice.OffsetProgressionY,
                        DeclaredLattice.SkewX,        DeclaredLattice.SkewY,
                        CellX, CellY, WithinX, WithinY);

    ProjectWithinCell(CellX, CellY, WithinX, WithinY,
                      DeclaredLattice.ReflectionMask, DeclaredLattice.RotationIncrement,
                      Classified.WithinX, Classified.WithinY);

    Classified.CellX  = CellX;
    Classified.CellY = CellY;

    // 🔴 The variation is a **function of the cell ordinal** and of nothing else. That is what makes it survive a
    //    reopen, agree between a coarse level and the finer one that replaces it, and agree between `82`'s host
    //    preview and `70`'s device resolution — `54` §1.
    if (DeclaredVariation.Declared == VariationSubject::Permuted)
    {
        const std::uint32_t Folded = FoldedCellIndex(CellX, CellY);

        Classified.VariationIndex =
            ProjectPermutedIndex(Folded, DeclaredVariation.PatternSeed) % DeclaredVariation.DeclaredSpan;

        const double Fraction = DeclaredVariation.DeclaredSpan <= 1u
                              ? 0.0
                              : static_cast<double>(Classified.VariationIndex)
                              / static_cast<double>(DeclaredVariation.DeclaredSpan - 1u);

        Classified.VariationScale = DeclaredVariation.LowerScale
                                  + (DeclaredVariation.UpperScale - DeclaredVariation.LowerScale) * Fraction;
    }
    else if (DeclaredVariation.Declared == VariationSubject::Progressive)
    {
        // 📝 A progression is indexed by position rather than permuted, so a gradient across a bolt of cloth is
        //    expressible without a permutation pretending to be one.
        const double Fraction = ProjectVariation(FoldedCellIndex(CellX, 0), DeclaredVariation.PatternSeed);

        Classified.VariationIndex = static_cast<std::uint32_t>(CellX + CellY);
        Classified.VariationScale   = DeclaredVariation.LowerScale
                                    + (DeclaredVariation.UpperScale - DeclaredVariation.LowerScale) * Fraction;
    }

    return Deliver<ClassifiedCell>::Result(Classified);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const LatticeSpecification&     TilingSpecification::Lattice() const   { return DeclaredLattice;   }
const std::vector<CellContent>& TilingSpecification::Content() const   { return DeclaredContent;   }
const VariationSpecification&   TilingSpecification::Variation() const { return DeclaredVariation; }
std::uint32_t                   TilingSpecification::NestingDepth() const { return Depth;          }
bool                            TilingSpecification::LatticeDeclared() const { return LatticeHeld; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TILINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> TilingIndex::Declare()
{
    if (Declared.size() >= TilingLimit)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "the tiling ceiling was reached" });

    const std::uint32_t TilingIndex = static_cast<std::uint32_t>(Declared.size());

    Declared.push_back(TilingSpecification{});

    return Deliver<std::uint32_t>::Result(TilingIndex);
}

Deliver<const TilingSpecification*> TilingIndex::Resolve(std::uint32_t TilingIndex) const
{
    if (TilingIndex >= Declared.size())
        return Deliver<const TilingSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such tiling" });

    return Deliver<const TilingSpecification*>::Result(&Declared[TilingIndex]);
}

Deliver<TilingSpecification*> TilingIndex::Amend(std::uint32_t TilingIndex)
{
    if (TilingIndex >= Declared.size())
        return Deliver<TilingSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such tiling" });

    return Deliver<TilingSpecification*>::Result(&Declared[TilingIndex]);
}

Deliver<bool> TilingIndex::Nest(std::uint32_t EnclosingIndex, std::uint32_t NestedIndex)
{
    if (EnclosingIndex >= Declared.size() || NestedIndex >= Declared.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such tiling" });

    if (EnclosingIndex == NestedIndex)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a tiling cannot nest itself" });

    const std::uint32_t Incoming = Declared[EnclosingIndex].NestingDepth() + 1u;

    if (Incoming > TilingNestingLimit)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "nesting is bounded at one level — `54` §3" });
    }

    // 📝 A tiling already carrying a nested element cannot itself become nested, because that would place a
    //    nested element two levels down without either declaration having said so.
    for (const CellContent& Held : Declared[NestedIndex].Content())
    {
        if (Held.Source == CellContentSource::NestedTiling)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the nested tiling already carries a nested element" });
        }
    }

    Declared[NestedIndex].DeclareNestingDepth(Incoming);

    return Deliver<bool>::Result(true);
}

std::uint32_t TilingIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Declared.size());
}

}   // namespace Slate
