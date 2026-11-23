//============================================================================================================================================
//                                                         EMISSIONSEQUENCE.CPP
//============================================================================================================================================
// 🧩 `50` §5 — the band walk that resolves an emitted image out of the domain, one texel centre at a time.

#include "SlateCompute/Compute/EmissionSequence/Api/EmissionSequence.h"

namespace Slate
{

namespace
{

constexpr std::size_t ComponentSpan = static_cast<std::size_t>(ComponentSlot::ComponentCount);

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

std::vector<ChannelPlacement> ProjectPlacements(const EmittedImage& Arranged)
{
    std::vector<ChannelPlacement> Derived;
    Derived.reserve(ComponentSpan);

    for (std::size_t Slot = 0u; Slot < ComponentSpan; ++Slot)
    {
        if (!Arranged.ComponentOccupied[Slot])
            continue;

        // 📝 One component per placement, and the ordinal **is** the component the arrangement declared. The
        //    slots are walked in ascending order, so the run comes out ascending — which is what lets the band
        //    walk scatter `70`'s dense resolution into the declared slots by position alone.
        ChannelPlacement Placing;
        Placing.Channel          = Arranged.Occupying[Slot];
        Placing.ComponentIndex = static_cast<std::uint32_t>(Slot);
        Placing.ComponentSpan    = 1u;

        Derived.push_back(Placing);
    }

    return Derived;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IT READS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> EmissionSequence::ConstructEmissionSequence(const EmissionSources& Supplied)
{
    // 🔴 The same `70` a promotion reads and `82` previews through. An absent resolver is not an export that
    //    degrades to something simpler — it is an export that would have to invent a second implementation, and
    //    the asset it shipped would disagree with what the artist was shown while texturing it.
    if (Supplied.Resolution == nullptr)
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "an emission has no resolver; `70` is the only path `50` §5 permits"});
    }

