//============================================================================================================================================
//                                                           DISPLAYPROJECTION.H
//============================================================================================================================================
// 🧩 `66` — exposure on radiance, one monotonic compression, then primaries and transfer through `36` exactly once.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/ToneProjection.slang.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EXPOSURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where the standing exposure comes from — the artist, or the reduction the metering measured.
/// note  🔴 `66` §2: the two are alternatives and never a sum. An authored exposure that the metering also
///        adapts is one the artist sets and watches drift back, and nothing on the display says which of the
///        two moved it.
/// tag   guarantee
enum class ExposureSubject : std::uint32_t
{
    Declared      = 0u,   // [-] - the artist's own value, held exactly as authored
    Metered       = 1u,   // [-] - driven by the measured reduction, adapting over the declared interval
    ExposureCount = 2u    // [-] - the closed count, never a subject
};

/// 🧩 What the exposure is, and — where it is metered — how fast it may move.
/// note  🚧 The interval and the ceiling are `66` §9's open rows and each blocks tuning alone. They are declared
///        here rather than in `Foundation/` because no second unit reads either — `00` §2's rule.
/// tag   nonallocating, nonthrowing
struct ExposureSpecification
{
    ExposureSubject  Source            = ExposureSubject::Declared;   // [-]  - which of the two drives it
    double           DeclaredExposure  = 0.0;                         // [EV] - the artist's value; a doubling per stop
    double           AdaptationSeconds = 0.4;                         // [s]  - the metered adaptation's time constant
    double           MeteredLimit    = 8.0;                         // [EV] - how far the metering may travel either way
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE COMPRESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the unbounded scene radiance is compressed into the display's range by.
/// note  🔴 The white magnitude is a **scene** magnitude and not a display one — `66` §3. It names the radiance
///        that reaches full display code, so lowering it brightens the image; reading it as a display ceiling
///        inverts the control the artist is given.
/// note  🚧 Both figures are `66` §9's open rows. The blend is exposed because full hue preservation keeps
///        saturation all the way to clipping, which reads as a hard-edged bloom rather than as a bright highlight.
/// tag   nonallocating, nonthrowing
struct ToneSpecification
{
    double  WhiteMagnitude    = 6.0;   // [-] - the scene magnitude mapping to full display code
    double  PreservationBlend = 0.4;   // [-] - nought compresses per channel; one preserves hue entirely
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ENCODING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The two spaces the projection crosses, and whether the presentation format already carries a transfer.
/// note  🔴 `66` §4 and §8: the display space is queried or declared and **never assumed to be the working
///        space**. `Declare` refuses the two as one, because an image authored under that assumption is correct
///        on the machine it was authored on and silently wrong everywhere else.
/// note  🔴 `FormatCarriesTransfer` is read from `06`'s presentation format and never guessed. A format that
///        applies its own transfer, encoded again here, is the twice-encoded defect `66` §4 names — and it
///        presents as an image that is merely a bit washed out, which is why it survives review.
/// tag   nonallocating, nonthrowing
struct EncodeSpecification
{
    ColourSpaceSpecification  Working               = DeclaredWorkingSpace();   // [-] - wide and linear
    ColourSpaceSpecification  Display               = DeclaredDisplaySpace();   // [-] - the machine's own
    bool                      FormatCarriesTransfer = false;                    // [-] - `06` declared it, not this
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `66` — the recording, the metering that adapts in stops, and the four ordered operations of `08` §3 ⑧.
/// note  🔴 The order is exposure, compression, primaries, transfer — and it is the whole content of `66`.
///        Exposure applied after the compression is a scale on display code, which brightens a compressed image
///        rather than exposing a scene and never recovers highlight detail.
/// note  🔴 The primaries and the transfer are applied by `36`'s own `Project` and are never re-derived here.
///        Writing either by hand would be a second implementation of a conversion `02` §5 already declares as
///        one component, and the two would agree until one of them was amended.
/// note  🔴 The compression is **not invertible**, which is why `36` §6 samples a colour scene-referred, above
///        this. No correction applied to display code recovers what the compression discarded.
/// tag   owning
class DisplayProjection
{
public:

    // 📝 `08` §3 ⑧, after `64`'s accumulation at 40. The recording is the tone line itself, so nothing
    //    scene-referred may be ordered after it and everything display-referred must be.
    static constexpr std::uint32_t AmendmentIndex = 50u;   // [-] - `08` §3 ⑧

