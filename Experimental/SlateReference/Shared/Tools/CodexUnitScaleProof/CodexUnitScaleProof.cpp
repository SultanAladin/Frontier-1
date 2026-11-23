//============================================================================================================================================
//                                                        CODEXUNITSCALEPROOF.CPP
//============================================================================================================================================
// 🧩 Puts a claim either side of the metre/millimetre boundary.
//
// 🔴 THIS IS THE ONE REMAINING DEFECT CLASS THAT CORRUPTS A DRAWING IN SILENCE. A codex stores metres.
//    The editor works in metres, the parametric workspace in millimetres. The factor between them was
//    once a `constexpr` in a `.cpp`, applied unconditionally — right for its only caller, and a 1000x
//    displacement the moment a second caller arrived. It is a parameter now, but a parameter with no
//    test is only a better-documented landmine: nothing stops a future edit from defaulting it, swapping
//    the two constants at a call site, or "simplifying" the multiply away.
//
// ⚠️ EVERY PREVIOUS SABOTAGE THAT SCORED ZERO WAS A MISSING BOUNDARY CLAIM. So this proof does not merely
//    check that scaling works: it pins BOTH constants to exact values, pins the two hosts to the specific
//    constant each must use, and requires the round trip through both to return the original metres.

#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"
#include "SlateWorkspace/Discipline/CodexSceneProxy/Api/CodexSceneProxy.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{

int Claims = 0;
int Failures = 0;

void Require(bool Held, const std::string& Named)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  [failed] %s\n", Named.c_str());
    }
}

void RequireNear(double Measured, double Expected, double Tolerance, const std::string& Named)
{
    ++Claims;
    const double Divergence = std::fabs(Measured - Expected);
    if (!(Divergence <= Tolerance))
    {
        ++Failures;
        std::printf("  [failed] %s — measured %.9f, expected %.9f, out by %.3e\n",
                    Named.c_str(), Measured, Expected, Divergence);
    }
}

Slate::CodexSceneEntry EntryAt(double X, double Y, double Z)
{
    Slate::CodexSceneEntry Entry = {};
    Entry.Position[0] = X;
    Entry.Position[1] = Y;
    Entry.Position[2] = Z;
    return Entry;
}

}   // namespace

int main()
{
    using namespace Slate;

    std::printf("[CodexUnitScale] the metre/millimetre boundary\n");

    //--------------------------------------------------------------------------------------------------
    // The two constants, pinned exactly. A drifting constant is the whole defect.
    //--------------------------------------------------------------------------------------------------
    RequireNear(CodexMetresToMillimetres, 1000.0, 0.0,
                "one codex metre is exactly 1000 parametric millimetres");
    RequireNear(CodexMetresToMetres, 1.0, 0.0,
                "one codex metre is exactly 1 editor metre");

    // 🔴 The two must not be equal. If a future edit "unifies" them, every earlier claim here still
    //    passes while the parametric workspace silently renders at 1/1000 scale.
    Require(CodexMetresToMillimetres != CodexMetresToMetres,
            "the two unit scales must remain distinct");

    //--------------------------------------------------------------------------------------------------
    // A sample either side of the line: the SAME entry through BOTH scales.
    //--------------------------------------------------------------------------------------------------
    const CodexSceneEntry Entry = EntryAt(1.5, -2.25, 0.125);

    const SpatialPoint InMetres      = CodexScenePosition(Entry, CodexMetresToMetres);
    const SpatialPoint InMillimetres = CodexScenePosition(Entry, CodexMetresToMillimetres);

    // ⚠️ Fields are Left/Up/Forward, never X/Y/Z.
    RequireNear(InMetres.Left,     1.5,   0.0, "1.5 m reads as 1.5 in the editor");
    RequireNear(InMetres.Up,      -2.25,  0.0, "-2.25 m reads as -2.25 in the editor");
    RequireNear(InMetres.Forward,  0.125, 0.0, "0.125 m reads as 0.125 in the editor");

    RequireNear(InMillimetres.Left,     1500.0, 0.0, "1.5 m reads as 1500 mm in the parametric workspace");
    RequireNear(InMillimetres.Up,      -2250.0, 0.0, "-2.25 m reads as -2250 mm in the parametric workspace");
    RequireNear(InMillimetres.Forward,   125.0, 0.0, "0.125 m reads as 125 mm in the parametric workspace");

    // 🔴 THE DISPLACEMENT ITSELF. This is the claim that fails if the two are ever conflated: the same
    //    authored point must be exactly a thousand times further out in millimetres than in metres.
    RequireNear(InMillimetres.Left / InMetres.Left, 1000.0, 1.0e-12,
                "the same point is exactly 1000x further in millimetres than in metres");

    //--------------------------------------------------------------------------------------------------
    // The round trip. Scaling out and back must land on the authored metres.
    //--------------------------------------------------------------------------------------------------
    RequireNear(InMillimetres.Left    / CodexMetresToMillimetres, Entry.Position[0], 1.0e-12,
                "millimetres divided back by the scale return the authored metres");
    RequireNear(InMillimetres.Up      / CodexMetresToMillimetres, Entry.Position[1], 1.0e-12,
                "the round trip holds for a negative coordinate");
    RequireNear(InMillimetres.Forward / CodexMetresToMillimetres, Entry.Position[2], 1.0e-12,
                "the round trip holds for a sub-millimetre coordinate");

    //--------------------------------------------------------------------------------------------------
    // The origin is the one point where the two scales agree — which is exactly why a test that only
    // ever samples the origin proves nothing at all.
    //--------------------------------------------------------------------------------------------------
    const CodexSceneEntry AtOrigin = EntryAt(0.0, 0.0, 0.0);
    const SpatialPoint OriginMetres      = CodexScenePosition(AtOrigin, CodexMetresToMetres);
    const SpatialPoint OriginMillimetres = CodexScenePosition(AtOrigin, CodexMetresToMillimetres);
    RequireNear(OriginMetres.Left,      OriginMillimetres.Left,    0.0, "both scales agree at the origin (Left)");
    RequireNear(OriginMetres.Up,        OriginMillimetres.Up,      0.0, "both scales agree at the origin (Up)");
    RequireNear(OriginMetres.Forward,   OriginMillimetres.Forward, 0.0, "both scales agree at the origin (Forward)");

    // 📝 And a point that is NOT the origin must disagree, or the claim above is vacuous.
    Require(InMetres.Left != InMillimetres.Left,
            "away from the origin the two scales must disagree");

    //--------------------------------------------------------------------------------------------------
    // A scale of zero collapses a scene onto the origin. It is not a unit, and nothing should pass it,
    // but the arithmetic must stay predictable rather than produce a NaN.
    //--------------------------------------------------------------------------------------------------
    const SpatialPoint Collapsed = CodexScenePosition(Entry, 0.0);
    RequireNear(Collapsed.Left,    0.0, 0.0, "a zero scale collapses to the origin rather than a NaN");
    RequireNear(Collapsed.Up,      0.0, 0.0, "a zero scale collapses Up to the origin");
    RequireNear(Collapsed.Forward, 0.0, 0.0, "a zero scale collapses Forward to the origin");

    std::printf("[CodexUnitScale] %d claims, %d failures\n", Claims, Failures);
    return Failures == 0 ? 0 : 1;
}
