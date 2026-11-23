//============================================================================================================================================
//                                                          EMISSIONSEQUENCE.H
//============================================================================================================================================
// 🧩 `50` §5 — an export resolved from the domain at its declared extent, band by band, never read back from residency.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/AnalyticProjection/Api/AnalyticProjection.h"
#include "SlateDocument/Document/AssetInterchange/Api/AssetInterchange.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT BOUNDS AN EMISSION
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The widest edge an emission may declare. Sixteen thousand per edge at four components is a gigabyte in
//    single precision, which is the point past which an export is a decision the artist should have been asked
//    about rather than a wait they are subjected to. Rejected at Open, where the refusal costs nothing.
inline constexpr std::uint32_t EmissionExtentLimit = 16384u;   // [px] - per edge, per emitted image

// 📝 Rows one band resolves. `50` §5 runs an emission through `34` at `Background`, and a work item that
//    resolved a whole image would occupy a worker for the length of the export and starve nothing visibly while
//    doing it — right up until the artist closes the document. A band is the unit that can be abandoned.
inline constexpr std::uint32_t EmissionBandRows = 32u;           // [px] - rows one ResolveBand walks

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT AN EMISSION MAKES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One emitted image's texels, resolved and packed into the arrangement the specification declared.
/// note  🔴 `50` §5: these were resolved from `56`'s layers through `70`, at the emission's own extent. They are
///        **not** a readback of `20`'s resident tiles. Residency is a display decision bounded by device memory,
///        and an export bounded by what happened to be resident is an export whose content depends on where the
///        artist last looked — which is a defect that reproduces only on the machine that made it.
/// note  📝 Single precision, not the emitted depth. `50` §7 charges the quantisation to the codec at Tier B and
///        quantising here would quantise twice: once into this and once into the file, and the second one would
///        be quantising an already-quantised value.
/// tag   owning
struct EmittedTexels
{
    std::vector<float>  Texels         = {};   // [-]  - interleaved, ComponentCount per texel, row-major
    std::uint32_t       ExtentTexels   = 0u;   // [px] - per edge; the image is square
    std::uint32_t       ComponentCount = 0u;   // [-]  - always ComponentSlot::ComponentCount; unoccupied are zero
    std::uint32_t       SpaceIdentity  = 0u;   // [-]  - carried through, to be written into the file
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHERE IT RESOLVES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What an emission resolves through, borrowed and never owned.
/// note  🔴 The same `70` a promotion reads and the same one `82` previews through. Three consumers, one
///        resolver: an export that resolved by a second implementation would ship an asset that disagrees with
///        what the artist was shown while texturing it, and they would have no way to tell which one was wrong.
/// tag   nonallocating, nonthrowing
struct EmissionSources
{
    const AnalyticProjection*  Resolution = nullptr;   // [-] - `70`, the one resolver every path reads
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CHANNEL PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Derives the placements one emitted image's arrangement amounts to.
/// in    Arranged     [-]  the image, with its occupied components declared
/// out   Placements   [-]  one entry per occupied component, in ascending component order
/// note  🔴 The entries are in **ascending component order**, and the band walk depends on it: `70` resolves
///        densely — component zero of the resolution is the first channel the run names — and the walk scatters
///        its 𝑘th component into the 𝑘th occupied slot. An unordered run would scatter into the wrong slots and
///        produce an image that is plausible and wrong, which is `50` §5.1's whole warning.
/// note  🚧 `70` accepts the run and does not yet read it — `00` §12 carries the channel packing layout as open,
///        so nothing declares which components a channel occupies within a resolved texel. The scatter therefore
///        lives in the band walk today. The day `00` §12 is answered and `70` places by the run, the scatter
///        becomes the identity and is deleted; the run is handed over either way, so nothing else moves.
/// note  🔴 Every placement spans one component. A colour channel occupying three is declared as three entries
///        in the arrangement, because `50` §5.1 requires the arrangement be *presented to the artist* and an
///        entry that silently claimed the two components after it is an entry they cannot see the extent of.
/// cost  🚩
/// tag   api, nonthrowing
std::vector<ChannelPlacement> ProjectPlacements(const EmittedImage& Arranged);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

/// 🧩 The flattening tolerance an emission at one extent resolves at.
/// in    ExtentTexels  [px] per edge of the emitted image
/// out   Tolerance     [-]  one texel of the emitted extent, in domain units
/// note  📐 The same rule `70`'s `ToleranceAtLevel` states, at an extent `20` does not have a level for. An
///        emission's extent is the artist's declaration and need not be a reduction level at all, so the
///        tolerance is derived from the extent rather than looked up — one texel of what is being written,
///        which is the deviation below which a chord cannot move a sample.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr double ToleranceAtExtent(std::uint32_t ExtentTexels)
{
    return ExtentTexels > 0u ? 1.0 / static_cast<double>(ExtentTexels) : 1.0;
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EMISSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One emitted image resolved a band at a time, so a `Background` export never holds a worker for its length.
/// note  🔴 `50` §5: the document remains editable while this runs, and it reads **sealed** state per `48` §3 —
///        so an export started before an edit contains the state at the moment it started. That is discharged by
///        the caller handing in the sequence it sealed, not by anything here: a component that reached for the
///        live sequence would resolve half its bands from before an edit and half from after, and the seam
///        between them would land somewhere down the middle of the image.
/// note  🔴 Nothing here writes a file. `48` §3's write-verify-replace sequence and `04`'s `StorageExchange` are
///        both built and are the caller's to drive — an export that half-overwrites last week's export has
///        destroyed a deliverable to produce nothing, and that guarantee lives in the component that owns the
///        replacement rather than in the one that produced the bytes.
/// tag   owning
class EmissionSequence
{
public:

    /// 🧩 Takes the resolver every band reads.
    /// in    Supplied  [-]  borrowed; outlives this component
    /// out   Result   [-]  refuses with ContentUnsupported for an absent resolver
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> ConstructEmissionSequence(const EmissionSources& Supplied);

    /// 🧩 Opens one image of a validated emission, ready for its first band.
    /// in    Declaring     [-]  the emission specification; validated here, again, and not assumed
    /// in    Materials     [-]  the declared materials, so a channel no material declares is rejected
    /// in    ImageIndex  [-]  which of the specification's images this emission produces
    /// out   Result       [-]  refuses with HostDenied before Construct and while an emission stands, with
    ///                          ContentUnsupported outside the image count and above the extent ceiling, and
    ///                          with whatever the specification's own validation rejected
    /// post  🔴 the texel run is allocated once, whole, so no band reallocates mid-emission
    /// note  🔴 `Validate` is asked here even though `AssetInterchange::DeclareEmission` asked it already. The
    ///        specification is handed in by value and the two calls are separated by however long the artist
    ///        spent between declaring an export and starting it; validating once and trusting thereafter is
    ///        trusting a copy, and `50` §5.1's wrong arrangement is exactly what that copy would carry.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Open(const EmissionSpecification& Declaring,
                       const MaterialIndex&         Materials,
                       std::uint32_t                ImageIndex);

    /// 🧩 Resolves the next band of rows, and no more than that.
    /// in    Content   [-]  the sealed layer sequence the emission reads
    /// out   Result   [-]  refuses with HostDenied before Open, with ExtentExhausted once every row is
    ///                      resolved, and with whatever `70` rejected at the first position it rejected at
    /// post  the delivered count is rows resolved this call; zero is never delivered
    /// note  🔴 A refusal from `70` abandons the **whole** emission rather than leaving the band half-written.
    ///        `50` §2's rule for a partial intake is the same rule from the other direction: an image that is
    ///        resolved above a seam and zero below it is an asset the artist ships without noticing, whereas an
    ///        export that rejected is one they cannot miss.
    /// note  📝 Texels are sampled at their **centres** — (Row + ½)/Extent — and not at their corners. A corner
    ///        sample places the first texel exactly on the domain boundary, where a seam's two sides are equally
    ///        near and the resolution picks one arbitrarily.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> ResolveBand(const SurfaceLayerSequence& Content);

    /// 🧩 Whether rows remain to be resolved.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ResolutionOwed() const;

    /// 🧩 How many rows have been resolved, for whoever presents the export's progress.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t ResolvedRows() const;

    /// 🧩 Hands over the completed image and closes the emission.
    /// out   Result  [-]  refuses with HostDenied before Open and with ExtentExhausted while rows remain
    /// post  🔴 the emission is closed; the texels are moved out and this holds none
    /// note  🔴 Refuses while rows remain rather than delivering what stands. A partially resolved image handed
    ///        to a codec is a file that opens, looks approximately right, and is wrong along one edge.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<EmittedTexels> Seal();

    /// 🧩 Abandons the standing emission and reclaims its texels.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

private:

    EmittedTexels                  Producing       = {};        // [-] - the image being resolved, whole
    std::vector<ChannelPlacement>  Arrangement     = {};        // [-] - derived once at Open, read every band
    const AnalyticProjection*      Resolution      = nullptr;   // [-] - `70`, borrowed
    std::uint32_t                  RowsNext        = 0u;        // [px] - the first row the next band resolves
    bool                           EmissionOpen    = false;     // [-]  - false until Open delivered
    bool                           SourcesDeclared = false;     // [-]  - false until Construct delivered
};

// 📐 The resolution is `70`'s and is Bounded. The extent, the arrangement and the texel addressing are integer
//    and Exact. The component claims the weaker, which is Bounded — and claims nothing about the quantisation
//    into a file, because that is the codec's and is charged at Tier B by `50` §7.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