    /// 🧩 Declares the exposure, the compression and the two spaces as one admission.
    /// in    Exposing_  [-]  which of the two subjects drives the exposure, and its bounds
    /// in    Toning_    [-]  the scene white and the hue behaviour
    /// in    Encoding_  [-]  the working and display spaces, and what the format already carries
    /// out   Result    [-]  refuses with ContentUnsupported for a white magnitude of nothing, a blend outside
    ///                       the unit interval, a metered exposure with no adaptation interval, an undeclared
    ///                       space, and a display space that is the working space
    /// post  the metered exposure stands at the declared one and the metering has not yet adapted
    /// note  🔴 Rejected in full and never in part. A half-accepted declaration leaves the projection crossing
    ///        one validated space and one that was rejected, which is indistinguishable from a colour-managed
    ///        path by looking at it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(const ExposureSpecification& Exposing_,
                          const ToneSpecification&     Toning_,
                          const EncodeSpecification&   Encoding_);

    /// 🧩 Contributes `08` §3 ⑧'s recording.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// note  🔴 Declared scene-referred although it produces display code. The flag orders a recording *after*
    ///        the tone line, and this recording **is** the tone line — declaring it display-referred would place
    ///        it among the amenders of its own output.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 Advances the metered exposure one interval toward the reduction just measured.
    /// in    ReducedLuminance  [-]  the reduction `86`'s tick measured over the accumulated radiance
    /// in    ElapsedSeconds    [s]  since the previous advance
    /// out   Result           [-]  refuses with ContentUnsupported for a declared exposure and for a reduction
    ///                              of nothing
    /// note  📐 The adaptation is linear **in stops** and not in radiance. An adaptation linear in radiance
    ///        crosses a bright scene in one step and a dark one in fifty, which reads as the metering being
    ///        broken in exactly half of the documents an artist opens.
    /// note  📝 The first advance arrives at its target rather than adapting toward it, so a document does not
    ///        open at the wrong brightness and settle over a second — which reads as the application still
    ///        loading rather than as the metering working.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> AdvanceMetering(double ReducedLuminance, double ElapsedSeconds);

    /// 🧩 The linear scale the standing exposure applies to radiance.
    /// out   Scale  [-]  a doubling per stop, through `Shared/`'s own routine
    /// note  📝 The standing exposure is the metered one only where the source is metered **and** the metering
    ///        has advanced at least once. Before that it is the declared value, so nothing reads an exposure
    ///        no measurement has yet produced.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double ExposureScale() const;

    /// 🧩 Projects one accumulated radiance into display code — the four ordered operations, once each.
    /// in    Accumulated  [-]  scene-referred, in the working space, unbounded above
    /// out   Result      [-]  refuses with ContentUnsupported for an undeclared colour and for one that is not
    ///                         a coordinate in the declared working space, and with whatever `36` rejected
    /// note  🔴 The incoming space is compared rather than assumed. A radiance that arrived in another space is
    ///        compressed against a white magnitude that means nothing in it, and the result is plausible.
    /// note  🔴 Where the presentation format carries its own transfer, the display space is linearised before
    ///        `36` is called, so ours is withheld rather than applied twice — `66` §4.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<ColourSpecification> Project(const ColourSpecification& Accumulated) const;

    /// 🧩 Declares every measure; appends nothing.
    /// note  🔴 `66` appears in no row of `86` §4's register. An exposure that adapts is the metering working,
    ///        and reporting each advance would mean the register is never quiet.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Measured, TickPoint Sampled) const;

    const ExposureSpecification& Exposure() const;
    const ToneSpecification&     Tone() const;
    const EncodeSpecification&   Encoding() const;

private:

    ExposureSpecification  Exposing         = {};      // [-]  - as Declare validated it
    ToneSpecification      Toning           = {};      // [-]  - as Declare validated it
    EncodeSpecification    Encodings        = {};      // [-]  - the two spaces the projection crosses
    double                 MeteredExposure  = 0.0;     // [EV] - what the metering has adapted to
    bool                   MeteringCurrent = false;   // [-]  - false until one advance has measured
};

// 📐 The space comparison and the exposure subject are Exact; the exposure scale, the compression and `36`'s
//    conversion are Bounded. `DisplaySurface` is Tier D and the accumulated radiance arrives Perceptual, so the
//    component claims Perceptual — `00` §3's transitivity rule folds to the weakest.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
