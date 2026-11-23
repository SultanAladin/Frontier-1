/*============================================================================================================================================
                                                           RENDERSPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .RenderSpace Two-Tier Hybrid encoder & decoder

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RenderSpaceCodec.h"

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER SPACE ENCODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload RenderSpaceEncoder::BuildPayload(
    const RenderSpaceDocumentModel&    Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory     = SpaceSuffixCategory::RenderSpace;
    Payload.SchemaVersion      = 1u;
    Payload.DocumentTitle      = Model.DocumentTitle.empty() ? "DefaultEditorViewport" : Model.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.RenderWorkspace";

    toml::table ConfigTbl{};
    ConfigTbl.insert_or_assign("RenderEngineOrdinal", static_cast<int64_t>(Model.RenderConfiguration.Engine));
    ConfigTbl.insert_or_assign("ShadowAlgorithmOrdinal", static_cast<int64_t>(Model.RenderConfiguration.ShadowAlgorithm));
    ConfigTbl.insert_or_assign("ReflectionSourceOrdinal", static_cast<int64_t>(Model.RenderConfiguration.ReflectionSource));
    ConfigTbl.insert_or_assign("GlobalIlluminationCandidateOrdinal", static_cast<int64_t>(Model.RenderConfiguration.GiCandidate));
    ConfigTbl.insert_or_assign("DistanceFieldPenumbraOrdinal", static_cast<int64_t>(Model.RenderConfiguration.SdfPenumbraMode));
    ConfigTbl.insert_or_assign("DebugChannelOrdinal", static_cast<int64_t>(Model.RenderConfiguration.DebugChannel));
    ConfigTbl.insert_or_assign("ReflectionsEnabled", Model.RenderConfiguration.ReflectionsEnabled);
    ConfigTbl.insert_or_assign("RaytracingEnabled", Model.RenderConfiguration.RaytracingEnabled);
    ConfigTbl.insert_or_assign("AntiAliasingEnabled", Model.AntiAliasingEnabled);
    ConfigTbl.insert_or_assign("QualityTier", static_cast<int64_t>(Model.RenderConfiguration.QualityTier));
    ConfigTbl.insert_or_assign("PathTraceSamples", static_cast<int64_t>(Model.RenderConfiguration.PathTraceSamples));
    ConfigTbl.insert_or_assign("RenderScalePercent", static_cast<double>(Model.RenderScalePercent));
    ConfigTbl.insert_or_assign("CheapMatCapTwoByTwoShadow", Model.CheapMatCapTwoByTwoShadow);
    ConfigTbl.insert_or_assign("StaticCloudRenderScale", static_cast<double>(Model.StaticCloudRenderScale));
    Payload.DomainManifest.insert_or_assign(".RenderConfiguration", std::move(ConfigTbl));

    toml::table CascadeTbl{};
    CascadeTbl.insert_or_assign("FineCascadeHalfSpanMeters", static_cast<double>(Model.FineCascadeHalfSpanMeters));
    CascadeTbl.insert_or_assign("CoarseCascadeHalfSpanMeters", static_cast<double>(Model.CoarseCascadeHalfSpanMeters));
    CascadeTbl.insert_or_assign("CascadeResolutionTexels", static_cast<int64_t>(Model.CascadeResolutionTexels));
    CascadeTbl.insert_or_assign("FineDistanceFieldHalfMeters", static_cast<double>(Model.FineDistanceFieldHalfMeters));
    CascadeTbl.insert_or_assign("CoarseDistanceFieldHalfMeters", static_cast<double>(Model.CoarseDistanceFieldHalfMeters));
    Payload.DomainManifest.insert_or_assign(".ShadowCascades", std::move(CascadeTbl));

    toml::table ToneTbl{};
    ToneTbl.insert_or_assign("ExposureStopsEV", static_cast<double>(Model.ExposureStopsEV));
    ToneTbl.insert_or_assign("ToneTransformOperator", Model.ToneTransformOperator);
    Payload.DomainManifest.insert_or_assign(".ToneTransform", std::move(ToneTbl));

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool RenderSpaceEncoder::EncodeToBytes(
    const RenderSpaceDocumentModel&    Model,
    std::vector<std::byte>&            OutContainerBytes,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(BuildPayload(Model, OptionalRevision), OutContainerBytes, OutDiagnostic);
}

bool RenderSpaceEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const RenderSpaceDocumentModel&    Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    SpaceSuffixCategory   PathCat{};
    std::string           CanonExt{};
    std::string           IgnoreDiag{};
    if (SpaceContainerDecoder::ResolveSuffixCaseInsensitive(TargetPath.extension().string(), PathCat, CanonExt, IgnoreDiag) &&
        (PathCat == SpaceSuffixCategory::RenderSpace || PathCat == SpaceSuffixCategory::Render || PathCat == SpaceSuffixCategory::InstanceSpace))
    {
        Payload.SuffixCategory = PathCat;
    }
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER SPACE DECODER
//------------------------------------------------------------------------------------------------------------------------

bool RenderSpaceDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    RenderSpaceDocumentModel&    OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::RenderSpace &&
        Payload.SuffixCategory != SpaceSuffixCategory::Render &&
        Payload.SuffixCategory != SpaceSuffixCategory::InstanceSpace)
    {
        OutDiagnostic = "Expected .RenderSpace container category.";
        return false;
    }

    OutModel               = RenderSpaceDocumentModel{};
    OutModel.DocumentTitle = Payload.DocumentTitle;

    if (const auto* ConfigTbl = Payload.DomainManifest[".RenderConfiguration"].as_table())
    {
        OutModel.RenderConfiguration.Engine           = static_cast<GeometricRaster::WorldRenderEngine>((*ConfigTbl)["RenderEngineOrdinal"].value_or(int64_t{ 1 }));
        OutModel.RenderConfiguration.ShadowAlgorithm  = static_cast<GeometricRaster::SolarShadowAlgorithm>((*ConfigTbl)["ShadowAlgorithmOrdinal"].value_or(int64_t{ 4 }));
        OutModel.RenderConfiguration.ReflectionSource = static_cast<GeometricRaster::SpecularReflectionSource>((*ConfigTbl)["ReflectionSourceOrdinal"].value_or(int64_t{ 1 }));
        OutModel.RenderConfiguration.GiCandidate      = static_cast<GeometricRaster::RealTimeGiCandidate>((*ConfigTbl)["GlobalIlluminationCandidateOrdinal"].value_or(int64_t{ 1 }));
        OutModel.RenderConfiguration.SdfPenumbraMode  = static_cast<GeometricRaster::DirectLighting::SdfShadowPenumbraMode>((*ConfigTbl)["DistanceFieldPenumbraOrdinal"].value_or(int64_t{ 0 }));
        OutModel.RenderConfiguration.DebugChannel     = static_cast<uint32_t>((*ConfigTbl)["DebugChannelOrdinal"].value_or(int64_t{ 0 }));
        OutModel.RenderConfiguration.ReflectionsEnabled = (*ConfigTbl)["ReflectionsEnabled"].value_or(true);
        OutModel.RenderConfiguration.RaytracingEnabled  = (*ConfigTbl)["RaytracingEnabled"].value_or(true);
        OutModel.AntiAliasingEnabled                    = (*ConfigTbl)["AntiAliasingEnabled"].value_or(true);
        OutModel.RenderConfiguration.QualityTier        = static_cast<uint32_t>((*ConfigTbl)["QualityTier"].value_or(int64_t{ 1 }));
        OutModel.RenderConfiguration.PathTraceSamples   = static_cast<uint32_t>((*ConfigTbl)["PathTraceSamples"].value_or(int64_t{ 4 }));
        OutModel.RenderScalePercent                     = static_cast<float>((*ConfigTbl)["RenderScalePercent"].value_or(100.0));
        OutModel.CheapMatCapTwoByTwoShadow              = (*ConfigTbl)["CheapMatCapTwoByTwoShadow"].value_or(true);
        OutModel.StaticCloudRenderScale                 = static_cast<float>((*ConfigTbl)["StaticCloudRenderScale"].value_or(1.0));
    }

    if (const auto* CascadeTbl = Payload.DomainManifest[".ShadowCascades"].as_table())
    {
        OutModel.FineCascadeHalfSpanMeters     = static_cast<float>((*CascadeTbl)["FineCascadeHalfSpanMeters"].value_or(2.85));
        OutModel.CoarseCascadeHalfSpanMeters   = static_cast<float>((*CascadeTbl)["CoarseCascadeHalfSpanMeters"].value_or(8.50));
        OutModel.CascadeResolutionTexels       = static_cast<uint32_t>((*CascadeTbl)["CascadeResolutionTexels"].value_or(int64_t{ 1024 }));
        OutModel.FineDistanceFieldHalfMeters   = static_cast<float>((*CascadeTbl)["FineDistanceFieldHalfMeters"].value_or(4.20));
        OutModel.CoarseDistanceFieldHalfMeters = static_cast<float>((*CascadeTbl)["CoarseDistanceFieldHalfMeters"].value_or(14.0));
    }

    if (const auto* ToneTbl = Payload.DomainManifest[".ToneTransform"].as_table())
    {
        OutModel.ExposureStopsEV       = static_cast<float>((*ToneTbl)["ExposureStopsEV"].value_or(0.0));
        OutModel.ToneTransformOperator = (*ToneTbl)["ToneTransformOperator"].value_or(std::string{ "ACESFilmicSrgb" });
    }

    if (OptionalOutRevision != nullptr)
    {
        if (!RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic))
        {
            return false;
        }
    }

    OutDiagnostic.clear();
    return true;
}

bool RenderSpaceDecoder::DecodeFromBytes(
    std::span<const std::byte>   ContainerBytes,
    RenderSpaceDocumentModel&    OutModel,
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

bool RenderSpaceDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    RenderSpaceDocumentModel&    OutModel,
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
