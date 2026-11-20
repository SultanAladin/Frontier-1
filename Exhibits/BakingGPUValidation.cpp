/*==========================================================================================================================================
                                                        BAKINGGPUVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Vulkan GPU Baking Engine Validation: GTX BVH4 + optional RTX RayQuery 17-channel surface bake, OBJ/STL import, PNG export & 3D preview.

#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"
#include "Engine/GeometricRaster/Baking/GpuTextureBaker.h"
#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/Baking/TextureBakerRasterizer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

[[nodiscard]] bool Verify(bool Cond, const char* Msg) noexcept
{
    if (!Cond)
    {
        std::fprintf(stderr, "[BakingGPUValidation] FAIL: %s\n", Msg);
        return false;
    }
    return true;
}

[[nodiscard]] bool HasNonTrivialColorVariance(const std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H) noexcept
{
    if (W == 0u || H == 0u || Rgba.size() < static_cast<std::size_t>(W) * H * 4u)
    {
        return false;
    }
    uint8_t MinR = 255u, MaxR = 0u;
    uint8_t MinG = 255u, MaxG = 0u;
    uint8_t MinB = 255u, MaxB = 0u;
    for (std::size_t I = 0u; I + 3u < Rgba.size(); I += 16u)
    {
        MinR = std::min(MinR, Rgba[I + 0u]);
        MaxR = std::max(MaxR, Rgba[I + 0u]);
        MinG = std::min(MinG, Rgba[I + 1u]);
        MaxG = std::max(MaxG, Rgba[I + 1u]);
        MinB = std::min(MinB, Rgba[I + 2u]);
        MaxB = std::max(MaxB, Rgba[I + 2u]);
    }
    return (static_cast<unsigned>(MaxR) - static_cast<unsigned>(MinR)) > 8u ||
           (static_cast<unsigned>(MaxG) - static_cast<unsigned>(MinG)) > 8u ||
           (static_cast<unsigned>(MaxB) - static_cast<unsigned>(MinB)) > 8u;
}

[[nodiscard]] bool WriteMinimalObjFile(const std::filesystem::path& OutPath)
{
    std::ofstream Out(OutPath);
    if (!Out) return false;
    Out << "v -0.5 -0.5 0.0\n"
        << "v  0.5 -0.5 0.0\n"
        << "v  0.5  0.5 0.0\n"
        << "v -0.5  0.5 0.0\n"
        << "v  0.0  0.0 0.8\n"
        << "vt 0.1 0.1\n"
        << "vt 0.9 0.1\n"
        << "vt 0.9 0.9\n"
        << "vt 0.1 0.9\n"
        << "vt 0.5 0.5\n"
        << "f 1/1 2/2 3/3 4/4\n"
        << "f 1/1 2/2 5/5\n"
        << "f 2/2 3/3 5/5\n"
        << "f 3/3 4/4 5/5\n"
        << "f 4/4 1/1 5/5\n";
    return Out.good();
}

[[nodiscard]] bool WriteMinimalBinaryStlFile(const std::filesystem::path& OutPath)
{
    std::ofstream Out(OutPath, std::ios::binary | std::ios::trunc);
    if (!Out) return false;

    uint8_t Header[80] {};
    std::memcpy(Header, "FrontierTestSTL", 15u);
    Out.write(reinterpret_cast<const char*>(Header), 80);

    const uint32_t TriCount = 4u;
    Out.write(reinterpret_cast<const char*>(&TriCount), 4);

    struct TriPts { float V0[3]; float V1[3]; float V2[3]; };
    const TriPts Tris[4] {
        { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.25f, 0.25f, 1.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.25f, 0.25f, 1.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.25f, 1.0f } },
    };
    for (const auto& T : Tris)
    {
        const float ZeroN[3] { 0.0f, 0.0f, 0.0f };
        Out.write(reinterpret_cast<const char*>(ZeroN), 12);
        Out.write(reinterpret_cast<const char*>(T.V0), 12);
        Out.write(reinterpret_cast<const char*>(T.V1), 12);
        Out.write(reinterpret_cast<const char*>(T.V2), 12);
        const uint16_t Attr = 0u;
        Out.write(reinterpret_cast<const char*>(&Attr), 2);
    }
    return Out.good();
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::GeometricRaster::Baking;

#ifdef FRONTIER_REPO_ROOT_DIR
    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
#else
    const std::filesystem::path RepoRoot = std::filesystem::current_path();
#endif

    // 1. Validate GPU struct layouts match Slang shader byte layouts
    static_assert(sizeof(GpuBakeTriangle) == 112u);
    static_assert(sizeof(GpuLowSurfaceVertex) == 80u);
    static_assert(sizeof(GpuSurfaceBakeConfig) == 80u);
    static_assert(sizeof(GpuVolumeBakeConfig) == 64u);
    static_assert(sizeof(GpuPreviewConfig) == 160u);

    // 2. Validate all 5 High/Low studio studies
    for (uint8_t S = 0u; S < 5u; ++S)
    {
        const auto Study = StudyGeometryBuilder::ConstructStudioStudy(static_cast<StudyGeometryCategory>(S));
        if (!Verify(Study.LowTriangleCount > 0u && Study.HighTriangleCount > 0u, "Study has non-empty High & Low triangles") ||
            !Verify(Study.LowSurfaceVertices.size() == static_cast<std::size_t>(Study.LowTriangleCount) * 3u, "Study Low raster vertices aligned") ||
            !Verify(Study.UvIslandCount >= 1u, "Study UV island count >= 1") ||
            !Verify(Study.BoundsMax.Z > Study.BoundsMin.Z, "Study Z height non-degenerate"))
        {
            return 1;
        }
    }

    // 3. Validate OBJ & Binary STL intake + auto-UV charting for degenerate UVs
    const std::filesystem::path TempDir = std::filesystem::temp_directory_path() / "FrontierBakingGpuValidation";
    std::filesystem::create_directories(TempDir);
    {
        const std::filesystem::path ObjPath = TempDir / "TestStudy.obj";
        const std::filesystem::path StlPath = TempDir / "TestStudy.stl";
        if (!Verify(WriteMinimalObjFile(ObjPath), "Write temporary OBJ fixture") ||
            !Verify(WriteMinimalBinaryStlFile(StlPath), "Write temporary binary STL fixture"))
        {
            return 1;
        }
        GeometrySpace::PolygonNetwork ObjNet {};
        GeometrySpace::PolygonNetwork StlNet {};
        std::string Diag {};
        const PreparedStudyPair ObjPair = StudyGeometryBuilder::PrepareFromPolygonNetworks(ObjNet, ObjNet, "TestObj");
        if (!Verify(StudyGeometryBuilder::ImportPolygonNetworkFromFile(ObjPath, ObjNet, true, &Diag), Diag.c_str()) ||
            !Verify(ObjNet.Faces.size() == 5u, "OBJ imports 1 quad + 4 side faces") ||
            !Verify(StudyGeometryBuilder::PrepareFromPolygonNetworks(ObjNet, ObjNet, "TestObj").LowTriangleCount == 6u, "OBJ quad triangulates to 6 GPU triangles") ||
            !Verify(StudyGeometryBuilder::ImportPolygonNetworkFromFile(StlPath, StlNet, true, &Diag), Diag.c_str()) ||
            !Verify(StlNet.Faces.size() == 4u && !StlNet.ExtractParameterizedUVStream().empty(), "Binary STL loads 4 triangles with auto UVs"))
        {
            return 1;
        }
        (void)ObjPair;
    }

    // 4. Initialize Vulkan 1.2 + Baker + Viewport Rasterizer
    VulkanInterchange Device {};
    std::string VkDiag {};
    if (!Verify(Device.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    std::printf(
        "[BakingGPUValidation] Vulkan Adapter: %.*s (Hardware GPU: %s, RTX RayQuery: %s)\n",
        static_cast<int>(Device.QueryAdapterName().size()),
        Device.QueryAdapterName().data(),
        Device.QueryHardwareAccelerated() ? "YES" : "NO",
        Device.QueryHardwareRayQuerySupported() ? "YES" : "NO");

    GpuTextureBaker Baker {};
    if (!Verify(Baker.InitializeBaker(&Device), "Initialize GpuTextureBaker"))
    {
        return 1;
    }

    TextureBakerRasterizer ViewportRasterizer {};
    if (!Verify(ViewportRasterizer.InitializeRasterizer(&Device, &Baker), "Initialize TextureBakerRasterizer"))
    {
        return 1;
    }

    const PreparedStudyPair Teapot = StudyGeometryBuilder::ConstructStudioStudy(StudyGeometryCategory::UtahTeapot);
    if (!Verify(Baker.UploadStudyGeometry(Teapot), "Upload UtahTeapot to GPU baker"))
    {
        return 1;
    }

    // 5. Execute GTX BVH4 17-channel GPU surface bake
    SurfaceBakeExecutionSpec SurfSpec {};
    SurfSpec.ResolutionPx           = 256u;
    SurfSpec.AoSampleCount          = 8u;
    SurfSpec.FrontCageMeters        = 0.14f;
    SurfSpec.RearCageMeters         = 0.14f;
    SurfSpec.AoMaxRayDistanceMeters = 0.45f;
    SurfSpec.TangentSignY           = 1.0f;
    SurfSpec.EdgePaddingPx          = 4u;
    SurfSpec.ActiveTargetBitmask    = 0x1FFFFu;
    SurfSpec.PreferComputeBvh4      = true;

    const BakeCompletionSummary GtxSurf = Baker.ExecuteSurfaceBake(SurfSpec);
    if (!Verify(GtxSurf.SurfaceSucceeded, GtxSurf.DiagnosticText.c_str()) ||
        !Verify(GtxSurf.ExecutedBackend == BakeAccelerationEngine::GtxComputeBvh4, "GTX Compute BVH4 selected"))
    {
        return 1;
    }

    // Verify critical baked channels have real GPU pixel variance
    constexpr std::array<std::pair<uint32_t, const char*>, 8u> ChannelsToCheck {{
        { 0u,  "Tangent Normal" },
        { 1u,  "World Normal" },
        { 5u,  "Ambient Occlusion" },
        { 7u,  "Curvature" },
        { 10u, "Thickness" },
        { 11u, "Height / Displacement" },
        { 13u, "UV Island ID" },
        { 16u, "UV Wireframe" },
    }};
    for (const auto& [Slot, Label] : ChannelsToCheck)
    {
        const auto& Pixels = Baker.InspectChannelRgba8(Slot);
        if (!Verify(HasNonTrivialColorVariance(Pixels, 256u, 256u), Label))
        {
            return 1;
        }
    }

    // Optional RTX hardware ray query validation when adapter supports VK_KHR_ray_query
    if (Baker.QueryHardwareRayQueryAvailable())
    {
        SurfSpec.PreferComputeBvh4 = false;
        const BakeCompletionSummary RtxSurf = Baker.ExecuteSurfaceBake(SurfSpec);
        if (!Verify(RtxSurf.SurfaceSucceeded, RtxSurf.DiagnosticText.c_str()) ||
            !Verify(RtxSurf.ExecutedBackend == BakeAccelerationEngine::RtxHardwareRayQuery, "RTX RayQuery selected") ||
            !Verify(HasNonTrivialColorVariance(Baker.InspectChannelRgba8(5u), 256u, 256u), "RTX AO pixels non-trivial"))
        {
            return 1;
        }
    }

    // 6. Export PNG and validate output artifacts
    const std::filesystem::path PngPath = TempDir / "Baked_AO.png";
    if (!Verify(Baker.ExportSurfaceChannelPng(5u, PngPath), "Export AO PNG") ||
        !Verify(std::filesystem::file_size(PngPath) > 512u, "PNG file non-trivial"))
    {
        return 1;
    }

    // 7. Execute 3D studio preview rasterizer (unbaked + baked + HighWire + Cage)
    BakeViewportRenderSpec ViewSpec {};
    ViewSpec.WidthPx             = 640u;
    ViewSpec.HeightPx            = 480u;
    ViewSpec.InspectedTargetId   = 5u; // Ambient Occlusion
    ViewSpec.SurfaceBaked        = true;
    ViewSpec.ShowHighWireOverlay = true;
    ViewSpec.ShowCageOverlay     = true;
    ViewSpec.FrontCageMeters     = 0.14f;
    TextureBakerRasterizer::AlignCameraToStudy(Teapot, ViewSpec.Camera);

    if (!Verify(ViewportRasterizer.RenderSpatialViewport(ViewSpec), "Render 3D studio preview on Vulkan") ||
        !Verify(HasNonTrivialColorVariance(ViewportRasterizer.InspectSpatialRgba8(), 640u, 480u), "3D preview framebuffer shows shaded mesh"))
    {
        return 1;
    }

    // Clean teardown before exit
    std::error_code CleanupEc;
    std::filesystem::remove_all(TempDir, CleanupEc);
    ViewportRasterizer.TeardownRasterizer();
    Baker.TeardownBaker();
    Device.TeardownInterchange();

    std::printf("[BakingGPUValidation] ALL GPU BAKING CHECKS PASSED\n");
    return 0;
}
