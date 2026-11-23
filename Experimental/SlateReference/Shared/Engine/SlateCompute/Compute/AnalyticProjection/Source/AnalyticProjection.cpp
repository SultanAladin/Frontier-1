//============================================================================================================================================
//                                                         ANALYTICPROJECTION.CPP
//============================================================================================================================================
// 🧩 The four sources resolved at a domain position, the sequence composed over them, and the tile walk that flattens once.

#include "SlateCompute/Compute/AnalyticProjection/Api/AnalyticProjection.h"

#include <cmath>
#include "Shared/PlanarClassifier.slang.h"
#include "Shared/SampleProjection.slang.h"


namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   SAMPLE ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One resolved sample composed into the accumulating one, through `22` §3's specifications. `56` §2 declares
//    that a layer entry reads against what precedes it by exactly these, and `54` §3 declares the same for cell
//    content — one arithmetic, three consumers, which is why `Combination` sits in `Foundation/`.
void AccumulateSample(ResolvedSample&        Running,
                      const ResolvedSample&  Incoming,
                      CombineSpecification   Combination,
                      std::uint32_t          ComponentCount)
{
    if (!Incoming.SampleResolved || Incoming.Coverage <= 0.0)
        return;

    for (std::uint32_t Component = 0u; Component < ComponentCount; ++Component)
    {
        Running.Component[Component] = CombineValue(Combination,
                                                    Running.Component[Component],
                                                    Incoming.Component[Component],
                                                    Incoming.Coverage);
    }

    Running.Coverage       = CombineCoverage(Combination, Running.Coverage, Incoming.Coverage);
    Running.ComponentCount = ComponentCount;
    Running.SampleResolved = true;
}

// 📝 A declared colour spread across the components the caller asked for. Three components take the colour's own
//    coordinates; one takes the red coordinate, which is what a scalar channel authored as a colour means.
void AcceptColour(ResolvedSample& Writing, const ColourSpecification& Declared, std::uint32_t ComponentCount)
{
    if (ComponentCount >= 3u)
    {
        Writing.Component[0] = Declared.RedCoordinate;
        Writing.Component[1] = Declared.GreenCoordinate;
        Writing.Component[2] = Declared.BlueCoordinate;
    }
    else
    {
        for (std::uint32_t Component = 0u; Component < ComponentCount; ++Component)
            Writing.Component[Component] = Declared.RedCoordinate;
    }

    Writing.ComponentCount = ComponentCount;
    Writing.SampleResolved = true;
}

