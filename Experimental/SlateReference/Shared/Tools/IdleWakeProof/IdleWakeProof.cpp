//============================================================================================================================================
//                                                          IDLEWAKEPROOF.CPP
//============================================================================================================================================
// ⏱️ THE EDITOR MUST NOT PRESENT AN IMAGE NOBODY ASKED FOR.
//
// 🔴 It did, sixty times a second, forever. Under FIFO pacing the host rebuilt the whole interface and
//    presented a fresh image every tick whether or not one pixel differed, which measured at 8 to 9% of a
//    core with the artist's hands off the input. `RedrawScheduler` was written for exactly this, carries
//    the wake rule in `Waking`, is owned by `ViewportSequence` — and was read by nobody. The marks were
//    raised faithfully every tick and never once asked about.
//
// 🔴 THE SKETCH GEOMETRY WAS NOT THE COST, AND THIS PROOF RECORDS THAT SO NOBODY OPTIMISES IT AGAIN.
//    Projecting and tessellating a 240-curve sketch measures 162 microseconds a frame — under 1% of a
//    core at sixty frames a second. Measured before anything was changed. The expense was never the
//    shapes; it was rebuilding and presenting an interface that had not changed.
//
// 📝 The wake rule itself is checked here rather than the host loop, because the rule is the part that can
//    be wrong in a way nobody notices: a rule that always wakes costs the idle, and a rule that wrongly
//    sleeps freezes the window. Both directions are claimed.

#include "SlateUI/Interface/RedrawScheduler/Api/RedrawScheduler.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

// 📝 The wiring claims read the source text, so the proof needs the repository root. It is run from
//    there by its own runner, which is why a plain relative path is enough.
std::string ReadWhole(const char* Path)
{
    std::ifstream Stream(Path);
    if (!Stream)
        return std::string();

    std::ostringstream Gathered;
    Gathered << Stream.rdbuf();
    return Gathered.str();
}

void Require(bool Held, const char* Naming)
{
    ++Claims;
    if (Held)
        return;
    ++Failures;
    std::printf("  FAILED  %s\n", Naming);
}

}   // namespace

