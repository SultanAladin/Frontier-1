/*====================================================================================================================================
                                                          SOLIDARCCAMERA.CPP
====================================================================================================================================*/
// 🧩 Sketcher viewport camera: scene framing and box selection of edges over Engine/Render/OrbitCamera

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherCamera.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces::Sketcher
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

using Triple = std::array<double, 3>;

[[nodiscard]] double Dot(const Triple& Left, const Triple& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; }

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void FitSketcherCamera(OrbitCamera& Camera, const SketcherScene& Scene)
{
    FitOrbitCamera(Camera, Scene.BoundsLow, Scene.BoundsHigh);
}

std::vector<uint32_t> EncloseSketcherEdges(const SketcherScene& Scene, const OrbitCameraBasis& Frame, std::array<double, 2> Low,
                                           std::array<double, 2> High, bool XRayEnabled, const SketcherSectionClip* Section)
{
    const auto Kept = [&](const float Point[3])
    {
        return !Section || Section->Plane[0] * Point[0] + Section->Plane[1] * Point[1] + Section->Plane[2] * Point[2] + Section->Plane[3] >= 0.0;
    };
    // World point → viewport pixel; false behind the eye.
    const auto Project = [&](const float Point[3], std::array<double, 2>& Pixel)
    {
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Point[0] + Frame.ClipFromWorld[1 * 4 + Row] * Point[1] + Frame.ClipFromWorld[2 * 4 + Row] * Point[2]
                      + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 1.0e-12) return false;
        Pixel = { (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width, (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height };
        return true;
    };
    const auto Inside = [&](const std::array<double, 2>& Pixel) { return Pixel[0] >= Low[0] && Pixel[0] <= High[0] && Pixel[1] >= Low[1] && Pixel[1] <= High[1]; };
    Triple Span{};
    for (int Axis = 0; Axis < 3; ++Axis) Span[Axis] = Scene.BoundsHigh[Axis] - Scene.BoundsLow[Axis];
    const double Slack = 2.0 * Frame.WorldPerPixel + 1.0e-4 * std::sqrt(Dot(Span, Span));            // [len]

    std::vector<uint32_t> Enclosed;
    for (uint32_t EdgeIndex = 0; EdgeIndex < Scene.Edges.size(); ++EdgeIndex)
    {
        const SketcherSceneEdge& Edge = Scene.Edges[EdgeIndex];
        if (Edge.SegmentCount == 0) continue;
        bool WhollyCondition = true;
        for (uint32_t Index = Edge.FirstSegment; Index < Edge.FirstSegment + Edge.SegmentCount && WhollyCondition; ++Index)
        {
            std::array<double, 2> Start{}, End{};
            WhollyCondition = Project(Scene.Segments[Index].Start, Start) && Project(Scene.Segments[Index].End, End) && Inside(Start) && Inside(End);
        }
        if (!WhollyCondition) continue;
        bool KeptCondition = false;
        for (uint32_t Index = Edge.FirstSegment; Index < Edge.FirstSegment + Edge.SegmentCount && !KeptCondition; ++Index)
            KeptCondition = Kept(Scene.Segments[Index].Start) || Kept(Scene.Segments[Index].End);
        if (!KeptCondition) continue;
        bool VisibleCondition = XRayEnabled;
        for (uint32_t Index = Edge.FirstSegment; Index < Edge.FirstSegment + Edge.SegmentCount && !VisibleCondition; ++Index)
        {
            const SketcherSceneSegment& Segment = Scene.Segments[Index];
            const float Middle[3] = { 0.5f * (Segment.Start[0] + Segment.End[0]), 0.5f * (Segment.Start[1] + Segment.End[1]), 0.5f * (Segment.Start[2] + Segment.End[2]) };
            std::array<double, 2> Pixel{};
            if (!Kept(Middle) || !Project(Middle, Pixel)) continue;
            const OrbitRay Ray   = CastOrbitRay(Frame, Pixel[0], Pixel[1]);
            const double      Reach = (Middle[0] - Ray.Origin[0]) * Ray.Direction[0] + (Middle[1] - Ray.Origin[1]) * Ray.Direction[1] + (Middle[2] - Ray.Origin[2]) * Ray.Direction[2];
            const std::optional<SketcherPick> Face = PickSketcherScene(Scene, Ray, 0.0, false, Section);
            VisibleCondition = !Face || Face->Distance >= Reach - Slack;
        }
        if (VisibleCondition) Enclosed.push_back(EdgeIndex);
    }
    return Enclosed;
}

} // namespace Frontier::Workspaces::Sketcher
