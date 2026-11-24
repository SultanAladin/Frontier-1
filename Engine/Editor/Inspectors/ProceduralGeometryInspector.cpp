/*============================================================================================================================================
                                                      PROCEDURALGEOMETRYINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Live Category 4 Procedural Geometry Inspector backed by GeometrySpace::ProceduralGeometryRegistry + ProceduralGeometryAsset
//    Supports Parametric Sweep, Terrain, Boolean, Spline, Voronoi, Procedural Tree (Botanical), Building (Architectural), Tyre (Automotive)

#include "Engine/Editor/Inspectors/ProceduralGeometryInspector.h"
#include "Engine/Editor/Inspectors/GeometryInspectorShared.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

void ProceduralGeometryInspector::UpdateProceduralSurface(const ProceduralGeometryData& Instance)
{
    uint64_t Key = 0xA4040001ull ^ (static_cast<uint64_t>(Instance.GeneratorIndex) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.FeatureScaleMeters);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::GeometrySpecimenPreview, 3u,
        { static_cast<float>(Instance.GeneratorIndex), Instance.FeatureScaleMeters, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.68f, 0.64f, 0.56f, 1.0f }
    };
    ProceduralPreview.EnsureUpdatedGpu(Key, 96u, 96u, GpuSpec, [&](std::vector<uint8_t>& Pixels)
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

float ProceduralGeometryInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, ProceduralGeometryData& Instance,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    UpdateProceduralSurface(Instance);

    const auto& Registry = GeometrySpace::ProceduralGeometryRegistry::Global();
    const uint32_t SafeGenIdx = std::min(Instance.GeneratorIndex, 7u);
    LiveAsset.SyncCommonInspectorParameters(
        SafeGenIdx,
        Instance.RandomSeed,
        Instance.SubdivisionLevel,
        Instance.FeatureScaleMeters,
        Instance.AmplitudeMeters,
        Registry);

    if (Instance.AutoRebuild || !LiveAsset.IsValid())
    {
        if (LiveAsset.EnsureUpToDate(false, Registry))
        {
            Instance.BuildRevision = LiveAsset.QueryBuildRevision();
        }
    }

    const auto* ActiveGen = Registry.FindGenerator(LiveAsset.QueryGeneratorTypeId());
    const auto  Dims      = LiveAsset.IsValid()
        ? LiveAsset.QueryOutput().DimensionsMeters()
        : Mathematics::Scalar3D{ Instance.FeatureScaleMeters, Instance.FeatureScaleMeters, std::max(0.2f, Instance.AmplitudeMeters) };

    GeometryUi::GeometrySummaryDesc Desc {};
    Desc.ShapeName      = ActiveGen ? ActiveGen->QueryDisplayName().data() : "Procedural Mesh";
    Desc.DomainLabel    = ActiveGen ? GeometrySpace::ProceduralGeometryRegistry::DomainDisplayName(ActiveGen->QueryDomain()) : "Procedural";
    Desc.WidthMeters    = Dims.X;
    Desc.HeightMeters   = Dims.Z;
    Desc.DepthMeters    = Dims.Y;
    Desc.TriangleCount  = LiveAsset.IsValid() ? LiveAsset.QueryOutput().TriangleCount : 512u;
    Desc.ClusterCount   = std::max(1u, (Desc.TriangleCount + 127u) / 128u);
    Desc.LodLevels      = 3u;
    Desc.SubPartCount   = LiveAsset.IsValid() ? static_cast<uint32_t>(std::max<std::size_t>(1u, LiveAsset.QueryOutput().SubParts.size())) : 1u;
    Desc.SurfaceBinding = &Instance.Surface;
    Desc.Scene          = &Scene;

    Desc.ConstructCategoryCard = [&](PixelSpace& Srf, float WX, float CY, float WW, uint32_t& DragId, const PaneInputReading& Inp, bool& Mut) -> float
    {
        const float PadX = 25.0f;
        const float BX   = WX + PadX;
        const float BW   = WW - PadX * 2.0f;
        const Rectangle2D Card { WX, CY, WW, 412.0f };
        GeometryUi::ConstructGeomCard(Srf, Card);
        WorkspaceAppearance::ConstructWeightedText(Srf, FontWeightClass::Regular, { BX, Card.Y + 24.0f }, 15.0f, InspectorMetrics::HeadingInk, "Procedural generator");
        float Y = Card.Y + 58.0f;

        static constexpr std::array<const char*, 4u> SurfaceRow = {{ "Sweep", "Terrain", "Boolean", "Spline" }};
        static constexpr std::array<const char*, 4u> DomainRow  = {{ "Voronoi", "Tree", "Building", "Tyre" }};
        int32_t Pick0 = -1, Pick1 = -1;
        Y = InspectorPrimitives::ConstructSegmentTrack(Srf, BX, Y, BW, "SURFACE GENERATORS", SurfaceRow, (Instance.GeneratorIndex < 4u) ? Instance.GeneratorIndex : 99u, Inp, Pick0);
        if (Pick0 >= 0 && Pick0 < 4) { Instance.GeneratorIndex = static_cast<uint32_t>(Pick0); Mut = true; }
        Y = InspectorPrimitives::ConstructSegmentTrack(Srf, BX, Y, BW, "DOMAIN GENERATORS", DomainRow, (Instance.GeneratorIndex >= 4u) ? (Instance.GeneratorIndex - 4u) : 99u, Inp, Pick1);
        if (Pick1 >= 0 && Pick1 < 4) { Instance.GeneratorIndex = 4u + static_cast<uint32_t>(Pick1); Mut = true; }

        float SeedF = static_cast<float>(Instance.RandomSeed);
        float SubF  = static_cast<float>(Instance.SubdivisionLevel);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8840u, "Random seed",   "",  "%.0f", 1.0f,  999.0f, SeedF,                       DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8841u, "Subdivision",   "",  "%.0f", 1.0f,  8.0f,   SubF,                        DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8842u, "Feature scale", "m", "%.2f", 0.25f, 16.0f,  Instance.FeatureScaleMeters, DragId, Inp, Mut);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Srf, BX, Y, BW, 8843u, "Amplitude / H", "m", "%.2f", 0.10f, 24.0f,  Instance.AmplitudeMeters,    DragId, Inp, Mut);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Srf, BX, Y, BW, "Auto rebuild on edit", Instance.AutoRebuild, Inp, Mut);
        if (Mut)
        {
            Instance.RandomSeed       = static_cast<uint32_t>(std::clamp(std::lround(SeedF), 1l, 9999l));
            Instance.SubdivisionLevel = static_cast<uint32_t>(std::clamp(std::lround(SubF),  1l, 8l));
            LiveAsset.SyncCommonInspectorParameters(
                std::min(Instance.GeneratorIndex, 7u),
                Instance.RandomSeed,
                Instance.SubdivisionLevel,
                Instance.FeatureScaleMeters,
                Instance.AmplitudeMeters,
                Registry);
            if (Instance.AutoRebuild && LiveAsset.EnsureUpToDate(false, Registry))
            {
                Instance.BuildRevision = LiveAsset.QueryBuildRevision();
            }
        }
        return Card.Bottom() + 14.0f;
    };

    return GeometryUi::ConstructGeometryPages(Surface, LeftX, TopY, InnerWidth, Entry, Desc, ActivePage, ActiveDragId, Input, OutMutated, OutPageSwitched);
}

} // namespace Frontier