// 📐 Bilinear over a textured entry's interleaved run, clamped at the edges. Nearest would preserve every texel
//    exactly and would also shear every diagonal the artist textured, which reads as a defect rather than as a
//    resampling — `56` §3.1 chooses bilinear for the same reason and this is the same choice one layer down.
double SampleTextured(const TexturedContent& Held,
                     double                PositionX,
                     double                PositionY,
                     std::uint32_t         Component)
{
    if (Held.ExtentTexels == 0u || Held.Texels.empty() || Component >= Held.ComponentCount)
        return 0.0;

    const double Extent = static_cast<double>(Held.ExtentTexels);

    double XTexel  = PositionX  * Extent - 0.5;
    double YTexel = PositionY * Extent - 0.5;

    XTexel  = XTexel  < 0.0 ? 0.0 : (XTexel  > Extent - 1.0 ? Extent - 1.0 : XTexel);
    YTexel = YTexel < 0.0 ? 0.0 : (YTexel > Extent - 1.0 ? Extent - 1.0 : YTexel);

    const std::uint32_t MinimumX  = static_cast<std::uint32_t>(XTexel);
    const std::uint32_t MinimumY = static_cast<std::uint32_t>(YTexel);

    const std::uint32_t NextX  = MinimumX  + 1u < Held.ExtentTexels ? MinimumX  + 1u : MinimumX;
    const std::uint32_t NextY = MinimumY + 1u < Held.ExtentTexels ? MinimumY + 1u : MinimumY;

    const double FractionX  = XTexel  - static_cast<double>(MinimumX);
    const double FractionY = YTexel - static_cast<double>(MinimumY);

    const std::size_t Stride = static_cast<std::size_t>(Held.ComponentCount);

    const std::size_t LowerLeft  = (static_cast<std::size_t>(MinimumY) * Held.ExtentTexels + MinimumX) * Stride;
    const std::size_t LowerRight = (static_cast<std::size_t>(MinimumY) * Held.ExtentTexels + NextX)  * Stride;
    const std::size_t UpperLeft  = (static_cast<std::size_t>(NextY)  * Held.ExtentTexels + MinimumX) * Stride;
    const std::size_t UpperRight = (static_cast<std::size_t>(NextY)  * Held.ExtentTexels + NextX)  * Stride;

    const double Lower = static_cast<double>(Held.Texels[LowerLeft  + Component]) * (1.0 - FractionX)
                       + static_cast<double>(Held.Texels[LowerRight + Component]) * FractionX;

    const double Upper = static_cast<double>(Held.Texels[UpperLeft  + Component]) * (1.0 - FractionX)
                       + static_cast<double>(Held.Texels[UpperRight + Component]) * FractionX;

    return Lower * (1.0 - FractionY) + Upper * FractionY;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AnalyticProjection::ConstructAnalyticProjection(const AnalyticSources& Supplied_)
{
    Supplied = Supplied_;

    // 📝 Nothing is rejected here. A document holding no tiling resolves every one of its layers without one, and
    //    refusing at construction would refuse a document for content it does not contain. `SourcesPresent`
    //    below is what a promotion asks, against the sequence it is about to resolve.
    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE FLATTENING CACHE
//------------------------------------------------------------------------------------------------------------------------

const std::vector<std::vector<PlanarPosition>>* AnalyticProjection::Flattening(std::uint32_t  SourceIndex,
                                                                               double         Tolerance) const
{
    if (Supplied.Outlines == nullptr || SourceIndex >= Supplied.Outlines->size())
        return nullptr;

    for (const FlattenedOutline& Held : Flattenings)
    {
        // 📝 Compared for **equality** rather than within a tolerance of its own. The tolerance arrives from
        //    `ToleranceAtLevel`, which is one reciprocal of an integer product, so two asks at one level produce
        //    the identical double and a tolerance comparison would only admit a level it should have rejected.
        if (Held.SourceIndex == SourceIndex && Held.Tolerance == Tolerance)
            return &Held.Flattened;
    }

    FlattenedOutline Incoming;

    Incoming.SourceIndex = SourceIndex;
    Incoming.Tolerance     = Tolerance;
    Incoming.Flattened     = (*Supplied.Outlines)[SourceIndex].Flatten(Tolerance);

    if (Incoming.Flattened.empty())
        return nullptr;

    Flattenings.push_back(std::move(Incoming));

    return &Flattenings.back().Flattened;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FOUR SOURCES
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolveOutlineAt(std::uint32_t  SourceIndex,
                                                             double         SourceX,
                                                             double         SourceY,
                                                             double         Tolerance) const
{
    if (Supplied.Outlines == nullptr || SourceIndex >= Supplied.Outlines->size())
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "no outline stands at that source ordinal" });
    }

    const VectorInterchange& Declared = (*Supplied.Outlines)[SourceIndex];

    // 🔴 Read through the cache, which flattens on first ask and holds the run for the rest of the walk.
    //    Flattening here directly would flatten eighteen thousand times per tile for one unchanging polyline,
    //    which is the difference between a promotion that fits its budget and one that never does.
    const std::vector<std::vector<PlanarPosition>>* Flattened = Flattening(SourceIndex, Tolerance);

    if (Flattened == nullptr)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "the outline flattened to no path" });
    }

    ResolvedSample Resolved;

    // 🔴 A boundary classifies as boundary and is accepted as covered. `52` §4 resolves a boundary to zero
    //    rather than to inside so that the classification itself carries no bias; accepting it here is the
    //    outward rounding `38` §6 applies everywhere else, and refusing it would give every outline a one-texel
    //    gap along its own edge.
    if (Declared.Classify(*Flattened, SourceX, SourceY) < 0)
        return Deliver<ResolvedSample>::Result(Resolved);

    if (Declared.Declared().ColourDeclared)
        AcceptColour(Resolved, Declared.Declared().DeclaredColour, ResolvedComponentLimit);

    Resolved.Coverage       = 1.0;
    Resolved.SampleResolved = true;

    return Deliver<ResolvedSample>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 BARE OUTLINE RUNS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 🔴 A second classification beside `VectorInterchange::Classify`, and it exists because a glyph and a tiling
//    cell element both carry `OutlinePath` runs and neither carries a `VectorInterchange` to ask. It mirrors `52`
//    §4's combination rule exactly — each path under its own declared fill rule, combined by the outermost
//    containment found, a boundary anywhere winning outright — because the two answering differently would give
//    a placed glyph a different silhouette from the same outline placed as vector content.
// ⚠️ 🚧 The mirror is held by review rather than by assertion. `52`'s form is a member over a specification and
//    this one is a free routine over a run, so neither is expressible as the other's declaration; `00` §11's
//    parity gate covers `Shared/` and reaches neither.
std::int32_t ClassifyOutlinePaths(const std::vector<OutlinePath>&  Paths,
                                  double                          Tolerance,
                                  double                          PointX,
                                  double                          PointY)
{
    std::int32_t Resolved = -1;

    for (const OutlinePath& Path : Paths)
    {
        std::vector<PlanarPosition> Traversed = Flatten(Path.Origin, Path.Segments, Tolerance);

        if (Traversed.size() < 3u)
            continue;

        // 📝 Closed for classification only, exactly as `52` §4 closes an open path. A fill rule accumulates over
        //    a closed polyline, and leaving the run open leaks the winding across the gap.
        const PlanarPosition& Last = Traversed.back();

        if (Last.PositionX != Path.Origin.PositionX || Last.PositionY != Path.Origin.PositionY)
            Traversed.push_back(Path.Origin);

        Signed32 WindingCount    = 0;
        Signed32 CrossingCount   = 0;
        Signed32 DividerTouched = 0;

        for (std::size_t Index = 0u; Index + 1u < Traversed.size(); ++Index)
        {
            AccumulateWinding(Traversed[Index].PositionX,      Traversed[Index].PositionY,
                              Traversed[Index + 1u].PositionX, Traversed[Index + 1u].PositionY,
                              PointX, PointY,
                              WindingCount, CrossingCount, DividerTouched);
        }

        const Signed32 Containment = ResolveContainment(WindingCount,
                                                        CrossingCount,
                                                        DividerTouched,
                                                        Path.Rule == FillRule::EvenOdd ? 1 : 0);

        if (Containment == 0)
            return 0;

        if (Containment > 0)
            Resolved = 1;
    }

    return Resolved;
}

