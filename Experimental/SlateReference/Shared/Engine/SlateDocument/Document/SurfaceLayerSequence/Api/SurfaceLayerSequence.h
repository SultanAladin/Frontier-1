//============================================================================================================================================
//                                                         SURFACELAYERSEQUENCE.H
//============================================================================================================================================
// 🧩 The ordered content of one surface — the single source of truth a resident tile is a projection of.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE FOUR CONTENT SOURCES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where an entry's content comes from — `56` §3's four sources.
/// note  🔴 Only the first stores texels. The other three store a **description** that `70` resolves at whatever
///        reduction level was promoted, which is what `20` §2.1's third reconstruction source reads.
/// tag   guarantee
enum class LayerContentSource : std::uint32_t
{
    TexturedImpressions = 0u,   // [-] - the texels are the authored thing — `22`
    PlacedContent      = 1u,   // [-] - a source plus a transform — `72`
    Tiling             = 2u,   // [-] - the pattern declaration — `54`
    AnalyticResolution = 3u,   // [-] - an outline or an analytic source — `70`
    NestedSequence     = 4u,   // [-] - an entry that is itself a sequence — §4.1
    MaterialConstants  = 5u,   // [-] - channel constants held by MaterialSpecification
    SourceCount        = 6u    // [-] - the closed count, never a source
};

