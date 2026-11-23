//============================================================================================================================================
//                                                          BRUSHSPECIFICATION.H
//============================================================================================================================================
// 🧩 The brush every stroke in `22` is resolved against — a shape, a spacing, a channel set, and dynamics that read input.

#pragma once

#include "Foundation/Combination.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE IMPRESSION SHAPE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where one impression's coverage comes from — `58` §3's three sources.
/// note  🔴 A shape is **coverage, not colour**. An imported image supplies where the impression applies and the
///        values it applies come from §2's channel set. A shape carrying colour would make every brush a
///        per-channel asset, and an artist changing colour would be editing an image.
/// tag   guarantee
enum class ShapeSource : std::uint32_t
{
    Analytic     = 0u,   // [-] - a profile from centre to edge, resolved at the extent
    Imagery      = 1u,   // [-] - a single-channel image through `50`
    VectorOutline = 2u,  // [-] - an outline through `52`, classified at Tier A
    SourceCount  = 3u    // [-] - the closed count, never a source
};

/// 🧩 How the analytic profile falls from centre to edge.
/// tag   guarantee
enum class ProfileSubject : std::uint32_t
{
    Constant     = 0u,   // [-] - full coverage to the edge, then nothing
    Linear       = 1u,   // [-] - falls to nothing at the edge
    Quadratic    = 2u,   // [-] - falls faster near the edge
    Sigmoid      = 3u,   // [-] - a soft shoulder both ends
    ProfileCount = 4u    // [-] - the closed count, never a profile
};

/// 🧩 What sets one impression's rotation — `58` §3.1.
/// tag   guarantee
enum class RotationSubject : std::uint32_t
{
    Fixed         = 0u,   // [-] - one declared angle
    PathRelative  = 1u,   // [-] - the tangent of the resampled path
    InputDriven   = 2u,   // [-] - a reported stylus rotation axis
    RotationCount = 3u    // [-] - the closed count, never a rotation
};

