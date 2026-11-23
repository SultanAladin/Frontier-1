//============================================================================================================================================
//                                                         DISPLAYPROJECTION.CPP
//============================================================================================================================================
// 🧩 Exposure first, compression second, primaries third, transfer once — and the refusal that stops one monitor being every monitor.

#include "SlateCompute/Compute/DisplayProjection/Api/DisplayProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

const char* const DisplayRecordingIdentity = "66-DisplayProjection";

// 📐 Luminance in the working space, from the space's own second primary weight. Taken as the middle row of the
//    tristimulus projection would give — but `ColourProjection` derives that matrix internally and exposes no
//    row, so the reduction the caller supplies is a magnitude and this is only the target it adapts toward.
constexpr double MiddleGreyLuminance = 0.18;   // [-] - the reduction the metering drives toward

}   // namespace

Deliver<bool> DisplayProjection::Declare(const ExposureSpecification& Exposing_,
                                         const ToneSpecification&     Toning_,
                                         const EncodeSpecification&   Encoding_)
{
    if (!(Toning_.WhiteMagnitude > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a white magnitude of nothing compresses everything to nothing" });
    }

    if (Toning_.PreservationBlend < 0.0 || Toning_.PreservationBlend > 1.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the hue preservation blend lies outside the unit interval" });
    }

    // ⚠️ A metered exposure with no interval adapts instantly, which `66` §2 refuses by name: the artist corrects
    //    a stroke against a value that has already changed.
    if (Exposing_.Source == ExposureSubject::Metered && !(Exposing_.AdaptationSeconds > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a metered exposure declares no adaptation interval" });
    }

    if (!Encoding_.Working.SpaceDeclared() || !Encoding_.Display.SpaceDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a space was undeclared" });

    // 🔴 `66` §4 and §8: the display space is queried or declared and **never assumed to be the working space**.
    //    Accepting the two as one is the assumption defect itself — an image that is correct on the machine it
    //    was authored on and silently wrong everywhere else, and indistinguishable from a colour-managed path
    //    by looking at it.
    if (Encoding_.Working.SpaceIdentity == Encoding_.Display.SpaceIdentity)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the display space may not be the working space — `36` §9" });
    }

    Exposing         = Exposing_;
    Toning           = Toning_;
    Encodings        = Encoding_;
    MeteredExposure  = Exposing_.DeclaredExposure;
    MeteringCurrent = false;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DisplayProjection::Contribute(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = DisplayRecordingIdentity;

    Declared.Produces = { SharedTarget::DisplaySurface };
    Declared.Reads    = { SharedTarget::AccumulationSurface };
    Declared.Amends   = {};

    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";

    // 🔴 Declared scene-referred and **not** display-referred, even though it produces display code. The flag
    //    orders a recording *after* the tone line, and this recording **is** the tone line — declaring it
    //    display-referred would place it among the amenders of its own output.
    Declared.DisplayReferred  = false;
    Declared.AmendmentIndex = AmendmentIndex;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE METERING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DisplayProjection::AdvanceMetering(double ReducedLuminance, double ElapsedSeconds)
{
    if (Exposing.Source != ExposureSubject::Metered)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the exposure is declared rather than metered" });
    }

    if (!(ReducedLuminance > 0.0))
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a reduction of nothing names no exposure" });
    }

    // 📐 The exposure that would carry the reduction onto middle grey, in stops. Expressed logarithmically so
    //    that the adaptation below is linear in stops rather than in radiance — an adaptation linear in radiance
    //    crosses a bright scene in one step and a dark one in fifty.
    const double Target = std::log2(MiddleGreyLuminance / ReducedLuminance);

    const double Limit = Exposing.MeteredLimit;
    const double Bounded = Target < -Limit ? -Limit : (Target > Limit ? Limit : Target);

    if (!MeteringCurrent)
    {
        // 📝 The first advance arrives at its target rather than adapting toward it from the declared exposure.
        //    Adapting on the first rotation makes every document open at the wrong brightness and settle over
        //    a second, which reads as the application still loading.
        MeteredExposure  = Bounded;
        MeteringCurrent = true;

        return Deliver<bool>::Result(true);
    }

    const double Interval = Exposing.AdaptationSeconds;
    const double Fraction = ElapsedSeconds <= 0.0 ? 0.0 : 1.0 - std::exp(-ElapsedSeconds / Interval);

    MeteredExposure += (Bounded - MeteredExposure) * Fraction;

    return Deliver<bool>::Result(true);
}

double DisplayProjection::ExposureScale() const
{
    const double Current = Exposing.Source == ExposureSubject::Metered && MeteringCurrent
                          ? MeteredExposure
                          : Exposing.DeclaredExposure;

    return ProjectExposureScale(Current);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ColourSpecification> DisplayProjection::Project(const ColourSpecification& Accumulated) const
{
    if (!Accumulated.ColourDeclared())
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the accumulated radiance declares no space" });
    }

    if (Accumulated.SpaceIdentity != Encodings.Working.SpaceIdentity)
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the radiance is not a coordinate in the working space" });
    }

    // ① Exposure, in the working space, as a scale on radiance — `66` §2.
    const double Scale = ExposureScale();

    const double ExposedRed   = Accumulated.RedCoordinate   * Scale;
    const double ExposedGreen = Accumulated.GreenCoordinate * Scale;
    const double ExposedBlue  = Accumulated.BlueCoordinate  * Scale;

    // ② The compression, with the declared hue behaviour — `66` §3 and §3.1.
    double CompressedRed   = 0.0;
    double CompressedGreen = 0.0;
    double CompressedBlue  = 0.0;

    ProjectToneTriple(ExposedRed, ExposedGreen, ExposedBlue,
                      Toning.WhiteMagnitude,
                      Toning.PreservationBlend,
                      CompressedRed, CompressedGreen, CompressedBlue);

    ColourSpecification Compressed;
    Compressed.RedCoordinate   = CompressedRed;
    Compressed.GreenCoordinate = CompressedGreen;
    Compressed.BlueCoordinate  = CompressedBlue;
    Compressed.SpaceIdentity   = Encodings.Working.SpaceIdentity;

    // ③ Primaries and white adaptation, and ④ the transfer — both through `36`, which applies them in that order
    //    and applies the transfer exactly once. Doing either by hand here would be a second implementation of a
    //    conversion `02` §5 already declares as one component.
    if (Encodings.FormatCarriesTransfer)
    {
        // 🔴 `06` declared a presentation format that carries its own transfer, so ours is withheld. Applying
        //    both is the twice-encoded defect `66` §4 names, and it presents as an image that is merely a bit
        //    washed out — which is why it survives review on every project that has it.
        ColourSpaceSpecification Linearised = Encodings.Display;
        Linearised.Transfer                 = TransferSubject::Linear;

        return Slate::Project(Compressed, Encodings.Working, Linearised);
    }

    return Slate::Project(Compressed, Encodings.Working, Encodings.Display);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORTING
//------------------------------------------------------------------------------------------------------------------------

void DisplayProjection::Report(MeasureIndex& Measured, TickPoint Sampled) const
{
    Measured.DeclareMagnitude("66 §2 DisplayProjection", "Exposure",
                              Exposing.Source == ExposureSubject::Metered && MeteringCurrent
                                  ? MeteredExposure
                                  : Exposing.DeclaredExposure,
                              Sampled);

    Measured.DeclareMagnitude("66 §3 DisplayProjection", "WhiteMagnitude", Toning.WhiteMagnitude, Sampled);
    Measured.DeclareCount("66 §4 DisplayProjection", "DisplaySpace", Encodings.Display.SpaceIdentity, Sampled);
}

const ExposureSpecification& DisplayProjection::Exposure() const { return Exposing;  }
const ToneSpecification&     DisplayProjection::Tone() const     { return Toning;    }
const EncodeSpecification&   DisplayProjection::Encoding() const { return Encodings; }

}   // namespace Slate