int main()
{
    using namespace Slate;

    std::printf("[IdleWakeProof]\n");

    // ① A REGISTERED PANEL ARRIVES MARKED, so the first tick is never slept through.
    {
        RedrawScheduler Scheduler;
        const Deliver<std::uint32_t> Enrolled = Scheduler.Register("Viewport");
        Require(Enrolled.Resolved, "a panel enrols");
        Require(Scheduler.Marked(), "a freshly registered panel stands marked");
        Require(Scheduler.Waking(false, false),
                "so a tick that registers a panel is never slept through");
    }

    // ② THE RULE SLEEPS ONLY WHEN ALL THREE OPERANDS ARE QUIET.
    {
        RedrawScheduler Scheduler;
        const Deliver<std::uint32_t> Enrolled = Scheduler.Register("Viewport");
        Require(Enrolled.Resolved, "a panel enrols");
        Scheduler.Retire();

        Require(!Scheduler.Waking(false, false),
                "nothing marked, nothing moving, nothing arrived: the host must sleep");

        // 🔴 EACH OPERAND ALONE MUST WAKE IT. A rule missing one of the three produces a panel that
        //    freezes mid-transition and resumes when the artist happens to move the pointer, which
        //    gets attributed to the input device rather than to the rule.
        Require(Scheduler.Waking(true, false),
                "a settling spring wakes the host on its own");
        Require(Scheduler.Waking(false, true),
                "an arrival wakes the host on its own");

        Scheduler.Mark(Enrolled.Delivered, RedrawMark::Recolour);
        Require(Scheduler.Waking(false, false),
                "a raised mark wakes the host on its own");
    }

    // ③ A MARK SURVIVES UNTIL IT IS RETIRED, so a change cannot be slept through before it is drawn.
    {
        RedrawScheduler Scheduler;
        const Deliver<std::uint32_t> Enrolled = Scheduler.Register("Viewport");
        Require(Enrolled.Resolved, "a panel enrols");
        Scheduler.Retire();
        Require(!Scheduler.Waking(false, false), "quiet after retirement");

        Scheduler.Mark(Enrolled.Delivered, RedrawMark::Rearrange);
        Require(Scheduler.Waking(false, false), "marked, so waking");
        Require(Scheduler.Waking(false, false),
                "and STILL waking when asked again, because asking is not retiring");

        Scheduler.Retire();
        Require(!Scheduler.Waking(false, false), "and quiet once the content has been sealed");
    }

    // ④ THE DEARER MARK WINS, which is what keeps a hover from cancelling a re-solve.
    {
        RedrawScheduler Scheduler;
        const Deliver<std::uint32_t> Enrolled = Scheduler.Register("Viewport");
        Require(Enrolled.Resolved, "a panel enrols");
        Scheduler.Retire();

        Scheduler.Mark(Enrolled.Delivered, RedrawMark::Rearrange);
        Scheduler.Mark(Enrolled.Delivered, RedrawMark::Recolour);
        Require(Scheduler.Current(Enrolled.Delivered) == RedrawMark::Rearrange,
                "a cheap mark arriving after a dear one does not lower it");
    }

    // ⑤ EVERY PANEL WAKES TOGETHER when the display or the appearance moves under all of them.
    {
        RedrawScheduler Scheduler;
        const Deliver<std::uint32_t> First  = Scheduler.Register("North");
        const Deliver<std::uint32_t> Second = Scheduler.Register("South");
        Require(First.Resolved && Second.Resolved, "two panels enrol");
        Scheduler.Retire();
        Require(!Scheduler.Waking(false, false), "both quiet");

        Scheduler.MarkEvery(RedrawMark::Rearrange);
        Require(Scheduler.Current(First.Delivered) == RedrawMark::Rearrange
             && Scheduler.Current(Second.Delivered) == RedrawMark::Rearrange,
                "a resize marks every registered panel, not the one that noticed");
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    //  ⑥ THE WIRING, NOT THE RULE.
    //
    //  🔴 EVERY CLAIM ABOVE PASSED WHILE THE EDITOR WAS UNUSABLE. The rule was correct in isolation and
    //     wrong in place, twice over, and a unit proof that only ever calls the rule cannot see either:
    //
    //       ① `Waking` was handed the interface's OWN pointer record, which `ImGui::NewFrame` fills —
    //          and `NewFrame` runs inside `Advance`, AFTER the wake question. So the rule was asked
    //          "did anything happen during the last frame we drew?". Once the startup eases settled the
    //          record froze, the answer was "no" forever, and the host slept through every later click.
    //
    //       ② The idle path then called `Complete`, which opens the display scope the host never opened
    //          — and that scope carries `LOAD_OP_CLEAR`. Surrendering an unrecorded tick PRESENTED THE
    //          CLEAR GROUND. The window wiped itself to the clear ink every idle tick and stayed there
    //          with its chrome alive: the editor flashed the viewport once and went black.
    //
    //  📝 Both are questions about which call sits before which, so they are claimed against the source
    //     text. A behavioural proof would need a device, a window and an artist not touching the mouse.
    // ─────────────────────────────────────────────────────────────────────────────────────────────────
    {
        const std::string Session  = ReadWhole("Engine/SlateRuntime/Session/SessionSequence/Source/SessionSequence.cpp");
        const std::string Viewport = ReadWhole("Engine/SlateUI/Interface/ViewportSequence/Source/ViewportSequence.cpp");

        Require(!Session.empty() && !Viewport.empty(), "the two wiring sources are readable");

        // ① THE WAKE QUESTION IS ASKED BEFORE THE TICK IS OPENED.
        const std::size_t AskedAt   = Session.find("Viewport.Waking(");
        const std::size_t AcquireAt = Session.find("Lifetime.Await(Ground)");

        Require(AskedAt != std::string::npos, "the session asks the wake rule at all");
        Require(AcquireAt != std::string::npos, "the session opens a tick");
        Require(AskedAt < AcquireAt,
                "the wake question precedes the acquire, so a sleeping tick holds no display image");

        // ② NO UNRECORDED TICK IS EVER SURRENDERED. `Complete` after a skipped recording presents the
        //    clear ground; the idle path must doze instead.
        const std::size_t DozeAt = Session.find("Lifetime.Doze(WakeIntervalSeconds)");
        Require(DozeAt != std::string::npos, "the idle path dozes");
        Require(DozeAt > AskedAt && DozeAt < AcquireAt,
                "and it dozes between the question and the acquire, never after one");

        // ③ THE RULE'S INPUT COMES FROM THE WINDOW SYSTEM, NOT FROM THE INTERFACE.
        Require(Session.find("Viewport.Waking(Lifetime.Stirred())") != std::string::npos,
                "the arrival is read live from the window system");
        Require(Viewport.find("SurfaceOwned.Pointer()") == std::string::npos
             || Viewport.find("MarksOwned.Waking(Moving(), ArtistStirred)") != std::string::npos,
                "and the rule no longer sources arrival from the frame-gated pointer record");

        // ④ THE MARKS MUST HAVE A CALLER. `Marked()` is one third of the rule, and for the whole of this
        //    defect's life `Mark` and `MarkEvery` had ZERO callers outside this proof — so that third was
        //    permanently false and the rule collapsed to "is something animating".
        const std::string Scheduler = ReadWhole("Engine/SlateUI/Interface/RedrawScheduler/Source/RedrawScheduler.cpp");
        Require(!Scheduler.empty(), "the scheduler source is readable");
        Require(Viewport.find("MarksOwned.Mark") != std::string::npos,
                "something actually marks a panel, or `Marked()` is dead weight in the rule");
    }

    std::printf("[IdleWakeProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