    Resolution      = Supplied.Resolution;
    SourcesDeclared = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     OPENING ONE IMAGE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> EmissionSequence::Open(const EmissionSpecification& Declaring,
                                     const MaterialIndex&         Materials,
                                     std::uint32_t                ImageIndex)
{
    if (!SourcesDeclared)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied,
                                      "no resolver was declared; Construct before opening an emission"});
    }

    if (EmissionOpen)
    {
        return Deliver<bool>::Refuse({RefusalReason::HostDenied,
                                      "an emission already stands; Seal or Reclaim it before opening another"});
    }

    // 🔴 Validated here and not assumed validated. The specification arrived by value and however long the
    //    artist spent between declaring the export and starting it sits between the two calls; trusting the
    //    earlier validation is trusting a copy, and `50` §5.1's wrong arrangement is what that copy carries.
    const Deliver<bool> Validated = Declaring.Validate(Materials);
    if (!Validated.Resolved)
    {
        return Validated;
    }

    if (ImageIndex >= Declaring.Images.size())
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "no such image in the emission specification"});
    }

    const EmittedImage& Arranged = Declaring.Images[ImageIndex];

    if (Arranged.ExtentTexels > EmissionExtentLimit)
    {
        return Deliver<bool>::Refuse({RefusalReason::ContentUnsupported,
                                      "the declared extent exceeds what one emission may produce"});
    }

    Arrangement = ProjectPlacements(Arranged);

    // 📝 Allocated once, whole, so no band reallocates mid-emission. A run that grew per band would move every
    //    texel already resolved, and it would do it under a `Background` worker while the artist is texturing.
    Producing.ExtentTexels   = Arranged.ExtentTexels;
    Producing.ComponentCount = static_cast<std::uint32_t>(ComponentSpan);
    Producing.SpaceIdentity  = Arranged.SpaceIdentity;
    Producing.Texels.assign(static_cast<std::size_t>(Arranged.ExtentTexels) *
                            static_cast<std::size_t>(Arranged.ExtentTexels) * ComponentSpan,
                            0.0f);

    RowsNext     = 0u;
    EmissionOpen = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE BAND OF ROWS
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> EmissionSequence::ResolveBand(const SurfaceLayerSequence& Content)
{
    if (!EmissionOpen)
    {
        return Deliver<std::uint32_t>::Refuse({RefusalReason::HostDenied,
                                               "no emission stands; Open before resolving a band"});
    }

    if (RowsNext >= Producing.ExtentTexels)
    {
        return Deliver<std::uint32_t>::Refuse({RefusalReason::ExtentExhausted,
                                               "every row is resolved; Seal the emission"});
    }

    const std::uint32_t Extent     = Producing.ExtentTexels;
    const double        Reciprocal = 1.0 / static_cast<double>(Extent);
    const double        Tolerance  = ToleranceAtExtent(Extent);

    // 📝 `70` is asked for as many components as the arrangement occupies, not for the four an emitted texel
    //    carries. An image occupying one slot is a scalar channel and resolving four components for it costs
    //    four times the walk to produce three values the scatter then discards.
    const std::uint32_t Requesting = static_cast<std::uint32_t>(Arrangement.size());

    std::uint32_t RowsLast = RowsNext + EmissionBandRows;
    if (RowsLast > Extent)
        RowsLast = Extent;

    for (std::uint32_t Row = RowsNext; Row < RowsLast; ++Row)
    {
        // 📝 Texel **centres**, not corners. A corner sample places the first texel exactly on the domain
        //    boundary, where a seam's two sides are equally near and the resolution picks one arbitrarily.
        const double PositionY = (static_cast<double>(Row) + 0.5) * Reciprocal;

        for (std::uint32_t Column = 0u; Column < Extent; ++Column)
        {
            const double PositionX = (static_cast<double>(Column) + 0.5) * Reciprocal;

            const Deliver<ResolvedSample> Resolved = Resolution->ResolveAt(Content,
                                                                           Arrangement,
                                                                           PositionX,
                                                                           PositionY,
                                                                           Tolerance,
                                                                           Requesting);

            // 🔴 A refusal abandons the **whole** emission rather than leaving the band half-written. An image
            //    resolved above a seam and zero below it is an asset the artist ships without noticing; an
            //    export that rejected is one they cannot miss.
            if (!Resolved.Resolved)
            {
                const Refusal Declining = Resolved.Error;
                Reclaim();
                return Deliver<std::uint32_t>::Refuse(Declining);
            }

            const ResolvedSample& Sampled = Resolved.Resolve();

            // 📝 An unresolved position leaves its texel at the zero `Open` wrote. `70` answers
            //    `SampleResolved` false where no source covers the position at all, and that is an
            //    uncovered part of the domain rather than a failure — a chart partition leaves gutters,
            //    and `50` §5 exports the domain rather than only the charted part of it.
            if (!Sampled.SampleResolved)
                continue;

            const std::size_t TexelIndex = (static_cast<std::size_t>(Row) * static_cast<std::size_t>(Extent) +
                                              static_cast<std::size_t>(Column)) * ComponentSpan;

            // 🚧 The scatter. `70` resolves **densely** — its 𝑘th component belongs to the 𝑘th entry of the
            //    arrangement — because `00` §12 leaves the packing layout open and `70` therefore accepts the
            //    run without reading it. The arrangement is ascending by construction, so the 𝑘th entry names
            //    the slot the 𝑘th component occupies. When `00` §12 is answered and `70` places by the run,
            //    this becomes the identity and is deleted; nothing above it changes.
            for (std::size_t Entry = 0u; Entry < Arrangement.size() && Entry < ResolvedComponentLimit; ++Entry)
            {
                const std::size_t Slot = static_cast<std::size_t>(Arrangement[Entry].ComponentIndex);

                if (Slot < ComponentSpan)
                    Producing.Texels[TexelIndex + Slot] = static_cast<float>(Sampled.Component[Entry]);
            }
        }
    }

    const std::uint32_t Walked = RowsLast - RowsNext;
    RowsNext = RowsLast;

    return Deliver<std::uint32_t>::Result(Walked);
}

bool EmissionSequence::ResolutionOwed() const
{
    return EmissionOpen && RowsNext < Producing.ExtentTexels;
}

std::uint32_t EmissionSequence::ResolvedRows() const
{
    return RowsNext;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HANDING IT OVER
//------------------------------------------------------------------------------------------------------------------------

Deliver<EmittedTexels> EmissionSequence::Seal()
{
    if (!EmissionOpen)
    {
        return Deliver<EmittedTexels>::Refuse({RefusalReason::HostDenied,
                                               "no emission stands; Open before sealing one"});
    }

    // 🔴 Refuses while rows remain rather than delivering what stands. A partially resolved image handed to a
    //    codec is a file that opens, looks approximately right, and is wrong along one edge.
    if (RowsNext < Producing.ExtentTexels)
    {
        return Deliver<EmittedTexels>::Refuse({RefusalReason::ExtentExhausted,
                                               "rows remain unresolved; the emission is not complete"});
    }

    const EmittedTexels Sealed = Producing;

    Reclaim();

    return Deliver<EmittedTexels>::Result(Sealed);
}

void EmissionSequence::Reclaim()
{
    // 📝 The run is cleared **and** its capacity released. An emission at sixteen thousand per edge holds a
    //    gigabyte, and holding it against a next export that may never come is holding it for the session.
    Producing.Texels.clear();
    Producing.Texels.shrink_to_fit();

    Producing.ExtentTexels   = 0u;
    Producing.ComponentCount = 0u;
    Producing.SpaceIdentity  = 0u;

    Arrangement.clear();
    RowsNext     = 0u;
    EmissionOpen = false;
}

}   // namespace Slate
