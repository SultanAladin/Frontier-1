//============================================================================================================================================
//                                                           ASSETINTERCHANGE.H
//============================================================================================================================================
// 🧩 Topology and imagery in, textured channels out — one guarantee, and intake that never repairs.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/IntakeIndex/Api/IntakeIndex.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A CODEC HANDS OVER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One source-named object or group and the exact source face ordinals belonging to it.
/// note  A face may belong to several groups, so membership is a set rather than one lossy group index.
struct DecodedFaceSet
{
    std::string Name = {};
    std::vector<std::uint32_t> Faces = {};
};

/// 🧩 A decoded topology, faithful to the source and repaired in no respect.
/// note  🔴 `50` §2 ①: intake **never repairs**. `38`'s non-mutation rule begins here — a codec that welds
///        vertices, reverses winding or drops a degenerate face has produced a specification that no longer
///        describes the file the artist supplied, and `38`'s guarantee that an index means the same thing
///        afterwards is broken before `38` has run.
/// note  `GeometryCodec` fills this handover shape. Document intake consumes it and parses no file format.
/// tag   owning
struct DecodedTopology
{
    std::vector<DocumentPosition>            Positions          = {};      // [mm] - at the file's own width
    std::vector<std::vector<std::uint32_t>>  Faces              = {};      // [-]  - corner runs, any count
    std::vector<DomainCoordinate>            CornerCoordinates  = {};      // [-]  - empty where absent
    std::vector<SurfaceDirection>            Perpendiculars     = {};      // [-]  - empty where absent
    std::vector<TangentBasis>                TangentBases       = {};      // [-]  - empty where absent
    std::vector<std::uint32_t>               MaterialRegistration = {};      // [-]  - material ordinal per face
    std::vector<std::string>                 MaterialNames      = {};      // [-]  - source spelling by ordinal
    std::vector<DecodedFaceSet>              ObjectMemberships  = {};      // [-]  - source object face sets
    std::vector<DecodedFaceSet>              GroupMemberships   = {};      // [-]  - overlapping source group face sets
    std::vector<std::string>                 UnsupportedNamed   = {};      // [-]  - constructs that will not survive
    std::string                              OriginPath         = {};      // [-]  - where it was read from
    double                                   UnitScale          = 1.0;     // [-]  - applied once, at intake
    bool                                     UnitScaleDeclared  = false;   // [-]  - the file carried a convention
};

/// 🧩 A decoded image, its original retained.
/// note  🔴 `36` §3 and `50` §4: the original is retained so re-conversion is always **from it**. Converting a
///        converted image is the defect where correcting an artist's mistake makes the image worse than the
///        mistake did.
/// note  🔴 Bit depth is retained and never narrowed at intake. A sixteen-bit source narrowed on the way in is a
///        precision loss with no origin anybody can point at afterwards.
/// tag   owning
struct DecodedImage
{
    std::vector<std::uint8_t>  Original       = {};      // [-]  - retained verbatim for re-conversion
    std::string                OriginPath     = {};      // [-]  - where it was read from
    std::uint32_t              Width          = 0u;      // [px]
    std::uint32_t              Height         = 0u;      // [px]
    std::uint32_t              ComponentCount = 0u;      // [-]  - components per texel
    std::uint32_t              BitDepth       = 8u;      // [-]  - bits per component, as the file carried them
    std::uint32_t              SpaceIdentity  = 0u;      // [-]  - as the content declared it
    bool                       SpaceDeclared  = false;   // [-]  - the format carried a space at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE EMISSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One component of an emitted image.
/// tag   guarantee
enum class ComponentSlot : std::uint32_t
{
    Red            = 0u,   // [-]
    Green          = 1u,   // [-]
    Blue           = 2u,   // [-]
    Alpha          = 3u,   // [-]
    ComponentCount = 4u    // [-] - the closed count, never a component
};

/// 🧩 One image an emission produces — its extent, its space, and which channel occupies which component.
/// note  🔴 `50` §5.1: the arrangement is **declared, never conventional**. Packing occlusion, roughness and
///        metalness into three components is one convention among many and the consumer decides which; a wrong
///        arrangement is a shipped asset that renders as a plausible, wrong surface.
/// tag   owning
struct EmittedImage
{
    std::string     NamePattern                                          = {};      // [-]  - overrides the specification's
    std::uint32_t   ExtentTexels                                         = 0u;      // [px] - per edge, per image
    std::uint32_t   SpaceIdentity                                        = 0u;      // [-]  - written into the file
    ChannelSubject  Occupying[static_cast<std::size_t>(ComponentSlot::ComponentCount)] = {};
    bool            ComponentOccupied[static_cast<std::size_t>(ComponentSlot::ComponentCount)] = {};
};

/// 🧩 What an export produces — which channels, at what extent, in what layout, for which consumer.
/// note  🔴 `50` §5: an emission resolves the domain at the declared extent through the same path `20` promotes
///        tiles with, and is **not** a readback of resident tiles. Residency is a display decision bounded by
///        device memory; an export bounded by what happened to be resident is an export whose content depends on
///        where the artist last looked.
/// note  🚧 The resolution itself waits on `56`, `70` and `20`. What is built here is the declaration, its
///        validation and its naming — the three parts that are wrong most often and cost nothing to check.
/// tag   owning
struct EmissionSpecification
{
    std::vector<EmittedImage>  Images      = {};   // [-] - one entry per emitted image
    std::string                NamePattern = {};   // [-] - over owner, material and channel

