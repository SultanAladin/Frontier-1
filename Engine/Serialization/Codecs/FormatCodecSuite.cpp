/*============================================================================================================================================
                                                          FORMATCODECSUITE.CPP
============================================================================================================================================*/
// 🧩 Implementation of typed Frontier codecs and concrete Two-Tier Hybrid example generation across all 63 formats

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Codecs/FormatCodecSuite.h"

#include "Engine/Serialization/Embedding/InstanceContainerCodec.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace Frontier::Serialization
{

namespace
{

[[nodiscard]] toml::array PackFloat3(const std::array<float, 3u>& Vec)
{
    toml::array Out{};
    Out.push_back(static_cast<double>(Vec[0]));
    Out.push_back(static_cast<double>(Vec[1]));
    Out.push_back(static_cast<double>(Vec[2]));
    return Out;
}

[[nodiscard]] toml::array PackFloat2(const std::array<float, 2u>& Vec)
{
    toml::array Out{};
    Out.push_back(static_cast<double>(Vec[0]));
    Out.push_back(static_cast<double>(Vec[1]));
    return Out;
}

[[nodiscard]] std::array<float, 3u> ReadFloat3(
    const toml::table&            Section,
    std::string_view              Key,
    const std::array<float, 3u>&  Fallback)
{
    const toml::array* Arr = Section[Key].as_array();
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return Fallback;
    }
    return {
        static_cast<float>((*Arr)[0].value_or<double>(Fallback[0])),
        static_cast<float>((*Arr)[1].value_or<double>(Fallback[1])),
        static_cast<float>((*Arr)[2].value_or<double>(Fallback[2]))
    };
}

[[nodiscard]] std::array<float, 2u> ReadFloat2(
    const toml::table&            Section,
    std::string_view              Key,
    const std::array<float, 2u>&  Fallback)
{
    const toml::array* Arr = Section[Key].as_array();
    if (Arr == nullptr || Arr->size() < 2u)
    {
        return Fallback;
    }
    return {
        static_cast<float>((*Arr)[0].value_or<double>(Fallback[0])),
        static_cast<float>((*Arr)[1].value_or<double>(Fallback[1]))
    };
}

[[nodiscard]] std::vector<std::byte> PackFloatSpan(std::span<const float> Scalars)
{
    std::vector<std::byte> Bytes(Scalars.size() * sizeof(float));
    if (!Scalars.empty())
    {
        std::memcpy(Bytes.data(), Scalars.data(), Bytes.size());
    }
    return Bytes;
}

[[nodiscard]] std::string FormatExampleUuid(uint16_t CategoryIndex)
{
    std::ostringstream Stream{};
    Stream << "0193b5c0-7a1e-7000-8000-"
           << std::hex << std::nouppercase << std::setw(12) << std::setfill('0')
           << static_cast<uint32_t>(CategoryIndex + 1000u);
    return Stream.str();
}

void WriteCameraOpticsFields(const CameraOpticsDocument& Doc, toml::table& OutSection)
{
    OutSection.insert("ApertureBladeCount",        static_cast<int64_t>(Doc.ApertureBladeCount));
    OutSection.insert("ApertureFStop",             static_cast<double>(Doc.ApertureFStop));
    OutSection.insert("BladeCurvature",            static_cast<double>(Doc.BladeCurvature));
    OutSection.insert("BokehCatsEyeFactor",        static_cast<double>(Doc.BokehCatsEyeFactor));
    OutSection.insert("BokehSphericalAberration",  static_cast<double>(Doc.BokehSphericalAberration));
    OutSection.insert("ChromaticDispersionRms",    static_cast<double>(Doc.ChromaticDispersionRms));
    OutSection.insert("FocalLengthMillimeters",    static_cast<double>(Doc.FocalLengthMillimeters));
    OutSection.insert("FocusBreathingRatio",       static_cast<double>(Doc.FocusBreathingRatio));
    OutSection.insert("FocusDistanceMeters",       static_cast<double>(Doc.FocusDistanceMeters));
    OutSection.insert("OpticalVignettingStrength", static_cast<double>(Doc.OpticalVignettingStrength));
    OutSection.insert("OpticsTitle",               Doc.OpticsTitle);
    OutSection.insert("RadialDistortionK",         PackFloat3(Doc.RadialDistortionK));
    OutSection.insert("SensorHeightMillimeters",   static_cast<double>(Doc.SensorHeightMillimeters));
    OutSection.insert("SensorIsoSensitivity",      static_cast<double>(Doc.SensorIsoSensitivity));
    OutSection.insert("SensorWidthMillimeters",    static_cast<double>(Doc.SensorWidthMillimeters));
    OutSection.insert("ShutterAngleDegrees",       static_cast<double>(Doc.ShutterAngleDegrees));
    OutSection.insert("ShutterSpeedSeconds",       static_cast<double>(Doc.ShutterSpeedSeconds));
    OutSection.insert("TangentialDistortionP",     PackFloat2(Doc.TangentialDistortionP));
}

void ReadCameraOpticsFields(const toml::table& Section, CameraOpticsDocument& OutDoc)
{
    OutDoc.ApertureBladeCount        = static_cast<uint32_t>(Section["ApertureBladeCount"].value_or<int64_t>(9));
    OutDoc.ApertureFStop             = static_cast<float>(Section["ApertureFStop"].value_or<double>(2.8));
    OutDoc.BladeCurvature            = static_cast<float>(Section["BladeCurvature"].value_or<double>(0.35));
    OutDoc.BokehCatsEyeFactor        = static_cast<float>(Section["BokehCatsEyeFactor"].value_or<double>(0.18));
    OutDoc.BokehSphericalAberration  = static_cast<float>(Section["BokehSphericalAberration"].value_or<double>(0.12));
    OutDoc.ChromaticDispersionRms    = static_cast<float>(Section["ChromaticDispersionRms"].value_or<double>(0.0008));
    OutDoc.FocalLengthMillimeters    = static_cast<float>(Section["FocalLengthMillimeters"].value_or<double>(50.0));
    OutDoc.FocusBreathingRatio       = static_cast<float>(Section["FocusBreathingRatio"].value_or<double>(0.015));
    OutDoc.FocusDistanceMeters       = static_cast<float>(Section["FocusDistanceMeters"].value_or<double>(6.5));
    OutDoc.OpticalVignettingStrength = static_cast<float>(Section["OpticalVignettingStrength"].value_or<double>(0.22));
    OutDoc.OpticsTitle               = Section["OpticsTitle"].value_or<std::string>("CinePrime50mm");
    OutDoc.RadialDistortionK         = ReadFloat3(Section, "RadialDistortionK", OutDoc.RadialDistortionK);
    OutDoc.SensorHeightMillimeters   = static_cast<float>(Section["SensorHeightMillimeters"].value_or<double>(24.0));
    OutDoc.SensorIsoSensitivity      = static_cast<float>(Section["SensorIsoSensitivity"].value_or<double>(100.0));
    OutDoc.SensorWidthMillimeters    = static_cast<float>(Section["SensorWidthMillimeters"].value_or<double>(36.0));
    OutDoc.ShutterAngleDegrees       = static_cast<float>(Section["ShutterAngleDegrees"].value_or<double>(180.0));
    OutDoc.ShutterSpeedSeconds       = static_cast<float>(Section["ShutterSpeedSeconds"].value_or<double>(1.0 / 125.0));
    OutDoc.TangentialDistortionP     = ReadFloat2(Section, "TangentialDistortionP", OutDoc.TangentialDistortionP);
}

void WriteAtmosphereFields(const AtmosphereDocument& Doc, toml::table& OutSection)
{
    OutSection.insert("AtmosphereTitle",           Doc.AtmosphereTitle);
    OutSection.insert("AtmosphericTurbidity",      static_cast<double>(Doc.AtmosphericTurbidity));
    OutSection.insert("CelestialStarfieldEnabled", Doc.CelestialStarfieldEnabled);
    OutSection.insert("CloudCoverageFraction",     static_cast<double>(Doc.CloudCoverageFraction));
    OutSection.insert("GroundAlbedoReflectance",   static_cast<double>(Doc.GroundAlbedoReflectance));
    OutSection.insert("MieAnisotropyG",            static_cast<double>(Doc.MieAnisotropyG));
    OutSection.insert("MieScaleHeightMeters",      static_cast<double>(Doc.MieScaleHeightMeters));
    OutSection.insert("OzoneAbsorptionWeight",     static_cast<double>(Doc.OzoneAbsorptionWeight));
    OutSection.insert("RayleighScaleHeightMeters", static_cast<double>(Doc.RayleighScaleHeightMeters));
    OutSection.insert("SolarAngularDiameterDeg",   static_cast<double>(Doc.SolarAngularDiameterDeg));
    OutSection.insert("SolarAzimuthDegrees",       static_cast<double>(Doc.SolarAzimuthDegrees));
    OutSection.insert("SolarDiskEnabled",          Doc.SolarDiskEnabled);
    OutSection.insert("SolarElevationDegrees",     static_cast<double>(Doc.SolarElevationDegrees));
    OutSection.insert("SolarIlluminanceLux",       static_cast<double>(Doc.SolarIlluminanceLux));
    OutSection.insert("ZenithTintLinearRgb",       PackFloat3(Doc.ZenithTintLinearRgb));
}

void ReadAtmosphereFields(const toml::table& Section, AtmosphereDocument& OutDoc)
{
    OutDoc.AtmosphereTitle           = Section["AtmosphereTitle"].value_or<std::string>("ClearSkyDaylight");
    OutDoc.AtmosphericTurbidity      = static_cast<float>(Section["AtmosphericTurbidity"].value_or<double>(2.1));
    OutDoc.CelestialStarfieldEnabled = Section["CelestialStarfieldEnabled"].value_or<bool>(false);
    OutDoc.CloudCoverageFraction     = static_cast<float>(Section["CloudCoverageFraction"].value_or<double>(0.08));
    OutDoc.GroundAlbedoReflectance   = static_cast<float>(Section["GroundAlbedoReflectance"].value_or<double>(0.22));
    OutDoc.MieAnisotropyG            = static_cast<float>(Section["MieAnisotropyG"].value_or<double>(0.78));
    OutDoc.MieScaleHeightMeters      = static_cast<float>(Section["MieScaleHeightMeters"].value_or<double>(1200.0));
    OutDoc.OzoneAbsorptionWeight     = static_cast<float>(Section["OzoneAbsorptionWeight"].value_or<double>(1.0));
    OutDoc.RayleighScaleHeightMeters = static_cast<float>(Section["RayleighScaleHeightMeters"].value_or<double>(8000.0));
    OutDoc.SolarAngularDiameterDeg   = static_cast<float>(Section["SolarAngularDiameterDeg"].value_or<double>(0.533));
    OutDoc.SolarAzimuthDegrees       = static_cast<float>(Section["SolarAzimuthDegrees"].value_or<double>(135.0));
    OutDoc.SolarDiskEnabled          = Section["SolarDiskEnabled"].value_or<bool>(true);
    OutDoc.SolarElevationDegrees     = static_cast<float>(Section["SolarElevationDegrees"].value_or<double>(42.0));
    OutDoc.SolarIlluminanceLux       = static_cast<float>(Section["SolarIlluminanceLux"].value_or<double>(105000.0));
    OutDoc.ZenithTintLinearRgb       = ReadFloat3(Section, "ZenithTintLinearRgb", OutDoc.ZenithTintLinearRgb);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CONFIGURATION FAMILY CODECS
//------------------------------------------------------------------------------------------------------------------------

bool FormatCodecSuite::EncodeEngineSettings(
    const EngineSettingsDocument& Document,
    HybridContainerDocument&      OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::EngineSettings;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::EngineSettings));
    OutContainer.DocumentTitle      = "EngineDefault";
    OutContainer.AuthoringWorkspace = "Frontier.Configuration";

    toml::table Body{};
    Body.insert("ActiveProjectIndexPath",      Document.ActiveProjectIndexPath);
    Body.insert("CompiledRepositoryDirectory", Document.CompiledRepositoryDirectory);
    Body.insert("DefaultGraphicsSettingsPath", Document.DefaultGraphicsSettingsPath);
    Body.insert("DefaultStartupWorldPath",     Document.DefaultStartupWorldPath);
    Body.insert("DocumentRevisionLimitMiB",    static_cast<int64_t>(Document.DocumentRevisionLimitMiB));
    Body.insert("GlobalRevisionLimitMiB",      static_cast<int64_t>(Document.GlobalRevisionLimitMiB));
    Body.insert("RepositoryQuotaMegabytes",    static_cast<int64_t>(Document.RepositoryQuotaMegabytes));
    OutContainer.TomlHeader.insert("EngineSettings", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodeEngineSettings(
    const HybridContainerDocument& Container,
    EngineSettingsDocument&        OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::EngineSettings)
    {
        OutDiagnostic = "Expected .EngineSettings container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["EngineSettings"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [EngineSettings] section.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion               = Container.SchemaVersion;
    OutDocument.ActiveProjectIndexPath      = (*Body)["ActiveProjectIndexPath"].value_or(std::string(OutDocument.ActiveProjectIndexPath));
    OutDocument.CompiledRepositoryDirectory = (*Body)["CompiledRepositoryDirectory"].value_or(std::string(OutDocument.CompiledRepositoryDirectory));
    OutDocument.DefaultGraphicsSettingsPath = (*Body)["DefaultGraphicsSettingsPath"].value_or(std::string(OutDocument.DefaultGraphicsSettingsPath));
    OutDocument.DefaultStartupWorldPath     = (*Body)["DefaultStartupWorldPath"].value_or(std::string(OutDocument.DefaultStartupWorldPath));
    OutDocument.DocumentRevisionLimitMiB    = static_cast<uint32_t>((*Body)["DocumentRevisionLimitMiB"].value_or<int64_t>(215));
    OutDocument.GlobalRevisionLimitMiB      = static_cast<uint32_t>((*Body)["GlobalRevisionLimitMiB"].value_or<int64_t>(750));
    OutDocument.RepositoryQuotaMegabytes    = static_cast<uint64_t>((*Body)["RepositoryQuotaMegabytes"].value_or<int64_t>(2048));
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodeRuntimeConfiguration(
    const RuntimeConfigurationDocument& Document,
    HybridContainerDocument&            OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::RuntimeConfiguration;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::RuntimeConfiguration));
    OutContainer.DocumentTitle      = "RuntimeDefault";
    OutContainer.AuthoringWorkspace = "Frontier.Configuration";

    toml::table Body{};
    Body.insert("ApplicationTitle",       Document.ApplicationTitle);
    Body.insert("EnableVerticalSync",     Document.EnableVerticalSync);
    Body.insert("EnableVulkanValidation", Document.EnableVulkanValidation);
    Body.insert("StartupWorldPath",       Document.StartupWorldPath);
    Body.insert("TargetFramesPerSecond",  static_cast<int64_t>(Document.TargetFramesPerSecond));
    Body.insert("WindowHeightPixels",     static_cast<int64_t>(Document.WindowHeightPixels));
    Body.insert("WindowWidthPixels",      static_cast<int64_t>(Document.WindowWidthPixels));
    OutContainer.TomlHeader.insert("RuntimeConfiguration", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodeRuntimeConfiguration(
    const HybridContainerDocument& Container,
    RuntimeConfigurationDocument&  OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::RuntimeConfiguration)
    {
        OutDiagnostic = "Expected .RuntimeConfiguration container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["RuntimeConfiguration"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [RuntimeConfiguration] section.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion          = Container.SchemaVersion;
    OutDocument.ApplicationTitle       = (*Body)["ApplicationTitle"].value_or(std::string(OutDocument.ApplicationTitle));
    OutDocument.EnableVerticalSync     = (*Body)["EnableVerticalSync"].value_or<bool>(true);
    OutDocument.EnableVulkanValidation = (*Body)["EnableVulkanValidation"].value_or<bool>(true);
    OutDocument.StartupWorldPath       = (*Body)["StartupWorldPath"].value_or(std::string(OutDocument.StartupWorldPath));
    OutDocument.TargetFramesPerSecond  = static_cast<uint32_t>((*Body)["TargetFramesPerSecond"].value_or<int64_t>(60));
    OutDocument.WindowHeightPixels     = static_cast<uint32_t>((*Body)["WindowHeightPixels"].value_or<int64_t>(1080));
    OutDocument.WindowWidthPixels      = static_cast<uint32_t>((*Body)["WindowWidthPixels"].value_or<int64_t>(1920));
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodePlatformSettings(
    const PlatformSettingsDocument& Document,
    HybridContainerDocument&        OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::PlatformSettings;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::PlatformSettings));
    OutContainer.DocumentTitle      = "PlatformDefault";
    OutContainer.AuthoringWorkspace = "Frontier.Configuration";

    toml::table Body{};
    Body.insert("PreferDedicatedTransfer", Document.PreferDedicatedTransfer);
    Body.insert("PreferredGraphicsApi",    Document.PreferredGraphicsApi);
    Body.insert("PreferredPresentMode",    Document.PreferredPresentMode);
    Body.insert("TargetPlatformName",      Document.TargetPlatformName);
    Body.insert("WorkerThreadCap",         static_cast<int64_t>(Document.WorkerThreadCap));
    OutContainer.TomlHeader.insert("PlatformSettings", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodePlatformSettings(
    const HybridContainerDocument& Container,
    PlatformSettingsDocument&      OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::PlatformSettings)
    {
        OutDiagnostic = "Expected .PlatformSettings container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["PlatformSettings"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [PlatformSettings] section.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion           = Container.SchemaVersion;
    OutDocument.PreferDedicatedTransfer = (*Body)["PreferDedicatedTransfer"].value_or<bool>(true);
    OutDocument.PreferredGraphicsApi    = (*Body)["PreferredGraphicsApi"].value_or(std::string(OutDocument.PreferredGraphicsApi));
    OutDocument.PreferredPresentMode    = (*Body)["PreferredPresentMode"].value_or(std::string(OutDocument.PreferredPresentMode));
    OutDocument.TargetPlatformName      = (*Body)["TargetPlatformName"].value_or(std::string(OutDocument.TargetPlatformName));
    OutDocument.WorkerThreadCap         = static_cast<uint32_t>((*Body)["WorkerThreadCap"].value_or<int64_t>(0));
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodeProjectIndex(
    const ProjectIndexDocument& Document,
    HybridContainerDocument&    OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::ProjectIndex;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = Document.ProjectIdentifier;
    OutContainer.DocumentTitle      = Document.ProjectName;
    OutContainer.AuthoringWorkspace = "Frontier.ProjectWorkspace";

    toml::table Body{};
    Body.insert("ProjectIdentifier", Document.ProjectIdentifier);
    Body.insert("ProjectName",       Document.ProjectName);
    Body.insert("StartupWorldPath",  Document.StartupWorldPath);

    toml::array SearchRoots{};
    for (const std::string& Root : Document.ContentSearchRoots)
    {
        SearchRoots.push_back(Root);
    }
    Body.insert("ContentSearchRoots", std::move(SearchRoots));

    toml::array Worlds{};
    for (const std::string& WorldPath : Document.RegisteredWorlds)
    {
        Worlds.push_back(WorldPath);
    }
    Body.insert("RegisteredWorlds", std::move(Worlds));

    OutContainer.TomlHeader.insert("ProjectIndex", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodeProjectIndex(
    const HybridContainerDocument& Container,
    ProjectIndexDocument&          OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::ProjectIndex)
    {
        OutDiagnostic = "Expected .ProjectIndex container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["ProjectIndex"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [ProjectIndex] section.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion     = Container.SchemaVersion;
    OutDocument.ProjectIdentifier = (*Body)["ProjectIdentifier"].value_or(std::string(Container.StableIdentifier));
    OutDocument.ProjectName       = (*Body)["ProjectName"].value_or(std::string(Container.DocumentTitle));
    OutDocument.StartupWorldPath  = (*Body)["StartupWorldPath"].value_or(std::string(OutDocument.StartupWorldPath));
    OutDocument.ContentSearchRoots.clear();
    if (const toml::array* Roots = (*Body)["ContentSearchRoots"].as_array())
    {
        for (const auto& Entry : *Roots)
        {
            if (auto Str = Entry.value<std::string>())
            {
                OutDocument.ContentSearchRoots.push_back(*Str);
            }
        }
    }
    OutDocument.RegisteredWorlds.clear();
    if (const toml::array* Worlds = (*Body)["RegisteredWorlds"].as_array())
    {
        for (const auto& Entry : *Worlds)
        {
            if (auto Str = Entry.value<std::string>())
            {
                OutDocument.RegisteredWorlds.push_back(*Str);
            }
        }
    }
    OutDiagnostic.clear();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                   CAMERA OPTICS, ATMOSPHERE, FRACTURE & WORLDSPACE
//------------------------------------------------------------------------------------------------------------------------

bool FormatCodecSuite::EncodeCameraOptics(
    const CameraOpticsDocument& Document,
    HybridContainerDocument&    OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::CameraOptics;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::CameraOptics));
    OutContainer.DocumentTitle      = Document.OpticsTitle;
    OutContainer.AuthoringWorkspace = "Frontier.OpticsWorkspace";

    toml::table Body{};
    WriteCameraOpticsFields(Document, Body);
    OutContainer.TomlHeader.insert("CameraOptics", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodeCameraOptics(
    const HybridContainerDocument& Container,
    CameraOpticsDocument&          OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::CameraOptics)
    {
        OutDiagnostic = "Expected .CameraOptics container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["CameraOptics"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [CameraOptics] section.";
        return false;
    }
    OutDocument = {};
    OutDocument.SchemaVersion = Container.SchemaVersion;
    ReadCameraOpticsFields(*Body, OutDocument);
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodeAtmosphere(
    const AtmosphereDocument& Document,
    HybridContainerDocument&  OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::Atmosphere;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::Atmosphere));
    OutContainer.DocumentTitle      = Document.AtmosphereTitle;
    OutContainer.AuthoringWorkspace = "Frontier.AtmosphereWorkspace";

    toml::table Body{};
    WriteAtmosphereFields(Document, Body);
    OutContainer.TomlHeader.insert("Atmosphere", std::move(Body));
    return true;
}

bool FormatCodecSuite::DecodeAtmosphere(
    const HybridContainerDocument& Container,
    AtmosphereDocument&            OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::Atmosphere)
    {
        OutDiagnostic = "Expected .Atmosphere container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["Atmosphere"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [Atmosphere] section.";
        return false;
    }
    OutDocument = {};
    OutDocument.SchemaVersion = Container.SchemaVersion;
    ReadAtmosphereFields(*Body, OutDocument);
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodeFractureNetwork(
    const FractureNetworkDocument& Document,
    HybridContainerDocument&       OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::FractureNetwork;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(FormatSuffixCategory::FractureNetwork));
    OutContainer.DocumentTitle      = Document.NetworkTitle;
    OutContainer.AuthoringWorkspace = "Frontier.PhysicsWorkspace";

    toml::table Body{};
    Body.insert("BondCount",       static_cast<int64_t>(Document.CohesiveBonds.size()));
    Body.insert("CellCount",       static_cast<int64_t>(Document.Cells.size()));
    Body.insert("NetworkTitle",    Document.NetworkTitle);
    Body.insert("SurfaceMaterial", Document.SurfaceMaterial);
    OutContainer.TomlHeader.insert("FractureNetwork", std::move(Body));

    toml::array CellArray{};
    for (const FractureCellRecord& Cell : Document.Cells)
    {
        toml::table Row{};
        Row.insert("CellIdentifier",          static_cast<int64_t>(Cell.CellIdentifier));
        Row.insert("CentroidMeters",          PackFloat3(Cell.CentroidMeters));
        Row.insert("ContainmentIndex",        static_cast<int64_t>(Cell.ContainmentIndex));
        Row.insert("MassKilograms",           static_cast<double>(Cell.MassKilograms));
        Row.insert("RuptureThresholdNewtons", static_cast<double>(Cell.RuptureThresholdNewtons));
        CellArray.push_back(std::move(Row));
    }
    OutContainer.TomlHeader.insert("FractureCells", std::move(CellArray));

    toml::array BondArray{};
    for (const FractureBondRecord& Bond : Document.CohesiveBonds)
    {
        toml::table Row{};
        Row.insert("CellIndexA",           static_cast<int64_t>(Bond.CellIndexA));
        Row.insert("CellIndexB",           static_cast<int64_t>(Bond.CellIndexB));
        Row.insert("CohesiveAreaSqMeters", static_cast<double>(Bond.CohesiveAreaSqMeters));
        Row.insert("TensileStrengthMpa",   static_cast<double>(Bond.TensileStrengthMpa));
        BondArray.push_back(std::move(Row));
    }
    OutContainer.TomlHeader.insert("CohesiveBonds", std::move(BondArray));

    // Pack shard centroids + mass into 64B-aligned Tier 2 binary block for zero-copy solver upload
    std::vector<float> PackedShardMetrics{};
    PackedShardMetrics.reserve(Document.Cells.size() * 4u);
    for (const FractureCellRecord& Cell : Document.Cells)
    {
        PackedShardMetrics.push_back(Cell.CentroidMeters[0]);
        PackedShardMetrics.push_back(Cell.CentroidMeters[1]);
        PackedShardMetrics.push_back(Cell.CentroidMeters[2]);
        PackedShardMetrics.push_back(Cell.MassKilograms);
    }
    if (!PackedShardMetrics.empty())
    {
        BinarySectionBlock Block{};
        Block.SectionIdentifier = "VoronoiShardMetrics";
        Block.SubFormatTag      = ".FractureNetwork";
        Block.PayloadBytes      = PackFloatSpan(PackedShardMetrics);
        OutContainer.BinarySections.push_back(std::move(Block));
    }
    return true;
}

bool FormatCodecSuite::DecodeFractureNetwork(
    const HybridContainerDocument& Container,
    FractureNetworkDocument&       OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::FractureNetwork)
    {
        OutDiagnostic = "Expected .FractureNetwork container.";
        return false;
    }
    const toml::table* Body = Container.TomlHeader["FractureNetwork"].as_table();
    if (Body == nullptr)
    {
        OutDiagnostic = "Missing [FractureNetwork] section.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion   = Container.SchemaVersion;
    OutDocument.NetworkTitle    = (*Body)["NetworkTitle"].value_or(std::string(Container.DocumentTitle));
    OutDocument.SurfaceMaterial = (*Body)["SurfaceMaterial"].value_or<std::string>("TemperedAutomotiveGlass");

    if (const toml::array* Cells = Container.TomlHeader["FractureCells"].as_array())
    {
        for (const auto& Entry : *Cells)
        {
            if (const toml::table* Row = Entry.as_table())
            {
                FractureCellRecord Rec{};
                Rec.CellIdentifier          = static_cast<uint32_t>((*Row)["CellIdentifier"].value_or<int64_t>(0));
                Rec.ContainmentIndex        = static_cast<int32_t>((*Row)["ContainmentIndex"].value_or<int64_t>(-1));
                Rec.CentroidMeters          = ReadFloat3(*Row, "CentroidMeters", Rec.CentroidMeters);
                Rec.MassKilograms           = static_cast<float>((*Row)["MassKilograms"].value_or<double>(0.25));
                Rec.RuptureThresholdNewtons = static_cast<float>((*Row)["RuptureThresholdNewtons"].value_or<double>(1850.0));
                OutDocument.Cells.push_back(Rec);
            }
        }
    }

    if (const toml::array* Bonds = Container.TomlHeader["CohesiveBonds"].as_array())
    {
        for (const auto& Entry : *Bonds)
        {
            if (const toml::table* Row = Entry.as_table())
            {
                FractureBondRecord Bond{};
                Bond.CellIndexA           = static_cast<uint32_t>((*Row)["CellIndexA"].value_or<int64_t>(0));
                Bond.CellIndexB           = static_cast<uint32_t>((*Row)["CellIndexB"].value_or<int64_t>(0));
                Bond.CohesiveAreaSqMeters = static_cast<float>((*Row)["CohesiveAreaSqMeters"].value_or<double>(0.015));
                Bond.TensileStrengthMpa   = static_cast<float>((*Row)["TensileStrengthMpa"].value_or<double>(45.0));
                OutDocument.CohesiveBonds.push_back(Bond);
            }
        }
    }
    OutDiagnostic.clear();
    return true;
}

bool FormatCodecSuite::EncodeWorldSpace(
    const WorldSpaceDocument& Document,
    HybridContainerDocument&  OutContainer)
{
    OutContainer = {};
    OutContainer.SuffixCategory     = FormatSuffixCategory::WorldSpace;
    OutContainer.SchemaVersion      = Document.SchemaVersion;
    OutContainer.StableIdentifier   = Document.StableIdentifier;
    OutContainer.DocumentTitle      = Document.WorldTitle;
    OutContainer.AuthoringWorkspace = "Frontier.WorldWorkspace";

    toml::table CameraSection{};
    WriteCameraOpticsFields(Document.StartupCameraOptics, CameraSection);
    CameraSection.insert("CameraPositionMeters", PackFloat3(Document.CameraPositionMeters));
    CameraSection.insert("CameraTargetMeters",   PackFloat3(Document.CameraTargetMeters));
    OutContainer.TomlHeader.insert("StartupCamera", std::move(CameraSection));

    toml::table AtmoSection{};
    WriteAtmosphereFields(Document.Atmosphere, AtmoSection);
    OutContainer.TomlHeader.insert("Atmosphere", std::move(AtmoSection));

    EmbeddedPillarEntry CamDecl{};
    CamDecl.PillarCategory    = FormatSuffixCategory::CameraOptics;
    CamDecl.SectionIdentifier = "StartupCamera";
    CamDecl.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
    OutContainer.EmbeddedPillars.push_back(std::move(CamDecl));

    EmbeddedPillarEntry AtmoDecl{};
    AtmoDecl.PillarCategory    = FormatSuffixCategory::Atmosphere;
    AtmoDecl.SectionIdentifier = "Atmosphere";
    AtmoDecl.ResidencyMode     = EmbeddedResidencyMode::TomlSection;
    OutContainer.EmbeddedPillars.push_back(std::move(AtmoDecl));

    toml::array Placements{};
    for (const WorldPlacementEntry& Entry : Document.Placements)
    {
        toml::table Row{};
        Row.insert("ContainmentIndex",     static_cast<int64_t>(Entry.ContainmentIndex));
        Row.insert("DisplayTitle",         Entry.DisplayTitle);
        Row.insert("InstanceRelativePath", Entry.InstanceRelativePath);
        Row.insert("PlacementIdentifier",  Entry.PlacementIdentifier);
        Row.insert("PositionMeters",       PackFloat3(Entry.PositionMeters));
        Row.insert("RotationEulerDegrees", PackFloat3(Entry.RotationEulerDegrees));
        Row.insert("ScaleFactors",         PackFloat3(Entry.ScaleFactors));
        Row.insert("VisibleInViewport",    Entry.VisibleInViewport);
        Placements.push_back(std::move(Row));
    }
    OutContainer.TomlHeader.insert("WorldPlacements", std::move(Placements));
    return true;
}

bool FormatCodecSuite::DecodeWorldSpace(
    const HybridContainerDocument& Container,
    WorldSpaceDocument&            OutDocument,
    std::string&                   OutDiagnostic)
{
    if (Container.SuffixCategory != FormatSuffixCategory::WorldSpace)
    {
        OutDiagnostic = "Expected .WorldSpace container.";
        return false;
    }

    OutDocument = {};
    OutDocument.SchemaVersion    = Container.SchemaVersion;
    OutDocument.StableIdentifier = Container.StableIdentifier;
    OutDocument.WorldTitle       = Container.DocumentTitle;

    if (const toml::table* Cam = Container.TomlHeader["StartupCamera"].as_table())
    {
        ReadCameraOpticsFields(*Cam, OutDocument.StartupCameraOptics);
        OutDocument.CameraPositionMeters = ReadFloat3(*Cam, "CameraPositionMeters", OutDocument.CameraPositionMeters);
        OutDocument.CameraTargetMeters   = ReadFloat3(*Cam, "CameraTargetMeters",   OutDocument.CameraTargetMeters);
    }

    if (const toml::table* Atmo = Container.TomlHeader["Atmosphere"].as_table())
    {
        ReadAtmosphereFields(*Atmo, OutDocument.Atmosphere);
    }

    if (const toml::array* Placements = Container.TomlHeader["WorldPlacements"].as_array())
    {
        for (const auto& Entry : *Placements)
        {
            if (const toml::table* Row = Entry.as_table())
            {
                WorldPlacementEntry Placement{};
                Placement.ContainmentIndex     = static_cast<int32_t>((*Row)["ContainmentIndex"].value_or<int64_t>(-1));
                Placement.DisplayTitle         = (*Row)["DisplayTitle"].value_or<std::string>("UnnamedPlacement");
                Placement.InstanceRelativePath = (*Row)["InstanceRelativePath"].value_or<std::string>("");
                Placement.PlacementIdentifier  = (*Row)["PlacementIdentifier"].value_or<std::string>("");
                Placement.PositionMeters       = ReadFloat3(*Row, "PositionMeters",       Placement.PositionMeters);
                Placement.RotationEulerDegrees = ReadFloat3(*Row, "RotationEulerDegrees", Placement.RotationEulerDegrees);
                Placement.ScaleFactors         = ReadFloat3(*Row, "ScaleFactors",         Placement.ScaleFactors);
                Placement.VisibleInViewport    = (*Row)["VisibleInViewport"].value_or<bool>(true);
                OutDocument.Placements.push_back(std::move(Placement));
            }
        }
    }

    OutDiagnostic.clear();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       UNIVERSAL 63-FORMAT EXAMPLE GENERATOR
//------------------------------------------------------------------------------------------------------------------------

WorldSpaceDocument FormatCodecSuite::ConstructVehicleClearSkyWorldExample()
{
    WorldSpaceDocument World{};
    World.WorldTitle                         = "VehicleClearSkyDefault";
    World.StartupCameraOptics.OpticsTitle    = "VehicleShowcase35mm";
    World.StartupCameraOptics.FocalLengthMillimeters = 35.0f;
    World.StartupCameraOptics.ApertureFStop  = 4.0f;
    World.CameraPositionMeters               = { 7.1f, -6.2f, 2.35f };
    World.CameraTargetMeters                 = { 0.0f, 0.0f, 0.95f };
    World.Atmosphere.AtmosphereTitle         = "ClearSkyDaylight";
    World.Atmosphere.SolarDiskEnabled        = true;
    World.Atmosphere.CelestialStarfieldEnabled = false;
    World.Atmosphere.SolarElevationDegrees   = 42.0f;
    World.Atmosphere.SolarAzimuthDegrees     = 135.0f;

    WorldPlacementEntry GroundFloor{};
    GroundFloor.PlacementIdentifier  = "pl-0001-ground-floor";
    GroundFloor.DisplayTitle         = "InfiniteAsphaltPlane";
    GroundFloor.ContainmentIndex     = -1;
    GroundFloor.InstanceRelativePath = "Content/Instances/AsphaltGround.Instance";
    World.Placements.push_back(std::move(GroundFloor));

    WorldPlacementEntry HeroVehicle{};
    HeroVehicle.PlacementIdentifier  = "pl-0002-hero-vehicle";
    HeroVehicle.DisplayTitle         = "ControlVehicleHero";
    HeroVehicle.ContainmentIndex     = -1;
    HeroVehicle.InstanceRelativePath = "Content/Instances/VehicleDefault.Instance";
    HeroVehicle.PositionMeters       = { 0.0f, 0.0f, 0.0f };
    World.Placements.push_back(std::move(HeroVehicle));

    WorldPlacementEntry ExhaustPlume{};
    ExhaustPlume.PlacementIdentifier  = "pl-0003-exhaust-plume";
    ExhaustPlume.DisplayTitle         = "VehicleExhaustPlume";
    ExhaustPlume.ContainmentIndex     = 1;
    ExhaustPlume.InstanceRelativePath = "Content/Particles/ExhaustPlume.ParticleSpace";
    ExhaustPlume.PositionMeters       = { -3.05f, 0.42f, 0.31f };
    World.Placements.push_back(std::move(ExhaustPlume));

    return World;
}

HybridContainerDocument FormatCodecSuite::ConstructExampleDocument(FormatSuffixCategory Category)
{
    // ① Use rich specialized encoders for showcase formats
    HybridContainerDocument Doc{};
    switch (Category)
    {
        case FormatSuffixCategory::EngineSettings:
            (void)EncodeEngineSettings(EngineSettingsDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::RuntimeConfiguration:
            (void)EncodeRuntimeConfiguration(RuntimeConfigurationDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::PlatformSettings:
            (void)EncodePlatformSettings(PlatformSettingsDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::ProjectIndex:
            (void)EncodeProjectIndex(ProjectIndexDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::CameraOptics:
            (void)EncodeCameraOptics(CameraOpticsDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::Atmosphere:
            (void)EncodeAtmosphere(AtmosphereDocument{}, Doc);
            return Doc;
        case FormatSuffixCategory::WorldSpace:
            (void)EncodeWorldSpace(ConstructVehicleClearSkyWorldExample(), Doc);
            return Doc;
        case FormatSuffixCategory::FractureNetwork:
        {
            FractureNetworkDocument Net{};
            Net.Cells = {
                { 0u, -1, { -0.18f, 0.0f, 0.25f }, 0.34f, 1650.0f },
                { 1u, -1, {  0.00f, 0.0f, 0.22f }, 0.29f, 1520.0f },
                { 2u, -1, {  0.19f, 0.0f, 0.26f }, 0.31f, 1740.0f }
            };
            Net.CohesiveBonds = {
                { 0u, 1u, 0.018f, 48.0f },
                { 1u, 2u, 0.016f, 46.0f }
            };
            (void)EncodeFractureNetwork(Net, Doc);
            return Doc;
        }
        case FormatSuffixCategory::Instance:
        case FormatSuffixCategory::ParticleSpace:
        case FormatSuffixCategory::InterfaceSpace:
        {
            const MultiPillarInstanceDocument MultiDoc =
                (Category == FormatSuffixCategory::Instance)
                    ? InstanceContainerCodec::ConstructVehicleDefaultExample()
                    : (Category == FormatSuffixCategory::ParticleSpace)
                          ? InstanceContainerCodec::ConstructExhaustPlumeParticleExample()
                          : InstanceContainerCodec::ConstructHudOverlayInterfaceExample();
            std::vector<std::byte> Encoded{};
            std::string            Ignored{};
            (void)InstanceContainerCodec::EncodeToBytes(MultiDoc, Encoded, Ignored);
            (void)HybridContainerDecoder::DecodeFromBytes(Encoded, Doc, Ignored);
            return Doc;
        }
        default:
            break;
    }

    // ② Construct meaningful, schema-faithful example documents for every remaining active & paused Frontier pillar
    const FormatSuffixDescriptor& Desc = InspectFormatSuffixDescriptor(Category);
    const std::string ExampleStem      = std::string("Example") + std::string(Desc.CanonicalSuffix.substr(1u));
    const bool UsesTier2BinaryPayload  = (Desc.PillarGroup != FormatPillarGroup::Configuration &&
                                          Desc.PillarGroup != FormatPillarGroup::Logic);
    Doc.SuffixCategory     = Category;
    Doc.SchemaVersion      = Desc.DefaultSchema;
    Doc.StableIdentifier   = FormatExampleUuid(static_cast<uint16_t>(Category));
    Doc.DocumentTitle      = ExampleStem;
    Doc.AuthoringWorkspace = Desc.FoundationalPillar ? "Frontier.PillarWorkspace" : "Frontier.FormatWorkspace";

    toml::table Spec{};
    Spec.insert("CanonicalSuffix",    std::string(Desc.CanonicalSuffix));
    Spec.insert("EditorOnly",         Desc.EditorOnly);
    Spec.insert("FoundationalPillar", Desc.FoundationalPillar);
    Spec.insert("SupportsEmbedding",  Desc.SupportsEmbedding);
    Spec.insert("UsesTier2Binary",    UsesTier2BinaryPayload);

    // Add category-specific realistic example parameters (especially for paused pillars like PhysicsSpace, FluidGrid, ForceField, etc.)
    switch (Category)
    {
        case FormatSuffixCategory::PhysicsSpace:
            Spec.insert("SolverEngine",          "JoltPhysics-5.6.0");
            Spec.insert("GravityMetersPerSecSq", -9.80665);
            Spec.insert("SubStepRateHertz",      static_cast<int64_t>(120));
            Spec.insert("RigidBodyCount",        static_cast<int64_t>(16));
            break;
        case FormatSuffixCategory::ForceField:
            Spec.insert("FieldCategory",         "DirectionalWindTurbulence");
            Spec.insert("SteadyWindVelocityMps", PackFloat3({ 8.5f, 1.2f, 0.0f }));
            Spec.insert("TurbulenceAmplitude",   1.75);
            Spec.insert("GridDimensions",        PackFloat3({ 4.0f, 4.0f, 2.0f }));
            break;
        case FormatSuffixCategory::FluidGrid:
            Spec.insert("SolverFormulation",     "FLIP-PIC-Incompressible");
            Spec.insert("VoxelPitchMeters",      0.05);
            Spec.insert("KinematicViscosity",    0.000015);
            Spec.insert("DensityKilogramsPerM3", 1.225);
            break;
        case FormatSuffixCategory::RibbonTrail:
            Spec.insert("SegmentWidthMeters",    0.12);
            Spec.insert("MaxTrailPoints",        static_cast<int64_t>(96));
            Spec.insert("UVTilingPerMeter",      0.5);
            break;
        case FormatSuffixCategory::SwarmBehavior:
            Spec.insert("AgentCount",            static_cast<int64_t>(256));
            Spec.insert("SeparationWeight",      1.45);
            Spec.insert("AlignmentWeight",       1.10);
            Spec.insert("CohesionWeight",        0.85);
            break;
        case FormatSuffixCategory::SpawnSource:
            Spec.insert("DistributionShape",     "ConicalNozzle");
            Spec.insert("RatePerSecond",         120.0);
            Spec.insert("ConeHalfAngleDegrees",  18.0);
            break;
        case FormatSuffixCategory::BehaviorSpace:
            Spec.insert("EvaluationCadenceHz",   static_cast<int64_t>(60));
            Spec.insert("RuleSetIdentifier",     "DeterministicBehaviorExample");
            Spec.insert("StrictZeroAllocation",  true);
            break;
        case FormatSuffixCategory::PixelSpace:
            Spec.insert("CanvasWidthPixels",     static_cast<int64_t>(2048));
            Spec.insert("CanvasHeightPixels",    static_cast<int64_t>(2048));
            Spec.insert("PackedRegionCount",     static_cast<int64_t>(64));
            Spec.insert("MipPaddingPixels",      static_cast<int64_t>(4));
            break;
        case FormatSuffixCategory::GeometrySpace:
            Spec.insert("PolygonCount",          static_cast<int64_t>(4096));
            Spec.insert("CreaseAngleDegrees",    38.0);
            Spec.insert("CoordinateConvention",  "RightHandedZUpMeters");
            break;
        case FormatSuffixCategory::MaterialSpace:
            Spec.insert("ShadingStandard",       "OpenPBR-1.1");
            Spec.insert("SurfaceRoughness",      0.24);
            Spec.insert("SurfaceMetalness",      0.85);
            Spec.insert("ClearcoatWeight",       1.0);
            break;
        case FormatSuffixCategory::AudioSpace:
            Spec.insert("SampleRateHertz",       static_cast<int64_t>(48000));
            Spec.insert("ChannelLayout",         "AmbisonicFirstOrder");
            Spec.insert("LoudnessTargetLufs",    -14.0);
            break;
        case FormatSuffixCategory::WorkspaceLayout:
            Spec.insert("EditorOnlyGuard",       "FRONTIER_PROFILE_EDITOR");
            Spec.insert("ActiveWorkspace",       "ViewportAuthoring");
            Spec.insert("PanelCount",            static_cast<int64_t>(5));
            break;
        default:
            Spec.insert("ExampleSeed",           static_cast<int64_t>(static_cast<uint16_t>(Category) * 37u + 11u));
            Spec.insert("Units",                 "SI-Meters-Seconds-Radians");
            break;
    }

    Doc.TomlHeader.insert("FormatExample", std::move(Spec));

    // Attach a verified 64B-aligned Tier 2 binary payload for every binary-bearing format
    if (UsesTier2BinaryPayload)
    {
        const float Seed = static_cast<float>(static_cast<uint16_t>(Category) + 1u);
        const std::array<float, 16u> SamplePayload = {
            Seed * 0.25f, Seed * 0.50f, Seed * 0.75f, 1.0f,
            -1.0f, 0.0f, 1.0f, Seed,
            0.125f, 0.250f, 0.375f, 0.500f,
            0.625f, 0.750f, 0.875f, 1.000f
        };
        BinarySectionBlock Block{};
        Block.SectionIdentifier = "ExampleBinaryPayload";
        Block.SubFormatTag      = std::string(Desc.CanonicalSuffix);
        Block.PayloadBytes      = PackFloatSpan(SamplePayload);
        Doc.BinarySections.push_back(std::move(Block));
    }

    return Doc;
}

bool FormatCodecSuite::GenerateAllFormatExamples(
    const std::filesystem::path&        OutputRootDirectory,
    std::vector<std::filesystem::path>& OutWrittenFilePaths,
    std::string&                        OutDiagnostic)
{
    OutWrittenFilePaths.clear();
    for (const FormatSuffixDescriptor& Desc : EnumerateFormatSuffixDescriptors())
    {
        const std::filesystem::path SubFolder = Desc.FoundationalPillar
                                                    ? (OutputRootDirectory / "TierA_SpacePillars")
                                                    : (OutputRootDirectory / "TierB_SubFormats");
        const std::string StemName = std::string("Example") + std::string(Desc.CanonicalSuffix.substr(1u));
        const std::filesystem::path FilePath = SubFolder / (StemName + std::string(Desc.CanonicalSuffix));

        const HybridContainerDocument ExampleDoc = ConstructExampleDocument(Desc.Category);
        if (!HybridContainerEncoder::EncodeToFile(FilePath, ExampleDoc, OutDiagnostic))
        {
            return false;
        }

        // Immediately round-trip verify every written example file on disk
        HybridContainerDocument VerifiedDoc{};
        if (!HybridContainerDecoder::DecodeFromFile(FilePath, VerifiedDoc, OutDiagnostic))
        {
            return false;
        }
        if (VerifiedDoc.SuffixCategory != Desc.Category)
        {
            OutDiagnostic = "Round-trip category mismatch for example: " + FilePath.string();
            return false;
        }

        OutWrittenFilePaths.push_back(FilePath);
    }

    OutDiagnostic.clear();
    return true;
}

} // namespace Frontier::Serialization