/// 🧩 The coverage of one impression.
/// note  ⚠️ Path-relative rotation reads the tangent of the **resampled** path from `22` §1 ③, never of the raw
///        input. Raw input at a low sample rate produces a tangent that jitters at every reported position, and
///        a shaped brush then flickers along a stroke the artist drew smoothly.
/// tag   nonallocating, nonthrowing
struct ImpressionShape
{
    ShapeSource      Source          = ShapeSource::Analytic;
    ProfileSubject   Profile         = ProfileSubject::Linear;     // [-]   - read at Analytic
    std::uint32_t    SourceIndex   = 0u;                         // [-]   - into `50` or `52`
    RotationSubject  Rotated         = RotationSubject::Fixed;     // [-]
    double           FixedRotation   = 0.0;                        // [deg] - read at Fixed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DYNAMICS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which input axis a dynamic reads — `58` §4's five rows.
/// note  🔴 Speed and path distance are **never absent**: both are derived, speed from `04` §3's arrival
///        timestamps and distance from the resampled path. The three the stylus reports may be absent, and an
///        absent axis falls back to a declared value rather than reading as zero — a tablet reporting no tilt
///        and a stylus held upright are different facts.
/// tag   guarantee
enum class DynamicAxis : std::uint32_t
{
    Pressure     = 0u,   // [-] - reported by the stylus
    Tilt         = 1u,   // [-] - reported where supported
    Rotation     = 2u,   // [-] - reported where supported
    Speed        = 3u,   // [-] - derived from arrival timestamps; never absent
    PathDistance = 4u,   // [-] - derived from the resampled path; never absent
    AxisCount    = 5u    // [-] - the closed count, never an axis
};

/// 🧩 Which brush parameter a dynamic drives.
/// tag   guarantee
enum class DynamicParameter : std::uint32_t
{
    Extent           = 0u,   // [-] - the impression radius
    CoverageStrength = 1u,   // [-] - how strongly it applies
    Rotation         = 2u,   // [-] - the impression's own turn
    Spacing          = 3u,   // [-] - the distance to the next impression
    ParameterCount   = 4u    // [-] - the closed count, never a parameter
};

/// 🧩 How a dynamic moves across its declared interval.
/// note  🔴 Declared, **never linear by assumption** — `58` §4. Pressure mapped linearly onto radius feels wrong
///        to every artist who has used a stylus, and a brush that cannot state its own progression is a brush
///        every artist immediately abandons. `ProgressionCount` is the undeclared value and is rejected.
/// tag   guarantee
enum class ProgressionSubject : std::uint32_t
{
    Linear          = 0u,   // [-]
    Quadratic       = 1u,   // [-] - slow at first
    Radical         = 2u,   // [-] - fast at first
    Sigmoid         = 3u,   // [-] - soft at both ends
    ProgressionCount = 4u   // [-] - undeclared; rejected at declaration
};

/// 🧩 One input axis mapped onto one parameter.
/// note  🔴 An absent axis falls back to `AbsentFallback` and never to zero. `22` §1 states it from the input
///        side; this is the document that has to act on the distinction, because the dynamic is here.
/// tag   nonallocating, nonthrowing
struct DynamicSpecification
{
    DynamicAxis         Axis           = DynamicAxis::Pressure;
    DynamicParameter    Parameter      = DynamicParameter::Extent;
    ProgressionSubject  Progression    = ProgressionSubject::ProgressionCount;   // [-] - must be declared
    double              LowerScale     = 0.0;    // [-] - the parameter's factor at the axis's low end
    double              UpperScale     = 1.0;    // [-] - and at its high end
    double              AbsentFallback = 1.0;    // [-] - the factor when the device reports no such axis
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SPACING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How a resampled path is spaced.
/// note  🔴 `58` §5: spacing is declared **relative to the impression extent**, so a brush resized keeps its
///        character. It bounds the impression count and therefore the work in `22` §2, which is why it carries a
///        floor — a spacing declarable arbitrarily fine is a brush that can stall a stroke.
/// tag   nonallocating, nonthrowing
struct SpacingSpecification
{
    double  RelativeSpacing = 0.25;   // [-] - as a fraction of the impression extent
    bool    FloorReached    = false;  // [-] - the declared floor bounded it; reported through `86`
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      VARIATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What varies from impression to impression, and over what interval.
/// note  🔴 `58` §6: the sequence is **stroke-seeded** and the seed is recorded with the transaction. A stroke
///        varying from an unrecorded source resolves differently every time it is re-resolved, so undo and redo
///        would produce a different stroke from the one the artist made, and `20`'s reconstruction of an evicted
///        tile would disagree with what was on screen.
/// tag   nonallocating, nonthrowing
struct VaryingSpecification
{
    double  ExtentVariation   = 0.0;   // [-] - as a fraction of the extent
    double  RotationVariation = 0.0;   // [deg]
    double  CoverageVariation = 0.0;   // [-] - as a fraction of the strength
    double  PositionVariation = 0.0;   // [-] - as a fraction of the extent, about the path
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ONE CHANNEL VALUE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One of `42`'s channels and the value the brush writes to it.
/// note  🔴 `58` §2: a brush declares a **channel set with a value per channel**, never a single colour. `22`
///        §5's multi-channel stroke is one transaction because one brush wrote both channels; a brush carrying
///        only a colour would make texturing roughness a separate tool with separate presets and separate undo.
/// tag   nonallocating, nonthrowing
struct BrushChannelValue
{
    ChannelSubject       Channel        = ChannelSubject::ChannelCount;
    double               ScalarValue    = 0.0;    // [-] - read at a scalar measure
    ColourSpecification  ColourValue    = {};     // [-] - read at a colour measure; carries its space
    bool                 ColourDeclared = false;  // [-] - which of the two above is meaningful
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT ONE IMPRESSION GETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The brush's parameters resolved at one impression of a stroke.
/// note  ⚠️ Deliberately **not** `22` §1 ④'s `ImpressionSample`. That carries a domain position and belongs to
///        `22`, which assembles it from this plus the resampled path. Naming this the same thing would give one
///        spelling two owners across two documents.
/// tag   nonallocating, nonthrowing
struct ResolvedBrush
{
    double  Extent            = 1.0;   // [-]   - in the domain, after dynamics and variation
    double  Rotation          = 0.0;   // [deg]
    double  CoverageStrength  = 1.0;   // [-]
    double  Spacing           = 0.25;  // [-]   - relative to the extent
    double  DisplacementX = 0.0;   // [-]   - positional variation about the path
    double  DisplacementY = 0.0;  // [-]
};

/// 🧩 What the input device reported for one impression, and which of it is genuinely present.
/// note  🔴 The presence flags are read before the magnitudes. `04` §3 carries absence through to here precisely
///        so that a dynamic can fall back rather than read a fabricated zero.
/// tag   nonallocating, nonthrowing
struct ResolvedAxes
{
    double  Pressure         = 0.0;    // [-]
    double  Tilt             = 0.0;    // [-] - normalised from the reported angle
    double  Rotation         = 0.0;    // [-] - normalised from the reported barrel angle
    double  Speed            = 0.0;    // [-] - normalised; derived, never absent
    double  PathDistance     = 0.0;    // [-] - normalised; derived, never absent
    bool    PressureReported = false;  // [-]
    bool    TiltReported     = false;  // [-]
    bool    RotationReported = false;  // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE BRUSH
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One brush — everything §2 declares, and nothing about which tool is active.
/// note  ⚠️ A brush is **not a tool**. `76` holds which brush is active and holds tool state generally; this
///        declares what a brush *is*. A brush stored inside the tool would make brushes unshareable between
///        documents and unsavable independently of the interface.
/// tag   owning
class BrushSpecification
{
public:

