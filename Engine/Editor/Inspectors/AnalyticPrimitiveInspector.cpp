/*============================================================================================================================================
                                                      ANALYTICPRIMITIVEINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Live Category 1 Analytical Primitive Inspector (Sphere, Box, Cylinder, Cone, Torus, Capsule, Plane)

#include "Engine/Editor/Inspectors/AnalyticPrimitiveInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

void AnalyticPrimitiveInspector::UpdatePrimitiveSurface(const AnalyticPrimitiveData& Instance)
{
    uint64_t Key = 0xA1010001ull ^ (static_cast<uint64_t>(Instance.FormIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.RadiusMeters);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.HeightMeters);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::GeometrySpecimenPreview, 0u,
        { static_cast<float>(Instance.FormIndex), Instance.RadiusMeters, Instance.HeightMeters, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.62f, 0.68f, 0.78f, 1.0f }
    };
    PrimitivePreview.EnsureUpdatedGpu(Key, 96u, 96u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        for (uint32_t Y = 0u; Y < 96u; ++Y)
        {
            for (uint32_t X = 0u; X < 96u; ++X)
            {
                const std::size_t Off = (static_cast<std::size_t>(Y) * 96u + X) * 4u;
                Pixels[Off + 0u] = 36u; Pixels[Off + 1u] = 38u; Pixels[Off + 2u] = 44u; Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AnalyticPrimitiveInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AnalyticPrimitiveData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdatePrimitiveSurface(Instance);
    static constexpr std::array<const char*, 7u> ShapeNames = {{
        "Sphere", "Cube", "Cylinder", "Cone", "Torus", "Capsule", "Plane"
    }};
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName      = ShapeNames[std::min(Instance.FormIndex, 6u)];
    Desc.DomainLabel    = "Analytic primitive";
    Desc.WidthMeters    = std::max(0.1f, Instance.RadiusMeters * 2.0f);
    Desc.HeightMeters   = std::max(0.1f, Instance.HeightMeters);
    Desc.DepthMeters    = std::max(0.1f, Instance.RadiusMeters * 2.0f);
    Desc.TriangleCount  = std::max(12u, Instance.RadialSegments * 16u);
    Desc.ClusterCount   = std::max(1u, (Desc.TriangleCount + 127u) / 128u);
    Desc.LodLevels      = 3u;
    Desc.SurfaceBinding = &Instance.Surface;
    Desc.Scene          = &Scene;

    Desc.ConstructCategoryCard = [&](PixelSpace& Srf, float WX, float CY, float WW, uint32_t& DragId, const PaneInputReading& Inp, bool& Mut) -> float
    {
        const float PadX = 25.0f;
        const float BX   = WX + PadX;
        const float BW   = WW - PadX * 2.0f;
        const Rectangle2D Card { WX, CY, WW, 324.0f };
        GeometryUi::ConstructGeomCard(Srf, Card);
        WorkspaceAppearance::ConstructWeightedText(Srf, FontWeightClass::Regular, { BX, Card.Y + 24.0f }, 15.0f, InspectorMetrics::HeadingInk, "Analytic parameters");
        float Y = Card.Y + 58.0f;

        static constexpr std::array<const char*, 4u> PrimaryShapes = {{ "Sphere", "Box", "Cyl", "Torus" }};
        static constexpr std::array<uint32_t, 4u>    PrimaryMap    = {{ 0u, 1u, 2u, 4u }};
        uint32_t SelP = 0u;
        for (uint32_t I = 0u; I < 4u; ++I) if (Instance.FormIndex == PrimaryMap[I]) SelP = I;
        int32_t PickP = -1;
        Y = InspectorPrimitives::ConstructSegmentTrack(Srf, BX, Y, BW, "PRIMITIVE FORM", PrimaryShapes, SelP, Inp, PickP);
        if (PickP >= 0 && PickP < 4) { Instance.FormIndex = PrimaryMap[static_cast<std::size_t>(PickP)]; Mut = true; }

        float SegsF = static_cast<float>(Instance.RadialSegments);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8830u, "Primary radius", "m",  "%.2f", 0.05f, 8.0f,  Instance.RadiusMeters,     DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8831u, "Height / span",  "m",  "%.2f", 0.05f, 12.0f, Instance.HeightMeters,     DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8832u, "Radial rings",   "",   "%.0f", 8.0f,  96.0f, SegsF,                     DragId, Inp, Mut);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8833u, "Edge bevel",   "mm", "%.1f", 0.0f,  40.0f, Instance.BevelMillimeters, DragId, Inp, Mut);
        if (Mut)
        {
            Instance.RadialSegments = static_cast<uint32_t>(std::clamp(std::lround(SegsF), 8l, 96l));
            Entry.ScaleX = std::max(0.05f, Instance.RadiusMeters * 2.0f);
            Entry.ScaleY = std::max(0.05f, Instance.RadiusMeters * 2.0f);
            Entry.ScaleZ = std::max(0.05f, Instance.HeightMeters);
        }
        return Card.Bottom() + 14.0f;
    };

    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
