//============================================================================================================================================
//                                                       TRANSMISSIONSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The classification that keeps foliage cheap, the sorted insertion that discards the farthest, and the walk that reads the column backwards.

#include "SlateCompute/Compute/TransmissionSequence/Api/TransmissionSequence.h"

#include "Shared/ReflectanceProjection.slang.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The two recordings' own names, spelled once each. The schedule orders by them and `86` reports by them, and
//    two spellings of one name are two recordings as far as the ordering is concerned.
const char* const CollectionRecordingIdentity = "62-TransmissionSequence-Collect";
const char* const ResolutionRecordingIdentity = "62-TransmissionSequence-Resolve";

double Bounded(double Magnitude, double Lower, double Upper)
{
    return Magnitude < Lower ? Lower : (Magnitude > Upper ? Upper : Magnitude);
}

}   // namespace

TransmissionBehaviour BehaviourOf(const MaterialSpecification& Declared)
{
    // 🔴 The order of the two tests is load-bearing. A material may declare a transmissive reflectance **and**
    //    a cutout registration, and `62` §2 resolves cutout at `16` — so the cutout test is asked first and the
    //    owner takes the cheap path. Reversed, a foliage material that an artist had also marked transmissive
    //    would enter the sorted column and cost what glass costs.
    if (Declared.CutoutRegistered())
        return TransmissionBehaviour::Cutout;

    if (Declared.Reflectance() == ReflectanceSelection::Transmissive)
        return TransmissionBehaviour::Transmissive;

    return TransmissionBehaviour::Opaque;
}

