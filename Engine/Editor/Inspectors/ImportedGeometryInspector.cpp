/*============================================================================================================================================
                                                       IMPORTEDGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Live Category 3 Imported Geometry Inspector (glTF, FBX, OBJ, USD, STEP, STL exchange pipeline)

#include "Engine/Editor/Inspectors/ImportedGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

#include <array>

namespace Frontier
{

void ImportedGeometryInspector::UpdateExchangeSurface(const ImportedGeometryData& Instance)
{
    uint64_t Key = 0xA3030001ull ^ (static_cast<uint64_t>(Instance.ExchangeFormatIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.ImportScaleFactor);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::GeometrySpecimenPreview, 2u,
        { static_cast<float>(Instance.ExchangeFormatIndex), Instance.ImportScaleFactor, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.56f, 0.70f, 0.76f, 1.0f }
    };
    ExchangePreview.EnsureUpdatedGpu(Key, 96u, 96u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
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

float ImportedGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ImportedGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateExchangeSurface(Instance);
    static constexpr std::array<const char*, 6u> FormatNames = {{ "glTF", "FBX", "OBJ", "USD", "STEP", "STL" }};
    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName      = FormatNames[std::min(Instance.ExchangeFormatIndex, 5u)];
    Desc.DomainLabel    = "Imported exchange mesh";
    Desc.TriangleCount  = Instance.TriangleCount;
    Desc.ClusterCount   = std::max(1u, (Instance.TriangleCount + 127u) / 128u);
    Desc.LodLevels      = Instance.BuildClusterPyramid ? 3u : 1u;
    Desc.HasLocalSdf    = Instance.BuildLocalSdf;
    Desc.SurfaceBinding = &Instance.Surface;
    Desc.Scene          = &Scene;

    Desc.ConstructCategoryCard = [&](PixelSpace& Srf, float WX, float CY, float WW, uint32_t& DragId, const PaneInputReading& Inp, bool& Mut) -> float
    {
        const float PadX = 25.0f;
        const float BX   = WX + PadX;
        const float BW   = WW - PadX * 2.0f;
        const Rectangle2D Card { WX, CY, WW, 304.0f };
        GeometryUi::ConstructGeomCard(Srf, Card);
        WorkspaceAppearance::ConstructWeightedText(Srf, FontWeightClass::Regular, { BX, Card.Y + 24.0f }, 15.0f, InspectorMetrics::HeadingInk, "Import pipeline");
        float Y = Card.Y + 58.0f;

        static constexpr std::array<const char*, 2u> UpAxes = {{ "Z-Up", "Y-Up" }};
        int32_t PickAxis = -1;
        Y = InspectorPrimitives::ConstructSegmentTrack(Srf, BX, Y, BW, "SOURCE UP AXIS", UpAxes, std::min(Instance.UpAxisIndex, 1u), Inp, PickAxis);
        if (PickAxis >= 0 && PickAxis <= 1) { Instance.UpAxisIndex = static_cast<uint32_t>(PickAxis); Mut = true; }

        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8860u, "Import scale", "x",       "%.2f", 0.01f, 10.0f, Instance.ImportScaleFactor,  DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8861u, "Crease angle", "\xC2\xB0","%.0f", 0.0f,  90.0f, Instance.CreaseAngleDegrees, DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructToggleFieldRow(Srf, BX, Y, BW, "Build cluster pyramid", Instance.BuildClusterPyramid, Inp, Mut);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Srf, BX, Y, BW, "Build local SDF",     Instance.BuildLocalSdf,       Inp, Mut);
        if (Mut)
        {
            Entry.ScaleX = std::max(0.01f, Instance.ImportScaleFactor);
            Entry.ScaleY = std::max(0.01f, Instance.ImportScaleFactor);
            Entry.ScaleZ = std::max(0.01f, Instance.ImportScaleFactor);
        }
        return Card.Bottom() + 14.0f;
    };

    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