/// 🧩 Whether content from one source can be rebuilt from what the document holds.
/// note  🔴 `20` §4 never decides this. It reads the decision made here, because a residency system classifying
///        content itself would be classifying content it can only see as texels.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool SourceReconstructible(LayerContentSource Source)
{
    return Source != LayerContentSource::TexturedImpressions;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAINTED TEXELS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one thing in a document that is stored as texels rather than as a description.
/// note  🔴 `56` §3: textured texels are stored at the surface's **working extent** and are the one content a
///        change of working extent or a domain re-partition resamples. Everything else is re-resolved, and
///        re-resolution is exact where resampling is not.
/// note  💾 Held interleaved, one span per entry, so that a surface with thirty textured layers is thirty
///        allocations rather than thirty times the channel count.
/// tag   owning
struct TexturedContent
{
    std::vector<float>  Texels         = {};   // [-] - interleaved, ComponentCount per texel
    std::uint32_t       ExtentTexels   = 0u;   // [px] - per edge; the surface's working extent
    std::uint32_t       ComponentCount = 1u;   // [-] - components the entry writes per texel
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHERE A CHANNEL LANDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which components of one textured entry a channel occupies.
/// note  🔴 Declared here rather than in `22`, because it describes the layout of a `TexturedContent` and `22` is
///        not the only reader. `70` resolves into the same components and sits below `22` in `00` §9.1's strata,
///        so a declaration held there would be a back edge from stratum 7 to stratum 9.
/// note  🚧 Supplied by the caller rather than derived from `42`. `00` §12 carries the channel packing layout as
///        open, so deriving one here would answer that question in the one place nobody would look for it.
/// note  🔴 A colour channel spans three components and a scalar one spans one. The span is declared rather than
///        inferred from the measure, so a two-component packing of a colour refuses at the caller's validation
///        instead of writing three channels over each other.
/// tag   nonallocating, nonthrowing
struct ChannelPlacement
{
    ChannelSubject  Channel          = ChannelSubject::ChannelCount;   // [-] - which of `42`'s twenty
    std::uint32_t   ComponentIndex = 0u;                             // [-] - first component within the entry
    std::uint32_t   ComponentSpan    = 1u;                             // [-] - one for a scalar, three for a colour
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      COVERAGE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where an entry applies, and how strongly.
/// note  🔴 `56` §5: coverage is **content**, and is revised through `RevisionSequence` like any other content.
///        Texturing coverage is a stroke, undone by `22` §4's extent-bounded inverse, and nothing about it is a
///        separate mechanism.
/// note  📝 It carries the same four sources §3 declares, because it may be textured, placed, tiled or resolved
///        analytically. A coverage that could only be textured would make a tiled mask impossible.
/// tag   owning
struct CoverageSpecification
{
    LayerContentSource  Source          = LayerContentSource::AnalyticResolution;
    std::uint32_t       SourceIndex   = 0u;      // [-] - into `54`, `52` or `50`
    TexturedContent      Textured         = {};      // [-] - read at TexturedImpressions
    double              UniformStrength = 1.0;     // [-] - applied to whatever the source resolves
    std::uint32_t       ChannelMask     = 0u;      // [-] - zero means every channel the layer writes
    bool                Inverted        = false;   // [-] - true maps resolved coverage to one-minus-coverage
    bool                CoverageDeclared = false;  // [-] - false applies the entry everywhere at full strength
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE ENTRY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One entry of a surface's content, declaring all four of `56` §2's fields and nothing implicit.
/// note  🔴 The combination is `22` §3's `CombineSpecification`, unamended and not re-declared. `22` applies it
///        between impressions within a stroke; this applies it between entries. Two documents declaring two sets
///        of behaviours produce a surface whose result changes depending on whether content arrived as a stroke
///        or as a layer, which the artist cannot see and cannot correct.
/// note  ⚠️ An entry declaring no channel subset writes **nothing**. It is accepted rather than rejected, because
///        an artist may legitimately hold a layer that writes nothing while they decide; what is forbidden is
///        defaulting to all twenty, which silently overwrites roughness with a colour layer and is discovered at
///        export.
/// tag   owning
struct LayerSpecification
{
    LayerIdentity         Identity        = {};                            // [-] - `10` §2.1's integer pair
    LayerContentSource    Source          = LayerContentSource::TexturedImpressions;
    std::uint32_t         SourceIndex   = 0u;                            // [-] - into `54`, `72`, `52` or `50`
    std::uint32_t         NestedIndex   = 0u;                            // [-] - read at NestedSequence
    std::uint32_t         ChannelMask     = 0u;                            // [-] - one bit per `42` channel
    CombineSpecification  Combination     = CombineSpecification::Over;    // [-] - `22` §3's, unamended
    CoverageSpecification Coverage        = {};                            // [-]
    TexturedContent        Textured         = {};                            // [-] - read at TexturedImpressions
    std::string           Name            = {};                            // [-] - editable and used by export naming
    bool                  PresenceEnabled = true;                          // [-] - hiding is a recorded amendment
    bool                  Mandatory       = false;                         // [-] - base material cannot be removed
    bool                  ResampleOwed    = false;                         // [-] - a re-partition has moved the domain
};

/// 🧩 Whether an entry writes one of `42`'s channels.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool EntryWritesChannel(const LayerSpecification& Entry, ChannelSubject Channel)
{
    return Channel != ChannelSubject::ChannelCount
        && (Entry.ChannelMask & (1u << static_cast<std::uint32_t>(Channel))) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The ordered content of one surface, per channel, and the only ordering authority over it.
/// note  ⚠️ `Stack` is banned. The ordered content of a surface is a `SurfaceLayerSequence` and a position in it
///        is a sequence position.
/// note  🔴 `56` §4: sequence position is the sole answer to "what is on top". No entry carries a depth, a
///        priority or an ordinal that could disagree with its position, and `12`'s outliner **presents** that
///        order rather than holding a second one — which is why reordering in the outliner and reordering in a
///        layer panel can never disagree.
/// note  🔴 `56` §7: nothing here addresses a tile, a texel population or a device resource. Entries address
///        `68`'s parametric domain, and `20` projects the sequence onto whatever is resident. A sequence holding
///        device extents would make the working extent a property of the file, and changing it an import.
/// tag   owning
class SurfaceLayerSequence
{
public:

    /// 🧩 Appends one entry at the end of the sequence, issuing its identity.
    /// in    Declaring  [-]  every one of §2's four fields
    /// out   Result    [-]  refuses with ContentUnsupported for a nested entry beyond the declared depth, for
    ///                       textured content whose span does not match its declared extent, and with
    ///                       ExtentExhausted at the entry ceiling
    /// post  the entry sits last, which is topmost
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<LayerIdentity> Append(const LayerSpecification& Declaring);

    /// 🧩 Moves one entry to a declared sequence position.
    /// in    Subject   [-]  the entry
    /// in    Position  [-]  where it should sit; clamped to the end
    /// out   Result   [-]  refuses with IdentityStale when the entry no longer resolves, and carries the prior
    ///                      position so the caller's inverse is one ordinal rather than a whole ordering
    /// note  🔴 One transaction against two sequence positions — `56` §6. The inverse is the prior position and
    ///        nothing else, which is what makes reordering affordable in `RevisionSequence`.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Reorder(LayerIdentity Subject, std::uint32_t Position);

    /// 🧩 Presents or hides one entry.
    /// out   Result  [-]  refuses with IdentityStale; carries the prior standing as the inverse
    /// note  ⚠️ A recorded amendment, per `56` §6. An artist who hides a layer, saves, reopens and finds it
    ///        presented has been told the document does not hold what they see.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclarePresence(LayerIdentity Subject, bool PresenceEnabled);

    /// 🧩 Renames one entry. The name is document data and supplies material/export labels.
    Deliver<std::string> DeclareName(LayerIdentity Subject, const std::string& Name);

    /// 🧩 Changes which material channels one entry writes.
    /// out   Result  [-]  previous channel mask, or IdentityStale when the row no longer resolves
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> DeclareChannelMask(LayerIdentity Subject, std::uint32_t ChannelMask);

    /// 🧩 Changes one entry's content source without replacing its identity.
    /// out   Result  [-]  previous source, or IdentityStale when the row no longer resolves
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<LayerContentSource> DeclareSource(LayerIdentity Subject, LayerContentSource Source);

    /// 🧩 Changes the mask/coverage that gates one entry.
    /// out   Result  [-]  previous coverage declaration, or IdentityStale when the row no longer resolves
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<CoverageSpecification> DeclareCoverage(LayerIdentity Subject, const CoverageSpecification& Coverage);

    /// 🧩 Amends one entry's combination.
    /// out   Result  [-]  refuses with IdentityStale; carries the prior combination
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<CombineSpecification> DeclareCombination(LayerIdentity Subject, CombineSpecification Declaring);

    /// 🧩 Removes one entry, retaining its description and never its resolved texels.
    /// out   Result  [-]  refuses with IdentityStale
    /// note  🔴 `56` §6: removing an entry that stores a description retains the **description**. Texels resolved
    ///        from a description live in `SurfaceDepot` under `20` §4 and are reconstructible by definition, so
    ///        recording them in an inverse would store a derivation inside the document to undo a change that
    ///        does not need it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<LayerSpecification> Withdraw(LayerIdentity Subject);

    /// 🧩 Nests one sequence inside this one as a single entry.
    /// out   Result  [-]  the registered ordinal, into this surface's own nested sequences; refuses with
    ///                     ExtentExhausted beyond `LayerNestingLimit`
    /// note  🔴 §4.1: the nested content combines **internally first**, and the enclosing entry's combination and
    ///        coverage are each applied **once**, to the nested result. Applying the enclosing coverage per entry
    ///        instead is the defect that makes a partly covered nested sequence darken at its own internal
    ///        overlaps — the same defect `22` §3 fixes by accumulating a stroke once and applying it once, at a
    ///        different scale.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Nest();

    /// 🧩 Resamples every textured entry into a re-partitioned domain, on the tick.
    /// in    IncomingRevision  [-]  the partition revision `68` advanced to
    /// in    Remapping         [-]  supplied by the caller: a position in the new domain, answered with the
    ///                              position it occupied in the old one
    /// in    Reporting         [-]  where `86` §4's `56` §3.1 row lands
    /// in    Sampled           [ns] the tick's own reading
    /// out   Result           [-]  refuses with HostDenied when no remapping is supplied
    /// note  🔴 `00` §10 conflict 40, discharged from this side. `68` advances the revision and derives the
    ///        unwrap; **this** resamples the authored texels, because `68` is in `SlateCompute` and cannot reach
    ///        the document content at all. The remapping crosses as a callable so that nothing here names `68`.
    /// note  🔴 This is the **one operation in the engine that resamples authored content**, and it is reported
    ///        for exactly that reason. Everything else is re-resolved, and re-resolution is exact where
    ///        resampling is not.
    /// note  ⚠️ Sampling is bilinear, so a re-partition softens texture. That is a property of the operation and
    ///        not a defect in it; what would be a defect is performing it without saying so.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Resample(std::uint64_t                                                       IncomingRevision,
                           const std::function<bool(double, double, double&, double&)>&        Remapping,
                           ReportSequence&                                                     Reporting,
                           TickPoint                                                           Sampled);

    /// 🧩 One entry, by identity.
    /// out   Result  [-]  refuses with IdentityStale
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const LayerSpecification*> Resolve(LayerIdentity Subject) const;

    /// 🧩 One entry's textured texels, for the one mechanism permitted to amend them.
    /// out   Result  [-]  refuses with IdentityStale when the entry no longer resolves, and with
    ///                     ContentUnsupported when its source is not TexturedImpressions
    /// note  🔴 `22` is the only caller. Textured texels are the one content this sequence stores rather than
    ///        describes — §3 — so this is the one route by which authored content is written at all, and every
    ///        write through it is inside a transaction whose inverse is bounded by the extents it touched.
    /// note  ⚠️ A reconstructible entry refuses. Writing texels into a placed or tiled entry would make the
    ///        resolved projection the source of truth for content the description already holds, and `20` §4
    ///        would then evict the artist's edit at the first memory pressure.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<TexturedContent*> AmendTextured(LayerIdentity Subject);

    /// 🧩 The entries, in sequence order — bottom first.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<LayerSpecification>& Entries() const;

    /// 🧩 One nested sequence, by ordinal.
    /// out   Result  [-]  refuses with ContentUnsupported outside the nested count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const SurfaceLayerSequence*> Nested(std::uint32_t NestedIndex) const;

    /// 🧩 One nested sequence, for amending.
    /// out   Result  [-]  refuses with ContentUnsupported outside the nested count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<SurfaceLayerSequence*> AmendNested(std::uint32_t NestedIndex);

    /// 🧩 Which sequence position one entry sits at.
    /// out   Result  [-]  refuses with IdentityStale
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> PositionOf(LayerIdentity Subject) const;

    /// 🧩 The union of channels every presented entry writes — what `42`'s layered channels resolve against.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t WrittenChannels() const;

    /// 🧩 Whether any entry stores texels, and is therefore not evictable.
    /// note  🔴 `20` §5's gate read from this side: no tile is the source of truth for any content, and a tile
    ///        holding a projection of one of these entries may be discarded while the entry may not.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    bool AuthoredContentHeld() const;

    /// 🧩 How deeply this sequence is nested; zero for a surface's own.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t NestingDepth() const;

    /// 🧩 The partition revision the textured content currently addresses.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t AddressedRevision() const;

    std::uint32_t EntryCount() const;
    std::uint32_t NestedCount() const;

private:

    static constexpr std::uint32_t EntryLimit = 4096u;   // [-] - entries one sequence may hold

    // 🚧 `56` §6 leaves the nesting depth open and records that it blocks interface presentation alone. A bound
    //    is declared here rather than left absent, because unbounded nesting makes `WrittenChannels` and
    //    `AuthoredContentHeld` recurse to a depth no declaration states. Read by this sequence only, so `00` §2
    //    keeps it here rather than in `Foundation/`.
    static constexpr std::uint32_t LayerNestingLimit = 8u;   // [-] - levels a sequence may nest

    std::size_t Located(LayerIdentity Subject) const;

    std::vector<LayerSpecification>    Sequenced;                   // [-] - bottom first; position is the order
    std::vector<SurfaceLayerSequence>  NestedSequences;             // [-] - by nested ordinal
    std::uint32_t                      RegisteredGeneration  = 1u;      // [-] - advanced at every withdrawal
    std::uint32_t                      Depth             = 0u;      // [-] - zero for a surface's own sequence
    std::uint64_t                      DescribedRevision = 0u;      // [-] - the partition the texels address
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ENTRIES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Identity and lookup across one surface's sequence, nested sequences included.
/// note  📝 Declared apart from the sequence because §4.1's nesting makes "which sequence holds this entry" a
///        question a single sequence cannot answer about an entry it does not hold.
/// tag   owning
class LayerIndex
{
public:

    /// 🧩 Locates one entry anywhere in a sequence, including inside its nested sequences.
    /// in    Sequence  [-]  the surface's own sequence
    /// in    Subject   [-]  the entry
    /// out   Result   [-]  refuses with IdentityStale when nothing in the whole nesting holds it
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<const LayerSpecification*> Locate(const SurfaceLayerSequence& Sequence, LayerIdentity Subject);

    /// 🧩 How many entries the whole nesting holds.
    /// cost  🚩
    /// tag   api, nonthrowing
    static std::uint32_t SpannedCount(const SurfaceLayerSequence& Sequence);
};

// 📐 Entry identity and sequence position are Exact; channel combination, coverage and domain positions are
//    Bounded. The component claims Bounded, per `00` §3's transitivity rule.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
