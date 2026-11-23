//============================================================================================================================================
//                                                      TRANSFORMGIZMOPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the transform gizmo keeps the HTML reference's geometry, on the C++ GPU path, while its
//    interactive reaches still land where the artist sees the handles.

#include "SlateWorkspace/Discipline/TransformGizmo/Api/TransformGizmo.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-6)
{
    return std::fabs(Left - Right) <= Tolerance;
}

std::string ReadWhole(const char* Path)
{
    std::ifstream Stream(Path);
    if (!Stream)
        return std::string();
    std::ostringstream Gathered;
    Gathered << Stream.rdbuf();
    return Gathered.str();
}

const SpatialBasis Ground = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
const PlaneExtent  Panel  = { 0.0f, 0.0f, 800.0f, 600.0f };

ViewportStanding Ortho(double Scale)
{
    ViewportStanding View;
    View.Orientation = ViewportOrientation::Top;
    View.Focus       = { 0.0, 0.0, 0.0 };
    View.OrthoScale  = Scale;
    View.Distance    = 240.0;
    return View;
}

ViewportStanding Perspective(double Distance)
{
    ViewportStanding View;
    View.Orientation = ViewportOrientation::Isometric;
    View.Focus       = { 0.0, 0.0, 0.0 };
    View.OrthoScale  = 4.0;
    View.Distance    = Distance;
    return View;
}

//------------------------------------------------------------------------------------------------------------------------
// 1. THE C++ TABLE IS THE HTML TABLE
//------------------------------------------------------------------------------------------------------------------------

void ProveHtmlReferenceLiftedExactly()
{
    std::printf("\n1. The measurement table is lifted from References/Gizmo.html\n");

    const std::string Html = ReadWhole("References/Gizmo.html");
    Claim(!Html.empty(), "the HTML gizmo reference is readable");
    if (Html.empty())
        return;

    for (const char* Needle : {
             "new THREE.ConeGeometry(0.06, 0.18, 24)",
             "new THREE.CylinderGeometry(0.06,0.06,0.14,24)",
             "const half = 0.08",
             "const arcRadius = TIP*0.62",
             "degToRad(31)",
             "const arcBand = 0.038",
             "new THREE.TorusGeometry(0.16, 0.008, 12, 48)",
         })
        Claim(Html.find(Needle) != std::string::npos, "the expected gizmo primitive is stated in the HTML reference");

    Claim(Near(GizmoMeasure::AxisEnd, 82.0), "the C++ tip distance is the 82 px table lift");
    Claim(Near(GizmoMeasure::ConeRadius, GizmoMeasure::AxisEnd * (0.06 / 0.95)),
          "the move cone radius matches the HTML cone ratio exactly");
    Claim(Near(GizmoMeasure::ConeLength, GizmoMeasure::AxisEnd * (0.18 / 0.95)),
          "the move cone length matches the HTML cone ratio exactly");
    Claim(GizmoMeasure::ConeSegments == 24u, "the move cone keeps the HTML segment count");

    Claim(Near(GizmoMeasure::ScaleRadius, GizmoMeasure::AxisEnd * (0.06 / 0.95)),
          "the scale cylinder radius matches the HTML cylinder ratio exactly");
    Claim(Near(GizmoMeasure::ScaleLength, GizmoMeasure::AxisEnd * (0.14 / 0.95)),
          "the scale cylinder length matches the HTML cylinder ratio exactly");
    Claim(Near(GizmoMeasure::ScaleCentre, GizmoMeasure::AxisEnd * ((0.95 - 0.28) / 0.95)),
          "the scale cylinder centre matches the HTML offset exactly");
    Claim(GizmoMeasure::CylinderSegments == 24u, "the scale cylinder keeps the HTML segment count");

    Claim(Near(GizmoMeasure::PlaneHalf, GizmoMeasure::AxisEnd * (0.08 / 0.95)),
          "the plane square half-width matches the HTML square exactly");
    Claim(Near(GizmoMeasure::PlaneCentre, GizmoMeasure::AxisEnd * ((0.95 - 0.08) / 0.95)),
          "the plane square centre matches the HTML offset exactly");

    Claim(Near(GizmoMeasure::RotateRadius, GizmoMeasure::AxisEnd * 0.62),
          "the rotation arc radius matches the HTML arc exactly");
    Claim(Near(GizmoMeasure::RotateHalfWidth, GizmoMeasure::AxisEnd * (0.038 / 0.95)),
          "the rotation arc band matches the HTML arc exactly");
    Claim(Near(GizmoMeasure::RotateSweepRadians, 31.0 * 3.14159265358979323846 / 180.0),
          "the rotation arc sweep matches the HTML arc exactly");
    Claim(GizmoMeasure::RotateSegments == 24u, "the rotation arc keeps the HTML segment count");

    Claim(Near(GizmoMeasure::CentreRingRadius, GizmoMeasure::AxisEnd * (0.16 / 0.95)),
          "the centre torus major radius matches the HTML torus exactly");
    Claim(Near(GizmoMeasure::CentreRingTube, GizmoMeasure::AxisEnd * (0.008 / 0.95)),
          "the centre torus tube radius matches the HTML torus exactly");
    Claim(GizmoMeasure::CentreRingRadialSegments == 12u, "the centre torus keeps the HTML radial segments");
    Claim(GizmoMeasure::CentreRingTubularSegments == 48u, "the centre torus keeps the HTML tubular segments");
}

