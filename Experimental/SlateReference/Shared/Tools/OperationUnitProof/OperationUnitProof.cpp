//============================================================================================================================================
//                                                        OPERATIONUNITPROOF.CPP
//============================================================================================================================================
// 🧩 The readout seam of the sketch operations: the sessions beneath work in MILLIMETRES and know nothing
//    else; the artist works in whatever unit the panel says. This proves the two directions of that
//    conversion are exact inverses, that the slider's RANGE travels with the figure, and that the
//    declared precision is enough to see the value change.
//
// 🔴 WHY THIS EXISTS. The reported defect was "the slider is too conservative -- I could be working in
//    metres but you give me a small mm slider". Both halves of that were real and they are different
//    bugs. The figure was published raw, so a 500 mm radius read "500" in a box labelled mm whatever the
//    artist had chosen; and the RANGE was left in millimetres while the reading was not, so even once the
//    number was converted the slider ran to 500 while the value could only reach 0.5 -- the whole travel
//    bunched into the first thousandth of the track.
//
// 📝 The conversion is modelled here exactly as `SketchOperationDriver` performs it, rather than by
//    driving the driver: the readout needs a live `RecordingSurface` and an ImGui context, neither of
//    which exists headlessly. What CAN be proven headlessly is the arithmetic, which is the whole of the
//    defect -- and it is proven against `Foundation/MeasureDisplay.h` itself, not against a copy of it.

#include "Foundation/MeasureDisplay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

using namespace Slate;

