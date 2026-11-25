/*============================================================================================================================================
                                                        NATIVEGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Live Category 2 Native Engine Geometry Inspector (.GeometrySpace / ClusterPyramid / LocalSDF / OpenPBR)

#include "Engine/Editor/Inspectors/NativeGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

namespace Frontier
{

void NativeGeometryInspector::UpdateSpecimenSurface(const NativeGeometryData& Instance)
{
    uint64_t Key = 0xA2020001ull ^ (static_cast<uint64_t>(Instance.TriangleCount) << 4u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.LodPixelError);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::GeometrySpecimenPreview, 1u,
        { static_cast<float>(Instance.TriangleCount), Instance.LodPixelError, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.58f, 0.66f, 0.74f, 1.0f }
    };
    SpecimenPreview.EnsureUpdatedGpu(Key, 96u, 96u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
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

float NativeGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, NativeGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateSpecimenSurface(Instance);
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName      = "Native Mesh";
    Desc.DomainLabel    = "Native .GeometrySpace";
    Desc.TriangleCount  = Instance.TriangleCount;
    Desc.ClusterCount   = Instance.ClusterCount;
    Desc.LodLevels      = std::max(1u, Instance.LodTierCount);
    Desc.HasLocalSdf    = Instance.LocalSdfEnabled;
    Desc.SurfaceBinding = &Instance.Surface;
    Desc.Scene          = &Scene;

    Desc.ConstructCategoryCard = [&](PixelSpace& Srf, float WX, float CY, float WW, uint32_t& DragId, const PaneInputReading& Inp, bool& Mut) -> float
    {
        const float PadX = 25.0f;
        const float BX   = WX + PadX;
        const float BW   = WW - PadX * 2.0f;
        const Rectangle2D Card { WX, CY, WW, 246.0f };
        GeometryUi::ConstructGeomCard(Srf, Card);
        WorkspaceAppearance::ConstructWeightedText(Srf, FontWeightClass::Regular, { BX, Card.Y + 24.0f }, 15.0f, InspectorMetrics::HeadingInk, "Native mesh settings");
        float Y = Card.Y + 58.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8850u, "LOD pixel error", "px",        "%.2f", 0.25f, 8.0f,  Instance.LodPixelError,      DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8851u, "Crease angle",    "\xC2\xB0",  "%.0f", 0.0f,  90.0f, Instance.CreaseAngleDegrees, DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Srf, BX, Y, BW, "Local SDF occlusion", Instance.LocalSdfEnabled, Inp, Mut);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Srf, BX, Y, BW, "Double-sided shading", Instance.DoubleSided, Inp, Mut);
        return Card.Bottom() + 14.0f;
    };

    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