    /// 🧩 Declares the impression shape.
    /// out   Result  [-]  refuses with ContentUnsupported for an undeclared profile at an analytic source
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareShape(const ImpressionShape& Declaring);

    /// 🧩 Declares the impression extent, in domain units.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive extent
    /// note  🚧 `58` §11 and `22` §7 both carry whether the extent is declared in the domain or in screen terms.
    ///        It is the domain here, which is the answer that makes a stroke survive a change of working
    ///        resolution; the open row stands and nothing above reads a screen extent.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareExtent(double Extent);

    /// 🧩 Declares the spacing, bounded below by the declared floor.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive spacing
    /// post  the floor's having been reached is recorded, and is reported through `86` by `Report`
    /// note  🔴 The floor is applied and **said**, never applied silently — `58` §5. A brush quietly coarsened
    ///        below its declared spacing textures a stroke the artist did not ask for and cannot account for.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareSpacing(double RelativeSpacing);

    /// 🧩 Declares one channel and the value written to it.
    /// out   Result  [-]  refuses with ContentUnsupported for an out-of-range channel, for a colour value
    ///                     declaring no space, and for a channel already declared
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareChannel(const BrushChannelValue& Declaring);

    /// 🧩 Declares one dynamic.
    /// out   Result  [-]  refuses with ContentUnsupported for an undeclared progression, for an out-of-range
    ///                     axis or parameter, and for a second dynamic on the same parameter
    /// note  🔴 One dynamic per parameter. Two dynamics driving one radius is two answers to one question, and
    ///        whichever the resolution applied second would win by accident of declaration order.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareDynamic(const DynamicSpecification& Declaring);

    /// 🧩 Declares the combination this brush's strokes apply.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareCombination(CombineSpecification Declaring);

    /// 🧩 Declares what varies from impression to impression.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareVariation(const VaryingSpecification& Declaring);

    /// 🧩 Resolves the brush's parameters at one impression of one stroke.
    /// in    Axes              [-]  what the device reported, and which of it is present
    /// in    ImpressionIndex [-]  the impression's position within the stroke
    /// in    StrokeSeed        [-]  recorded with the transaction — `58` §6
    /// out   Resolved          [-]  extent, rotation, strength, spacing and displacement
    /// note  🔴 The variation reads `02` §6's shared permutation at Tier A, so the sequence `82` previews with
    ///        and the sequence `22` resolves with are the same sequence. One that disagreed would give a preview
    ///        whose variation is not the variation the artist gets.
    /// note  🔴 An absent axis takes its dynamic's declared fallback. Reading zero would make a pressure brush
    ///        texture nothing on a mouse and a tilt brush behave as though every stylus were flat.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    ResolvedBrush Resolve(const ResolvedAxes& Axes,
                          std::uint32_t       ImpressionIndex,
                          std::uint32_t       StrokeSeed) const;

