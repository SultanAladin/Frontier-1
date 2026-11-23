// 🧩 The viewport footer's Ortho/Perspective toggle must reach the camera.
//
// 🔴 `ResolveFreeCamera` hardcoded `Camera.Perspective = true`. The panel stored the artist's choice and
//    the sketch overlays were handed a literal `true` besides, so pressing Ortho changed a label and
//    nothing else. This proof fails if the mode or the scale stops being carried, and -- the part a type
//    checker cannot see -- if the orthographic arm ever tracks the perspective one.

#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{

std::uint32_t Claims  = 0u;
std::uint32_t Failures = 0u;

void Require(bool Held, const char* Claim)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Claim);
    }
}

// 📝 Aimed back down the +Z axis and tilted down, so the sample lattice on the ground is in front of
//    the eye. A camera pointed away from the samples projects nothing and would pass vacuously.
constexpr double ProofYaw   = 180.0;
constexpr double ProofPitch = -30.0;
constexpr double ProofScale = 3.0;

}   // namespace

int main()
{
    const PlaneExtent  Body = Spanning(0.0f, 0.0f, 1280.0f, 720.0f);
    const SpatialPoint Eye{ 0.0, 8.0, 14.0 };

    const ResolvedCamera Perspective = ResolveFreeCamera(Eye, ProofYaw, ProofPitch, 60.0, true, ProofScale);
    const ResolvedCamera Orthographic = ResolveFreeCamera(Eye, ProofYaw, ProofPitch, 60.0, false, ProofScale);

    Require(Perspective.Perspective, "a perspective request resolves a perspective camera");
    Require(!Orthographic.Perspective, "an orthographic request resolves an orthographic camera");
    Require(std::fabs(Orthographic.OrthoScale - ProofScale) < 1e-9,
            "the orthographic scale is carried, not defaulted to one pixel per metre");

    // ⚠️ The default keeps every existing caller perspective.
    const ResolvedCamera Defaulted = ResolveFreeCamera(Eye, ProofYaw, ProofPitch, 60.0);
    Require(Defaulted.Perspective, "the default remains perspective");

    // 🔴 Same eye, same orientation: the two arms must genuinely disagree. If a future edit resolves
    //    both through the perspective path this is the claim that catches it.
    std::uint32_t Both = 0u;
    std::uint32_t Differing = 0u;
    for (int Row = -4; Row <= 4; ++Row)
        for (int Column = -4; Column <= 4; ++Column)
        {
            const SpatialPoint Sample{ Row * 1.5, 0.0, Column * 1.5 };
            float PerspectiveX = 0.0f, PerspectiveY = 0.0f, OrthoX = 0.0f, OrthoY = 0.0f;
            if (!ProjectFromCamera(Perspective, Body, Sample, PerspectiveX, PerspectiveY))
                continue;
            if (!ProjectFromCamera(Orthographic, Body, Sample, OrthoX, OrthoY))
                continue;
            ++Both;
            if (std::fabs(PerspectiveX - OrthoX) > 0.5f || std::fabs(PerspectiveY - OrthoY) > 0.5f)
                ++Differing;
        }

    Require(Both >= 20u, "the proof lattice is visible to both cameras");
    Require(Differing >= Both - Both / 4u, "the orthographic arm does not track the perspective one");

    // 🔴 Parallel projection: equal spacing in the world is equal spacing on screen, at any depth. This
    //    is the property that makes orthographic useful for drawing, and the one a wrong implementation
    //    silently loses.
    float NearX = 0.0f, NearY = 0.0f, MiddleX = 0.0f, MiddleY = 0.0f, FarX = 0.0f, FarY = 0.0f;
    Require(ProjectFromCamera(Orthographic, Body, { -3.0, 0.0, 0.0 }, NearX, NearY), "the proof sample is visible");
    Require(ProjectFromCamera(Orthographic, Body, {  0.0, 0.0, 0.0 }, MiddleX, MiddleY), "the proof sample is visible");
    Require(ProjectFromCamera(Orthographic, Body, {  3.0, 0.0, 0.0 }, FarX, FarY), "the proof sample is visible");
    const double LeftSpan  = std::hypot(MiddleX - NearX, MiddleY - NearY);
    const double RightSpan = std::hypot(FarX - MiddleX, FarY - MiddleY);
    Require(std::fabs(LeftSpan - RightSpan) < 1e-3, "orthographic spacing is uniform across the view");
    Require(std::fabs(LeftSpan - 3.0 * ProofScale) < 1e-3,
            "orthographic spacing honours the scale it was given");

    // ⚠️ Depth must not change size when orthographic -- the defining difference from perspective.
    float ShallowX = 0.0f, ShallowY = 0.0f, DeepX = 0.0f, DeepY = 0.0f;
    float ShallowFarX = 0.0f, ShallowFarY = 0.0f, DeepFarX = 0.0f, DeepFarY = 0.0f;
    Require(ProjectFromCamera(Orthographic, Body, { -1.0, 0.0,  2.0 }, ShallowX, ShallowY), "the proof sample is visible");
    Require(ProjectFromCamera(Orthographic, Body, {  1.0, 0.0,  2.0 }, ShallowFarX, ShallowFarY), "the proof sample is visible");
    Require(ProjectFromCamera(Orthographic, Body, { -1.0, 0.0, -6.0 }, DeepX, DeepY), "the proof sample is visible");
    Require(ProjectFromCamera(Orthographic, Body, {  1.0, 0.0, -6.0 }, DeepFarX, DeepFarY), "the proof sample is visible");
    const double ShallowWidth = std::hypot(ShallowFarX - ShallowX, ShallowFarY - ShallowY);
    const double DeepWidth    = std::hypot(DeepFarX - DeepX, DeepFarY - DeepY);
    Require(std::fabs(ShallowWidth - DeepWidth) < 1e-3,
            "an orthographic pair keeps its width as it recedes");

    // 📝 And the perspective arm must still shrink with depth, or the proof above would pass on a
    //    camera that had quietly become orthographic everywhere.
    float PerspectiveShallowX = 0.0f, PerspectiveShallowY = 0.0f;
    float PerspectiveShallowFarX = 0.0f, PerspectiveShallowFarY = 0.0f;
    float PerspectiveDeepX = 0.0f, PerspectiveDeepY = 0.0f;
    float PerspectiveDeepFarX = 0.0f, PerspectiveDeepFarY = 0.0f;
    Require(ProjectFromCamera(Perspective, Body, { -1.0, 0.0,  2.0 }, PerspectiveShallowX, PerspectiveShallowY), "the proof sample is visible");
    Require(ProjectFromCamera(Perspective, Body, {  1.0, 0.0,  2.0 }, PerspectiveShallowFarX, PerspectiveShallowFarY), "the proof sample is visible");
    Require(ProjectFromCamera(Perspective, Body, { -1.0, 0.0, -6.0 }, PerspectiveDeepX, PerspectiveDeepY), "the proof sample is visible");
    Require(ProjectFromCamera(Perspective, Body, {  1.0, 0.0, -6.0 }, PerspectiveDeepFarX, PerspectiveDeepFarY), "the proof sample is visible");
    const double PerspectiveShallowWidth =
        std::hypot(PerspectiveShallowFarX - PerspectiveShallowX, PerspectiveShallowFarY - PerspectiveShallowY);
    const double PerspectiveDeepWidth =
        std::hypot(PerspectiveDeepFarX - PerspectiveDeepX, PerspectiveDeepFarY - PerspectiveDeepY);
    Require(PerspectiveShallowWidth > PerspectiveDeepWidth + 1.0,
            "a perspective pair shrinks as it recedes");

    // ⚠️ The eye keeps its orientation across the toggle: pressing Ortho must not spin the view.
    Require(std::fabs(Perspective.Frame.Forward.Left - Orthographic.Frame.Forward.Left) < 1e-12 &&
            std::fabs(Perspective.Frame.Forward.Up - Orthographic.Frame.Forward.Up) < 1e-12 &&
            std::fabs(Perspective.Frame.Forward.Forward - Orthographic.Frame.Forward.Forward) < 1e-12,
            "the toggle changes the projection, not where the camera looks");

    std::printf("[ViewportProjectionMode] %u claims, %u failures (%u lattice samples, %u differing)\n",
                Claims, Failures, Both, Differing);
    return Failures == 0u ? 0 : 1;
}
