/*============================================================================================================================================
                                                    COMPONENTEDITORRENDERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 3 Vulkan .slang verification: OpenPbrDirect (Zero GI), 7 inspection channels, Layer-1 X-ray constraints, 128x128 icon & STL/PLY/GLB

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentSpecimenBaker.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Interchange/GeometryInterchange.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::ComponentClass;
using namespace Frontier::GeometricRaster;
using namespace Frontier::GeometricRaster::ComponentEditor;

bool Require(bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::printf("[PASS] %s\n", Label);
    return true;
}

double ComputeRgbVariance(const std::vector<uint8_t>& Rgba)
{
    if (Rgba.size() < 4u) return 0.0;
    const std::size_t N = Rgba.size() / 4u;
    double MeanR = 0.0, MeanG = 0.0, MeanB = 0.0;
    for (std::size_t I = 0u; I < N; ++I)
    {
        MeanR += Rgba[I * 4u + 0u];
        MeanG += Rgba[I * 4u + 1u];
        MeanB += Rgba[I * 4u + 2u];
    }
    MeanR /= static_cast<double>(N);
    MeanG /= static_cast<double>(N);
    MeanB /= static_cast<double>(N);

    double Var = 0.0;
    for (std::size_t I = 0u; I < N; ++I)
    {
        const double DR = Rgba[I * 4u + 0u] - MeanR;
        const double DG = Rgba[I * 4u + 1u] - MeanG;
        const double DB = Rgba[I * 4u + 2u] - MeanB;
        Var += DR * DR + DG * DG + DB * DB;
    }
    return Var / static_cast<double>(N);
}

bool ValidateExtendedInterchangeFormats()
{
    // 1. Binary STL tetrahedron
    std::vector<uint8_t> StlBytes(84u + 4u * 50u, 0u);
    const uint32_t TriCount = 4u;
    std::memcpy(StlBytes.data() + 80u, &TriCount, 4u);
    auto WriteTri = [&](uint32_t Index, const float A[3], const float B[3], const float C[3])
    {
        uint8_t* Dst = StlBytes.data() + 84u + static_cast<std::size_t>(Index) * 50u + 12u;
        std::memcpy(Dst + 0u,  A, 12u);
        std::memcpy(Dst + 12u, B, 12u);
        std::memcpy(Dst + 24u, C, 12u);
    };
    const float P0[3] = { 0.0f, 0.0f, 0.0f };
    const float P1[3] = { 1.0f, 0.0f, 0.0f };
    const float P2[3] = { 0.0f, 1.0f, 0.0f };
    const float P3[3] = { 0.0f, 0.0f, 1.0f };
    WriteTri(0u, P0, P2, P1);
    WriteTri(1u, P0, P1, P3);
    WriteTri(2u, P1, P2, P3);
    WriteTri(3u, P2, P0, P3);

    GeometrySpace::ConstructedSampleAsset StlAsset {};
    std::string Diag {};
    if (!Require(Interchange::GeometryInterchange::ImportStlBytes(StlBytes, "TestStl", StlAsset, Diag) &&
                 StlAsset.Network.Faces.size() == 4u,
                 "GeometryInterchange::ImportStlBytes decodes 4-face binary STL"))
    {
        return false;
    }

    // 2. ASCII PLY quad
    const std::string PlyText =
        "ply\n"
        "format ascii 1.0\n"
        "element vertex 4\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n"
        "0 0 0\n"
        "1 0 0\n"
        "1 1 0\n"
        "0 1 0\n"
        "4 0 1 2 3\n";
    GeometrySpace::ConstructedSampleAsset PlyAsset {};
    const std::span<const uint8_t> PlySpan(reinterpret_cast<const uint8_t*>(PlyText.data()), PlyText.size());
    if (!Require(Interchange::GeometryInterchange::ImportPlyBytes(PlySpan, "TestPly", PlyAsset, Diag) &&
                 PlyAsset.Network.Faces.size() == 2u,
                 "GeometryInterchange::ImportPlyBytes triangulates ASCII PLY quad"))
    {
        return false;
    }

    // 3. GLB binary container (header + JSON + BIN chunk)
    const float Pos[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    const uint16_t Ind[3] = { 0u, 1u, 2u };
    std::vector<uint8_t> BinChunk(36u + 8u, 0u);
    std::memcpy(BinChunk.data(), Pos, 36u);
    std::memcpy(BinChunk.data() + 36u, Ind, 6u);

    std::string JsonChunk =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":44}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]}";
    while ((JsonChunk.size() % 4u) != 0u) JsonChunk.push_back(' ');

    const uint32_t TotalLen = static_cast<uint32_t>(12u + 8u + JsonChunk.size() + 8u + BinChunk.size());
    std::vector<uint8_t> Glb(TotalLen, 0u);
    auto WriteU32 = [&](std::size_t Off, uint32_t V) { std::memcpy(Glb.data() + Off, &V, 4u); };
    WriteU32(0u, 0x46546C67u); // "glTF"
    WriteU32(4u, 2u);
    WriteU32(8u, TotalLen);
    WriteU32(12u, static_cast<uint32_t>(JsonChunk.size()));
    WriteU32(16u, 0x4E4F534Au); // "JSON"
    std::memcpy(Glb.data() + 20u, JsonChunk.data(), JsonChunk.size());
    const std::size_t BinHdr = 20u + JsonChunk.size();
    WriteU32(BinHdr, static_cast<uint32_t>(BinChunk.size()));
    WriteU32(BinHdr + 4u, 0x004E4942u); // "BIN\0"
    std::memcpy(Glb.data() + BinHdr + 8u, BinChunk.data(), BinChunk.size());

    GeometrySpace::ConstructedSampleAsset GlbAsset {};
    if (!Require(Interchange::GeometryInterchange::ImportGltf2BinaryBytes(Glb, "TestGlb", GlbAsset, Diag) &&
                 GlbAsset.Network.Faces.size() == 1u,
                 "GeometryInterchange::ImportGltf2BinaryBytes decodes GLB JSON + BIN chunks"))
    {
        return false;
    }
    return true;
}

} // namespace

int main()
{
    std::printf("=== ComponentEditorRenderValidation ===\n");
    if (!ValidateExtendedInterchangeFormats())
    {
        return 1;
    }

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ModelsDir = RepoRoot / "Experimental/Shared/Models";
    const std::filesystem::path OutDir    = RepoRoot / "Exhibits/Images/ComponentEditor/Phase3Channels";

    VulkanInterchange Vulkan {};
    std::string Diag {};
    if (!Require(Vulkan.InitializeInterchange(RepoRoot, &Diag), "VulkanInterchange::InitializeInterchange"))
    {
        std::fprintf(stderr, "       Diagnostic: %s\n", Diag.c_str());
        return 2;
    }

    ComponentViewportRenderer Renderer {};
    if (!Require(Renderer.InitializeVulkan(Vulkan, ShaderDir), "ComponentViewportRenderer::InitializeVulkan (.slang SPIR-V)"))
    {
        return 3;
    }

    ControlVehicleBuildResult Build {};
    if (!Require(ControlVehicleClassBuilder::ConstructControlVehicle(ModelsDir, Build, Diag),
                 "ControlVehicleClassBuilder::ConstructControlVehicle"))
    {
        return 4;
    }

    const ComponentViewportCamera HeroCam = Renderer.ComputeFramedCameraForClass(
        Build.ClassDefinition, Build.GeometryRepository, 0.72f, 0.28f);

    struct ChannelCase
    {
        ComponentInspectionChannel Channel;
        const char*                FileName;
    };
    const ChannelCase Cases[] = {
        { ComponentInspectionChannel::Lit,         "01-Lit-OpenPbrDirect.png" },
        { ComponentInspectionChannel::Unlit,       "02-Unlit.png" },
        { ComponentInspectionChannel::Density,     "03-Density.png" },
        { ComponentInspectionChannel::Normals,     "04-Normals.png" },
        { ComponentInspectionChannel::UvChecker,   "05-UvChecker.png" },
        { ComponentInspectionChannel::LodClusters, "06-LodClusters.png" },
        { ComponentInspectionChannel::Collision,   "07-Collision.png" },
    };

    for (const auto& Case : Cases)
    {
        ComponentRenderOptions Opts {};
        Opts.Channel        = Case.Channel;
        Opts.ShowDotFloor   = true;
        Opts.ShowDebugHelpers = true;
        Opts.ElapsedSeconds = 0.5f;
        const auto FrameOut = Renderer.RenderClassViewport(
            Build.ClassDefinition, Build.GeometryRepository, HeroCam, Opts, 1024u, 640u, true);

        const double Var = ComputeRgbVariance(FrameOut.DisplayRgba8);
        if (!Require(FrameOut.VulkanGpuActive && FrameOut.DrawnTriangles > 10000u && Var > 20.0, Case.FileName))
        {
            return 5;
        }
        (void)ComponentViewportRenderer::WritePngFile(OutDir / Case.FileName, FrameOut.WidthPx, FrameOut.HeightPx, FrameOut.DisplayRgba8);

        if (Case.Channel == ComponentInspectionChannel::Lit)
        {
            const auto PickCenter = Renderer.QueryPickAtPixel(Build.ClassDefinition, 512u, 320u);
            if (!Require(PickCenter.Valid(), "MRT GPU PickID resolves ControlVehicle component at viewport center"))
            {
                return 6;
            }
        }
    }

    // Layer-1 X-ray Constraint visualization (Spring + Hinge + Dock)
    const ComponentInstanceToken SpringTok = Build.ClassDefinition.Storage.FindTokenByTitle("SuspensionFL");
    const ComponentInstanceToken DockTok   = Build.ClassDefinition.Storage.FindTokenByTitle("RearWingConstraint");
    {
        ComponentRenderOptions Opts {};
        Opts.Channel          = ComponentInspectionChannel::Lit;
        Opts.ShowDotFloor     = true;
        Opts.ShowDebugHelpers = true;
        Opts.ElapsedSeconds   = 0.75f;
        Opts.SelectedToken    = SpringTok;
        Opts.LiveConstraintCoordinates[SpringTok.SlotIndex] = -0.045f;
        const auto Out = Renderer.RenderClassViewport(Build.ClassDefinition, Build.GeometryRepository, HeroCam, Opts, 1024u, 640u, true);
        (void)ComponentViewportRenderer::WritePngFile(OutDir / "08-XRay-SpringConstraint.png", Out.WidthPx, Out.HeightPx, Out.DisplayRgba8);
        if (!Require(ComputeRgbVariance(Out.DisplayRgba8) > 20.0, "X-ray Spring Constraint renders cleanly over vehicle"))
        {
            return 7;
        }
    }
    {
        ComponentRenderOptions Opts {};
        Opts.Channel          = ComponentInspectionChannel::Lit;
        Opts.ShowDotFloor     = true;
        Opts.ShowDebugHelpers = true;
        Opts.ElapsedSeconds   = 0.75f;
        Opts.SelectedToken    = DockTok;
        Opts.DockIntactMask[DockTok.SlotIndex] = true;
        const auto Out = Renderer.RenderClassViewport(Build.ClassDefinition, Build.GeometryRepository, HeroCam, Opts, 1024u, 640u, true);
        (void)ComponentViewportRenderer::WritePngFile(OutDir / "09-XRay-DockConstraint.png", Out.WidthPx, Out.HeightPx, Out.DisplayRgba8);
    }

    // 128x128 transparent cutout thumbnail bake
    if (!Require(ComponentSpecimenBaker::BakeClassThumbnail(Renderer, Build.ClassDefinition, Build.GeometryRepository, 128u),
                 "ComponentSpecimenBaker::BakeClassThumbnail (128x128)"))
    {
        return 8;
    }
    const uint8_t CornerAlpha = Build.ClassDefinition.BakedIconRgba8[3u];
    const uint8_t CenterAlpha = Build.ClassDefinition.BakedIconRgba8[(64u * 128u + 64u) * 4u + 3u];
    if (!Require(CornerAlpha == 0u && CenterAlpha > 200u,
                 "Baked 128x128 thumbnail has transparent studio cutout corners and opaque vehicle silhouette"))
    {
        return 9;
    }
    (void)ComponentViewportRenderer::WritePngFile(
        OutDir / "10-ControlVehicle-Icon-128.png",
        Build.ClassDefinition.BakedIconWidth,
        Build.ClassDefinition.BakedIconHeight,
        Build.ClassDefinition.BakedIconRgba8);

    Renderer.TeardownVulkan();
    Vulkan.TeardownInterchange();
    std::printf("ALL ComponentEditorRenderValidation CHECKS PASSED.\n");
    return 0;
}