// 📐 One position carried into a cell element's own unit square. The inverse of a translate–rotate–scale, taken
//    in that order and in the element's own frame, matching `ProjectIntoSource`'s convention so that a source
//    placed inside a tiling cell and the same source placed as a decal read their position identically.
void ProjectIntoElement(const CellContent& Placed,
                        double WithinX, double WithinY,
                        double& SourceX, double& SourceY)
{
    const double Width  = WithinX  - Placed.PlacedX;
    const double Height = WithinY - Placed.PlacedY;

    const double Radians = Placed.PlacedRotation * Pi / 180.0;
    const double Cosine  = std::cos(Radians);
    const double Sine    = std::sin(Radians);

    const double TurnedX  =  Cosine * Width + Sine   * Height;
    const double TurnedY = -Sine   * Width + Cosine * Height;

    const double Scale = Placed.PlacedScale != 0.0 ? Placed.PlacedScale : 1.0;

    SourceX  = TurnedX  / Scale + 0.5;
    SourceY = TurnedY / Scale + 0.5;
}

// 📝 One integer folded into a running one. `70` §2's comparison is a single equality per tile, so the fold has
//    only to change when any operand changes — it carries no ordering and names nothing.
void FoldRevision(std::uint64_t& Running, std::uint64_t Incoming)
{
    Running ^= Incoming + 0x9E3779B97F4A7C15ull + (Running << 6) + (Running >> 2);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        TEXT
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolveTextAt(std::uint32_t  SourceIndex,
                                                         double         SourceX,
                                                         double         SourceY,
                                                         double         Tolerance) const
{
    if (Supplied.Texts == nullptr || SourceIndex >= Supplied.Texts->size() || Supplied.Typeface == nullptr)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "no text or no typeface stands at that source ordinal" });
    }

    const ResolvedText&        Declared = (*Supplied.Texts)[SourceIndex];
    const TypefaceInterchange& Faced    = *Supplied.Typeface;

    ResolvedSample Resolved;

    if (Declared.GlyphSequence.empty())
        return Deliver<ResolvedSample>::Result(Resolved);

    // 📐 The sequence is walked in typeface units and the position is carried into them once, rather than each
    //    glyph's outline being carried into the unit square. `52` §3's advances and pair adjustments are declared
    //    in those units, so converting them per glyph would apply the same division as many times as there are
    //    glyphs and would accumulate the divisions' rounding along the line — which reads as text whose spacing
    //    drifts toward its end.
    const double UnitsPerEm = Faced.UnitsPerEm() > 0.0 ? Faced.UnitsPerEm() : 1000.0;

    const double PositionXUnits  = SourceX  * UnitsPerEm;
    const double PositionYUnits = SourceY * UnitsPerEm;
    const double ToleranceUnits      = Tolerance    * UnitsPerEm;

    double Advanced = 0.0;

    for (std::size_t Index = 0u; Index < Declared.GlyphSequence.size(); ++Index)
    {
        const std::uint32_t GlyphIdentity = Declared.GlyphSequence[Index];

        // 🔴 A glyph the typeface does not declare refuses rather than advancing past nothing. `52` §3 stores the
        //    glyph sequence precisely so that shaping is not re-run per resolution; a sequence naming a glyph the
        //    typeface no longer carries is a typeface that was replaced, and skipping it silently would reflow
        //    the text the artist already positioned.
        const Deliver<const GlyphSpecification*> Held = Faced.ResolveGlyph(GlyphIdentity);

        if (!Held.Resolved)
            return Deliver<ResolvedSample>::Refuse(Held.Error);

        const GlyphSpecification& Glyph = *Held.Resolve();

        // 📝 The pair adjustment displaces the **later** glyph and is applied before this one is placed, not
        //    after it advances. Applied afterwards it would displace the glyph after this one instead, and the
        //    line would be correct everywhere except its first pair.
        if (Index != 0u)
            Advanced += Faced.Adjustment(Declared.GlyphSequence[Index - 1u], GlyphIdentity);

        const double GlyphX  = PositionXUnits  - (Advanced + Glyph.BearingX);
        const double GlyphY = PositionYUnits -  Glyph.BearingY;

        if (ClassifyOutlinePaths(Glyph.Paths, ToleranceUnits, GlyphX, GlyphY) >= 0)
        {
            // 📝 A boundary is accepted as covered, matching `ResolveOutlineAt`. Refusing it would give every
            //    glyph a one-texel gap along its own edge, which at text extents is most of the glyph's weight.
            Resolved.Coverage       = 1.0;
            Resolved.ComponentCount = ResolvedComponentLimit;
            Resolved.SampleResolved = true;

            return Deliver<ResolvedSample>::Result(Resolved);
        }

        Advanced += Glyph.Advance;
    }

    return Deliver<ResolvedSample>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TILING
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolveTilingAt(std::uint32_t  TilingIndex,
                                                            double         SourceX,
                                                            double         SourceY,
                                                            double         Tolerance,
                                                            std::uint32_t  NestingDepth) const
{
    if (Supplied.Tilings == nullptr)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "no tiling index was supplied" });
    }

    // 🔴 `54` §3's bound, tested on the way **down** rather than at the declaration. `TilingIndex::Nest` already
    //    refuses a second level, so a depth beyond the ceiling here is a declaration that was amended after it
    //    was nested — and recursing on it is an unbounded resolution `20` §2.2's evaluation budget cannot bound.
    if (NestingDepth > TilingNestingLimit)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "the nesting exceeds `54` §3's declared ceiling" });
    }

    const Deliver<const TilingSpecification*> Declared = Supplied.Tilings->Resolve(TilingIndex);

    if (!Declared.Resolved)
        return Deliver<ResolvedSample>::Refuse(Declared.Error);

    const TilingSpecification& Tiled = *Declared.Resolve();

    const Deliver<ClassifiedCell> Classified = Tiled.Classify(SourceX, SourceY);

    if (!Classified.Resolved)
        return Deliver<ResolvedSample>::Refuse(Classified.Error);

    const ClassifiedCell& Cell = Classified.Resolve();

    ResolvedSample Resolved;

    // 📝 The elements are walked in declaration order, which `54` §3 makes the combination order. Nothing here
    //    sorts them: the ordering **is** the declaration, and a resolution that reordered would give one cell a
    //    different result from the identical cell one lattice step away.
    for (const CellContent& Element : Tiled.Content())
    {
        double ElementX  = 0.0;
        double ElementY = 0.0;
        ProjectIntoElement(Element, Cell.WithinX, Cell.WithinY, ElementX, ElementY);

        ResolvedSample Incoming;

        switch (Element.Source)
        {
            case CellContentSource::DeclaredColour:
            {
                // 📝 Constant within its own coverage, and its coverage is the element's unit square. `54` §3
                //    declares no shape for a colour element, so the square is the whole of what it covers.
                if (ElementX < 0.0 || ElementX > 1.0 || ElementY < 0.0 || ElementY > 1.0)
                    break;

                AcceptColour(Incoming, Element.DeclaredColour, ResolvedComponentLimit);

                Incoming.Coverage = 1.0;
                break;
            }

            case CellContentSource::VectorOutline:
            {
                const Deliver<ResolvedSample> Outlined =
                    ResolveOutlineAt(Element.SourceIndex, ElementX, ElementY, Tolerance);

                if (!Outlined.Resolved)
                    return Outlined;

                Incoming = Outlined.Resolve();
                break;
            }

            case CellContentSource::NestedTiling:
            {
                const Deliver<ResolvedSample> Nested = ResolveTilingAt(Element.SourceIndex,
                                                                      ElementX,
                                                                      ElementY,
                                                                      Tolerance,
                                                                      NestingDepth + 1u);

                if (!Nested.Resolved)
                    return Nested;

                Incoming = Nested.Resolve();
                break;
            }

            case CellContentSource::Imagery:
            {
                // ⚠️ 🚧 Rejected for the reason the class note gives: `50` retains a decoded image's original
                //    bytes and performs `36` §3's conversion into the working space nowhere, so sampling here
                //    would return values in a content-native space nothing declared.
                return Deliver<ResolvedSample>::Refuse(
                    { RefusalReason::ContentUnsupported, "imagery is not resolvable; `36` §3's conversion is unbuilt" });
            }

            case CellContentSource::SourceCount:
                break;
        }

        // 🔴 `54` §1's variation scales the **coverage** and never the value. Scaling the value would make a
        //    varied cell a different colour rather than a lighter application of the same one, and an artist
        //    declaring a variation interval on a weave would watch its threads change hue rather than weight.
        Incoming.Coverage *= Cell.VariationScale;

        AccumulateSample(Resolved, Incoming, Element.Combination, ResolvedComponentLimit);
    }

    return Deliver<ResolvedSample>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLACED CONTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolvePlacedAt(std::uint32_t  PlacementIndex,
                                                            double         PositionX,
                                                            double         PositionY,
                                                            double         Tolerance) const
{
    if (Supplied.Placements == nullptr)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "no placement index was supplied" });
    }

    const Deliver<const PlacementSpecification*> Declared = Supplied.Placements->Resolve(PlacementIndex);

    if (!Declared.Resolved)
        return Deliver<ResolvedSample>::Refuse(Declared.Error);

    const PlacementSpecification& Placed = *Declared.Resolve();

    double SourceX  = 0.0;
    double SourceY = 0.0;

    // 🔴 `72` §3's whole row, and the reason this component needs no per-source placement path. A placement is a
    //    transform into a source space; the source is then one of the three below, resolved exactly as it would
    //    be resolved were it applied as a layer.
    if (!ProjectIntoSource(Placed, PositionX, PositionY, SourceX, SourceY))
        return Deliver<ResolvedSample>::Result(ResolvedSample{});

    switch (Placed.Source)
    {
        case PlacedSource::VectorOutline:
            return ResolveOutlineAt(Placed.SourceIndex, SourceX, SourceY, Tolerance);

        case PlacedSource::Text:
            return ResolveTextAt(Placed.SourceIndex, SourceX, SourceY, Tolerance);

        case PlacedSource::Tiling:
            return ResolveTilingAt(Placed.SourceIndex, SourceX, SourceY, Tolerance, 0u);

        case PlacedSource::Imagery:
            return Deliver<ResolvedSample>::Refuse(
                { RefusalReason::ContentUnsupported, "imagery is not resolvable; `36` §3's conversion is unbuilt" });

        case PlacedSource::SourceCount:
            break;
    }

    return Deliver<ResolvedSample>::Refuse({ RefusalReason::ContentUnsupported, "no such placed source" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE ENTRY
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolveEntryAt(const LayerSpecification&  Held,
                                                           double                     PositionX,
                                                           double                     PositionY,
                                                           double                     Tolerance,
                                                           std::uint32_t              ComponentCount) const
{
    ResolvedSample Resolved;

    switch (Held.Source)
    {
        case LayerContentSource::TexturedImpressions:
        {
            // 🔴 Textured entries are resolved here, in the same walk, per the class note. `20` §2.1 lists three
            //    reconstruction sources and a mixed sequence needs all three composed; a resolver that skipped
            //    textured entries would hand `20` a partial tile it has no route to complete.
            if (Held.Textured.ExtentTexels == 0u || Held.Textured.Texels.empty())
                return Deliver<ResolvedSample>::Result(Resolved);

            for (std::uint32_t Component = 0u; Component < ComponentCount; ++Component)
            {
                Resolved.Component[Component] =
                    SampleTextured(Held.Textured, PositionX, PositionY, Component);
            }

            Resolved.Coverage       = 1.0;
            Resolved.ComponentCount = ComponentCount;
            Resolved.SampleResolved = true;

            break;
        }

        case LayerContentSource::PlacedContent:
        {
            const Deliver<ResolvedSample> Placed =
                ResolvePlacedAt(Held.SourceIndex, PositionX, PositionY, Tolerance);

            if (!Placed.Resolved)
                return Placed;

            Resolved = Placed.Resolve();
            break;
        }

        case LayerContentSource::Tiling:
        {
            const Deliver<ResolvedSample> Tiled =
                ResolveTilingAt(Held.SourceIndex, PositionX, PositionY, Tolerance, 0u);

            if (!Tiled.Resolved)
                return Tiled;

            Resolved = Tiled.Resolve();
            break;
        }

        case LayerContentSource::AnalyticResolution:
        {
            const Deliver<ResolvedSample> Outlined =
                ResolveOutlineAt(Held.SourceIndex, PositionX, PositionY, Tolerance);

            if (!Outlined.Resolved)
                return Outlined;

            Resolved = Outlined.Resolve();
            break;
        }

        case LayerContentSource::NestedSequence:
        {
            // 🔴 A nested entry is resolved by `ResolveAt`, which holds the sequence this one only names. `56`
            //    §4.1 combines the nested content internally first and applies the enclosing entry's combination
            //    and coverage **once** to the result, and only the routine holding both sequences can do that.
            return Deliver<ResolvedSample>::Refuse(
                { RefusalReason::HostDenied, "a nested entry is resolved by the sequence walk, not per entry" });
        }

        case LayerContentSource::SourceCount:
            return Deliver<ResolvedSample>::Refuse({ RefusalReason::ContentUnsupported, "no such content source" });
    }

    if (!Held.Coverage.CoverageDeclared || !Resolved.SampleResolved)
        return Deliver<ResolvedSample>::Result(Resolved);

    // 🔴 Coverage restricts where the entry applies and is resolved through the **same four sources** its content
    //    is — `56` §5. A coverage that could only be textured would make a tiled mask impossible, and an artist
    //    masking a pattern with a pattern is the ordinary case rather than the exotic one.
    double Covered = Held.Coverage.UniformStrength;

    switch (Held.Coverage.Source)
    {
        case LayerContentSource::TexturedImpressions:
        {
            Covered *= Held.Coverage.Textured.ExtentTexels == 0u || Held.Coverage.Textured.Texels.empty()
                     ? 1.0
                     : SampleTextured(Held.Coverage.Textured, PositionX, PositionY, 0u);
            break;
        }

        case LayerContentSource::PlacedContent:
        {
            const Deliver<ResolvedSample> Masked =
                ResolvePlacedAt(Held.Coverage.SourceIndex, PositionX, PositionY, Tolerance);

            if (!Masked.Resolved)
                return Masked;

            Covered *= Masked.Resolve().Coverage;
            break;
        }

        case LayerContentSource::Tiling:
        {
            const Deliver<ResolvedSample> Masked =
                ResolveTilingAt(Held.Coverage.SourceIndex, PositionX, PositionY, Tolerance, 0u);

            if (!Masked.Resolved)
                return Masked;

            Covered *= Masked.Resolve().Coverage;
            break;
        }

        case LayerContentSource::AnalyticResolution:
        {
            const Deliver<ResolvedSample> Masked =
                ResolveOutlineAt(Held.Coverage.SourceIndex, PositionX, PositionY, Tolerance);

            if (!Masked.Resolved)
                return Masked;

            Covered *= Masked.Resolve().Coverage;
            break;
        }

        case LayerContentSource::NestedSequence:
        case LayerContentSource::SourceCount:
        {
            // 📝 A nested sequence is not a coverage source. `56` §5's four sources are §3's four, and a nested
            //    sequence is the fifth entry source rather than one of them.
            return Deliver<ResolvedSample>::Refuse(
                { RefusalReason::ContentUnsupported, "a nested sequence is not a coverage source" });
        }
    }

    Resolved.Coverage *= Covered < 0.0 ? 0.0 : (Covered > 1.0 ? 1.0 : Covered);

    return Deliver<ResolvedSample>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CONTENT REVISION
//------------------------------------------------------------------------------------------------------------------------

std::uint64_t AnalyticProjection::ContentRevision(const SurfaceLayerSequence& Content) const
{
    std::uint64_t Folded = 1u;

    FoldRevision(Folded, Content.AddressedRevision());
    FoldRevision(Folded, static_cast<std::uint64_t>(Content.EntryCount()));

    for (const LayerSpecification& Held : Content.Entries())
    {
        // 📝 Every field `56` exposes that a resolution reads. The entry's generation advances at withdrawal, so
        //    a slot reused by a later entry folds differently from the entry it replaced — `56`'s own reason for
        //    advancing it.
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Identity.SlotIndex));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Identity.SlotGeneration));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Source));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.SourceIndex));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.ChannelMask));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Combination));
        FoldRevision(Folded, Held.PresenceEnabled ? 1u : 0u);
        FoldRevision(Folded, Held.Coverage.CoverageDeclared ? 1u : 0u);
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Coverage.Source));
        FoldRevision(Folded, static_cast<std::uint64_t>(Held.Coverage.SourceIndex));

        if (Held.Source == LayerContentSource::PlacedContent && Supplied.Placements != nullptr)
            FoldRevision(Folded, Supplied.Placements->Revision(Held.SourceIndex));
    }

    // 📝 The nested sequences are folded in whole, so an amendment inside one advances the enclosing surface's
    //    revision. `56` §4.1 reads a nested sequence as one entry and this reads it as one operand.
    for (std::uint32_t NestedIndex = 0u; NestedIndex < Content.NestedCount(); ++NestedIndex)
    {
        const Deliver<const SurfaceLayerSequence*> Nesting = Content.Nested(NestedIndex);

        if (Nesting.Resolved)
            FoldRevision(Folded, ContentRevision(*Nesting.Resolve()));
    }

    // ⚠️ 🚧 A textured amendment is **not** observable here. `56` exposes a textured entry's texels for amendment
    //    and advances no counter over them, so a stroke sealed into an entry folds to the number it already had
    //    and `SurfaceTileSpace::Promote` answers `AlreadyResident`. `22`'s Seal must therefore evict or re-resolve
    //    the cells it recorded directly, and `56` §10's open row is what closes this properly.
    return Folded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE POSITION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedSample> AnalyticProjection::ResolveAt(const SurfaceLayerSequence&           Content,
                                                      const std::vector<ChannelPlacement>&  Placements,
                                                      double                                PositionX,
                                                      double                                PositionY,
                                                      double                                Tolerance,
                                                      std::uint32_t                         ComponentCount) const
{
    if (ComponentCount == 0u || ComponentCount > ResolvedComponentLimit)
    {
        return Deliver<ResolvedSample>::Refuse(
            { RefusalReason::ContentUnsupported, "the component count is zero or above the declared ceiling" });
    }

    ResolvedSample Resolved;

    for (const LayerSpecification& Held : Content.Entries())
    {
        if (!Held.PresenceEnabled)
            continue;

        ResolvedSample Incoming;

        if (Held.Source == LayerContentSource::NestedSequence)
        {
            const Deliver<const SurfaceLayerSequence*> Nesting = Content.Nested(Held.NestedIndex);

            if (!Nesting.Resolved)
                return Deliver<ResolvedSample>::Refuse(Nesting.Error);

            // 🔴 `56` §4.1: the nested content combines **internally first**, and this entry's combination and
            //    coverage are each applied once to the nested result. Applying the enclosing coverage per nested
            //    entry instead darkens a partly covered nested sequence at its own internal overlaps, which is
            //    `22` §3's defect at a different scale.
            const Deliver<ResolvedSample> Nested = ResolveAt(*Nesting.Resolve(),
                                                             Placements,
                                                             PositionX,
                                                             PositionY,
                                                             Tolerance,
                                                             ComponentCount);

            if (!Nested.Resolved)
                return Nested;

            Incoming = Nested.Resolve();

            if (Held.Coverage.CoverageDeclared)
            {
                LayerSpecification Enclosing = Held;
                Enclosing.Source             = LayerContentSource::TexturedImpressions;
                Enclosing.Textured            = TexturedContent{};

                // 📝 The enclosing coverage is resolved through the entry path with the nested content's own
                //    source stood down, so one routine resolves coverage for every entry rather than two.
                const Deliver<ResolvedSample> Masked =
                    ResolveEntryAt(Enclosing, PositionX, PositionY, Tolerance, ComponentCount);

                if (!Masked.Resolved)
                    return Masked;

                Incoming.Coverage *= Masked.Resolve().Coverage;
            }
        }
        else
        {
            const Deliver<ResolvedSample> Entry =
                ResolveEntryAt(Held, PositionX, PositionY, Tolerance, ComponentCount);

            if (!Entry.Resolved)
                return Entry;

            Incoming = Entry.Resolve();
        }

        // 📝 🚧 `Placements` is accepted and not yet read. `00` §12 carries the channel packing layout as open,
        //    so nothing declares which components a channel occupies within a resolved texel; a resolution that
        //    scattered by the supplied run would be answering that question here, in the one place nobody would
        //    look for the answer. The run is carried so that the signature does not change when it is answered.
        static_cast<void>(Placements);

        AccumulateSample(Resolved, Incoming, Held.Combination, ComponentCount);
    }

    return Deliver<ResolvedSample>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE TILE
//------------------------------------------------------------------------------------------------------------------------

Deliver<ResolvedTile> AnalyticProjection::ResolveTile(const SurfaceLayerSequence&           Content,
                                                      const std::vector<ChannelPlacement>&  Placements,
                                                      CellAddress                           Addressed,
                                                      std::uint32_t                         ComponentCount) const
{
    if (Addressed.Level >= ReductionLevelCount)
        return Deliver<ResolvedTile>::Refuse({ RefusalReason::ContentUnsupported, "no such reduction level" });

    if (ComponentCount == 0u || ComponentCount > ResolvedComponentLimit)
    {
        return Deliver<ResolvedTile>::Refuse(
            { RefusalReason::ContentUnsupported, "the component count is zero or above the declared ceiling" });
    }

    const std::uint32_t CellsPerEdge = CellsPerEdgeAt(Addressed.Level);

    if (Addressed.X >= CellsPerEdge || Addressed.Y >= CellsPerEdge)
        return Deliver<ResolvedTile>::Refuse({ RefusalReason::ContentUnsupported, "no such cell at that level" });

    const std::uint32_t WorkingExtent = CellsPerEdge * PhysicalTileTexels;
    const double        Tolerance     = ToleranceAtLevel(Addressed.Level);

    // 🔴 Flattened once per distinct outline **before** the walk, at this level's tolerance. `Flattening` holds
    //    the runs for the duration of this call and releases them with it; flattening inside the walk would
    //    flatten `StoredTexelsPerEdge` squared times per tile for a result that does not vary across it.
    Flattenings.clear();

    ResolvedTile Produced;
    Produced.ComponentCount   = ComponentCount;
    Produced.ResolvedRevision = ContentRevision(Content);

    const std::size_t TexelSpan = static_cast<std::size_t>(StoredTexelsPerEdge)
                                * static_cast<std::size_t>(StoredTexelsPerEdge)
                                * static_cast<std::size_t>(ComponentCount);

    Produced.Texels.assign(TexelSpan, 0.0f);

    // 📐 The apron is walked with the interior, per `20` §1. Texel (i, j) of the stored extent sits at working
    //    texel (X·PhysicalTileTexels + i − PhysicalTileApron, …), so the first and last four rows and columns
    //    address the neighbouring cells' border — which is exactly the duplication the apron is.
    const std::int64_t OriginX  = static_cast<std::int64_t>(Addressed.X)  * PhysicalTileTexels
                                    - static_cast<std::int64_t>(PhysicalTileApron);
    const std::int64_t OriginY = static_cast<std::int64_t>(Addressed.Y) * PhysicalTileTexels
                                    - static_cast<std::int64_t>(PhysicalTileApron);

    const double Edge = static_cast<double>(WorkingExtent);

    for (std::uint32_t Y = 0u; Y < StoredTexelsPerEdge; ++Y)
    {
        for (std::uint32_t X = 0u; X < StoredTexelsPerEdge; ++X)
        {
            const double PositionX  = (static_cast<double>(OriginX  + X)  + 0.5) / Edge;
            const double PositionY = (static_cast<double>(OriginY + Y) + 0.5) / Edge;

            const Deliver<ResolvedSample> Sampled =
                ResolveAt(Content, Placements, PositionX, PositionY, Tolerance, ComponentCount);

            if (!Sampled.Resolved)
            {
                // 📝 Rejected whole rather than left part-written. `20`'s promotion charges its budget against a
                //    tile it is about to write; a tile written to the first refusing texel is one the residency
                //    records as resolved and every later sample reads as content.
                Flattenings.clear();

                return Deliver<ResolvedTile>::Refuse(Sampled.Error);
            }

            const ResolvedSample& Resolved = Sampled.Resolve();

            const std::size_t Writing = (static_cast<std::size_t>(Y) * StoredTexelsPerEdge + X)
                                      * static_cast<std::size_t>(ComponentCount);

            for (std::uint32_t Component = 0u; Component < ComponentCount; ++Component)
                Produced.Texels[Writing + Component] = static_cast<float>(Resolved.Component[Component]);
        }
    }

    // 🔴 Charged to `20` §2.2's **second** measure and mirroring `Estimate` entry for entry — one unit per
    //    presented analytic entry, a nested sequence counted once. A charge derived differently here would make
    //    the budget bound one number and the resolution spend another.
    for (const LayerSpecification& Held : Content.Entries())
    {
        if (!Held.PresenceEnabled || Held.Source == LayerContentSource::TexturedImpressions)
            continue;

        Produced.EvaluationUnits += EvaluationUnitsPerEntry;
    }

    Flattenings.clear();

    return Deliver<ResolvedTile>::Result(Produced);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS PRESENT
//------------------------------------------------------------------------------------------------------------------------

bool AnalyticProjection::SourcesPresent(const SurfaceLayerSequence& Content) const
{
    for (const LayerSpecification& Held : Content.Entries())
    {
        if (!Held.PresenceEnabled)
            continue;

        switch (Held.Source)
        {
            case LayerContentSource::AnalyticResolution:
            {
                if (Supplied.Outlines == nullptr || Held.SourceIndex >= Supplied.Outlines->size())
                    return false;

                break;
            }

            case LayerContentSource::Tiling:
            {
                if (Supplied.Tilings == nullptr || !Supplied.Tilings->Resolve(Held.SourceIndex).Resolved)
                    return false;

                break;
            }

            case LayerContentSource::PlacedContent:
            {
                if (Supplied.Placements == nullptr)
                    return false;

                const Deliver<const PlacementSpecification*> Placed =
                    Supplied.Placements->Resolve(Held.SourceIndex);

                if (!Placed.Resolved)
                    return false;

                // 📝 The placement's own source is confirmed too, because a placement resolving through an absent
                //    library refuses at the texel rather than at the promotion — which charges the budget for a
                //    walk whose first sample refuses.
                switch (Placed.Resolve()->Source)
                {
                    case PlacedSource::VectorOutline:
                    {
                        if (Supplied.Outlines == nullptr
                         || Placed.Resolve()->SourceIndex >= Supplied.Outlines->size())
                        {
                            return false;
                        }

                        break;
                    }

                    case PlacedSource::Text:
                    {
                        if (Supplied.Texts == nullptr
                         || Supplied.Typeface == nullptr
                         || Placed.Resolve()->SourceIndex >= Supplied.Texts->size())
                        {
                            return false;
                        }

                        break;
                    }

                    case PlacedSource::Tiling:
                    {
                        if (Supplied.Tilings == nullptr
                         || !Supplied.Tilings->Resolve(Placed.Resolve()->SourceIndex).Resolved)
                        {
                            return false;
                        }

                        break;
                    }

                    case PlacedSource::Imagery:
                    case PlacedSource::SourceCount:
                        return false;
                }

                break;
            }

            case LayerContentSource::NestedSequence:
            {
                const Deliver<const SurfaceLayerSequence*> Nesting = Content.Nested(Held.NestedIndex);

                if (!Nesting.Resolved || !SourcesPresent(*Nesting.Resolve()))
                    return false;

                break;
            }

            case LayerContentSource::TexturedImpressions:
            case LayerContentSource::SourceCount:
                break;
        }
    }

    return true;
}

}   // namespace Slate