bool CoverageResolved(const MaterialSpecification& Declared, double Coverage)
{
    return Coverage >= Declared.CutoutThreshold();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

TransmissionSpecification DeclaredTransmission(const ResolvedChannelSet& Resolved)
{
    constexpr std::size_t Albedo       = static_cast<std::size_t>(ChannelSubject::AlbedoColour);
    constexpr std::size_t Roughness    = static_cast<std::size_t>(ChannelSubject::Roughness);
    constexpr std::size_t Opacity      = static_cast<std::size_t>(ChannelSubject::Opacity);
    constexpr std::size_t Transmission = static_cast<std::size_t>(ChannelSubject::Transmission);
    constexpr std::size_t Refraction   = static_cast<std::size_t>(ChannelSubject::RefractionRatio);

    TransmissionSpecification Declaring;
    Declaring.Opacity         = Resolved.Component[Opacity][0];
    Declaring.Transmission    = Resolved.Component[Transmission][0];
    Declaring.RefractionRatio = Resolved.Component[Refraction][0];
    Declaring.Roughness       = Resolved.Component[Roughness][0];

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
        Declaring.TintComponent[Component] = Resolved.Component[Albedo][Component];

    return Declaring;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORDINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> TransmissionSequence::ContributeCollection(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = CollectionRecordingIdentity;

    // 🔴 `TransmissionIndex` is produced here and by nothing else. `62` §3 declares it because `VisibilityIndex`
    //    holds one identity and `DepthSurface` one depth: collecting a set of fragments needs a target that can
    //    hold a set, and amending either of the two that cannot is what makes a transmissive owner occlude
    //    the surface it exists to reveal.
    Declared.Produces = { SharedTarget::TransmissionIndex };
    Declared.Reads    = { SharedTarget::DepthSurface };
    Declared.Amends   = {};

    Declared.Command            = RecordingCommand::GraphicsRecording;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";
    Declared.DisplayReferred    = false;
    Declared.AmendmentIndex   = CollectAmendmentIndex;

    return Schedule.Contribute(Declared);
}

Deliver<bool> TransmissionSequence::ContributeResolution(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = ResolutionRecordingIdentity;

    // 🔴 Amends `RadianceSurface` and produces nothing. `18` §6 produced it; `08` §2's amendment list is what
    //    makes a second write into it legal, and the amendment ordinal is what makes the list an ordering rather
    //    than a contribution accident — `30` §5 reads what this leaves, so a reflection of a transmissive
    //    owner shows that owner.
    Declared.Produces = {};
    Declared.Reads    = { SharedTarget::TransmissionIndex, SharedTarget::DepthSurface };
    Declared.Amends   = { SharedTarget::RadianceSurface };

    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";
    Declared.DisplayReferred    = false;
    Declared.AmendmentIndex   = ResolveAmendmentIndex;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INSERTION
//------------------------------------------------------------------------------------------------------------------------

bool TransmissionSequence::Insert(TransmissionColumn&         Column,
                                  const TransmissionFragment& Incoming,
                                  double                      OpaqueDepth) const
{
    // 🔴 `62` §3: a transmissive surface behind the opaque depth is discarded rather than collected. Under the
    //    reversed convention "behind" is a **lesser** coordinate, so the comparison reads the way it does; written
    //    the other way round the column fills with fragments nothing can see and the visible ones truncate out.
    if (Incoming.Depth < OpaqueDepth)
        return false;

    std::uint32_t HeldKey[TransmissionDepth]     = {};
    std::uint32_t HeldSurface[TransmissionDepth] = {};

    for (std::uint32_t Index = 0u; Index < Column.HeldCount; ++Index)
    {
        HeldKey[Index]     = Column.Held[Index].DepthKey;
        HeldSurface[Index] = Column.Held[Index].SurfaceWord;
    }

    const std::uint32_t Slot = ProjectTransmissionSlot(HeldKey,
                                                       HeldSurface,
                                                       Column.HeldCount,
                                                       Incoming.DepthKey,
                                                       Incoming.SurfaceWord);

    if (Slot == SlateTransmissionAbsent)
    {
        // 📝 Full, and the arrival is farther than everything held. Counted rather than dropped silently — the
        //    count is what `86` presents and what `62` §9's open ceiling row is measured against.
        ++Column.TruncatedCount;
        return false;
    }

    // 📝 The tail is displaced upward and the entry that falls off the end is the farthest held, which is
    //    exactly `62` §3.1's direction. Shifting downward instead would discard the nearest — correct code with
    //    the sign of the whole rule inverted, and invisible until two panes overlap.
    if (Column.HeldCount == TransmissionDepth)
        ++Column.TruncatedCount;

    const std::uint32_t Last = Column.HeldCount < TransmissionDepth ? Column.HeldCount : TransmissionDepth - 1u;

    for (std::uint32_t Index = Last; Index > Slot; --Index)
        Column.Held[Index] = Column.Held[Index - 1u];

    Column.Held[Slot] = Incoming;

    if (Column.HeldCount < TransmissionDepth)
        ++Column.HeldCount;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE AMENDMENT
//------------------------------------------------------------------------------------------------------------------------

void TransmissionSequence::AmendRadiance(const double                     Behind[3],
                                         const TransmissionSpecification& Declared,
                                         const double                     Shaded[3],
                                         double                           ViewCosine,
                                         double                           Amended[3]) const
{
    // 📐 The normal-incidence reflectance of a dielectric of the declared ratio, against the medium it sits in.
    //    Air is taken as unity: an owner nested inside another transmissive owner carries its own ratio
    //    and nothing here tracks a stack of them — `00` §5.1's third substitution point covers the absence.
    const double Ratio      = Declared.RefractionRatio > 0.0 ? Declared.RefractionRatio : 1.0;
    const double Departure  = (Ratio - 1.0) / (Ratio + 1.0);
    const double Incidence0 = Departure * Departure;

    const double Fresnel = ProjectFresnelSchlick(Incidence0, Bounded(ViewCosine, 0.0, 1.0));

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        const double Surviving = ProjectTransmittedFraction(Declared.Opacity,
                                                            Declared.Transmission,
                                                            Declared.TintComponent[Component],
                                                            Fresnel);

        // 🔴 The fragment's own shaded radiance is **added** rather than interpolated toward. `18` already
        //    scaled it by everything that decides how much of it there is, so interpolating here would apply the
        //    opacity twice and a sheet of glass would darken its own reflection as it became more transparent.
        Amended[Component] = Behind[Component] * Surviving + Shaded[Component];
    }
}

void TransmissionSequence::Resolve(const TransmissionColumn&                     Column,
                                   const std::vector<TransmissionSpecification>& Declared,
                                   const std::vector<std::array<double, 3>>&     Shaded,
                                   const std::vector<double>&                    ViewCosine,
                                   double                                        Current[3]) const
{
    const std::size_t Spanned = Column.HeldCount;

    if (Declared.size() < Spanned || Shaded.size() < Spanned || ViewCosine.size() < Spanned)
        return;

    // 🔴 Back to front. The column is nearest first, so the walk runs from the last held entry to the first —
    //    each fragment amends what every farther fragment has already left standing.
    for (std::size_t Passed = Spanned; Passed-- > 0u;)
    {
        double Behind[3]  = { Current[0], Current[1], Current[2] };
        double Amended[3] = { 0.0, 0.0, 0.0 };

        AmendRadiance(Behind, Declared[Passed], Shaded[Passed].data(), ViewCosine[Passed], Amended);

        Current[0] = Amended[0];
        Current[1] = Amended[1];
        Current[2] = Amended[2];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void TransmissionSequence::DeclareRotation(std::uint32_t OwnerCount,
                                           std::uint32_t MaximumColumnDepth,
                                           std::uint32_t TruncatedThisRecording)
{
    Reported.OwnerCount         = OwnerCount;
    Reported.MaximumColumnDepth   = MaximumColumnDepth;
    Reported.TruncatedThisRecording = TruncatedThisRecording;
    Reported.TruncatedTotal       += TruncatedThisRecording;
}

void TransmissionSequence::Report(ReportSequence& Reporting, MeasureIndex& Measured, TickPoint Sampled) const
{
    // 🔴 `86` §4's `62` §3.1 row. Coalesced by the ceiling as its subject rather than by the pixel: a per-pixel
    //    subject presents a million entries for one pane of glass, and `86` §6's retention ceiling would then be
    //    doing the discarding rather than reporting it.
    if (Reported.TruncatedThisRecording != 0u)
    {
        ReportSpecification Truncated;
        Truncated.Origin         = "62 §3.1 TransmissionSequence";
        Truncated.Subject        = "ColumnLimit";
        Truncated.Detail         = "transmissive layers beyond the per-pixel ceiling were discarded, farthest first";
        Truncated.SubjectIndex = TransmissionDepth;
        Truncated.Verdict    = ReportVerdict::Truncated;
        Truncated.Arrival        = Sampled;

        Reporting.Append(Truncated);
    }

    Measured.DeclareCount("62 §3 TransmissionSequence", "Owners", Reported.OwnerCount, Sampled);
    Measured.DeclareCount("62 §3 TransmissionSequence", "MaximumColumnDepth", Reported.MaximumColumnDepth, Sampled);
    Measured.DeclareCount("62 §3.1 TransmissionSequence", "Truncated", Reported.TruncatedTotal, Sampled);
}

const TransmissionMetrics& TransmissionSequence::Metrics() const { return Reported; }

}   // namespace Slate
