/*============================================================================================================================================
                                                        DEFAULTCONTENTGENERATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of DefaultContentGenerator populating and verifying the complete zero-abbreviation Content/ .Space repository

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Tools/DefaultContentGenerator.h"

#include "Engine/Codecs/ComponentSpaceCodec.h"
#include "Engine/Codecs/ControlVehicleSurfaceDecoder.h"
#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Codecs/ImageCodec.h"
#include "Engine/Codecs/MaterialCodec.h"
#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Codecs/ProjectCodec.h"
#include "Engine/Codecs/RenderSpaceCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Codecs/TextureCodec.h"
#include "Engine/Codecs/UVCodec.h"
#include "Engine/Codecs/VolumetricSpaceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Frontier::Tools
{

namespace
{

[[nodiscard]] bool RecordWrittenFile(
    const std::filesystem::path& RepositoryRoot,
    const std::filesystem::path& AbsoluteFile,
    GeneratedContentReport&      InOutReport,
    std::string&                 OutDiagnostic)
{
    std::error_code IoError{};
    const auto ByteSize = std::filesystem::file_size(AbsoluteFile, IoError);
    if (IoError || ByteSize == 0u)
    {
        OutDiagnostic = "Generated container missing or empty: " + AbsoluteFile.generic_string();
        return false;
    }

    Codecs::SpaceContainerPayload Decoded{};
    if (!Codecs::SpaceContainerDecoder::DecodeFromFile(AbsoluteFile, Decoded, OutDiagnostic))
    {
        return false;
    }
    if ((Decoded.Tier2StartOffset % 64u) != 0u)
    {
        OutDiagnostic = "Unaligned Tier2StartOffset in " + AbsoluteFile.generic_string();
        return false;
    }

    InOutReport.GeneratedAssetCount += 1u;
    InOutReport.VerifiedAssetCount  += 1u;
    InOutReport.TotalPayloadBytes   += static_cast<std::size_t>(ByteSize);
    InOutReport.WrittenRelativePaths.push_back(std::filesystem::relative(AbsoluteFile, RepositoryRoot, IoError));
    return true;
}

[[nodiscard]] Codecs::MaterialSpaceDocumentModel BuildMaterialModel(
    std::string_view                      Title,
    const MaterialSpace::SurfaceMaterial& Surface)
{
    Codecs::MaterialSpaceDocumentModel Model{};
    Model.MaterialTitle            = std::string(Title);
    Model.ShaderSourceTag          = "OpenPbrSurfaceScattering.slang";
    Model.IndexOfRefraction        = static_cast<double>(Surface.Specular.DielectricIOR);
    Model.PhysicalMaterialIncluded = true;
    Model.AuthoredMaterial         = Surface;
    Model.AuthoredMaterial.MaterialName = std::string(Title);

    Model.CompiledRegisters.push_back(MaterialSpace::CompileCompactMaterialEntry(Model.AuthoredMaterial));
    Model.CompiledBounceSummaries.push_back(MaterialSpace::CompileBounceSlabEntry(Model.AuthoredMaterial));
    return Model;
}

void BuildBoxPolygonDocument(
    std::string_view              Title,
    Mathematics::Scalar3D         HalfExtents,
    std::string_view              MaterialPath,
    std::string_view              SurfaceParamPath,
    Codecs::GeometrySpaceDocument& OutDoc)
{
    OutDoc                        = Codecs::GeometrySpaceDocument{};
    OutDoc.Network.NetworkTitle   = std::string(Title);
    OutDoc.CreaseAngleDegrees     = 38.0f;
    OutDoc.SurfaceParameterPath   = std::string(SurfaceParamPath);
    OutDoc.SurfaceSlots.push_back({ 0u, "PrimarySurface", std::string(MaterialPath) });

    const float Hx = HalfExtents.X;
    const float Hy = HalfExtents.Y;
    const float Hz = HalfExtents.Z;
    const std::array<Mathematics::Axis3D, 8u> Corners {{
        { -Hx, -Hy, -Hz }, {  Hx, -Hy, -Hz }, {  Hx,  Hy, -Hz }, { -Hx,  Hy, -Hz },
        { -Hx, -Hy,  Hz }, {  Hx, -Hy,  Hz }, {  Hx,  Hy,  Hz }, { -Hx,  Hy,  Hz },
    }};
    for (const auto& Pos : Corners)
    {
        (void)OutDoc.Network.AppendVertex(Pos);
    }

    struct QuadSpec
    {
        uint32_t            A, B, C, D;
        Mathematics::Axis3D Normal;
    };
    const std::array<QuadSpec, 6u> Quads {{
        { 4u, 5u, 6u, 7u, {  0.0f,  0.0f,  1.0f } },
        { 0u, 3u, 2u, 1u, {  0.0f,  0.0f, -1.0f } },
        { 1u, 2u, 6u, 5u, {  1.0f,  0.0f,  0.0f } },
        { 3u, 0u, 4u, 7u, { -1.0f,  0.0f,  0.0f } },
        { 2u, 3u, 7u, 6u, {  0.0f,  1.0f,  0.0f } },
        { 0u, 1u, 5u, 4u, {  0.0f, -1.0f,  0.0f } },
    }};
    for (const QuadSpec& Q : Quads)
    {
        const uint32_t Tri0[3]{ Q.A, Q.B, Q.C };
        const uint32_t Tri1[3]{ Q.A, Q.C, Q.D };
        (void)OutDoc.Network.AppendFace(Tri0, {}, {}, 0u, 0u);
        (void)OutDoc.Network.AppendFace(Tri1, {}, {}, 0u, 0u);
        for (int Tri = 0; Tri < 2; ++Tri)
        {
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                OutDoc.FaceCornerNormals.push_back(Q.Normal.X);
                OutDoc.FaceCornerNormals.push_back(Q.Normal.Y);
                OutDoc.FaceCornerNormals.push_back(Q.Normal.Z);
            }
        }
    }
    OutDoc.ClustersIncluded = OutDoc.ClusterHierarchy.BuildFromPolygonNetwork(OutDoc.Network, 3u);
}

void BuildSpherePolygonDocument(
    std::string_view               Title,
    float                          RadiusMeters,
    uint32_t                       Slices,
    uint32_t                       Stacks,
    std::string_view               MaterialPath,
    std::string_view               SurfaceParamPath,
    Codecs::GeometrySpaceDocument& OutDoc)
{
    OutDoc                      = Codecs::GeometrySpaceDocument{};
    OutDoc.Network.NetworkTitle = std::string(Title);
    OutDoc.CreaseAngleDegrees   = 89.0f;
    OutDoc.SurfaceParameterPath = std::string(SurfaceParamPath);
    OutDoc.SurfaceSlots.push_back({ 0u, "PrimarySurface", std::string(MaterialPath) });

    std::vector<Mathematics::Axis3D> UnitNormals;
    for (uint32_t St = 0u; St <= Stacks; ++St)
    {
        const float V     = static_cast<float>(St) / static_cast<float>(Stacks);
        const float Theta = V * static_cast<float>(Constants::Pi);
        const float SinT  = std::sin(Theta);
        const float CosT  = std::cos(Theta);
        for (uint32_t Sl = 0u; Sl <= Slices; ++Sl)
        {
            const float U   = static_cast<float>(Sl) / static_cast<float>(Slices);
            const float Phi = U * 2.0f * static_cast<float>(Constants::Pi);
            const Mathematics::Axis3D Nrm{ SinT * std::cos(Phi), SinT * std::sin(Phi), CosT };
            UnitNormals.push_back(Nrm);
            (void)OutDoc.Network.AppendVertex(Nrm * RadiusMeters);
        }
    }

    const uint32_t RowStride = Slices + 1u;
    for (uint32_t St = 0u; St < Stacks; ++St)
    {
        for (uint32_t Sl = 0u; Sl < Slices; ++Sl)
        {
            const uint32_t I0 = St * RowStride + Sl;
            const uint32_t I1 = I0 + 1u;
            const uint32_t I2 = (St + 1u) * RowStride + Sl;
            const uint32_t I3 = I2 + 1u;
            if (St > 0u)
            {
                const uint32_t TriA[3]{ I0, I2, I1 };
                (void)OutDoc.Network.AppendFace(TriA, {}, {}, 0u, 0u);
                for (uint32_t Idx : TriA)
                {
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].X);
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].Y);
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].Z);
                }
            }
            if (St + 1u < Stacks)
            {
                const uint32_t TriB[3]{ I1, I2, I3 };
                (void)OutDoc.Network.AppendFace(TriB, {}, {}, 0u, 0u);
                for (uint32_t Idx : TriB)
                {
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].X);
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].Y);
                    OutDoc.FaceCornerNormals.push_back(UnitNormals[Idx].Z);
                }
            }
        }
    }
    OutDoc.ClustersIncluded = OutDoc.ClusterHierarchy.BuildFromPolygonNetwork(OutDoc.Network, 3u);
}

void BuildFrustumPolygonDocument(
    std::string_view               Title,
    float                          BottomRadius,
    float                          TopRadius,
    float                          HeightMeters,
    uint32_t                       RadialSegments,
    std::string_view               MaterialPath,
    Codecs::GeometrySpaceDocument& OutDoc)
{
    OutDoc                      = Codecs::GeometrySpaceDocument{};
    OutDoc.Network.NetworkTitle = std::string(Title);
    OutDoc.CreaseAngleDegrees   = 45.0f;
    OutDoc.SurfaceSlots.push_back({ 0u, "PrimarySurface", std::string(MaterialPath) });

    const float HalfH       = HeightMeters * 0.5f;
    const uint32_t BotCenter = OutDoc.Network.AppendVertex({ 0.0f, 0.0f, -HalfH });
    const uint32_t TopCenter = OutDoc.Network.AppendVertex({ 0.0f, 0.0f,  HalfH });

    for (uint32_t Seg = 0u; Seg < RadialSegments; ++Seg)
    {
        const float Phi = (static_cast<float>(Seg) / static_cast<float>(RadialSegments)) * 2.0f * static_cast<float>(Constants::Pi);
        const float C   = std::cos(Phi);
        const float S   = std::sin(Phi);
        (void)OutDoc.Network.AppendVertex({ C * BottomRadius, S * BottomRadius, -HalfH });
        (void)OutDoc.Network.AppendVertex({ C * TopRadius,    S * TopRadius,     HalfH });
    }

    const float SlopeZ = (BottomRadius - TopRadius) / std::max(0.01f, HeightMeters);
    for (uint32_t Seg = 0u; Seg < RadialSegments; ++Seg)
    {
        const uint32_t NextSeg = (Seg + 1u) % RadialSegments;
        const uint32_t B0      = 2u + Seg * 2u;
        const uint32_t T0      = B0 + 1u;
        const uint32_t B1      = 2u + NextSeg * 2u;
        const uint32_t T1      = B1 + 1u;

        const float Phi0 = (static_cast<float>(Seg) / static_cast<float>(RadialSegments)) * 2.0f * static_cast<float>(Constants::Pi);
        const float Phi1 = (static_cast<float>(NextSeg) / static_cast<float>(RadialSegments)) * 2.0f * static_cast<float>(Constants::Pi);
        const Mathematics::Axis3D N0 = Mathematics::NormalizeAxis({ std::cos(Phi0), std::sin(Phi0), SlopeZ }, { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D N1 = Mathematics::NormalizeAxis({ std::cos(Phi1), std::sin(Phi1), SlopeZ }, { 1.0f, 0.0f, 0.0f });

        const uint32_t Side0[3]{ B0, B1, T1 };
        (void)OutDoc.Network.AppendFace(Side0, {}, {}, 0u, 0u);
        for (const auto& N : { N0, N1, N1 })
        {
            OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { N.X, N.Y, N.Z });
        }

        if (TopRadius > 1.0e-4f)
        {
            const uint32_t Side1[3]{ B0, T1, T0 };
            (void)OutDoc.Network.AppendFace(Side1, {}, {}, 0u, 0u);
            for (const auto& N : { N0, N1, N0 })
            {
                OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { N.X, N.Y, N.Z });
            }

            const uint32_t CapTop[3]{ TopCenter, T0, T1 };
            (void)OutDoc.Network.AppendFace(CapTop, {}, {}, 0u, 0u);
            for (int K = 0; K < 3; ++K)
            {
                OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { 0.0f, 0.0f, 1.0f });
            }
        }

        const uint32_t CapBot[3]{ BotCenter, B1, B0 };
        (void)OutDoc.Network.AppendFace(CapBot, {}, {}, 0u, 0u);
        for (int K = 0; K < 3; ++K)
        {
            OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { 0.0f, 0.0f, -1.0f });
        }
    }
    OutDoc.ClustersIncluded = OutDoc.ClusterHierarchy.BuildFromPolygonNetwork(OutDoc.Network, 3u);
}

void BuildTorusPolygonDocument(
    std::string_view               Title,
    float                          MajorRadius,
    float                          MinorRadius,
    uint32_t                       MajorSegments,
    uint32_t                       MinorSegments,
    std::string_view               MaterialPath,
    Codecs::GeometrySpaceDocument& OutDoc)
{
    OutDoc                      = Codecs::GeometrySpaceDocument{};
    OutDoc.Network.NetworkTitle = std::string(Title);
    OutDoc.CreaseAngleDegrees   = 89.0f;
    OutDoc.SurfaceSlots.push_back({ 0u, "PrimarySurface", std::string(MaterialPath) });

    std::vector<Mathematics::Axis3D> Normals;
    for (uint32_t Maj = 0u; Maj < MajorSegments; ++Maj)
    {
        const float U  = (static_cast<float>(Maj) / static_cast<float>(MajorSegments)) * 2.0f * static_cast<float>(Constants::Pi);
        const float Cu = std::cos(U);
        const float Su = std::sin(U);
        for (uint32_t Min = 0u; Min < MinorSegments; ++Min)
        {
            const float V  = (static_cast<float>(Min) / static_cast<float>(MinorSegments)) * 2.0f * static_cast<float>(Constants::Pi);
            const float Cv = std::cos(V);
            const float Sv = std::sin(V);
            const Mathematics::Axis3D Nrm{ Cu * Cv, Su * Cv, Sv };
            const Mathematics::Axis3D Pos{ (MajorRadius + MinorRadius * Cv) * Cu, (MajorRadius + MinorRadius * Cv) * Su, MinorRadius * Sv };
            Normals.push_back(Nrm);
            (void)OutDoc.Network.AppendVertex(Pos);
        }
    }

    for (uint32_t Maj = 0u; Maj < MajorSegments; ++Maj)
    {
        const uint32_t NextMaj = (Maj + 1u) % MajorSegments;
        for (uint32_t Min = 0u; Min < MinorSegments; ++Min)
        {
            const uint32_t NextMin = (Min + 1u) % MinorSegments;
            const uint32_t I0      = Maj * MinorSegments + Min;
            const uint32_t I1      = NextMaj * MinorSegments + Min;
            const uint32_t I2      = NextMaj * MinorSegments + NextMin;
            const uint32_t I3      = Maj * MinorSegments + NextMin;
            const uint32_t TriA[3]{ I0, I1, I2 };
            const uint32_t TriB[3]{ I0, I2, I3 };
            (void)OutDoc.Network.AppendFace(TriA, {}, {}, 0u, 0u);
            for (uint32_t Idx : TriA)
            {
                OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { Normals[Idx].X, Normals[Idx].Y, Normals[Idx].Z });
            }
            (void)OutDoc.Network.AppendFace(TriB, {}, {}, 0u, 0u);
            for (uint32_t Idx : TriB)
            {
                OutDoc.FaceCornerNormals.insert(OutDoc.FaceCornerNormals.end(), { Normals[Idx].X, Normals[Idx].Y, Normals[Idx].Z });
            }
        }
    }
    OutDoc.ClustersIncluded = OutDoc.ClusterHierarchy.BuildFromPolygonNetwork(OutDoc.Network, 3u);
}

[[nodiscard]] Codecs::UVSpaceDocumentModel BuildSurfaceParameterDocument(
    std::string_view Title,
    double           ReferenceScaleMeters,
    uint32_t         SampleQuadCount)
{
    Codecs::UVSpaceDocumentModel Doc{};
    Doc.DocumentTitle        = std::string(Title);
    Doc.GridColumns          = 10u;
    Doc.ReferenceScaleMeters = ReferenceScaleMeters;
    Doc.Tiles.push_back({ 0, 0, 1001u, 1024.0, 8.0 });
    Doc.ParameterizedUVPairs.reserve(SampleQuadCount * 8u);
    for (uint32_t Q = 0u; Q < SampleQuadCount; ++Q)
    {
        const float BaseU = static_cast<float>(Q % 8u) * 0.125f;
        const float BaseV = static_cast<float>((Q / 8u) % 8u) * 0.125f;
        const float Step  = 0.110f;
        Doc.ParameterizedUVPairs.insert(
            Doc.ParameterizedUVPairs.end(),
            { BaseU, BaseV, BaseU + Step, BaseV, BaseU + Step, BaseV + Step, BaseU, BaseV + Step });
    }
    return Doc;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              DEFAULT CONTENT GENERATOR
//------------------------------------------------------------------------------------------------------------------------

bool DefaultContentGenerator::GenerateRepositoryContent(
    const std::filesystem::path& RepositoryRoot,
    GeneratedContentReport&      OutReport,
    std::string&                 OutDiagnostic)
{
    OutReport = GeneratedContentReport{};
    const std::filesystem::path ContentRoot = RepositoryRoot / "Content";

    // 1. Architectural & Automotive Materials (.MaterialSpace)
    struct NamedMaterialEntry
    {
        const char*                   SubFolder;
        const char*                   Stem;
        MaterialSpace::SurfaceMaterial Material;
    };

    auto MakeMat = [](const char* Name, Mathematics::LinearRGB Col, float Rough, float Metal, float Coat = 0.0f, float CoatRough = 0.04f, float Trans = 0.0f, float Ior = 1.50f)
    {
        MaterialSpace::SurfaceMaterial M{};
        M.MaterialName                    = Name;
        M.Diffuse.BaseColor               = Col;
        M.Specular.Roughness              = Rough;
        M.Specular.Metalness              = Metal;
        M.Specular.DielectricIOR          = Ior;
        M.Clearcoat.CoatWeight            = Coat;
        M.Clearcoat.CoatRoughness         = CoatRough;
        M.Transmission.TransmissionWeight = Trans;
        return M;
    };

    MaterialSpace::SurfaceMaterial XenonMat = MakeMat("XenonHeadlampOptic", { 0.90f, 0.94f, 1.00f }, 0.04f, 0.0f, 0.92f, 0.02f);
    XenonMat.PorosityLight.EmissionTint          = { 1.0f, 0.97f, 0.92f };
    XenonMat.PorosityLight.EmissionLuminanceNits = 24.0f;

    MaterialSpace::SurfaceMaterial PaintMat = MakeMat("ControlVehicleMetallicPaint", { 0.09f, 0.28f, 0.74f }, 0.18f, 0.72f, 1.00f, 0.03f);
    PaintMat.Specular.EdgeTintF82 = { 0.55f, 0.78f, 1.00f };

    MaterialSpace::SurfaceMaterial RimAlloyMat = MakeMat("ForgedTitaniumRimAlloy", { 0.84f, 0.86f, 0.90f }, 0.16f, 0.96f);
    RimAlloyMat.Specular.EdgeTintF82 = { 0.96f, 0.97f, 0.99f };

    MaterialSpace::SurfaceMaterial BrakeMat = MakeMat("MachinedSteelBrakeRotor", { 0.66f, 0.68f, 0.71f }, 0.28f, 0.94f);
    BrakeMat.Specular.Anisotropy = 0.55f;

    const std::array<NamedMaterialEntry, 16u> Materials {{
        { "Architectural", "GroundPlaneAsphalt",         MakeMat("GroundPlaneAsphalt",         { 0.22f, 0.23f, 0.25f }, 0.54f, 0.00f, 0.04f, 0.36f) },
        { "Architectural", "GalleryNeutralPlaster",      MakeMat("GalleryNeutralPlaster",      { 0.76f, 0.75f, 0.72f }, 0.72f, 0.00f) },
        { "Architectural", "BrushedGoldConductor",       MakeMat("BrushedGoldConductor",       { 0.94f, 0.76f, 0.34f }, 0.18f, 1.00f) },
        { "Architectural", "AbbeCrownGlassOrb",          MakeMat("AbbeCrownGlassOrb",          { 0.96f, 0.99f, 1.00f }, 0.02f, 0.00f, 0.0f, 0.02f, 0.96f, 1.52f) },
        { "Automotive",    "ControlVehicleMetallicPaint",PaintMat },
        { "Automotive",    "VehicleObsidianCanopy",      MakeMat("VehicleObsidianCanopy",      { 0.03f, 0.035f, 0.045f }, 0.08f, 0.00f, 1.00f, 0.02f) },
        { "Automotive",    "VehicleTintedWindshield",    MakeMat("VehicleTintedWindshield",    { 0.05f, 0.09f, 0.13f }, 0.025f, 0.00f, 1.00f, 0.015f, 0.0f, 1.52f) },
        { "Automotive",    "VehicleIntakePolymer",       MakeMat("VehicleIntakePolymer",       { 0.07f, 0.075f, 0.082f }, 0.52f, 0.00f) },
        { "Automotive",    "VulcanizedTyreRubber",       MakeMat("VulcanizedTyreRubber",       { 0.045f, 0.046f, 0.050f }, 0.68f, 0.00f) },
        { "Automotive",    "ForgedTitaniumRimAlloy",     RimAlloyMat },
        { "Automotive",    "AnodizedBronzeWheelCap",     MakeMat("AnodizedBronzeWheelCap",     { 0.78f, 0.54f, 0.24f }, 0.20f, 0.92f) },
        { "Automotive",    "MachinedSteelBrakeRotor",    BrakeMat },
        { "Automotive",    "SatinGraphiteTrim",          MakeMat("SatinGraphiteTrim",          { 0.14f, 0.15f, 0.17f }, 0.34f, 0.40f) },
        { "Automotive",    "SmokedHeadlampHousing",      MakeMat("SmokedHeadlampHousing",      { 0.12f, 0.13f, 0.15f }, 0.05f, 0.00f, 0.92f, 0.03f) },
        { "Automotive",    "XenonHeadlampOptic",         XenonMat },
        { "Automotive",    "RubyTaillampOptic",          MakeMat("RubyTaillampOptic",          { 0.36f, 0.025f, 0.028f }, 0.06f, 0.00f, 0.88f, 0.035f) },
    }};

    for (const NamedMaterialEntry& Entry : Materials)
    {
        const std::filesystem::path Target = ContentRoot / "Materials" / Entry.SubFolder / (std::string(Entry.Stem) + ".MaterialSpace");
        const auto Model = BuildMaterialModel(Entry.Stem, Entry.Material);
        if (!Codecs::MaterialEncoder::EncodeToFile(Target, Model, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, Target, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 2. Surface Parameterizations (.SurfaceParameterSpace)
    struct SurfaceParamSpec
    {
        const char* SubFolder;
        const char* Stem;
        double      ScaleMeters;
        uint32_t    QuadCount;
    };
    const std::array<SurfaceParamSpec, 7u> SurfaceParams {{
        { "Primitives", "GroundPlaneParameterization",        24.0, 12u },
        { "Primitives", "UnitBoxParameterization",             1.0,  6u },
        { "Primitives", "CalibrationSphereParameterization",   1.0, 32u },
        { "Vehicles",   "ControlVehicleParameterization",      4.5, 64u },
        { "Vehicles",   "ControlVehicleBodyParameterization",  4.5, 48u },
        { "Vehicles",   "ControlVehicleRimParameterization",   0.7, 24u },
        { "Vehicles",   "ControlVehicleTyreParameterization",  0.7, 24u },
    }};
    for (const SurfaceParamSpec& Spec : SurfaceParams)
    {
        const std::filesystem::path Target = ContentRoot / "SurfaceParameters" / Spec.SubFolder / (std::string(Spec.Stem) + ".SurfaceParameterSpace");
        const auto Model = BuildSurfaceParameterDocument(Spec.Stem, Spec.ScaleMeters, Spec.QuadCount);
        if (!Codecs::UVEncoder::EncodeToFile(Target, Model, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, Target, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 3. Primitive & ControlVehicle Geometry (.GeometrySpace)
    {
        Codecs::GeometrySpaceDocument GroundDoc{};
        BuildBoxPolygonDocument(
            "GroundPlane",
            { 12.0f, 12.0f, 0.08f },
            "Content/Materials/Architectural/GroundPlaneAsphalt.MaterialSpace",
            "Content/SurfaceParameters/Primitives/GroundPlaneParameterization.SurfaceParameterSpace",
            GroundDoc);
        const auto GroundPath = ContentRoot / "Geometry/Primitives/GroundPlane.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(GroundPath, GroundDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, GroundPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::GeometrySpaceDocument BoxDoc{};
        BuildBoxPolygonDocument(
            "UnitBox",
            { 0.5f, 0.5f, 0.5f },
            "Content/Materials/Architectural/GalleryNeutralPlaster.MaterialSpace",
            "Content/SurfaceParameters/Primitives/UnitBoxParameterization.SurfaceParameterSpace",
            BoxDoc);
        const auto BoxPath = ContentRoot / "Geometry/Primitives/UnitBox.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(BoxPath, BoxDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, BoxPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::GeometrySpaceDocument SphereDoc{};
        BuildSpherePolygonDocument(
            "CalibrationSphere",
            0.5f,
            28u,
            18u,
            "Content/Materials/Architectural/BrushedGoldConductor.MaterialSpace",
            "Content/SurfaceParameters/Primitives/CalibrationSphereParameterization.SurfaceParameterSpace",
            SphereDoc);
        const auto SpherePath = ContentRoot / "Geometry/Primitives/CalibrationSphere.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(SpherePath, SphereDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, SpherePath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::GeometrySpaceDocument CylDoc{};
        BuildFrustumPolygonDocument(
            "StandardCylinder",
            0.5f,
            0.5f,
            1.0f,
            24u,
            "Content/Materials/Architectural/GalleryNeutralPlaster.MaterialSpace",
            CylDoc);
        const auto CylPath = ContentRoot / "Geometry/Primitives/StandardCylinder.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(CylPath, CylDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, CylPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::GeometrySpaceDocument ConeDoc{};
        BuildFrustumPolygonDocument(
            "TrafficCone",
            0.5f,
            0.0f,
            1.0f,
            24u,
            "Content/Materials/Architectural/GalleryNeutralPlaster.MaterialSpace",
            ConeDoc);
        const auto ConePath = ContentRoot / "Geometry/Primitives/TrafficCone.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(ConePath, ConeDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, ConePath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::GeometrySpaceDocument TorusDoc{};
        BuildTorusPolygonDocument(
            "CalibrationTorus",
            0.38f,
            0.14f,
            28u,
            16u,
            "Content/Materials/Architectural/BrushedGoldConductor.MaterialSpace",
            TorusDoc);
        const auto TorusPath = ContentRoot / "Geometry/Primitives/CalibrationTorus.GeometrySpace";
        if (!Codecs::GeometryEncoder::EncodeToFile(TorusPath, TorusDoc, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, TorusPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // Decode ControlVehicle (25,068 triangles, 38-deg creased smooth normals) and write ControlVehicle + sub-part .GeometrySpace containers
    Mathematics::Axis3D   VehicleCenter { 0.0f, 0.0f, 0.68f };
    Mathematics::Scalar3D VehicleSpan   { 4.52f, 1.98f, 1.36f };
    {
        static constexpr std::array<std::pair<const char*, const char*>, 11u> VehicleSlotMap {{
            { "BodyCoat",       "Content/Materials/Automotive/ControlVehicleMetallicPaint.MaterialSpace" },
            { "CanopyGloss",    "Content/Materials/Automotive/VehicleObsidianCanopy.MaterialSpace" },
            { "VehicleGlass",   "Content/Materials/Automotive/VehicleTintedWindshield.MaterialSpace" },
            { "VehiclePlastic", "Content/Materials/Automotive/VehicleIntakePolymer.MaterialSpace" },
            { "TyreRubber",     "Content/Materials/Automotive/VulcanizedTyreRubber.MaterialSpace" },
            { "RimAlloy",       "Content/Materials/Automotive/ForgedTitaniumRimAlloy.MaterialSpace" },
            { "WheelCapAccent", "Content/Materials/Automotive/AnodizedBronzeWheelCap.MaterialSpace" },
            { "BrakeDisc",      "Content/Materials/Automotive/MachinedSteelBrakeRotor.MaterialSpace" },
            { "VehicleTrim",    "Content/Materials/Automotive/SatinGraphiteTrim.MaterialSpace" },
            { "HeadlightLens",  "Content/Materials/Automotive/SmokedHeadlampHousing.MaterialSpace" },
            { "TaillightLens",  "Content/Materials/Automotive/RubyTaillampOptic.MaterialSpace" },
        }};

        Codecs::AssembledControlVehicle DecodedVehicle{};
        std::string VehicleDiag{};
        const bool HasFctVehicle = Codecs::ControlVehicleSurfaceDecoder::DecodeAssembledVehicle(
            RepositoryRoot,
            Codecs::VehicleCoordinateConvention::FrontierWorldZUp,
            DecodedVehicle,
            VehicleDiag);

        auto BuildVehicleDoc = [&](std::string_view DocTitle, std::string_view ParamPath, int PartCategoryFilter) -> Codecs::GeometrySpaceDocument
        {
            Codecs::GeometrySpaceDocument Doc{};
            Doc.Network.NetworkTitle = std::string(DocTitle);
            Doc.CreaseAngleDegrees   = 38.0f;
            Doc.SurfaceParameterPath = std::string(ParamPath);
            for (uint32_t SlotIdx = 0u; SlotIdx < VehicleSlotMap.size(); ++SlotIdx)
            {
                Doc.SurfaceSlots.push_back({ SlotIdx, VehicleSlotMap[SlotIdx].first, VehicleSlotMap[SlotIdx].second });
            }

            if (!HasFctVehicle || DecodedVehicle.Triangles.empty())
            {
                BuildBoxPolygonDocument(DocTitle, { 2.26f, 0.99f, 0.68f }, VehicleSlotMap[0u].second, ParamPath, Doc);
                return Doc;
            }

            Mathematics::Axis3D MinB {  1.0e9f,  1.0e9f,  1.0e9f };
            Mathematics::Axis3D MaxB { -1.0e9f, -1.0e9f, -1.0e9f };
            std::vector<const Codecs::DecodedVehicleTriangle*> Selected;
            Selected.reserve(DecodedVehicle.Triangles.size());
            for (const auto& Tri : DecodedVehicle.Triangles)
            {
                const bool Keep =
                    (PartCategoryFilter < 0) ||
                    (PartCategoryFilter == 0 && Tri.Part == Codecs::VehicleSubassemblyPart::BodyChassis) ||
                    (PartCategoryFilter == 1 && Tri.Part == Codecs::VehicleSubassemblyPart::RimFrontLeft) ||
                    (PartCategoryFilter == 2 && Tri.Part == Codecs::VehicleSubassemblyPart::TyreFrontLeft);
                if (!Keep)
                {
                    continue;
                }
                Selected.push_back(&Tri);
                for (const auto& P : { Tri.CornerA, Tri.CornerB, Tri.CornerC })
                {
                    MinB.X = std::min(MinB.X, P.X); MinB.Y = std::min(MinB.Y, P.Y); MinB.Z = std::min(MinB.Z, P.Z);
                    MaxB.X = std::max(MaxB.X, P.X); MaxB.Y = std::max(MaxB.Y, P.Y); MaxB.Z = std::max(MaxB.Z, P.Z);
                }
            }

            const Mathematics::Axis3D LocalOrigin = (PartCategoryFilter < 0)
                                                        ? DecodedVehicle.BoundsCenter
                                                        : (MinB + MaxB) * 0.5f;
            const auto TriCount = static_cast<uint32_t>(Selected.size());
            Doc.Network.ReserveStorage(TriCount * 3u, TriCount * 3u, TriCount);
            Doc.FaceCornerNormals.reserve(static_cast<std::size_t>(TriCount) * 9u);

            for (const Codecs::DecodedVehicleTriangle* Tri : Selected)
            {
                const uint32_t V0 = Doc.Network.AppendVertex(Tri->CornerA - LocalOrigin);
                const uint32_t V1 = Doc.Network.AppendVertex(Tri->CornerB - LocalOrigin);
                const uint32_t V2 = Doc.Network.AppendVertex(Tri->CornerC - LocalOrigin);
                const uint32_t FaceVerts[3]{ V0, V1, V2 };
                (void)Doc.Network.AppendFace(FaceVerts, {}, {}, 0u, static_cast<uint16_t>(Tri->SurfaceSlot));
                Doc.FaceCornerNormals.insert(
                    Doc.FaceCornerNormals.end(),
                    { Tri->NormalA.X, Tri->NormalA.Y, Tri->NormalA.Z,
                      Tri->NormalB.X, Tri->NormalB.Y, Tri->NormalB.Z,
                      Tri->NormalC.X, Tri->NormalC.Y, Tri->NormalC.Z });
            }
            return Doc;
        };

        if (HasFctVehicle && !DecodedVehicle.Triangles.empty())
        {
            VehicleCenter = DecodedVehicle.BoundsCenter;
            VehicleSpan   = {
                std::max(0.20f, DecodedVehicle.BoundsMax.X - DecodedVehicle.BoundsMin.X),
                std::max(0.20f, DecodedVehicle.BoundsMax.Y - DecodedVehicle.BoundsMin.Y),
                std::max(0.20f, DecodedVehicle.BoundsMax.Z - DecodedVehicle.BoundsMin.Z),
            };
        }

        struct VehicleGeoSpec
        {
            const char* Stem;
            const char* ParamPath;
            int         FilterCode;
        };
        const std::array<VehicleGeoSpec, 4u> VehicleGeos {{
            { "ControlVehicle",     "Content/SurfaceParameters/Vehicles/ControlVehicleParameterization.SurfaceParameterSpace",     -1 },
            { "ControlVehicleBody", "Content/SurfaceParameters/Vehicles/ControlVehicleBodyParameterization.SurfaceParameterSpace",  0 },
            { "ControlVehicleRim",  "Content/SurfaceParameters/Vehicles/ControlVehicleRimParameterization.SurfaceParameterSpace",   1 },
            { "ControlVehicleTyre", "Content/SurfaceParameters/Vehicles/ControlVehicleTyreParameterization.SurfaceParameterSpace",  2 },
        }};

        for (const VehicleGeoSpec& Spec : VehicleGeos)
        {
            const auto Doc  = BuildVehicleDoc(Spec.Stem, Spec.ParamPath, Spec.FilterCode);
            const auto Path = ContentRoot / "Geometry/Vehicles" / (std::string(Spec.Stem) + ".GeometrySpace");
            if (!Codecs::GeometryEncoder::EncodeToFile(Path, Doc, OutDiagnostic) ||
                !RecordWrittenFile(RepositoryRoot, Path, OutReport, OutDiagnostic))
            {
                return false;
            }
        }
    }

    // 4. ControlVehicle Component Rig (.ComponentSpace)
    {
        ComponentClass::ComponentClassDefinition VehicleRig{};
        VehicleRig.Title          = "ControlVehicle";
        VehicleRig.ClassArchetype = ComponentClass::ComponentClassArchetype::Driveable;
        VehicleRig.Storage.ResetWithSpatialOrigin("ControlVehicleOrigin");
        const auto OriginTok = VehicleRig.Storage.QueryOriginToken();

        const auto BodyTok = VehicleRig.Storage.AppendInstance(
            ComponentClass::ComponentCategory::GeometryInstance,
            "ControlVehicleBody",
            OriginTok);
        if (auto* Geo = VehicleRig.Storage.ResolveGeometry(BodyTok))
        {
            Geo->AssetIdentifier = "Content/Geometry/Vehicles/ControlVehicleBody.GeometrySpace";
        }

        const std::array<std::pair<const char*, Mathematics::Axis3D>, 4u> WheelOffsets {{
            { "FrontLeft",  {  1.38f,  0.82f, 0.34f } },
            { "FrontRight", {  1.38f, -0.82f, 0.34f } },
            { "RearLeft",   { -1.36f,  0.82f, 0.34f } },
            { "RearRight",  { -1.36f, -0.82f, 0.34f } },
        }};
        for (const auto& [CornerName, Offset] : WheelOffsets)
        {
            ComponentClass::ComponentTransform WheelXf{};
            WheelXf.LocationMeters = Offset;
            const auto SuspTok = VehicleRig.Storage.AppendInstance(
                ComponentClass::ComponentCategory::ConstraintInstance,
                std::string("Suspension") + CornerName,
                BodyTok,
                WheelXf);
            if (auto* Cst = VehicleRig.Storage.ResolveConstraint(SuspTok))
            {
                Cst->Form               = ComponentClass::PhysicsConstraintCategory::Spring;
                Cst->Axis               = ComponentClass::ConstraintAxis::AxisZ;
                Cst->NaturalFrequencyHz = 2.25f;
                Cst->DampingRatioZeta   = 0.45f;
            }
            const auto RimTok = VehicleRig.Storage.AppendInstance(
                ComponentClass::ComponentCategory::GeometryInstance,
                std::string("ControlVehicleRim") + CornerName,
                SuspTok);
            if (auto* Geo = VehicleRig.Storage.ResolveGeometry(RimTok))
            {
                Geo->AssetIdentifier = "Content/Geometry/Vehicles/ControlVehicleRim.GeometrySpace";
            }
            const auto TyreTok = VehicleRig.Storage.AppendInstance(
                ComponentClass::ComponentCategory::GeometryInstance,
                std::string("ControlVehicleTyre") + CornerName,
                RimTok);
            if (auto* Geo = VehicleRig.Storage.ResolveGeometry(TyreTok))
            {
                Geo->AssetIdentifier = "Content/Geometry/Vehicles/ControlVehicleTyre.GeometrySpace";
            }
        }
        (void)VehicleRig.Storage.AppendInstance(
            ComponentClass::ComponentCategory::VehicleDynamics,
            "ControlVehicleDynamics",
            OriginTok);
        VehicleRig.FinalizeCompiledRevision();

        const auto RigPath = ContentRoot / "Components/ControlVehicle.ComponentSpace";
        if (!Codecs::ComponentSpaceEncoder::EncodeToFile(RigPath, VehicleRig, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, RigPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 5. Photometry (.PhotometrySpace — Solar, AreaLuminaires, and 128x32 MeasuredLuminaires)
    {
        Codecs::PhotometrySpaceDocumentModel MorningSun{};
        MorningSun.DocumentTitle                          = "ClearMorningSun";
        MorningSun.LuminaireCategory                      = Codecs::PhotometryLuminaireCategory::SolarEphemeris;
        MorningSun.SolarSource.SunAngularRadiusRadians    = 0.00465f;
        MorningSun.SolarSource.RtxSamplesPerPixel         = 8u;
        MorningSun.SolarEphemeris.SolarEnabled            = true;
        MorningSun.SolarEphemeris.LocalHours              = 10.50f;
        MorningSun.SolarEphemeris.CycleAnimationEnabled   = false;
        MorningSun.SolarEphemeris.SolarIntensityMultiplier = 22.0f;
        MorningSun.SolarEphemeris.DirectGainMultiplier    = 2.65f;
        MorningSun.SolarSource.DirectionToSun = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(MorningSun.SolarEphemeris).WorldDirectionToSun;
        MorningSun.SolarSource.SolarIrradiance = { 5.40f, 5.12f, 4.72f };
        MorningSun.SolarSource.SunAngularRadiusRadians = 0.0093f;
        MorningSun.SolarSource.RtxSamplesPerPixel = 4u;
        const auto SunPath = ContentRoot / "Photometry/Solar/ClearMorningSun.PhotometrySpace";
        if (!Codecs::PhotometrySpaceEncoder::EncodeToFile(SunPath, MorningSun, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, SunPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::PhotometrySpaceDocumentModel NightSun{};
        NightSun.DocumentTitle                        = "NocturnalZeroSun";
        NightSun.LuminaireCategory                    = Codecs::PhotometryLuminaireCategory::SolarEphemeris;
        NightSun.SolarEphemeris.SolarEnabled          = false;
        NightSun.SolarEphemeris.LocalHours            = 23.0f;
        NightSun.SolarSource.DirectionToSun           = { 0.0f, 0.0f, -1.0f };
        NightSun.SolarSource.SolarIrradiance          = { 0.0f, 0.0f, 0.0f };
        const auto NightSunPath = ContentRoot / "Photometry/Solar/NocturnalZeroSun.PhotometrySpace";
        if (!Codecs::PhotometrySpaceEncoder::EncodeToFile(NightSunPath, NightSun, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, NightSunPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::PhotometrySpaceDocumentModel Softbox{};
        Softbox.DocumentTitle              = "CeilingSoftboxLuminaire";
        Softbox.LuminaireCategory          = Codecs::PhotometryLuminaireCategory::AreaQuadLuminaire;
        Softbox.AreaQuad.CenterWorld       = { 0.0f, 0.0f, 2.72f };
        Softbox.AreaQuad.RightTangent      = { 1.0f, 0.0f, 0.0f };
        Softbox.AreaQuad.UpBitangent       = { 0.0f, 1.0f, 0.0f };
        Softbox.AreaQuad.HalfWidthMeters   = 0.85f;
        Softbox.AreaQuad.HalfHeightMeters  = 0.45f;
        Softbox.AreaQuad.EmittedRadiance   = { 7.5f, 6.6f, 5.4f };
        const auto SoftboxPath = ContentRoot / "Photometry/AreaLuminaires/CeilingSoftboxLuminaire.PhotometrySpace";
        if (!Codecs::PhotometrySpaceEncoder::EncodeToFile(SoftboxPath, Softbox, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, SoftboxPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::PhotometrySpaceDocumentModel ShowroomArea = Softbox;
        ShowroomArea.DocumentTitle            = "ShowroomOverheadLuminaire";
        ShowroomArea.AreaQuad.CenterWorld     = { 0.0f, 0.0f, 3.60f };
        ShowroomArea.AreaQuad.HalfWidthMeters = 1.60f;
        ShowroomArea.AreaQuad.HalfHeightMeters = 0.70f;
        ShowroomArea.AreaQuad.EmittedRadiance = { 4.2f, 4.4f, 4.8f };
        const auto ShowroomPath = ContentRoot / "Photometry/AreaLuminaires/ShowroomOverheadLuminaire.PhotometrySpace";
        if (!Codecs::PhotometrySpaceEncoder::EncodeToFile(ShowroomPath, ShowroomArea, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, ShowroomPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        std::vector<Codecs::ResampledPhotometricProfile> Suite{};
        std::string SuiteDiag{};
        const bool LoadedSuite = Codecs::PhotometricProfileCodec::LoadCanonicalPhotometrySuite(RepositoryRoot, Suite, SuiteDiag);

        const std::array<const char*, 3u> MeasuredStems {{
            "LowBeamHeadlamp",
            "TailLampRed",
            "CarParkDownlight",
        }};
        for (std::size_t Idx = 0u; Idx < MeasuredStems.size(); ++Idx)
        {
            Codecs::PhotometrySpaceDocumentModel Meas{};
            Meas.DocumentTitle     = MeasuredStems[Idx];
            Meas.LuminaireCategory = Codecs::PhotometryLuminaireCategory::MeasuredCandela;
            Meas.CandelaIncluded   = true;
            if (LoadedSuite && Idx < Suite.size())
            {
                Meas.CandelaProfile = Suite[Idx];
            }
            else
            {
                Meas.CandelaProfile.ProfileLabel = MeasuredStems[Idx];
                Meas.CandelaProfile.PeakCandela  = 1800.0f;
                for (uint32_t V = 0u; V < Codecs::PhotometricVerticalCount; ++V)
                {
                    const float Theta = (static_cast<float>(V) / 127.0f) * static_cast<float>(Constants::Pi);
                    const float Gain  = std::max(0.0f, std::cos(Theta));
                    for (uint32_t H = 0u; H < Codecs::PhotometricHorizontalCount; ++H)
                    {
                        Meas.CandelaProfile.NormalizedCandela[H * Codecs::PhotometricVerticalCount + V] = Gain * Gain;
                    }
                }
            }
            const auto MeasPath = ContentRoot / "Photometry/MeasuredLuminaires" / (std::string(MeasuredStems[Idx]) + ".PhotometrySpace");
            if (!Codecs::PhotometrySpaceEncoder::EncodeToFile(MeasPath, Meas, OutDiagnostic) ||
                !RecordWrittenFile(RepositoryRoot, MeasPath, OutReport, OutDiagnostic))
            {
                return false;
            }
        }
    }

    // 6. Environment (.VolumetricSpace — ClearSkyDaylight, NocturnalStarrySky, StaticFullResolutionCumulus, CondensateNoiseVolume, Fog)
    {
        Codecs::VolumetricSpaceDocumentModel ClearSky{};
        ClearSky.DocumentTitle                             = "ClearSkyDaylight";
        ClearSky.Atmosphere.Medium.AtmosphereEnabled       = true;
        ClearSky.Atmosphere.Clouds.CloudsEnabled           = false;
        ClearSky.Atmosphere.Clouds.CloudShadow.ShadowEnabled = false;
        ClearSky.Atmosphere.Clouds.CloudRenderScale        = 1.00f;
        ClearSky.Atmosphere.Clouds.CloudMarchSteps         = 64u;
        ClearSky.Atmosphere.Clouds.WindSpeedMetersPerSecond = 0.0f;
        ClearSky.Atmosphere.Wind.SpeedMetersPerSecond      = 0.0f;
        ClearSky.Atmosphere.LocalCloud.LocalEnabled        = false;
        ClearSky.Atmosphere.Stars.StarfieldEnabled         = false;
        ClearSky.Atmosphere.Moons[0u].Visible              = false;
        ClearSky.FallbackSkyRadiance                       = { 0.42f, 0.58f, 0.88f };
        const auto ClearSkyPath = ContentRoot / "Environment/Skies/ClearSkyDaylight.VolumetricSpace";
        if (!Codecs::VolumetricSpaceEncoder::EncodeToFile(ClearSkyPath, ClearSky, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, ClearSkyPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::VolumetricSpaceDocumentModel NightSky = ClearSky;
        NightSky.DocumentTitle                        = "NocturnalStarrySky";
        NightSky.Atmosphere.Stars.StarfieldEnabled    = true;
        NightSky.Atmosphere.Moons[0u].Visible         = true;
        NightSky.FallbackSkyRadiance                  = { 0.012f, 0.018f, 0.032f };
        const auto NightSkyPath = ContentRoot / "Environment/Skies/NocturnalStarrySky.VolumetricSpace";
        if (!Codecs::VolumetricSpaceEncoder::EncodeToFile(NightSkyPath, NightSky, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, NightSkyPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::VolumetricSpaceDocumentModel StaticCumulus = ClearSky;
        StaticCumulus.DocumentTitle                              = "StaticFullResolutionCumulus";
        StaticCumulus.Atmosphere.Clouds.CloudsEnabled            = true;
        StaticCumulus.Atmosphere.Clouds.CloudShadow.ShadowEnabled = true;
        StaticCumulus.Atmosphere.Clouds.CloudRenderScale         = 1.00f;
        StaticCumulus.Atmosphere.Clouds.CloudMarchSteps          = 64u;
        StaticCumulus.Atmosphere.Clouds.WindSpeedMetersPerSecond = 0.0f;
        StaticCumulus.Atmosphere.Clouds.FollowWindEnabled        = false;
        const auto CumulusPath = ContentRoot / "Environment/Clouds/StaticFullResolutionCumulus.VolumetricSpace";
        if (!Codecs::VolumetricSpaceEncoder::EncodeToFile(CumulusPath, StaticCumulus, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, CumulusPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::VolumetricSpaceDocumentModel CondensateNoise = StaticCumulus;
        CondensateNoise.DocumentTitle          = "CondensateNoiseVolume";
        CondensateNoise.ShapeVolumeDimension   = 32u;
        CondensateNoise.ErosionVolumeDimension = 16u;
        CondensateNoise.ShapeNoiseTexels.resize(32u * 32u * 32u * 4u);
        for (std::size_t Idx = 0u; Idx < CondensateNoise.ShapeNoiseTexels.size(); ++Idx)
        {
            CondensateNoise.ShapeNoiseTexels[Idx] = static_cast<uint8_t>((Idx * 73u + 41u) & 0xFFu);
        }
        CondensateNoise.ErosionNoiseTexels.resize(16u * 16u * 16u * 4u);
        for (std::size_t Idx = 0u; Idx < CondensateNoise.ErosionNoiseTexels.size(); ++Idx)
        {
            CondensateNoise.ErosionNoiseTexels[Idx] = static_cast<uint8_t>((Idx * 131u + 19u) & 0xFFu);
        }
        const auto CondensatePath = ContentRoot / "Environment/Clouds/CondensateNoiseVolume.VolumetricSpace";
        if (!Codecs::VolumetricSpaceEncoder::EncodeToFile(CondensatePath, CondensateNoise, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, CondensatePath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::VolumetricSpaceDocumentModel HorizonFog = ClearSky;
        HorizonFog.DocumentTitle                        = "ClearHorizonHeightFog";
        HorizonFog.Atmosphere.Fog.HeightFog.Enabled     = true;
        HorizonFog.Atmosphere.Fog.HeightFog.Density     = 0.008f;
        HorizonFog.Atmosphere.Fog.HeightFog.FalloffHeightMeters = 450.0f;
        const auto FogPath = ContentRoot / "Environment/Fog/ClearHorizonHeightFog.VolumetricSpace";
        if (!Codecs::VolumetricSpaceEncoder::EncodeToFile(FogPath, HorizonFog, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, FogPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 7. Rendering Presets (.RenderSpace)
    {
        Codecs::RenderSpaceDocumentModel DefaultViewport{};
        DefaultViewport.DocumentTitle                 = "DefaultEditorViewport";
        DefaultViewport.RenderConfiguration.Engine    = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
        DefaultViewport.CheapMatCapTwoByTwoShadow     = true;
        DefaultViewport.FineCascadeHalfSpanMeters     = 2.85f;
        DefaultViewport.CoarseCascadeHalfSpanMeters   = 8.50f;
        DefaultViewport.CascadeResolutionTexels       = 1024u;
        DefaultViewport.FineDistanceFieldHalfMeters   = 4.20f;
        DefaultViewport.CoarseDistanceFieldHalfMeters = 14.0f;
        DefaultViewport.StaticCloudRenderScale        = 1.00f;
        const auto DefaultRenderPath = ContentRoot / "Rendering/DefaultEditorViewport.RenderSpace";
        if (!Codecs::RenderSpaceEncoder::EncodeToFile(DefaultRenderPath, DefaultViewport, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, DefaultRenderPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        Codecs::RenderSpaceDocumentModel NightViewport = DefaultViewport;
        NightViewport.DocumentTitle                    = "NightPhotometricViewport";
        NightViewport.ExposureStopsEV                  = 0.65f;
        const auto NightRenderPath = ContentRoot / "Rendering/NightPhotometricViewport.RenderSpace";
        if (!Codecs::RenderSpaceEncoder::EncodeToFile(NightRenderPath, NightViewport, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, NightRenderPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 8. Optics & Sampling Ranks (.TextureSpace & .ImageSpace)
    {
        Codecs::TextureSpaceDocumentModel BlueNoise{};
        BlueNoise.DocumentTitle     = "BlueNoiseSamplingRanks";
        BlueNoise.ChannelPackLayout = "RGBA8_Unorm";
        BlueNoise.LinkedUVSpacePath = "Content/SurfaceParameters/Primitives/UnitBoxParameterization.SurfaceParameterSpace";
        BlueNoise.TileLayers.push_back({ 1001u, 1u, 64u });
        BlueNoise.PackedTexelBytes.resize(64u * 64u * 4u);
        for (std::size_t Idx = 0u; Idx < BlueNoise.PackedTexelBytes.size(); ++Idx)
        {
            BlueNoise.PackedTexelBytes[Idx] = static_cast<uint8_t>((Idx * 197u + 53u) & 0xFFu);
        }
        const auto BlueNoisePath = ContentRoot / "Optics/BlueNoiseSamplingRanks.TextureSpace";
        if (!Codecs::TextureEncoder::EncodeToFile(BlueNoisePath, BlueNoise, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, BlueNoisePath, OutReport, OutDiagnostic))
        {
            return false;
        }

        const std::array<const char*, 3u> OpticalImageStems {{
            "StudioShadingSphereSheet",
            "LensFlareBakedSheet",
            "LunarSurfaceSheet",
        }};
        for (const char* Stem : OpticalImageStems)
        {
            Codecs::ImageSpaceDocumentModel Img{};
            Img.DocumentTitle   = Stem;
            Img.NominalWidthPx  = 512u;
            Img.NominalHeightPx = 512u;
            Img.Layers.push_back({ "PrimaryOpticalLayer", 1.0, true });
            Img.LinearRgbaPreview.resize(32u * 32u * 4u, 0.5f);
            const auto ImgPath = ContentRoot / "Optics" / (std::string(Stem) + ".ImageSpace");
            if (!Codecs::ImageEncoder::EncodeToFile(ImgPath, Img, OutDiagnostic) ||
                !RecordWrittenFile(RepositoryRoot, ImgPath, OutReport, OutDiagnostic))
            {
                return false;
            }
        }
    }

    // 9. Worlds (.WorldSpace — VehicleClearSkyDefault, NightCarParkPhotometric)
    {
        Codecs::WorldSpaceDocumentModel DefaultWorld{};
        DefaultWorld.DocumentTitle             = "VehicleClearSkyDefault";
        DefaultWorld.SolarPhotometryPath       = "Content/Photometry/Solar/ClearMorningSun.PhotometrySpace";
        DefaultWorld.EnvironmentVolumetricPath = "Content/Environment/Skies/ClearSkyDaylight.VolumetricSpace";
        DefaultWorld.RenderSettingsPath        = "Content/Rendering/DefaultEditorViewport.RenderSpace";
        DefaultWorld.EditorCamera.CameraOrigin       = { 4.85f, -4.15f, 1.68f };
        DefaultWorld.EditorCamera.TargetPoint        = { 0.10f,  0.00f, 0.62f };
        DefaultWorld.EditorCamera.WorldUpAxis        = { 0.00f,  0.00f, 1.00f };
        DefaultWorld.EditorCamera.VerticalFovRadians = 38.0f * Constants::DegToRadFloat;
        DefaultWorld.EditorCamera.NearClipMeters     = 0.12f;
        DefaultWorld.EditorCamera.FarClipMeters      = 5000.0f;

        Codecs::WorldGeometryPlacementEntry GroundPlacement{};
        GroundPlacement.PlacementTitle        = "GroundPlane";
        GroundPlacement.StableIdentifier      = "ground-plane";
        GroundPlacement.Subtitle              = "Box · 24.0 × 24.0 × 0.16 m · Z = 0 datum";
        GroundPlacement.AuthorNotes           = "Outdoor Z=0 ground plane supporting ControlVehicle under clear sky illumination.";
        GroundPlacement.GeometrySpacePath     = "Content/Geometry/Primitives/GroundPlane.GeometrySpace";
        GroundPlacement.PrimaryMaterialPath   = "Content/Materials/Architectural/GroundPlaneAsphalt.MaterialSpace";
        GroundPlacement.CenterWorld           = { 0.0f, 0.0f, -0.08f };
        GroundPlacement.DimensionsMeters      = { 24.0f, 24.0f, 0.16f };
        GroundPlacement.PrimitiveShapeOrdinal = 0u;
        DefaultWorld.GeometryPlacements.push_back(std::move(GroundPlacement));

        Codecs::WorldGeometryPlacementEntry VehiclePlacement{};
        VehiclePlacement.PlacementTitle        = "ControlVehicle";
        VehiclePlacement.StableIdentifier      = "control-vehicle";
        VehiclePlacement.Subtitle              = "ControlVehicle · 25,068 tris · 11 OpenPBR slots";
        VehiclePlacement.AuthorNotes           = "Data-driven ControlVehicle (.GeometrySpace + .ComponentSpace) resting tangent to Z = 0.";
        VehiclePlacement.GeometrySpacePath     = "Content/Geometry/Vehicles/ControlVehicle.GeometrySpace";
        VehiclePlacement.ComponentSpacePath    = "Content/Components/ControlVehicle.ComponentSpace";
        VehiclePlacement.PrimaryMaterialPath   = "Content/Materials/Automotive/ControlVehicleMetallicPaint.MaterialSpace";
        VehiclePlacement.CenterWorld           = VehicleCenter;
        VehiclePlacement.DimensionsMeters      = VehicleSpan;
        VehiclePlacement.PrimitiveShapeOrdinal = 0u;
        DefaultWorld.GeometryPlacements.push_back(std::move(VehiclePlacement));

        const auto DefaultWorldPath = ContentRoot / "Worlds/VehicleClearSkyDefault.WorldSpace";
        if (!Codecs::WorldSpaceEncoder::EncodeToFile(DefaultWorldPath, DefaultWorld, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, DefaultWorldPath, OutReport, OutDiagnostic))
        {
            return false;
        }

        // NightCarParkPhotometric.WorldSpace
        Codecs::WorldSpaceDocumentModel NightWorld = DefaultWorld;
        NightWorld.DocumentTitle             = "NightCarParkPhotometric";
        NightWorld.SolarPhotometryPath       = "Content/Photometry/Solar/NocturnalZeroSun.PhotometrySpace";
        NightWorld.EnvironmentVolumetricPath = "Content/Environment/Skies/NocturnalStarrySky.VolumetricSpace";
        NightWorld.RenderSettingsPath        = "Content/Rendering/NightPhotometricViewport.RenderSpace";
        NightWorld.MeasuredLuminairePlacements.push_back({
            "HeadlampFrontLeft",
            "Content/Photometry/MeasuredLuminaires/LowBeamHeadlamp.PhotometrySpace",
            { 2.22f, 0.68f, 0.66f }, { 0.99f, 0.0f, -0.14f }, { 0.0f, 1.0f, 0.0f }, { 42.0f, 44.0f, 48.0f }, 28.0f, 0.06f });
        NightWorld.MeasuredLuminairePlacements.push_back({
            "HeadlampFrontRight",
            "Content/Photometry/MeasuredLuminaires/LowBeamHeadlamp.PhotometrySpace",
            { 2.22f, -0.68f, 0.66f }, { 0.99f, 0.0f, -0.14f }, { 0.0f, 1.0f, 0.0f }, { 42.0f, 44.0f, 48.0f }, 28.0f, 0.06f });
        NightWorld.MeasuredLuminairePlacements.push_back({
            "TailLampRearLeft",
            "Content/Photometry/MeasuredLuminaires/TailLampRed.PhotometrySpace",
            { -2.24f, 0.66f, 0.72f }, { -0.99f, 0.0f, -0.12f }, { 0.0f, 1.0f, 0.0f }, { 14.0f, 0.9f, 0.8f }, 14.0f, 0.05f });
        NightWorld.MeasuredLuminairePlacements.push_back({
            "TailLampRearRight",
            "Content/Photometry/MeasuredLuminaires/TailLampRed.PhotometrySpace",
            { -2.24f, -0.66f, 0.72f }, { -0.99f, 0.0f, -0.12f }, { 0.0f, 1.0f, 0.0f }, { 14.0f, 0.9f, 0.8f }, 14.0f, 0.05f });
        NightWorld.MeasuredLuminairePlacements.push_back({
            "CarParkDownlightOverhead",
            "Content/Photometry/MeasuredLuminaires/CarParkDownlight.PhotometrySpace",
            { 0.0f, 0.0f, 4.20f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 28.0f, 27.0f, 25.0f }, 18.0f, 0.10f });
        const auto NightWorldPath = ContentRoot / "Worlds/NightCarParkPhotometric.WorldSpace";
        if (!Codecs::WorldSpaceEncoder::EncodeToFile(NightWorldPath, NightWorld, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, NightWorldPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    // 10. Root ContentRepositoryIndex.ProjectSpace
    {
        Codecs::ProjectSpaceDocumentModel RootProject{};
        RootProject.ProjectTitle     = "FrontierDefaultContentRepository";
        RootProject.StartupSpacePath = "Content/Worlds/VehicleClearSkyDefault.WorldSpace";
        RootProject.TargetTickRateHz = 120.0;
        for (const auto& RelPath : OutReport.WrittenRelativePaths)
        {
            RootProject.DependencyLocks.push_back({ RelPath.generic_string(), "1.0.0" });
        }
        const auto RootIndexPath = ContentRoot / "ContentRepositoryIndex.ProjectSpace";
        if (!Codecs::ProjectEncoder::EncodeToFile(RootIndexPath, RootProject, OutDiagnostic) ||
            !RecordWrittenFile(RepositoryRoot, RootIndexPath, OutReport, OutDiagnostic))
        {
            return false;
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool DefaultContentGenerator::VerifyRepositoryContent(
    const std::filesystem::path& RepositoryRoot,
    uint32_t&                    OutVerifiedCount,
    std::string&                 OutDiagnostic)
{
    OutVerifiedCount = 0u;
    const std::filesystem::path ContentRoot = RepositoryRoot / "Content";
    std::error_code IoError{};
    if (!std::filesystem::exists(ContentRoot, IoError))
    {
        OutDiagnostic = "Content directory does not exist under " + RepositoryRoot.generic_string();
        return false;
    }

    for (const auto& DirEntry : std::filesystem::recursive_directory_iterator(ContentRoot, IoError))
    {
        if (!DirEntry.is_regular_file())
        {
            continue;
        }
        Codecs::SpaceContainerPayload Decoded{};
        if (!Codecs::SpaceContainerDecoder::DecodeFromFile(DirEntry.path(), Decoded, OutDiagnostic))
        {
            return false;
        }
        if ((Decoded.Tier2StartOffset % 64u) != 0u)
        {
            OutDiagnostic = "Tier2StartOffset is not 64-byte aligned in " + DirEntry.path().generic_string();
            return false;
        }
        ++OutVerifiedCount;
    }

    if (OutVerifiedCount == 0u)
    {
        OutDiagnostic = "No .Space containers found inside Content/.";
        return false;
    }
    OutDiagnostic.clear();
    return true;
}

bool DefaultContentGenerator::EnsureDefaultContentAvailable(
    const std::filesystem::path& RepositoryRoot,
    std::string&                 OutDiagnostic)
{
    const std::filesystem::path DefaultWorld = RepositoryRoot / "Content/Worlds/VehicleClearSkyDefault.WorldSpace";
    std::error_code IoError{};
    if (std::filesystem::exists(DefaultWorld, IoError))
    {
        OutDiagnostic.clear();
        return true;
    }
    GeneratedContentReport Report{};
    return GenerateRepositoryContent(RepositoryRoot, Report, OutDiagnostic);
}

} // namespace Frontier::Tools