std::uint32_t Claims   = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* What)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("    FAIL  %s\n", What);
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-9)
{
    return std::abs(Left - Right) <= Tolerance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SEAM ITSELF
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the readout row carries, as `DriveSketchOperations` fills it in.
struct ReadoutRow
{
    float         Figure  = 0.0f;
    float         Minimum = 0.0f;
    float         Maximum = 0.0f;
    const char*   Unit    = "";
    std::uint32_t Places  = 2u;
};

/// 🧩 The publish direction: a session figure and its clamp, as the artist is shown them.
/// 📝 Mirrors the driver line for line -- figure through `ToDisplay`, unit through `MeasureUnitSuffix`,
///    limit through `ToDisplay` as well, places through `MeasureUnitPlaces`.
ReadoutRow Publish(double SessionFigureMillimetres,
                   double SessionLimitMillimetres,
                   MeasureUnit Unit,
                   bool Offset)
{
    ReadoutRow Row;
    Row.Figure = static_cast<float>(ToDisplay(SessionFigureMillimetres, Unit));
    Row.Unit   = MeasureUnitSuffix(Unit);
    Row.Places = MeasureUnitPlaces(Unit);

    const double ShownLimit = ToDisplay(SessionLimitMillimetres, Unit);
    Row.Minimum = Offset ? -static_cast<float>(ShownLimit) : 0.0f;
    Row.Maximum = static_cast<float>(ShownLimit > 0.0 ? ShownLimit : ToDisplay(1.0, Unit));
    return Row;
}

/// 🧩 The accept direction: what the session is asked for when the artist types a figure.
double Accept(float Typed, MeasureUnit Unit)
{
    return ToMillimetres(static_cast<double>(Typed), Unit);
}

/// 🧩 What the value pill actually renders, so the precision claim tests the drawn string.
void Rendered(float Value, std::uint32_t Places, char* Written, std::size_t Room)
{
    const int Held = static_cast<int>(Places > 6u ? 6u : Places);
    std::snprintf(Written, Room, "%.*f", Held, static_cast<double>(Value));
}

const MeasureUnit EveryUnit[4] = { MeasureUnit::Millimetre, MeasureUnit::Centimetre,
                                   MeasureUnit::Metre, MeasureUnit::Inch };

//------------------------------------------------------------------------------------------------------------------------

void ProveTheFigureIsShownInTheArtistsUnit()
{
    std::printf("\n1. The figure is shown in the artist's unit, not always in millimetres\n");

    // A 500 mm radius -- a half-metre fillet, the scale the defect was reported at.
    Claim(Near(Publish(500.0, 1000.0, MeasureUnit::Millimetre, false).Figure, 500.0, 1.0e-4),
          "500 mm shows as 500 with millimetres selected");
    Claim(Near(Publish(500.0, 1000.0, MeasureUnit::Centimetre, false).Figure, 50.0, 1.0e-4),
          "and as 50 with centimetres");
    Claim(Near(Publish(500.0, 1000.0, MeasureUnit::Metre, false).Figure, 0.5, 1.0e-6),
          "and as 0.5 with metres -- it used to show 500 whatever the panel said");
    Claim(Near(Publish(508.0, 1000.0, MeasureUnit::Inch, false).Figure, 20.0, 1.0e-4),
          "and 508 mm as exactly 20 inches");
}

void ProveTheUnitCellNamesTheUnit()
{
    std::printf("\n2. The unit cell beside the value says which unit that is\n");

    Claim(std::strcmp(Publish(1.0, 1.0, MeasureUnit::Millimetre, false).Unit, "mm") == 0, "mm");
    Claim(std::strcmp(Publish(1.0, 1.0, MeasureUnit::Centimetre, false).Unit, "cm") == 0, "cm");
    Claim(std::strcmp(Publish(1.0, 1.0, MeasureUnit::Metre, false).Unit, "m") == 0,
          "m -- the cell was hardcoded to mm, so it contradicted the number beside it");
    Claim(std::strcmp(Publish(1.0, 1.0, MeasureUnit::Inch, false).Unit, "in") == 0, "in");
}

void ProveTheRangeTravelsWithTheFigure()
{
    std::printf("\n3. THE REPORTED DEFECT: the slider's range must convert with the value\n");

    // 🔴 The corner's limit is half the shorter leg. On a half-metre part that is 500 mm.
    const double LimitMillimetres = 500.0;

    // What the row used to be: the reading converted, the range left in millimetres.
    const float StaleMaximum = static_cast<float>(LimitMillimetres);
    const ReadoutRow Corrected = Publish(250.0, LimitMillimetres, MeasureUnit::Metre, false);

    Claim(Near(StaleMaximum, 500.0, 1.0e-6),
          "left unconverted, a metre slider ran to 500");
    Claim(Near(Corrected.Maximum, 0.5, 1.0e-6),
          "converted, it runs to 0.5 -- which is the largest radius the corner will actually accept");

    // 📐 How much of the track the artist's value can reach. This is the "too conservative" complaint
    //    stated as a number: the usable travel was under a thousandth of the control.
    const double StaleFraction     = 0.25 / static_cast<double>(StaleMaximum);
    const double CorrectedFraction = static_cast<double>(Corrected.Figure)
                                   / static_cast<double>(Corrected.Maximum);

    Claim(StaleFraction < 0.001,
          "so the value sat in the first thousandth of the track: unusable as a control");
    Claim(Near(CorrectedFraction, 0.5, 1.0e-6),
          "and now sits at exactly half of it, where it belongs");

    // ⚠️ The limit must never be crossed by the conversion: what the slider offers is what the gesture
    //    will accept, in every unit.
    for (const MeasureUnit Unit : EveryUnit)
    {
        const ReadoutRow Row = Publish(0.0, LimitMillimetres, Unit, false);
        Claim(Near(Accept(Row.Maximum, Unit), LimitMillimetres, 1.0e-3),
              "the slider's top of travel accepts back to exactly the session's limit");
    }
}

void ProveTheTypedFigureReturnsExactly()
{
    std::printf("\n4. A typed figure returns to millimetres exactly, in every unit\n");

    for (const MeasureUnit Unit : EveryUnit)
    {
        const double Stored = 137.5;
        const float  Shown  = static_cast<float>(ToDisplay(Stored, Unit));
        Claim(Near(Accept(Shown, Unit), Stored, 1.0e-3),
              "a figure shown and then accepted is the figure that was stored");
    }

    Claim(Near(Accept(0.05f, MeasureUnit::Metre), 50.0, 1.0e-6),
          "typing 0.05 with metres showing asks the session for 50 mm, not 0.05");
    Claim(Near(Accept(2.0f, MeasureUnit::Inch), 50.8, 1.0e-6),
          "and typing 2 with inches showing asks for 50.8 mm");
}

void ProveAnOffsetsNegativeSideConvertsToo()
{
    std::printf("\n5. An Offset runs both ways, and both ways convert\n");

    const ReadoutRow Row = Publish(0.0, 500.0, MeasureUnit::Metre, true);
    Claim(Near(Row.Minimum, -0.5, 1.0e-6) && Near(Row.Maximum, 0.5, 1.0e-6),
          "the range is symmetric about zero in the shown unit");
    Claim(Near(Accept(Row.Minimum, MeasureUnit::Metre), -500.0, 1.0e-3),
          "and the inward extreme accepts back to the full negative limit");
}

void ProveThePrecisionIsEnoughToSeeTheValue()
{
    std::printf("\n6. The declared precision is enough to see the value change\n");

    // 🔴 A five-millimetre fillet is an ordinary thing to want. At two decimal places -- which every
    //    slider used to be fixed at -- it is indistinguishable from nothing once metres are showing.
    char Written[32] = {};

    Rendered(static_cast<float>(ToDisplay(5.0, MeasureUnit::Metre)), 2u, Written, sizeof(Written));
    Claim(std::strcmp(Written, "0.00") == 0,
          "at the old fixed two places, a 5 mm fillet in metres rendered as 0.00 -- indistinguishable "
          "from no fillet at all");

    const ReadoutRow Row = Publish(5.0, 500.0, MeasureUnit::Metre, false);
    Rendered(Row.Figure, Row.Places, Written, sizeof(Written));
    Claim(std::strcmp(Written, "0.005") == 0,
          "at the unit's declared places it renders as 0.005, which is the actual value");

    // 📝 And millimetres are unchanged, so nothing that already read well now reads worse.
    const ReadoutRow Plain = Publish(5.0, 500.0, MeasureUnit::Millimetre, false);
    Rendered(Plain.Figure, Plain.Places, Written, sizeof(Written));
    Claim(std::strcmp(Written, "5.00") == 0, "and millimetres still render exactly as they did");

    // ⚠️ The clamp on places must hold, so a caller cannot overrun the pill.
    Rendered(1.0f, 99u, Written, sizeof(Written));
    Claim(std::strlen(Written) < 16u, "an absurd precision is held to something the field can show");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("OPERATION UNIT PROOF\n");
    std::printf("=========================================================================\n");

    ProveTheFigureIsShownInTheArtistsUnit();
    ProveTheUnitCellNamesTheUnit();
    ProveTheRangeTravelsWithTheFigure();
    ProveTheTypedFigureReturnsExactly();
    ProveAnOffsetsNegativeSideConvertsToo();
    ProveThePrecisionIsEnoughToSeeTheValue();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