//------------------------------------------------------------------------------------------------------------------------
// 2. THE GIZMO IS STILL A CONSTANT SCREEN SIZE
//------------------------------------------------------------------------------------------------------------------------

void ProveConstantScreenSize()
{
    std::printf("\n2. The lifted HTML gizmo still holds its screen footprint at every zoom\n");

    for (double Scale : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
    {
        GizmoScreenBasis Screen = {};
        Claim(ResolveGizmoScreenBasis(Ground, Ortho(Scale), false, Panel, { 0.0, 0.0, 0.0 }, Screen),
              "the gizmo stands under orthographic projection");

        const SpatialPoint Origin = { 0.0, 0.0, 0.0 };
        const SpatialPoint Tip = Added(Origin, Scaled(Ground.Along, GizmoWorld(Screen, GizmoMeasure::AxisEnd)));
        float X = 0.0f;
        float Y = 0.0f;
        Claim(ProjectSpatialPoint(Ground, Ortho(Scale), false, Panel, Tip, X, Y),
              "the translated HTML tip projects");
        const double Pixels = std::sqrt((X - Screen.PivotX) * (X - Screen.PivotX)
                                      + (Y - Screen.PivotY) * (Y - Screen.PivotY));
        Claim(std::fabs(Pixels - GizmoMeasure::AxisEnd) < 0.5,
              "the tip still lands 82 px from the pivot at every orthographic zoom");
    }

    for (double Distance : { 80.0, 160.0, 320.0, 640.0, 1280.0 })
    {
        GizmoScreenBasis Screen = {};
        Claim(ResolveGizmoScreenBasis(Ground, Perspective(Distance), true, Panel, { 0.0, 0.0, 0.0 }, Screen),
              "the gizmo stands under perspective too");

        const SpatialPoint Origin = { 0.0, 0.0, 0.0 };
        const SpatialPoint Tip = Added(Origin, Scaled(Ground.Along, GizmoWorld(Screen, GizmoMeasure::AxisEnd)));
        float X = 0.0f;
        float Y = 0.0f;
        if (!ProjectSpatialPoint(Ground, Perspective(Distance), true, Panel, Tip, X, Y))
            continue;
        const double Pixels = std::sqrt((X - Screen.PivotX) * (X - Screen.PivotX)
                                      + (Y - Screen.PivotY) * (Y - Screen.PivotY));
        Claim(Pixels <= GizmoMeasure::AxisEnd + 0.5,
              "the perspective lift never overshoots its own hit footprint");
        Claim(Pixels > GizmoMeasure::AxisEnd * 0.85,
              "and stays acceptably close to the 82 px target");
    }
}

//------------------------------------------------------------------------------------------------------------------------
// 3. THE REACHES STILL LAND ON THE DRAWN PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void ProveHandlesReachable()
{
    std::printf("\n3. The interactive reaches land on the HTML-port primitives\n");

    GizmoScreenBasis Screen = {};
    Claim(ResolveGizmoScreenBasis(Ground, Ortho(4.0), false, Panel, { 0.0, 0.0, 0.0 }, Screen),
          "the gizmo stands on the proof bench");

    const float MoveX = Screen.PivotX + Screen.AlongX * static_cast<float>(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength * 0.5);
    const float MoveY = Screen.PivotY + Screen.AlongY * static_cast<float>(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength * 0.5);
    Claim(ResolveGizmoHandle(Screen, TransformManner::Move, MoveX, MoveY) == GizmoHandle::MoveX,
          "pointing at the X cone grabs move X");

    const float FreeX = Screen.PivotX + (Screen.AlongX + Screen.AcrossX) * static_cast<float>(GizmoMeasure::PlaneCentre);
    const float FreeY = Screen.PivotY + (Screen.AlongY + Screen.AcrossY) * static_cast<float>(GizmoMeasure::PlaneCentre);
    Claim(ResolveGizmoHandle(Screen, TransformManner::Move, FreeX, FreeY) == GizmoHandle::MoveFree,
          "pointing at the XZ plane square grabs free move");

    const double MidAngle = 3.14159265358979323846 * 0.25;
    const float RotX = Screen.PivotX + (Screen.AlongX * static_cast<float>(std::cos(MidAngle))
                                     +  Screen.AcrossX * static_cast<float>(std::sin(MidAngle)))
                                     * static_cast<float>(GizmoMeasure::RotateRadius);
    const float RotY = Screen.PivotY + (Screen.AlongY * static_cast<float>(std::cos(MidAngle))
                                     +  Screen.AcrossY * static_cast<float>(std::sin(MidAngle)))
                                     * static_cast<float>(GizmoMeasure::RotateRadius);
    Claim(ResolveGizmoHandle(Screen, TransformManner::Rotate, RotX, RotY) == GizmoHandle::Rotate,
          "pointing at the visible rotation arc grabs rotate");

    const float ScaleX = Screen.PivotX + Screen.AlongX * static_cast<float>(GizmoMeasure::ScaleCentre);
    const float ScaleY = Screen.PivotY + Screen.AlongY * static_cast<float>(GizmoMeasure::ScaleCentre);
    Claim(ResolveGizmoHandle(Screen, TransformManner::Scale, ScaleX, ScaleY) == GizmoHandle::ScaleX,
          "pointing at the X scale cylinder grabs scale X");

    Claim(ResolveGizmoHandle(Screen, TransformManner::Move, Screen.PivotX + 400.0f, Screen.PivotY + 400.0f)
          == GizmoHandle::None,
          "a point far from the gizmo still grabs nothing");
}

//------------------------------------------------------------------------------------------------------------------------
// 4. THE PORT STAYS ON THE GPU OVERLAY PATH
//------------------------------------------------------------------------------------------------------------------------

void ProveGpuOverlayPathStatesTheHtmlPrimitives()
{
    std::printf("\n4. The viewport overlay records the lifted HTML primitives on the GPU path\n");

    const std::string Overlay = ReadWhole("Engine/SlateWorkspace/Discipline/SketchViewportOverlay/Source/SketchViewportOverlay.cpp");
    Claim(!Overlay.empty(), "the overlay source is readable");
    if (Overlay.empty())
        return;

    for (const char* Needle : {
             "AddCone = [&](const SpatialDirection& Axis",
             "AddCylinder = [&](const SpatialDirection& Axis",
             "AddPlaneHandle = [&](const SpatialDirection& U",
             "AddArcBar = [&](const SpatialDirection& U",
             "AddBillboardTorus = [&](std::uint32_t Packed)",
             "Overlay.AddTriangle(",
         })
        Claim(Overlay.find(Needle) != std::string::npos,
              "the overlay states the expected GPU gizmo primitive");
}

//------------------------------------------------------------------------------------------------------------------------
// 5. A HANDLE STILL NAMES ITS MANNER AND ITS RESTRICTION
//------------------------------------------------------------------------------------------------------------------------

void ProveHandleMeaning()
{
    std::printf("\n5. What grabbing a handle still means\n");

    Claim(ResolveHandleManner(GizmoHandle::MoveX) == TransformManner::Move, "the X cone moves");
    Claim(ResolveHandleRestriction(GizmoHandle::MoveX) == TransformRestriction::AxisX, "and constrains along X");
    Claim(ResolveHandleManner(GizmoHandle::MoveZ) == TransformManner::Move, "the Z cone moves");
    Claim(ResolveHandleRestriction(GizmoHandle::MoveZ) == TransformRestriction::AxisZ, "and constrains along Z");
    Claim(ResolveHandleManner(GizmoHandle::MoveFree) == TransformManner::Move, "the plane square moves freely");
    Claim(ResolveHandleRestriction(GizmoHandle::MoveFree) == TransformRestriction::Free, "with no axis restriction");

    Claim(ResolveHandleManner(GizmoHandle::Rotate) == TransformManner::Rotate, "the arc-bars rotate");
    Claim(ResolveHandleRestriction(GizmoHandle::Rotate) == TransformRestriction::Screen,
          "under the existing sketch rotation restriction");

    Claim(ResolveHandleManner(GizmoHandle::ScaleX) == TransformManner::Scale, "the X cylinder scales");
    Claim(ResolveHandleRestriction(GizmoHandle::ScaleX) == TransformRestriction::AxisX, "along X only");
    Claim(ResolveHandleManner(GizmoHandle::ScaleZ) == TransformManner::Scale, "the Z cylinder scales");
    Claim(ResolveHandleRestriction(GizmoHandle::ScaleZ) == TransformRestriction::AxisZ, "along Z only");
    Claim(ResolveHandleManner(GizmoHandle::ScaleFree) == TransformManner::Scale, "the centre ring can still stand for free scale");
    Claim(ResolveHandleRestriction(GizmoHandle::ScaleFree) == TransformRestriction::Free, "with no axis restriction");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("TRANSFORM GIZMO PROOF\n");
    std::printf("=========================================================================\n");

    ProveHtmlReferenceLiftedExactly();
    ProveConstantScreenSize();
    ProveHandlesReachable();
    ProveGpuOverlayPathStatesTheHtmlPrimitives();
    ProveHandleMeaning();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