    /// 🧩 Appends the spacing-floor report, once, if the floor was reached.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, TickPoint Sampled);

    const ImpressionShape&                     Shape() const;
    const SpacingSpecification&                Spacing() const;
    const VaryingSpecification&                Variation() const;
    const std::vector<BrushChannelValue>&      Channels() const;
    const std::vector<DynamicSpecification>&   Dynamics() const;

    double                Extent() const;
    CombineSpecification  Combination() const;

    /// 🧩 Whether the brush writes one of `42`'s channels.
    /// cost  🚩
    /// tag   api, nonthrowing
    bool ChannelDeclared(ChannelSubject Channel) const;

private:

    ImpressionShape                    DeclaredShape       = {};
    SpacingSpecification               DeclaredSpacing     = {};
    VaryingSpecification               DeclaredVariation   = {};
    std::vector<BrushChannelValue>     DeclaredChannels;
    std::vector<DynamicSpecification>  DeclaredDynamics;
    double                             DeclaredExtent      = 0.02;   // [-] - in domain units
    CombineSpecification               DeclaredCombination = CombineSpecification::Over;
    bool                               FloorReported       = false;  // [-] - Report appends once
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE BRUSHES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Stored brushes, their identities and their groupings.
/// note  🔴 `58` §7 and `48` §6: brushes are stored **with the application**, not in the document. An artist who
///        sets a brush size and switches document is expressing a preference about how they are working, not
///        about that file.
/// tag   owning
class BrushIndex
{
public:

    /// 🧩 Declares one brush and issues its ordinal.
    /// in    Named    [-]  what the artist calls it; may be empty
    /// in    Grouping [-]  which grouping it is presented under; may be empty
    /// out   Result  [-]  refuses with ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const std::string& Named, const std::string& Grouping);

    Deliver<const BrushSpecification*> Resolve(std::uint32_t BrushIndex) const;
    Deliver<BrushSpecification*>       Amend(std::uint32_t BrushIndex);

    const std::string& DeclaredName(std::uint32_t BrushIndex) const;
    const std::string& DeclaredGrouping(std::uint32_t BrushIndex) const;

    std::uint32_t DeclaredCount() const;

private:

    static constexpr std::uint32_t BrushLimit = 4096u;   // [-] - brushes the application may hold

    std::vector<BrushSpecification>  Declared;
    std::vector<std::string>         DeclaredNames;
    std::vector<std::string>         DeclaredGroupings;
    std::string                      AbsentName;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              WHAT A STROKE RECORDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The brush parameters one stroke resolved with, recorded in its transaction.
/// note  🔴 `58` §7: a stroke records the **parameters**, not a reference to a brush. An artist who edits a brush
///        and then undoes an old stroke must get that stroke's inverse, not the inverse the current brush would
///        produce.
/// tag   owning
struct StrokeBrushRecord
{
    ImpressionShape                 Shape       = {};                            // [-]
    SpacingSpecification            Spacing     = {};                            // [-]
    VaryingSpecification            Variation   = {};                            // [-]
    std::vector<BrushChannelValue>  Channels    = {};                            // [-]
    double                          Extent      = 0.0;                           // [-]
    CombineSpecification            Combination = CombineSpecification::Over;    // [-]
    std::uint32_t                   StrokeSeed  = 0u;                            // [-] - `58` §6's seed
};

/// 🧩 Records one brush's resolved parameters, for a stroke's transaction.
/// cost  🚩
/// tag   api, nonthrowing
StrokeBrushRecord RecordBrush(const BrushSpecification& Resolving, std::uint32_t StrokeSeed);

// 📐 The variation sequence is Exact and parity-proven; the shape coverage, dynamic progressions and spacing are
//    Bounded. The component claims Bounded, per `00` §3's transitivity rule.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
