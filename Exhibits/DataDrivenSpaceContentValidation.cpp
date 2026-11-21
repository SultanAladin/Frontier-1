/*============================================================================================================================================
                                                  DATADRIVENSPACECONTENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation of explicit *Space container codecs, zero-abbreviation Content/ repository, and ControlVehicle WorldSpace loading

#include "Engine/Codecs/ComponentSpaceCodec.h"
#include "Engine/Codecs/GeometryCodec.h"
#include "Engine/Codecs/MaterialCodec.h"
#include "Engine/Codecs/PhotometrySpaceCodec.h"
#include "Engine/Codecs/ProjectCodec.h"
#include "Engine/Codecs/RenderSpaceCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Codecs/UVCodec.h"
#include "Engine/Codecs/VolumetricSpaceCodec.h"
#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Tools/DefaultContentGenerator.h"

#if defined(FRONTIER_PROFILE_EDITOR)
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

int main()
{
    using namespace Frontier;

    auto Require = [](bool Condition, const char* Label, const std::string& Detail = {}) -> bool
    {
        if (!Condition)
        {
            std::fprintf(stderr, "[DataDrivenSpaceContentValidation] FAILED: %s (%s)\n", Label, Detail.c_str());
            return false;
        }
        return true;
    };

    // 1. Verify explicit unabbreviated *Space suffixes and strict rejection of abbreviated .InSpace
    {
        Codecs::SpaceSuffixCategory Cat{};
        std::string                 Canonical{};
        std::string                 Diag{};

        for (const char* Suffix : { ".WorldSpace", ".PhotometrySpace", ".RenderSpace", ".SurfaceParameterSpace", ".InstanceSpace" })
        {
            if (!Require(Codecs::SpaceContainerDecoder::ResolveSuffixCaseInsensitive(Suffix, Cat, Canonical, Diag), Suffix, Diag))
            {
                return 1;
            }
            if (!Require(Canonical == Suffix, "Canonical suffix preservation", Canonical))
            {
                return 1;
            }
        }

        const bool AbbreviatedRejected = !Codecs::SpaceContainerDecoder::ResolveSuffixCaseInsensitive(".InSpace", Cat, Canonical, Diag);
        if (!Require(AbbreviatedRejected, "Abbreviated .InSpace must be rejected"))
        {
            return 1;
        }

        for (const char* SubTag : {
                 ".WorldHierarchy", ".WorldPlacement", ".EditorCamera", ".PhotometryHeader",
                 ".CandelaDistribution", ".SolarEphemeris", ".AtmosphericMedium", ".CloudDeck",
                 ".CondensateVolume", ".RenderConfiguration", ".ShadowCascades", ".ToneTransform",
                 ".SurfaceSlot", ".SmoothNormals" })
        {
            if (!Require(Codecs::SpaceContainerEncoder::NormalizeSubFormatTag(SubTag, Canonical, Diag), SubTag, Diag) ||
                !Require(Canonical == SubTag, SubTag, Canonical))
            {
                return 1;
            }
        }
    }

    // 2. Round-trip PhotometrySpace, VolumetricSpace, RenderSpace, and WorldSpace in memory (64B alignment + FNV-1a)
    {
        std::string             Diag{};
        std::vector<std::byte>  Bytes{};

        Codecs::PhotometrySpaceDocumentModel PhotoIn{};
        PhotoIn.DocumentTitle          = "TestHeadlamp";
        PhotoIn.LuminaireCategory      = Codecs::PhotometryLuminaireCategory::MeasuredCandela;
        PhotoIn.CandelaIncluded        = true;
        PhotoIn.CandelaProfile.PeakCandela = 2450.0f;
        PhotoIn.CandelaProfile.NormalizedCandela[42u] = 0.875f;

        Codecs::PhotometrySpaceDocumentModel PhotoOut{};
        if (!Require(Codecs::PhotometrySpaceEncoder::EncodeToBytes(PhotoIn, Bytes, Diag), "PhotometrySpace encode", Diag) ||
            !Require(Codecs::PhotometrySpaceDecoder::DecodeFromBytes(Bytes, PhotoOut, Diag), "PhotometrySpace decode", Diag) ||
            !Require(PhotoOut.CandelaIncluded && std::fabs(PhotoOut.CandelaProfile.NormalizedCandela[42u] - 0.875f) < 1.0e-6f, "PhotometrySpace candela fidelity"))
        {
            return 1;
        }

        Codecs::VolumetricSpaceDocumentModel VolIn{};
        VolIn.DocumentTitle                            = "TestCumulus";
        VolIn.Atmosphere.Clouds.CloudsEnabled          = true;
        VolIn.Atmosphere.Clouds.CloudRenderScale       = 1.00f;
        VolIn.Atmosphere.Clouds.WindSpeedMetersPerSecond = 0.0f;
        VolIn.ShapeVolumeDimension                     = 8u;
        VolIn.ShapeNoiseTexels.resize(8u * 8u * 8u * 4u, 127u);

        Codecs::VolumetricSpaceDocumentModel VolOut{};
        Bytes.clear();
        if (!Require(Codecs::VolumetricSpaceEncoder::EncodeToBytes(VolIn, Bytes, Diag), "VolumetricSpace encode", Diag) ||
            !Require(Codecs::VolumetricSpaceDecoder::DecodeFromBytes(Bytes, VolOut, Diag), "VolumetricSpace decode", Diag) ||
            !Require(VolOut.ShapeVolumeDimension == 8u && VolOut.ShapeNoiseTexels.size() == VolIn.ShapeNoiseTexels.size(), "VolumetricSpace condensate binary fidelity"))
        {
            return 1;
        }

        Codecs::RenderSpaceDocumentModel RenderIn{};
        RenderIn.DocumentTitle             = "TestViewport";
        RenderIn.CheapMatCapTwoByTwoShadow = true;
        RenderIn.FineCascadeHalfSpanMeters = 2.85f;
        RenderIn.StaticCloudRenderScale    = 1.00f;

        Codecs::RenderSpaceDocumentModel RenderOut{};
        Bytes.clear();
        if (!Require(Codecs::RenderSpaceEncoder::EncodeToBytes(RenderIn, Bytes, Diag), "RenderSpace encode", Diag) ||
            !Require(Codecs::RenderSpaceDecoder::DecodeFromBytes(Bytes, RenderOut, Diag), "RenderSpace decode", Diag) ||
            !Require(RenderOut.CheapMatCapTwoByTwoShadow && std::fabs(RenderOut.FineCascadeHalfSpanMeters - 2.85f) < 1.0e-5f, "RenderSpace fidelity"))
        {
            return 1;
        }

        Codecs::WorldSpaceDocumentModel WorldIn{};
        WorldIn.DocumentTitle             = "TestWorld";
        WorldIn.SolarPhotometryPath       = "Content/Photometry/Solar/ClearMorningSun.PhotometrySpace";
        WorldIn.EnvironmentVolumetricPath = "Content/Environment/Skies/ClearSkyDaylight.VolumetricSpace";
        WorldIn.RenderSettingsPath        = "Content/Rendering/DefaultEditorViewport.RenderSpace";
        Codecs::WorldGeometryPlacementEntry VehiclePlacement{};
        VehiclePlacement.PlacementTitle    = "ControlVehicle";
        VehiclePlacement.GeometrySpacePath = "Content/Geometry/Vehicles/ControlVehicle.GeometrySpace";
        WorldIn.GeometryPlacements.push_back(VehiclePlacement);

        Codecs::WorldSpaceDocumentModel WorldOut{};
        Bytes.clear();
        if (!Require(Codecs::WorldSpaceEncoder::EncodeToBytes(WorldIn, Bytes, Diag), "WorldSpace encode", Diag) ||
            !Require(Codecs::WorldSpaceDecoder::DecodeFromBytes(Bytes, WorldOut, Diag), "WorldSpace decode", Diag) ||
            !Require(WorldOut.GeometryPlacements.size() == 1u && WorldOut.GeometryPlacements[0u].PlacementTitle == "ControlVehicle", "WorldSpace ControlVehicle placement"))
        {
            return 1;
        }
    }

    // 3. Verify complete Content/ repository on disk
    std::filesystem::path RepoRoot = std::filesystem::current_path();
    for (int Depth = 0; Depth < 6; ++Depth)
    {
        if (std::filesystem::exists(RepoRoot / "EngineContent" / "EnvironmentArchives" / "ControlVehicle" / "ControlVehicleBody.face"))
        {
            break;
        }
        if (!RepoRoot.has_parent_path() || RepoRoot.parent_path() == RepoRoot)
        {
            break;
        }
        RepoRoot = RepoRoot.parent_path();
    }

    std::string GenDiag{};
    if (!Require(Tools::DefaultContentGenerator::EnsureDefaultContentAvailable(RepoRoot, GenDiag), "EnsureDefaultContentAvailable", GenDiag))
    {
        return 1;
    }

    uint32_t VerifiedAssets = 0u;
    if (!Require(Tools::DefaultContentGenerator::VerifyRepositoryContent(RepoRoot, VerifiedAssets, GenDiag), "VerifyRepositoryContent", GenDiag) ||
        !Require(VerifiedAssets >= 50u, "Complete Content/ asset population count", std::to_string(VerifiedAssets)))
    {
        return 1;
    }

    // Assert every file under Content/ uses an explicit *Space suffix (zero .toml, .ies, .face, .InSpace)
    for (const auto& DirEntry : std::filesystem::recursive_directory_iterator(RepoRoot / "Content"))
    {
        if (!DirEntry.is_regular_file())
        {
            continue;
        }
        const std::string Ext = DirEntry.path().extension().string();
        if (!Require(Ext.ends_with("Space"), "Explicit *Space suffix inside Content/", DirEntry.path().generic_string()))
        {
            return 1;
        }
    }

    // Verify ControlVehicle.ComponentSpace & ControlVehicle.GeometrySpace titles and triangle count
    {
        ComponentClass::ComponentClassDefinition RigOut{};
        if (!Require(Codecs::ComponentSpaceDecoder::DecodeFromFile(RepoRoot / "Content/Components/ControlVehicle.ComponentSpace", RigOut, GenDiag), "Decode ControlVehicle.ComponentSpace", GenDiag) ||
            !Require(RigOut.Title == "ControlVehicle", "ControlVehicle.ComponentSpace title", RigOut.Title))
        {
            return 1;
        }

        Codecs::GeometrySpaceDocument VehicleGeo{};
        if (!Require(Codecs::GeometryDecoder::DecodeFromFile(RepoRoot / "Content/Geometry/Vehicles/ControlVehicle.GeometrySpace", VehicleGeo, GenDiag), "Decode ControlVehicle.GeometrySpace", GenDiag) ||
            !Require(VehicleGeo.Network.NetworkTitle == "ControlVehicle", "ControlVehicle.GeometrySpace title", VehicleGeo.Network.NetworkTitle) ||
            !Require(VehicleGeo.Network.Faces.size() == 25068u, "ControlVehicle 25,068 faces", std::to_string(VehicleGeo.Network.Faces.size())) ||
            !Require(VehicleGeo.FaceCornerNormals.size() == 25068u * 9u, "ControlVehicle SmoothNormals binary section") ||
            !Require(VehicleGeo.SurfaceSlots.size() == 11u, "ControlVehicle 11 OpenPBR SurfaceSlots"))
        {
            return 1;
        }
    }

#if defined(FRONTIER_PROFILE_EDITOR)
    // 4. Verify Editor ShowcaseGalleryScene loads VehicleClearSkyDefault.WorldSpace & NightCarParkPhotometric.WorldSpace
    {
        GeometricRaster::ShowcaseWorldSceneBundle DefaultBundle{};
        GeometricRaster::BuildVehicleClearSkyDefaultScene(DefaultBundle, 640u, 360u);
        if (!Require(DefaultBundle.Objects.size() == 2u, "Default scene has 2 objects (GroundPlane + ControlVehicle)") ||
            !Require(DefaultBundle.Objects[1u].Title == "ControlVehicle", "Vehicle Title is ControlVehicle", DefaultBundle.Objects[1u].Title) ||
            !Require(DefaultBundle.Objects[1u].LocalSmoothTriangles.size() == 25068u, "ControlVehicle loaded 25,068 smooth triangles from .GeometrySpace") ||
            !Require(!DefaultBundle.Scene.Atmosphere.Clouds.CloudsEnabled, "Clear sky default has clouds disabled"))
        {
            return 1;
        }

        GeometricRaster::ShowcaseWorldSceneBundle NightBundle{};
        if (!Require(GeometricRaster::LoadWorldSceneFromSpaceFile(
                         RepoRoot / "Content/Worlds/NightCarParkPhotometric.WorldSpace",
                         RepoRoot,
                         NightBundle,
                         640u,
                         360u,
                         GenDiag),
                     "Load NightCarParkPhotometric.WorldSpace",
                     GenDiag) ||
            !Require(NightBundle.Scene.PhotometricRadiators.size() == 5u, "NightCarParkPhotometric 5 photometric radiators") ||
            !Require(NightBundle.Scene.PhotometricProfiles.size() == 3u, "NightCarParkPhotometric 3 resampled 128x32 profiles"))
        {
            return 1;
        }
    }
#endif

    std::printf(
        "[DataDrivenSpaceContentValidation] ALL CHECKS PASSED (%u verified Content/ .Space containers; vehicle named ControlVehicle).\n",
        VerifiedAssets);
    return 0;
}
