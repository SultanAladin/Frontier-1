/*============================================================================================================================================
                                                              MATERIALCODEC.CPP
============================================================================================================================================*/
// 🧩 .MaterialSpace Two-Tier Hybrid container encoder & decoder (TOML manifest + 64B-aligned 128 B CompactMaterialEntry & 32 B BounceSlabEntry)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/MaterialCodec.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

constexpr uint32_t MaterialRegisterSectionMagic = 0x3152544Du; // 'MTR1'
constexpr uint32_t BounceSummarySectionMagic    = 0x31534E42u; // 'BNS1'
constexpr uint32_t MaterialBinarySchemaVersion  = 1u;

template <typename PodType>
void AppendPodValue(std::vector<std::byte>& OutBytes, const PodType& SourceEntry)
{
    const std::size_t Offset = OutBytes.size();
    OutBytes.resize(Offset + sizeof(PodType));
    std::memcpy(OutBytes.data() + Offset, &SourceEntry, sizeof(PodType));
}

template <typename PodType>
bool ReadPodValue(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

void SerializePhysicalMaterialManifest(const MaterialSpace::SurfaceMaterial& Mat, toml::table& OutTable)
{
    OutTable.insert_or_assign("MaterialName", Mat.MaterialName);
    OutTable.insert_or_assign("BaseColor", toml::array{ Mat.Diffuse.BaseColor.R, Mat.Diffuse.BaseColor.G, Mat.Diffuse.BaseColor.B });
    OutTable.insert_or_assign("BaseWeight", static_cast<double>(Mat.Diffuse.BaseWeight));
    OutTable.insert_or_assign("DiffuseRoughness", static_cast<double>(Mat.Diffuse.DiffuseRoughness));

    OutTable.insert_or_assign("Metalness", static_cast<double>(Mat.Specular.Metalness));
    OutTable.insert_or_assign("SpecularWeight", static_cast<double>(Mat.Specular.SpecularWeight));
    OutTable.insert_or_assign("SpecularRoughness", static_cast<double>(Mat.Specular.Roughness));
    OutTable.insert_or_assign("Anisotropy", static_cast<double>(Mat.Specular.Anisotropy));
    OutTable.insert_or_assign("RotationRadians", static_cast<double>(Mat.Specular.RotationRadians));
    OutTable.insert_or_assign("DielectricIOR", static_cast<double>(Mat.Specular.DielectricIOR));
    OutTable.insert_or_assign("EdgeTintF82", toml::array{ Mat.Specular.EdgeTintF82.R, Mat.Specular.EdgeTintF82.G, Mat.Specular.EdgeTintF82.B });

    OutTable.insert_or_assign("CoatWeight", static_cast<double>(Mat.Clearcoat.CoatWeight));
    OutTable.insert_or_assign("CoatRoughness", static_cast<double>(Mat.Clearcoat.CoatRoughness));
    OutTable.insert_or_assign("CoatAnisotropy", static_cast<double>(Mat.Clearcoat.CoatAnisotropy));
    OutTable.insert_or_assign("CoatIOR", static_cast<double>(Mat.Clearcoat.CoatIOR));
    OutTable.insert_or_assign("CoatAbsorptionTint", toml::array{
        Mat.Clearcoat.CoatAbsorptionTint.R, Mat.Clearcoat.CoatAbsorptionTint.G, Mat.Clearcoat.CoatAbsorptionTint.B });
    OutTable.insert_or_assign("CoatThicknessMillimeter", static_cast<double>(Mat.Clearcoat.CoatThicknessMillimeter));

    OutTable.insert_or_assign("ThinFilmWeight", static_cast<double>(Mat.ThinFilm.ThinFilmWeight));
    OutTable.insert_or_assign("ThinFilmThicknessNanometers", static_cast<double>(Mat.ThinFilm.ThinFilmThicknessNanometers));
    OutTable.insert_or_assign("ThinFilmIOR", static_cast<double>(Mat.ThinFilm.ThinFilmIOR));

    OutTable.insert_or_assign("FlakeCoverage", static_cast<double>(Mat.GlintFlakes.FlakeCoverage));
    OutTable.insert_or_assign("FlakeIntrinsicRoughness", static_cast<double>(Mat.GlintFlakes.FlakeIntrinsicRoughness));
    OutTable.insert_or_assign("FlakeOrientationSpread", static_cast<double>(Mat.GlintFlakes.FlakeOrientationSpread));
    OutTable.insert_or_assign("FlakeLogDensityPerSqM", static_cast<double>(Mat.GlintFlakes.FlakeLogDensityPerSqM));
    OutTable.insert_or_assign("FlakePrimaryTint", toml::array{
        Mat.GlintFlakes.FlakePrimaryTint.R, Mat.GlintFlakes.FlakePrimaryTint.G, Mat.GlintFlakes.FlakePrimaryTint.B });
    OutTable.insert_or_assign("FlakeHorizonTint", toml::array{
        Mat.GlintFlakes.FlakeHorizonTint.R, Mat.GlintFlakes.FlakeHorizonTint.G, Mat.GlintFlakes.FlakeHorizonTint.B });
    OutTable.insert_or_assign("FlakeBurialDepth", static_cast<double>(Mat.GlintFlakes.FlakeBurialDepth));

    OutTable.insert_or_assign("TransmissionWeight", static_cast<double>(Mat.Transmission.TransmissionWeight));
    OutTable.insert_or_assign("AbbeDispersionNumber", static_cast<double>(Mat.Transmission.AbbeDispersionNumber));
    OutTable.insert_or_assign("VolumeAnisotropyG", static_cast<double>(Mat.Transmission.VolumeAnisotropyG));
    OutTable.insert_or_assign("ThinWalledSlab", Mat.Transmission.ThinWalledSlab);
    OutTable.insert_or_assign("AttenuationColor", toml::array{
        Mat.Transmission.AttenuationColor.R, Mat.Transmission.AttenuationColor.G, Mat.Transmission.AttenuationColor.B });
    OutTable.insert_or_assign("AttenuationDistanceMeter", static_cast<double>(Mat.Transmission.AttenuationDistanceMeter));

    OutTable.insert_or_assign("SubsurfaceWeight", static_cast<double>(Mat.Subsurface.SubsurfaceWeight));
    OutTable.insert_or_assign("SubsurfaceColor", toml::array{
        Mat.Subsurface.SubsurfaceColor.R, Mat.Subsurface.SubsurfaceColor.G, Mat.Subsurface.SubsurfaceColor.B });
    OutTable.insert_or_assign("MeanFreePathMillimeters", toml::array{
        Mat.Subsurface.MeanFreePathMillimeters.X, Mat.Subsurface.MeanFreePathMillimeters.Y, Mat.Subsurface.MeanFreePathMillimeters.Z });
    OutTable.insert_or_assign("PhaseAnisotropyG", static_cast<double>(Mat.Subsurface.PhaseAnisotropyG));

    OutTable.insert_or_assign("SheenWeight", static_cast<double>(Mat.Sheen.SheenWeight));
    OutTable.insert_or_assign("SheenRoughness", static_cast<double>(Mat.Sheen.SheenRoughness));
    OutTable.insert_or_assign("SheenColor", toml::array{ Mat.Sheen.SheenColor.R, Mat.Sheen.SheenColor.G, Mat.Sheen.SheenColor.B });

    OutTable.insert_or_assign("FiberWeight", static_cast<double>(Mat.Fiber.FiberWeight));
    OutTable.insert_or_assign("LongitudinalRoughness", static_cast<double>(Mat.Fiber.LongitudinalRoughness));
    OutTable.insert_or_assign("AzimuthalRoughness", static_cast<double>(Mat.Fiber.AzimuthalRoughness));
    OutTable.insert_or_assign("CuticleTiltRadians", static_cast<double>(Mat.Fiber.CuticleTiltRadians));
    OutTable.insert_or_assign("MelaninConcentration", static_cast<double>(Mat.Fiber.MelaninConcentration));
    OutTable.insert_or_assign("PheomelaninRednessFraction", static_cast<double>(Mat.Fiber.PheomelaninRednessFraction));

    OutTable.insert_or_assign("PorosityFraction", static_cast<double>(Mat.PorosityLight.PorosityFraction));
    OutTable.insert_or_assign("SurfaceWetnessFraction", static_cast<double>(Mat.PorosityLight.SurfaceWetnessFraction));
    OutTable.insert_or_assign("EmissionLuminanceNits", static_cast<double>(Mat.PorosityLight.EmissionLuminanceNits));
    OutTable.insert_or_assign("BlackbodyTemperatureKelvin", static_cast<double>(Mat.PorosityLight.BlackbodyTemperatureKelvin));
    OutTable.insert_or_assign("EmissionTint", toml::array{
        Mat.PorosityLight.EmissionTint.R, Mat.PorosityLight.EmissionTint.G, Mat.PorosityLight.EmissionTint.B });
}

void ReadLinearRgbArray(const toml::table& Tbl, std::string_view Key, Mathematics::LinearRGB& InOutCol)
{
    if (const toml::array* Arr = Tbl[Key].as_array(); Arr != nullptr && Arr->size() >= 3u)
    {
        InOutCol.R = static_cast<float>((*Arr)[0].value_or(static_cast<double>(InOutCol.R)));
        InOutCol.G = static_cast<float>((*Arr)[1].value_or(static_cast<double>(InOutCol.G)));
        InOutCol.B = static_cast<float>((*Arr)[2].value_or(static_cast<double>(InOutCol.B)));
    }
}

void DeserializePhysicalMaterialManifest(const toml::table& Tbl, MaterialSpace::SurfaceMaterial& OutMat)
{
    OutMat.MaterialName = Tbl["MaterialName"].value_or(OutMat.MaterialName);
    ReadLinearRgbArray(Tbl, "BaseColor", OutMat.Diffuse.BaseColor);
    OutMat.Diffuse.BaseWeight       = static_cast<float>(Tbl["BaseWeight"].value_or(1.0));
    OutMat.Diffuse.DiffuseRoughness = static_cast<float>(Tbl["DiffuseRoughness"].value_or(0.25));

    OutMat.Specular.Metalness       = static_cast<float>(Tbl["Metalness"].value_or(0.0));
    OutMat.Specular.SpecularWeight  = static_cast<float>(Tbl["SpecularWeight"].value_or(1.0));
    OutMat.Specular.Roughness       = static_cast<float>(Tbl["SpecularRoughness"].value_or(0.30));
    OutMat.Specular.Anisotropy      = static_cast<float>(Tbl["Anisotropy"].value_or(0.0));
    OutMat.Specular.RotationRadians = static_cast<float>(Tbl["RotationRadians"].value_or(0.0));
    OutMat.Specular.DielectricIOR   = static_cast<float>(Tbl["DielectricIOR"].value_or(1.50));
    ReadLinearRgbArray(Tbl, "EdgeTintF82", OutMat.Specular.EdgeTintF82);

    OutMat.Clearcoat.CoatWeight              = static_cast<float>(Tbl["CoatWeight"].value_or(0.0));
    OutMat.Clearcoat.CoatRoughness           = static_cast<float>(Tbl["CoatRoughness"].value_or(0.06));
    OutMat.Clearcoat.CoatAnisotropy          = static_cast<float>(Tbl["CoatAnisotropy"].value_or(0.0));
    OutMat.Clearcoat.CoatIOR                 = static_cast<float>(Tbl["CoatIOR"].value_or(1.52));
    ReadLinearRgbArray(Tbl, "CoatAbsorptionTint", OutMat.Clearcoat.CoatAbsorptionTint);
    OutMat.Clearcoat.CoatThicknessMillimeter = static_cast<float>(Tbl["CoatThicknessMillimeter"].value_or(0.05));

    OutMat.ThinFilm.ThinFilmWeight              = static_cast<float>(Tbl["ThinFilmWeight"].value_or(0.0));
    OutMat.ThinFilm.ThinFilmThicknessNanometers = static_cast<float>(Tbl["ThinFilmThicknessNanometers"].value_or(420.0));
    OutMat.ThinFilm.ThinFilmIOR                 = static_cast<float>(Tbl["ThinFilmIOR"].value_or(1.90));

    OutMat.GlintFlakes.FlakeCoverage           = static_cast<float>(Tbl["FlakeCoverage"].value_or(0.0));
    OutMat.GlintFlakes.FlakeIntrinsicRoughness = static_cast<float>(Tbl["FlakeIntrinsicRoughness"].value_or(0.045));
    OutMat.GlintFlakes.FlakeOrientationSpread  = static_cast<float>(Tbl["FlakeOrientationSpread"].value_or(0.16));
    OutMat.GlintFlakes.FlakeLogDensityPerSqM   = static_cast<float>(Tbl["FlakeLogDensityPerSqM"].value_or(7.0));
    ReadLinearRgbArray(Tbl, "FlakePrimaryTint", OutMat.GlintFlakes.FlakePrimaryTint);
    ReadLinearRgbArray(Tbl, "FlakeHorizonTint", OutMat.GlintFlakes.FlakeHorizonTint);
    OutMat.GlintFlakes.FlakeBurialDepth        = static_cast<float>(Tbl["FlakeBurialDepth"].value_or(0.35));

    OutMat.Transmission.TransmissionWeight       = static_cast<float>(Tbl["TransmissionWeight"].value_or(0.0));
    OutMat.Transmission.AbbeDispersionNumber     = static_cast<float>(Tbl["AbbeDispersionNumber"].value_or(55.0));
    OutMat.Transmission.VolumeAnisotropyG        = static_cast<float>(Tbl["VolumeAnisotropyG"].value_or(0.0));
    OutMat.Transmission.ThinWalledSlab           = Tbl["ThinWalledSlab"].value_or(false);
    ReadLinearRgbArray(Tbl, "AttenuationColor", OutMat.Transmission.AttenuationColor);
    OutMat.Transmission.AttenuationDistanceMeter = static_cast<float>(Tbl["AttenuationDistanceMeter"].value_or(0.25));

    OutMat.Subsurface.SubsurfaceWeight = static_cast<float>(Tbl["SubsurfaceWeight"].value_or(0.0));
    ReadLinearRgbArray(Tbl, "SubsurfaceColor", OutMat.Subsurface.SubsurfaceColor);
    if (const toml::array* Arr = Tbl["MeanFreePathMillimeters"].as_array(); Arr != nullptr && Arr->size() >= 3u)
    {
        OutMat.Subsurface.MeanFreePathMillimeters.X = static_cast<float>((*Arr)[0].value_or(3.6));
        OutMat.Subsurface.MeanFreePathMillimeters.Y = static_cast<float>((*Arr)[1].value_or(1.4));
        OutMat.Subsurface.MeanFreePathMillimeters.Z = static_cast<float>((*Arr)[2].value_or(0.7));
    }
    OutMat.Subsurface.PhaseAnisotropyG = static_cast<float>(Tbl["PhaseAnisotropyG"].value_or(0.0));

    OutMat.Sheen.SheenWeight    = static_cast<float>(Tbl["SheenWeight"].value_or(0.0));
    OutMat.Sheen.SheenRoughness = static_cast<float>(Tbl["SheenRoughness"].value_or(0.45));
    ReadLinearRgbArray(Tbl, "SheenColor", OutMat.Sheen.SheenColor);

    OutMat.Fiber.FiberWeight                = static_cast<float>(Tbl["FiberWeight"].value_or(0.0));
    OutMat.Fiber.LongitudinalRoughness      = static_cast<float>(Tbl["LongitudinalRoughness"].value_or(0.22));
    OutMat.Fiber.AzimuthalRoughness         = static_cast<float>(Tbl["AzimuthalRoughness"].value_or(0.28));
    OutMat.Fiber.CuticleTiltRadians         = static_cast<float>(Tbl["CuticleTiltRadians"].value_or(0.035));
    OutMat.Fiber.MelaninConcentration       = static_cast<float>(Tbl["MelaninConcentration"].value_or(0.45));
    OutMat.Fiber.PheomelaninRednessFraction = static_cast<float>(Tbl["PheomelaninRednessFraction"].value_or(0.20));

    OutMat.PorosityLight.PorosityFraction           = static_cast<float>(Tbl["PorosityFraction"].value_or(0.0));
    OutMat.PorosityLight.SurfaceWetnessFraction     = static_cast<float>(Tbl["SurfaceWetnessFraction"].value_or(0.0));
    OutMat.PorosityLight.EmissionLuminanceNits      = static_cast<float>(Tbl["EmissionLuminanceNits"].value_or(0.0));
    OutMat.PorosityLight.BlackbodyTemperatureKelvin = static_cast<float>(Tbl["BlackbodyTemperatureKelvin"].value_or(0.0));
    ReadLinearRgbArray(Tbl, "EmissionTint", OutMat.PorosityLight.EmissionTint);

    MaterialSpace::SanitizeSurfaceMaterial(OutMat);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                MATERIAL CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload MaterialEncoder::BuildPayload(
    const MaterialSpaceDocumentModel&  Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory  = SpaceSuffixCategory::MaterialSpace;
    Payload.DocumentTitle      = Model.MaterialTitle;
    Payload.AuthoringWorkspace = "Frontier.MaterialWorkspace";

    toml::table ShadingMeta;
    ShadingMeta.insert_or_assign("MaterialTitle", Model.MaterialTitle);
    ShadingMeta.insert_or_assign("ShaderSourceTag", Model.ShaderSourceTag);
    ShadingMeta.insert_or_assign("IndexOfRefraction", Model.IndexOfRefraction);
    ShadingMeta.insert_or_assign("PhysicalMaterialIncluded", Model.PhysicalMaterialIncluded);
    if (Model.PhysicalMaterialIncluded)
    {
        toml::table OpenPbrTable;
        SerializePhysicalMaterialManifest(Model.AuthoredMaterial, OpenPbrTable);
        ShadingMeta.insert_or_assign("OpenPbrParameters", std::move(OpenPbrTable));
    }
    Payload.DomainManifest.insert_or_assign(".ShadingModel", std::move(ShadingMeta));

    if (!Model.ParameterSlots.empty())
    {
        toml::array SlotArray;
        for (const MaterialParameterSlotEntry& Slot : Model.ParameterSlots)
        {
            toml::table Entry;
            Entry.insert_or_assign("ParameterName", Slot.ParameterName);
            Entry.insert_or_assign("ScalarMagnitude", Slot.ScalarMagnitude);
            Entry.insert_or_assign("TextureSpaceAsset", Slot.TextureSpaceAsset);
            SlotArray.push_back(std::move(Entry));
        }
        Payload.DomainManifest.insert_or_assign(".ParameterSlot", std::move(SlotArray));
    }

    // Attach 64B-aligned 128 B CompactMaterialEntry & 32 B BounceSlabEntry Tier 2 binary sections
    std::vector<MaterialSpace::CompactMaterialEntry> RegistersToWrite = Model.CompiledRegisters;
    std::vector<MaterialSpace::BounceSlabEntry>      SummariesToWrite = Model.CompiledBounceSummaries;
    if (Model.PhysicalMaterialIncluded && RegistersToWrite.empty())
    {
        RegistersToWrite.push_back(MaterialSpace::CompileCompactMaterialEntry(Model.AuthoredMaterial));
    }
    if (Model.PhysicalMaterialIncluded && SummariesToWrite.empty())
    {
        SummariesToWrite.push_back(MaterialSpace::CompileBounceSlabEntry(Model.AuthoredMaterial));
    }

    if (!RegistersToWrite.empty())
    {
        BinarySectionBlock RegSec {};
        RegSec.SectionIdentifier = "CompactMaterialRegister";
        RegSec.SubFormatTag      = ".MaterialRegister";
        const auto Count = static_cast<uint32_t>(RegistersToWrite.size());
        RegSec.PayloadBytes.reserve(12u + RegistersToWrite.size() * sizeof(MaterialSpace::CompactMaterialEntry));
        AppendPodValue(RegSec.PayloadBytes, MaterialRegisterSectionMagic);
        AppendPodValue(RegSec.PayloadBytes, MaterialBinarySchemaVersion);
        AppendPodValue(RegSec.PayloadBytes, Count);
        for (const auto& Entry : RegistersToWrite)
        {
            AppendPodValue(RegSec.PayloadBytes, Entry);
        }
        Payload.BinarySections.push_back(std::move(RegSec));
    }

    if (!SummariesToWrite.empty())
    {
        BinarySectionBlock SumSec {};
        SumSec.SectionIdentifier = "BounceLayerSummary";
        SumSec.SubFormatTag      = ".BounceSummary";
        const auto Count = static_cast<uint32_t>(SummariesToWrite.size());
        SumSec.PayloadBytes.reserve(12u + SummariesToWrite.size() * sizeof(MaterialSpace::BounceSlabEntry));
        AppendPodValue(SumSec.PayloadBytes, BounceSummarySectionMagic);
        AppendPodValue(SumSec.PayloadBytes, MaterialBinarySchemaVersion);
        AppendPodValue(SumSec.PayloadBytes, Count);
        for (const auto& Entry : SummariesToWrite)
        {
            AppendPodValue(SumSec.PayloadBytes, Entry);
        }
        Payload.BinarySections.push_back(std::move(SumSec));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool MaterialEncoder::EncodeToBytes(
    const MaterialSpaceDocumentModel&  Model,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Model, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool MaterialEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const MaterialSpaceDocumentModel&  Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Model, OptionalRevision), OutDiagnostic);
}

bool MaterialDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    MaterialSpaceDocumentModel&  OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::MaterialSpace)
    {
        OutDiagnostic = "Expected .MaterialSpace container category.";
        return false;
    }

    OutModel               = MaterialSpaceDocumentModel{};
    OutModel.MaterialTitle = Payload.DocumentTitle;
    if (const toml::table* ShadingMeta = Payload.DomainManifest[".ShadingModel"].as_table())
    {
        OutModel.MaterialTitle            = (*ShadingMeta)["MaterialTitle"].value_or(Payload.DocumentTitle);
        OutModel.ShaderSourceTag          = (*ShadingMeta)["ShaderSourceTag"].value_or(std::string{ "OpenPbrSurfaceScattering.slang" });
        OutModel.IndexOfRefraction        = (*ShadingMeta)["IndexOfRefraction"].value_or(1.5);
        OutModel.PhysicalMaterialIncluded = (*ShadingMeta)["PhysicalMaterialIncluded"].value_or(false);
        if (const toml::table* OpenPbrTable = (*ShadingMeta)["OpenPbrParameters"].as_table())
        {
            DeserializePhysicalMaterialManifest(*OpenPbrTable, OutModel.AuthoredMaterial);
            OutModel.PhysicalMaterialIncluded = true;
        }
    }

    if (const toml::array* SlotArray = Payload.DomainManifest[".ParameterSlot"].as_array())
    {
        for (const auto& SlotRef : *SlotArray)
        {
            if (const toml::table* Entry = SlotRef.as_table())
            {
                MaterialParameterSlotEntry Slot{};
                Slot.ParameterName     = (*Entry)["ParameterName"].value_or(std::string{});
                Slot.ScalarMagnitude   = (*Entry)["ScalarMagnitude"].value_or(0.0);
                Slot.TextureSpaceAsset = (*Entry)["TextureSpaceAsset"].value_or(std::string{});
                OutModel.ParameterSlots.push_back(std::move(Slot));
            }
        }
    }

    for (const BinarySectionBlock& Section : Payload.BinarySections)
    {
        if (Section.SubFormatTag == ".MaterialRegister")
        {
            std::span<const std::byte> Cursor(Section.PayloadBytes.data(), Section.PayloadBytes.size());
            uint32_t Magic = 0u, Ver = 0u, Count = 0u;
            if (!ReadPodValue(Cursor, Magic) || !ReadPodValue(Cursor, Ver) || !ReadPodValue(Cursor, Count) ||
                Magic != MaterialRegisterSectionMagic || Ver != MaterialBinarySchemaVersion)
            {
                OutDiagnostic = "Corrupt .MaterialRegister binary section in .MaterialSpace.";
                return false;
            }
            OutModel.CompiledRegisters.resize(Count);
            for (uint32_t Idx = 0u; Idx < Count; ++Idx)
            {
                if (!ReadPodValue(Cursor, OutModel.CompiledRegisters[Idx]))
                {
                    OutDiagnostic = "Truncated .MaterialRegister payload in .MaterialSpace.";
                    return false;
                }
            }
        }
        else if (Section.SubFormatTag == ".BounceSummary")
        {
            std::span<const std::byte> Cursor(Section.PayloadBytes.data(), Section.PayloadBytes.size());
            uint32_t Magic = 0u, Ver = 0u, Count = 0u;
            if (!ReadPodValue(Cursor, Magic) || !ReadPodValue(Cursor, Ver) || !ReadPodValue(Cursor, Count) ||
                Magic != BounceSummarySectionMagic || Ver != MaterialBinarySchemaVersion)
            {
                OutDiagnostic = "Corrupt .BounceSummary binary section in .MaterialSpace.";
                return false;
            }
            OutModel.CompiledBounceSummaries.resize(Count);
            for (uint32_t Idx = 0u; Idx < Count; ++Idx)
            {
                if (!ReadPodValue(Cursor, OutModel.CompiledBounceSummaries[Idx]))
                {
                    OutDiagnostic = "Truncated .BounceSummary payload in .MaterialSpace.";
                    return false;
                }
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool MaterialDecoder::DecodeFromBytes(
    std::span<const std::byte>   ContainerBytes,
    MaterialSpaceDocumentModel&  OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromBytes(ContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

bool MaterialDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    MaterialSpaceDocumentModel&  OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