    /// 🧩 Whether the specification describes an export that can be produced at all.
    /// in    Materials  [-]  the declared materials, so a channel no material declares is rejected
    /// out   Result    [-]  refuses with ContentUnsupported for an extent of zero, an image with no occupied
    ///                       component, a colour-carrying channel in an image declaring no space, and a channel
    ///                       occupying two components anywhere in the specification
    /// note  🔴 A channel emitted twice is two answers to one question, and the consumer reads whichever image
    ///        it loaded second. Rejected here rather than discovered by whoever ships the asset.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Validate(const MaterialIndex& Materials) const;
};

/// 🧩 Resolves an emission's declared naming pattern.
/// in    Pattern       [-]  carrying `{Owner}`, `{Material}`, `{Channel}` and `{Extent}`
/// in    OwnerName  [-]  what the artist called the owner
/// in    MaterialName  [-]  what they called the material
/// in    ChannelName   [-]  the channel's own spelling
/// in    ExtentTexels  [px] the emitted extent
/// out   Resolved      [-]  the pattern with every declared substitution applied
/// note  📝 An unrecognised substitution is left verbatim rather than emptied. A name that silently lost a field
///        collides with every other name that lost the same one, and the export overwrites itself.
/// cost  🚩
/// tag   api, nonthrowing
std::string ResolveName(const std::string& Pattern,
                        const std::string& OwnerName,
                        const std::string& MaterialName,
                        const std::string& ChannelName,
                        std::uint32_t      ExtentTexels);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTERCHANGE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Intake and emission as one guarantee, in both directions.
/// note  🔴 `50` §2: intake is three steps and they are separate. ① decodes faithfully — the codec's. ② registers
///        owners — this document's. ③ derives companions — `38`'s, through `34`, and where the cost lives.
/// note  🔴 `50` §8: a partially failed intake **registers nothing**. Half a topology registered as an owner is an
///        owner the artist will texture on and export.
/// tag   owning
class AssetInterchange
{
public:

    // 📝 One hundredth of a millimetre per source unit is the convention a silent file most often means. It is
    //    an assumption and is recorded and reported as one; `50` §3 requires nothing more and permits nothing
    //    less. The number is declared here because no second unit reads it.
    static constexpr double AssumedUnitScale = 1.0;   // [-] - source units to millimetres, when the file is silent

    /// 🧩 Registers one decoded topology into a topology structure, sealing it.
    /// in    Decoded  [-]  the decoded specification, faithful to the source
    /// in    Into     [-]  the structure to register into; untouched when the intake refuses
    /// in    Recorded [-]  where the assumption, if any, is recorded
    /// out   Result  [-]  refuses with ContentUnsupported for absent positions or absent face indexing — `50`
    ///                     §3 gives neither a default — and carries the structure's own refusal otherwise
    /// note  🔴 Unit scale is applied **once, at intake**, and is never carried as a per-owner multiplier.
    ///        A scene where each owner carries its own unit convention is a scene where `02` §3.2's rebasing
    ///        is correct and the geometry still does not line up.
    /// note  🔴 Every construct named in `UnsupportedNamed` is recorded **at intake** rather than at export.
    ///        `50` §6: a construct that will not survive is named at the moment it arrives, not at the moment it
    ///        is missed — by which time the artist has already built on it.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> IntakeTopology(const DecodedTopology& Decoded, TopologyStructure& Into, IntakeIndex& Recorded);

    /// 🧩 Records one decoded image's declared space, or the assumption made in its absence.
    /// in    Decoded   [-]  the decoded image, its original retained
    /// in    Recorded  [-]  where the assumption, if any, is recorded
    /// out   Result   [-]  refuses with ContentUnsupported for an image of no extent or no component
    /// note  🔴 `50` §4: this document **declares** and does not convert. `36` §3 converts once, at intake, into
    ///        the working space, and reads the channel measure from `42` at the point of use. There is no
    ///        inference here from file name, channel count or encoding — and this is the document where the
    ///        temptation lives, because a file called `_normal` looks like a helpful signal.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> IntakeImage(const DecodedImage& Decoded, IntakeIndex& Recorded);

    /// 🧩 Whether an emission may be started against the current document.
    /// in    Declaring  [-]  the emission specification
    /// in    Materials  [-]  the declared materials
    /// out   Result    [-]  carries the specification's own refusal
    /// note  🚧 `50` §5's resolution waits on `56`, `70` and `20`. Validation does not, and it is where the
    ///        wrong arrangement and the doubled channel are caught — before the artist has waited for an export.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareEmission(const EmissionSpecification& Declaring, const MaterialIndex& Materials);

    /// 🧩 The declared emission, for whoever presents it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const EmissionSpecification& Emission() const;

    /// 🧩 Constructs that were named at intake and will not survive an emission — `86`'s `50` §6 row.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<std::string>& Unsupported() const;

    /// 🧩 Appends the unsupported constructs to the register, once each.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, TickPoint Sampled);

    std::uint32_t IntakenTopologyCount() const;
    std::uint32_t IntakenImageCount() const;

private:

    EmissionSpecification     Declared;                     // [-] - as DeclareEmission validated it
    std::vector<std::string>  UnsupportedNamed;             // [-] - named at intake, awaiting `86`
    std::uint32_t             UnsupportedReported = 0u;     // [-] - how many have been appended
    std::uint32_t             TopologyCount       = 0u;     // [-] - topologies registered
    std::uint32_t             ImageCount          = 0u;     // [-] - images declared
};

// 📐 Position and index decode are Exact — widened, never narrowed. Unit scale and channel emission are Bounded:
//    one multiplication applied once, and one quantisation at the declared depth. The component claims Bounded.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
