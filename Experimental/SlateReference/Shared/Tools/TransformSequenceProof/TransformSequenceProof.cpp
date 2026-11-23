//============================================================================================================================================
//                                                      TRANSFORMSEQUENCEPROOF.CPP
//============================================================================================================================================
// 🧩 Pins the keyboard grammar lifted out of `ParametricSketchHost` at step 10d.
//
// 🔴 The grammar was nine file-local functions interleaved with viewport drawing, so it could not be
//    exercised without a Vulkan device and was therefore never tested at all. These claims are what the
//    keys are supposed to mean, written from the artist's side: `G X 5` moves five along X, a bare `X`
//    does nothing, backspace walks out of a command one step at a time, and rotation takes no axis.
//
// ⚠️ Where the shipped grammar is surprising, the surprise is pinned rather than corrected — a minus sign
//    is accepted mid-run, and two G in a single frame count as a double tap. Both are recorded as claims
//    with the reason, so a later reader changes them deliberately or not at all.

#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace Slate;

namespace
{

int Failures = 0;
int Checks   = 0;

void Claim(bool Held, const std::string& Stated)
{
    ++Checks;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAIL  %s\n", Stated.c_str());
}

TransformCommandIntake Typed(const char* Keys, bool Engaged = false,
                             TransformManner Current = TransformManner::Move)
{
    return ResolveTransformCommand(Keys, static_cast<std::uint32_t>(std::strlen(Keys)), Engaged, Current);
}

std::string Reads(const TransformStanding& Standing)
{
    char Delivered[64] = {};
    FormatTransformCommand(Standing, Delivered, sizeof(Delivered));
    return Delivered;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            1. STARTING A MANIPULATION
//------------------------------------------------------------------------------------------------------------------------

void ProveStarting()
{
    std::printf("1. G, R and S start a manipulation; only when none is standing\n");

    Claim(Typed("g").StartRequested && Typed("g").StartManner == TransformManner::Move, "g starts a move");
    Claim(Typed("R").StartRequested && Typed("R").StartManner == TransformManner::Rotate, "R starts a rotate");
    Claim(Typed("s").StartRequested && Typed("s").StartManner == TransformManner::Scale, "s starts a scale");
    Claim(Typed("G").StartManner == Typed("g").StartManner, "case does not matter");

    // 🔴 A second manner letter while one is standing must NOT restart. An artist who types `g` then `s`
    //    mid-drag is reaching for scale, and silently restarting would discard the move already made.
    Claim(!Typed("s", true, TransformManner::Move).StartRequested,
          "s must not start anything while a move is standing");
    Claim(!Typed("r", true, TransformManner::Scale).StartRequested,
          "r must not start anything while a scale is standing");

    // Only the FIRST manner letter in a frame wins.
    const TransformCommandIntake Both = Typed("rs");
    Claim(Both.StartRequested && Both.StartManner == TransformManner::Rotate,
          "the first manner letter in a frame wins, not the last");

    Claim(!Typed("").StartRequested, "an empty frame asks for nothing");
    Claim(!Typed("qwety").StartRequested, "letters with no meaning ask for nothing");

    // ⚠️ THE GRAMMAR HAS NO CONCEPT OF A WORD. Every character in the frame is read on its own, so any
    //    text carrying a g, r or s starts a manipulation — "qwerty" starts a rotate. That is safe only
    //    because the viewport receives keystrokes and a text field consumes its own; it would become a
    //    defect the moment a frame could carry both. Pinned so the constraint is visible, since the host
    //    it came from stated it nowhere.
    Claim(Typed("qwerty").StartRequested && Typed("qwerty").StartManner == TransformManner::Rotate,
          "a stray r in ordinary text starts a rotate — the reader sees characters, not words");
}

//------------------------------------------------------------------------------------------------------------------------
//                                              2. RESTRICTING IT TO AN AXIS
//------------------------------------------------------------------------------------------------------------------------

void ProveRestricting()
{
    std::printf("2. X and Z restrict, but only once a manner is known\n");

    // 🔴 The ordering rule, which is the whole reason the reader carries a working manner separately from
    //    the standing one.
    Claim(!Typed("x").RestrictionRequested, "a bare x restricts nothing — no manner is standing");
    Claim(!Typed("z").RestrictionRequested, "a bare z restricts nothing");

    const TransformCommandIntake Started = Typed("gx");
    Claim(Started.StartRequested && Started.RestrictionRequested &&
          Started.Restriction == TransformRestriction::AxisX,
          "g then x in ONE frame starts a move and restricts it to X");

    Claim(Typed("x", true, TransformManner::Move).RestrictionRequested,
          "x restricts a move that is already standing");
    Claim(Typed("z", true, TransformManner::Scale).Restriction == TransformRestriction::AxisZ,
          "z restricts a standing scale to Z");

    // ⚠️ Rotation in a sketch plane turns about the plane NORMAL, so the two in-plane letters name no
    //    rotation and are refused. Y is the normal and is the one that means something.
    Claim(!Typed("x", true, TransformManner::Rotate).RestrictionRequested,
          "x must not restrict a rotation — it is an in-plane direction");
    Claim(!Typed("rz").RestrictionRequested,
          "z must not restrict a rotation started in the same frame");

    // 🔴 `R Y 35` IS THE ARTIST'S OWN EXAMPLE, and Y matched no branch: it fell past every test and
    //    vanished. The rotation then happened anyway — about the normal, because that is the only
    //    rotation a plane has — so it was right by accident and said nothing about the axis.
    const TransformCommandIntake Turned = Typed("ry35");
    Claim(Turned.StartRequested && Turned.StartManner == TransformManner::Rotate,
          "R Y 35 starts a rotation");
    Claim(Turned.RestrictionRequested && Turned.Restriction == TransformRestriction::AxisY,
          "and the Y is READ, naming the plane normal rather than being silently dropped");
    Claim(std::string(Turned.NumericAppend) == "35",
          "and the 35 reaches the numeric run past the axis letter");

    Claim(Typed("y", true, TransformManner::Rotate).Restriction == TransformRestriction::AxisY,
          "y restricts a rotation already standing");

    // 🔴 Y IS A TRANSLATION AXIS. These two claims used to demand the opposite, on the grounds that a
    //    planar sketch cannot travel along its own normal — true of a pair of plane coordinates, and no
    //    longer true of `WorldSketchStructure`, which holds real spatial points. `G Y 30` was reading the
    //    G, dropping the Y and spending the 30 on nothing, so the shape did not move at all.
    Claim(Typed("y", true, TransformManner::Move).RestrictionRequested &&
          Typed("y", true, TransformManner::Move).Restriction == TransformRestriction::AxisY,
          "y restricts a move to the plane normal, lifting the sketch off its plane");
    Claim(Typed("y", true, TransformManner::Scale).RestrictionRequested &&
          Typed("y", true, TransformManner::Scale).Restriction == TransformRestriction::AxisY,
          "and a scale");

    // 📝 The artist's own spelling: the axis letter must not swallow the distance behind it.
    const TransformCommandIntake Lifted = Typed("gy30");
    Claim(Lifted.StartRequested && Lifted.StartManner == TransformManner::Move,
          "G Y 30 starts a move");
    Claim(Lifted.RestrictionRequested && Lifted.Restriction == TransformRestriction::AxisY,
          "and the Y is read as the plane normal");
    Claim(std::string(Lifted.NumericAppend) == "30",
          "and the 30 reaches the numeric run past the axis letter");

    // The last axis letter wins, so an artist correcting X to Z gets Z.
    Claim(Typed("xz", true, TransformManner::Move).Restriction == TransformRestriction::AxisZ,
          "the last axis letter in a frame wins");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                3. TYPING AN EXACT AMOUNT
//------------------------------------------------------------------------------------------------------------------------

void ProveNumeric()
{
    std::printf("3. Digits accumulate into an exact amount\n");

    Claim(std::string(Typed("g25").NumericAppend) == "25", "digits are taken alongside a manner letter");
    Claim(std::string(Typed("gx1.5").NumericAppend) == "1.5", "a decimal point is numeric");
    Claim(std::string(Typed("-4").NumericAppend) == "-4", "a minus sign is numeric");

    // ⚠️ Pinned, not endorsed: a minus is accepted anywhere in the run. `strtod` then stops at it, so the
    //    amount truncates rather than the keystroke being refused. This is what shipped.
    Claim(std::string(Typed("1-2").NumericAppend) == "1-2", "a minus mid-run is taken, and truncates later");

    Claim(std::string(Typed("gabc").NumericAppend).empty(), "letters are not numeric");

    char Run[TransformNumericLimit] = {};
    AppendTransformNumericRun(Run, sizeof(Run), "12");
    AppendTransformNumericRun(Run, sizeof(Run), "3");
    Claim(std::string(Run) == "123", "runs accumulate across frames");
    AppendTransformNumericRun(Run, sizeof(Run), "x9");
    Claim(std::string(Run) == "1239", "non-numeric characters are dropped, not refused");

    // 🔴 Overrun must terminate rather than run off the end.
    char Small[4] = {};
    AppendTransformNumericRun(Small, sizeof(Small), "123456789");
    Claim(std::strlen(Small) == 3u, "an over-long run is truncated to the extent");
    Claim(Small[3] == '\0', "and stays terminated");

    TransformStanding Standing;
    double Value = 0.0;
    Claim(!ResolveNumericOverride(Standing, Value), "nothing typed means no override");
    std::snprintf(Standing.Numeric, sizeof(Standing.Numeric), "7.25");
    Claim(ResolveNumericOverride(Standing, Value) && Value > 7.24 && Value < 7.26, "7.25 reads as 7.25");
    std::snprintf(Standing.Numeric, sizeof(Standing.Numeric), "-");
    Claim(!ResolveNumericOverride(Standing, Value), "a lone minus is not a number");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                4. BACKSPACE WALKS BACK OUT
//------------------------------------------------------------------------------------------------------------------------

void ProveRetracting()
{
    std::printf("4. Backspace gives back the last thing given, in order\n");

    TransformStanding Standing;
    Standing.Manner      = TransformManner::Move;
    Standing.Restriction = TransformRestriction::AxisX;
    std::snprintf(Standing.Numeric, sizeof(Standing.Numeric), "50");

    Claim(Reads(Standing) == "G X 50", "the command reads as typed");

    RetractTransformCommand(Standing);
    Claim(Reads(Standing) == "G X 5", "the amount goes one character at a time");
    RetractTransformCommand(Standing);
    Claim(Reads(Standing) == "G X", "then the amount is gone");

    // 🔴 The restriction goes only once the amount has, never before.
    Claim(Standing.Restriction == TransformRestriction::AxisX, "the restriction survives while digits remain");
    RetractTransformCommand(Standing);
    Claim(Reads(Standing) == "G" && Standing.Restriction == TransformRestriction::Free,
          "then the restriction goes");

    // ⚠️ It never abandons the manipulation itself — that is escape's job.
    RetractTransformCommand(Standing);
    Claim(Reads(Standing) == "G", "retracting past the start leaves the manipulation standing");

    TransformStanding Screened;
    Screened.Restriction = TransformRestriction::Screen;
    RetractTransformCommand(Screened);
    Claim(Screened.Restriction == TransformRestriction::Screen,
          "a screen restriction is not retracted — the artist never asked for it");

    TransformStanding Cleared;
    std::snprintf(Cleared.Numeric, sizeof(Cleared.Numeric), "99");
    ClearTransformNumeric(Cleared);
    Claim(std::string(Cleared.Numeric).empty(), "clearing forgets the whole amount at once");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            5. THE DOUBLE TAP THAT SLIDES
//------------------------------------------------------------------------------------------------------------------------

void ProveSliding()
{
    std::printf("5. A second G slides along the curve\n");

    Claim(Typed("gg").MoveTapCount == 2u, "two G in one frame count twice");
    Claim(Typed("g").MoveTapCount == 1u, "one G counts once");
    Claim(Typed("ggg").MoveTapCount == 3u, "every G is counted, not just the first two");

    // ⚠️ Both routes to the gesture. A slow frame can carry both taps, and dropping that case would make
    //    the gesture fail exactly when the machine is struggling.
    Claim(ResolveSlideRequested(2u, 1000.0, 0.0, true), "two taps in one frame slide, however long ago the last was");
    Claim(ResolveSlideRequested(1u, 1000.0, 800.0, true), "one tap within 350ms of the last slides");
    Claim(!ResolveSlideRequested(1u, 1000.0, 600.0, true), "one tap after 350ms does not");
    Claim(!ResolveSlideRequested(0u, 1000.0, 900.0, true), "no tap does not slide");

    // 🔴 A selection not on a curve cannot slide along one, however it is asked for.
    Claim(!ResolveSlideRequested(2u, 1000.0, 999.0, false), "no curve means no slide, even on a double tap");

    TransformStanding Sliding;
    Sliding.Manner          = TransformManner::Move;
    Sliding.SlideAlongCurve = true;
    Claim(Reads(Sliding) == "G G", "a slide reads as the keys that made it");
    std::snprintf(Sliding.Numeric, sizeof(Sliding.Numeric), "12");
    Claim(Reads(Sliding) == "G G 12", "with its amount");

    // A slide is a manner of moving, so it does not apply to rotate or scale.
    TransformStanding Scaling;
    Scaling.Manner          = TransformManner::Scale;
    Scaling.SlideAlongCurve = true;
    Claim(Reads(Scaling) == "S", "a scale never reads as a slide");
}

//------------------------------------------------------------------------------------------------------------------------
//                                              6. WHAT THE READOUT SAYS
//------------------------------------------------------------------------------------------------------------------------

void ProveReadout()
{
    std::printf("6. The readout matches the keys, and every manner has a word\n");

    TransformStanding Standing;
    Claim(Reads(Standing) == "G", "a bare move reads as G");
    Standing.Manner = TransformManner::Rotate;
    Claim(Reads(Standing) == "R", "a rotate reads as R");
    Standing.Manner = TransformManner::Scale;
    Claim(Reads(Standing) == "S", "a scale reads as S");

    // 📝 Free and Screen are the absence of a restriction; spelling them out would say nothing.
    Standing.Manner      = TransformManner::Move;
    Standing.Restriction = TransformRestriction::Free;
    Claim(Reads(Standing) == "G", "an unrestricted move shows no restriction");
    Standing.Restriction = TransformRestriction::Screen;
    Claim(Reads(Standing) == "G", "a screen restriction is not shown either");

    // 🔴 THE READOUT IS HOW THE ARTIST KNOWS THE AXIS WAS HEARD. `R Y 35` showed "R 35" while turning
    //    about Y anyway, which is indistinguishable from the axis having been ignored.
    Standing.Manner      = TransformManner::Rotate;
    Standing.Restriction = TransformRestriction::AxisY;
    Claim(Reads(Standing) == "R Y", "a rotation about the normal says so");
    std::snprintf(Standing.Numeric, sizeof(Standing.Numeric), "35");
    Claim(Reads(Standing) == "R Y 35", "and reads back the artist's whole command");
    Standing.Numeric[0] = '\0';
    Standing.Restriction = TransformRestriction::Free;

    Claim(std::string(TransformMannerText(TransformManner::Move)) == "Move", "Move has a word");
    Claim(std::string(TransformMannerText(TransformManner::Rotate)) == "Rotate", "Rotate has a word");
    Claim(std::string(TransformMannerText(TransformManner::Scale)) == "Scale", "Scale has a word");
    Claim(std::string(TransformRestrictionText(TransformRestriction::Curve)) == "Curve", "Curve has a word");

    // 🔴 Writing into no extent at all must not write.
    char None[1] = { 'Z' };
    FormatTransformCommand(Standing, None, 0u);
    Claim(None[0] == 'Z', "a zero extent is not written to");

    char Tight[3] = {};
    TransformStanding Long;
    Long.Restriction = TransformRestriction::AxisX;
    std::snprintf(Long.Numeric, sizeof(Long.Numeric), "12345");
    FormatTransformCommand(Long, Tight, sizeof(Tight));
    Claim(Tight[2] == '\0', "a tight extent stays terminated");
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== TransformSequence — the keyboard grammar ===\n\n");

    ProveStarting();
    ProveRestricting();
    ProveNumeric();
    ProveRetracting();
    ProveSliding();
    ProveReadout();

    std::printf("\n%d claims, %d failures\n", Checks, Failures);
    std::printf(Failures == 0 ? "PROVEN\n\n" : "REFUTED\n\n");
    return Failures == 0 ? 0 : 1;
}
