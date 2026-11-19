/*============================================================================================================================================
                                                             VECTORFIELDCODEC.CPP
============================================================================================================================================*/
// 🧩 Option A Two-Tier Hybrid .VectorFieldSpace encoder & decoder + inline .SimulationSpace ForceSources serialization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <cstring>

namespace Frontier::Codecs
{

namespace
{

using namespace Frontier::SimulationSpace::ForceFields;

[[nodiscard]] toml::array PackVec3Array(const SpatialVector3& V)
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(V.X));
    Arr.push_back(static_cast<double>(V.Y));
    Arr.push_back(static_cast<double>(V.Z));
    return Arr;
}

[[nodiscard]] SpatialVector3 UnpackVec3ArrayPtr(const toml::array* Arr, const SpatialVector3& Fallback) noexcept
{
    if (Arr != nullptr && Arr->size() >= 3u)
    {
        return {
            static_cast<float>((*Arr)[0].value_or(static_cast<double>(Fallback.X))),
            static_cast<float>((*Arr)[1].value_or(static_cast<double>(Fallback.Y))),
            static_cast<float>((*Arr)[2].value_or(static_cast<double>(Fallback.Z))),
            0.0f,
        };
    }
    return Fallback;
}

[[nodiscard]] SpatialVector3 UnpackVec3Array(const toml::node_view<const toml::node>& View, const SpatialVector3& Fallback) noexcept
{
    return UnpackVec3ArrayPtr(View.as_array(), Fallback);
}

template <typename PodType>
void AppendPodBytes(std::vector<std::byte>& Destination, const PodType& Pod)
{
    const auto* Ptr = reinterpret_cast<const std::byte*>(&Pod);
    Destination.insert(Destination.end(), Ptr, Ptr + sizeof(PodType));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           SHARED INLINE FORCESOURCE SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

void SerializeInlineForceSourcesToManifest(
    std::span<const ForceSourceRecordEntry> Sources,
    std::span<const std::string>            LinkedVectorFieldAssets,
    toml::table&                            OutManifest)
{
    if (!LinkedVectorFieldAssets.empty())
    {
        toml::array LinkArr;
        for (const std::string& PathStr : LinkedVectorFieldAssets)
        {
            LinkArr.push_back(PathStr);
        }
        OutManifest.insert_or_assign("LinkedVectorFieldAssets", std::move(LinkArr));
    }

    if (Sources.empty())
    {
        return;
    }

    toml::array SourceArray;
    toml::array MotionArray;

    for (const ForceSourceRecordEntry& Entry : Sources)
    {
        toml::table Src;
        Src.insert_or_assign("UniqueToken",           static_cast<int64_t>(Entry.UniqueToken));
        Src.insert_or_assign("OutlinerTokenId",       static_cast<int64_t>(Entry.OutlinerTokenId));
        Src.insert_or_assign("Label",                 Entry.Label);
        Src.insert_or_assign("ForceModel",            static_cast<int64_t>(Entry.Model));
        Src.insert_or_assign("StreamProfile",         static_cast<int64_t>(Entry.StreamProfile));
        Src.insert_or_assign("Attenuation",           static_cast<int64_t>(Entry.Attenuation));
        Src.insert_or_assign("DurationMode",          static_cast<int64_t>(Entry.DurationMode));
        Src.insert_or_assign("ReachEnvelope",         static_cast<int64_t>(Entry.Envelope));
        Src.insert_or_assign("CentreMeters",          PackVec3Array(Entry.CentreMeters));
        Src.insert_or_assign("EulerDegrees",          PackVec3Array(Entry.EulerDegrees));
        Src.insert_or_assign("HalfExtentsMeters",     PackVec3Array(Entry.HalfExtentsMeters));
        Src.insert_or_assign("RadiusMeters",          static_cast<double>(Entry.RadiusMeters));
        Src.insert_or_assign("Strength",              static_cast<double>(Entry.Strength));
        Src.insert_or_assign("FalloffExponent",       static_cast<double>(Entry.FalloffExponent));
        Src.insert_or_assign("CoreRadiusMeters",      static_cast<double>(Entry.CoreRadiusMeters));
        Src.insert_or_assign("FrequencyHertz",        static_cast<double>(Entry.FrequencyHertz));
        Src.insert_or_assign("TurbulenceScaleMeters", static_cast<double>(Entry.TurbulenceScaleMeters));
        Src.insert_or_assign("OctaveCount",           static_cast<int64_t>(Entry.OctaveCount));
        Src.insert_or_assign("SwirlRatio",            static_cast<double>(Entry.SwirlRatio));
        Src.insert_or_assign("BuoyancyDensityRatio",  static_cast<double>(Entry.BuoyancyDensityRatio));
        Src.insert_or_assign("DragCoefficient",       static_cast<double>(Entry.DragCoefficient));
        Src.insert_or_assign("Seed",                  static_cast<int64_t>(Entry.Seed));
        Src.insert_or_assign("Enabled",               Entry.Enabled);
        Src.insert_or_assign("Visible",               Entry.Visible);
        Src.insert_or_assign("Solo",                  Entry.Solo);
        Src.insert_or_assign("Locked",                Entry.Locked);
        Src.insert_or_assign("DynamicMotion",         Entry.DynamicMotion);
        Src.insert_or_assign("FormulaEnabled",        Entry.FormulaEnabled);
        Src.insert_or_assign("FormulaSourceText",     Entry.FormulaSourceText);
        SourceArray.push_back(std::move(Src));

        if (Entry.Motion.Topology != MotionPathGeometry::Static || Entry.Motion.WaypointCount > 0u)
        {
            toml::table Mot;
            Mot.insert_or_assign("ForceToken",                   static_cast<int64_t>(Entry.UniqueToken));
            Mot.insert_or_assign("Topology",                     static_cast<int64_t>(Entry.Motion.Topology));
            Mot.insert_or_assign("CycleBehavior",                static_cast<int64_t>(Entry.Motion.CycleBehavior));
            Mot.insert_or_assign("SpeedMetresPerSecond",         static_cast<double>(Entry.Motion.SpeedMetresPerSecond));
            Mot.insert_or_assign("PhaseOffset",                  static_cast<double>(Entry.Motion.PhaseOffset));
            Mot.insert_or_assign("OrbitRadiusMeters",            static_cast<double>(Entry.Motion.OrbitRadiusMeters));
            Mot.insert_or_assign("OrbitVerticalAmplitudeMeters", static_cast<double>(Entry.Motion.OrbitVerticalAmplitudeMeters));
            Mot.insert_or_assign("OrbitTurns",                   static_cast<double>(Entry.Motion.OrbitTurns));
            Mot.insert_or_assign("OrientAlongPathTangent",       Entry.Motion.OrientAlongPathTangent);
            toml::array WpArray;
            for (uint32_t WpIdx = 0u; WpIdx < std::min<uint32_t>(Entry.Motion.WaypointCount, MaxForceMotionWaypoints); ++WpIdx)
            {
                WpArray.push_back(PackVec3Array(Entry.Motion.Waypoints[WpIdx]));
            }
            Mot.insert_or_assign("Waypoints", std::move(WpArray));
            MotionArray.push_back(std::move(Mot));
        }
    }

    OutManifest.insert_or_assign(".ForceSource", std::move(SourceArray));
    if (!MotionArray.empty())
    {
        OutManifest.insert_or_assign(".MotionPath", std::move(MotionArray));
    }
}

void DeserializeInlineForceSourcesFromManifest(
    const toml::table&                  SourceManifest,
    std::vector<ForceSourceRecordEntry>& OutSources,
    std::vector<std::string>&            OutLinkedAssets)
{
    OutSources.clear();
    OutLinkedAssets.clear();

    if (const toml::array* LinkArr = SourceManifest["LinkedVectorFieldAssets"].as_array())
    {
        for (const auto& PathEntry : *LinkArr)
        {
            if (auto S = PathEntry.value<std::string>())
            {
                OutLinkedAssets.push_back(*S);
            }
        }
    }

    if (const toml::array* SrcArr = SourceManifest[".ForceSource"].as_array())
    {
        for (const auto& SrcNode : *SrcArr)
        {
            if (const toml::table* T = SrcNode.as_table())
            {
                ForceSourceRecordEntry Entry{};
                Entry.UniqueToken           = static_cast<uint32_t>((*T)["UniqueToken"].value_or<int64_t>(0));
                Entry.OutlinerTokenId       = static_cast<uint32_t>((*T)["OutlinerTokenId"].value_or<int64_t>(0));
                Entry.Label                 = (*T)["Label"].value_or(std::string{ "Force Field" });
                Entry.Model                 = static_cast<ForceModel>((*T)["ForceModel"].value_or<int64_t>(0));
                Entry.Domain                = QueryDomainForModel(Entry.Model);
                Entry.StreamProfile         = static_cast<StreamTransportProfile>((*T)["StreamProfile"].value_or<int64_t>(0));
                Entry.Attenuation           = static_cast<ForceAttenuationMode>((*T)["Attenuation"].value_or<int64_t>(0));
                Entry.DurationMode          = static_cast<ForceDurationMode>((*T)["DurationMode"].value_or<int64_t>(0));
                Entry.Envelope              = static_cast<ReachEnvelopeCategory>((*T)["ReachEnvelope"].value_or<int64_t>(0));
                Entry.CentreMeters          = UnpackVec3Array((*T)["CentreMeters"], Entry.CentreMeters);
                Entry.EvaluatedCentreMeters = Entry.CentreMeters;
                Entry.EulerDegrees          = UnpackVec3Array((*T)["EulerDegrees"], Entry.EulerDegrees);
                Entry.HalfExtentsMeters     = UnpackVec3Array((*T)["HalfExtentsMeters"], Entry.HalfExtentsMeters);
                Entry.RadiusMeters          = static_cast<float>((*T)["RadiusMeters"].value_or(30.0));
                Entry.Strength              = static_cast<float>((*T)["Strength"].value_or(18.0));
                Entry.FalloffExponent       = static_cast<float>((*T)["FalloffExponent"].value_or(1.5));
                Entry.CoreRadiusMeters      = static_cast<float>((*T)["CoreRadiusMeters"].value_or(4.0));
                Entry.FrequencyHertz        = static_cast<float>((*T)["FrequencyHertz"].value_or(0.45));
                Entry.TurbulenceScaleMeters = static_cast<float>((*T)["TurbulenceScaleMeters"].value_or(14.0));
                Entry.OctaveCount           = static_cast<uint32_t>((*T)["OctaveCount"].value_or<int64_t>(3));
                Entry.SwirlRatio            = static_cast<float>((*T)["SwirlRatio"].value_or(0.55));
                Entry.BuoyancyDensityRatio  = static_cast<float>((*T)["BuoyancyDensityRatio"].value_or(1.6));
                Entry.DragCoefficient       = static_cast<float>((*T)["DragCoefficient"].value_or(1.2));
                Entry.Seed                  = static_cast<uint32_t>((*T)["Seed"].value_or<int64_t>(1337));
                Entry.Enabled               = (*T)["Enabled"].value_or(true);
                Entry.Visible               = (*T)["Visible"].value_or(true);
                Entry.Solo                  = (*T)["Solo"].value_or(false);
                Entry.Locked                = (*T)["Locked"].value_or(false);
                Entry.DynamicMotion         = (*T)["DynamicMotion"].value_or(false);
                Entry.FormulaEnabled        = (*T)["FormulaEnabled"].value_or(false);
                Entry.FormulaSourceText     = (*T)["FormulaSourceText"].value_or(std::string{});
                OutSources.push_back(std::move(Entry));
            }
        }
    }

    if (const toml::array* MotArr = SourceManifest[".MotionPath"].as_array())
    {
        for (const auto& MotNode : *MotArr)
        {
            if (const toml::table* T = MotNode.as_table())
            {
                const uint32_t Token = static_cast<uint32_t>((*T)["ForceToken"].value_or<int64_t>(0));
                for (ForceSourceRecordEntry& Entry : OutSources)
                {
                    if (Entry.UniqueToken == Token)
                    {
                        Entry.Motion.Topology                     = static_cast<MotionPathGeometry>((*T)["Topology"].value_or<int64_t>(0));
                        Entry.Motion.CycleBehavior                = static_cast<MotionCycleBehavior>((*T)["CycleBehavior"].value_or<int64_t>(0));
                        Entry.Motion.SpeedMetresPerSecond         = static_cast<float>((*T)["SpeedMetresPerSecond"].value_or(8.0));
                        Entry.Motion.PhaseOffset                  = static_cast<float>((*T)["PhaseOffset"].value_or(0.0));
                        Entry.Motion.OrbitRadiusMeters            = static_cast<float>((*T)["OrbitRadiusMeters"].value_or(18.0));
                        Entry.Motion.OrbitVerticalAmplitudeMeters = static_cast<float>((*T)["OrbitVerticalAmplitudeMeters"].value_or(4.0));
                        Entry.Motion.OrbitTurns                   = static_cast<float>((*T)["OrbitTurns"].value_or(1.0));
                        Entry.Motion.OrientAlongPathTangent       = (*T)["OrientAlongPathTangent"].value_or(true);

                        Entry.Motion.WaypointCount = 0u;
                        if (const toml::array* Wps = (*T)["Waypoints"].as_array())
                        {
                            for (const auto& Wp : *Wps)
                            {
                                if (Entry.Motion.WaypointCount >= MaxForceMotionWaypoints)
                                {
                                    break;
                                }
                                Entry.Motion.Waypoints[Entry.Motion.WaypointCount++] = UnpackVec3ArrayPtr(Wp.as_array(), {});
                            }
                        }
                        Entry.Motion.EvaluatedArcLengthMeters = ComputeMotionPathArcLengthMeters(Entry);
                        break;
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           .VECTORFIELDSPACE ENCODER & DECODER
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload VectorFieldEncoder::BuildPayload(
    const VectorFieldSpaceDocument&    Document,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.SuffixCategory  = SpaceSuffixCategory::VectorFieldSpace;
    Payload.DocumentTitle      = Document.DocumentTitle;
    Payload.AuthoringWorkspace = "Frontier.SimulationWorkspace";

    toml::table Header;
    Header.insert_or_assign("CoordinateSystem", Document.CoordinateSystem);
    Header.insert_or_assign("CascadeResolution", static_cast<int64_t>(Document.CascadeResolution));
    Payload.DomainManifest.insert_or_assign(".VectorFieldHeader", std::move(Header));

    SerializeInlineForceSourcesToManifest(Document.ForceSources, Document.LinkedAssetPaths, Payload.DomainManifest);

#if FRONTIER_EDITOR_ENABLED
    if (Document.HasEditorOverlay)
    {
        toml::table Overlay;
        Overlay.insert_or_assign("Enabled",                Document.EditorOverlay.Enabled);
        Overlay.insert_or_assign("ShowHeatmap",            Document.EditorOverlay.ShowHeatmap);
        Overlay.insert_or_assign("ShowVectors",            Document.EditorOverlay.ShowVectors);
        Overlay.insert_or_assign("ShowStreamlines",        Document.EditorOverlay.ShowStreamlines);
        Overlay.insert_or_assign("ShowBounds",             Document.EditorOverlay.ShowBounds);
        Overlay.insert_or_assign("ShowMotionCurves",       Document.EditorOverlay.ShowMotionCurves);
        Overlay.insert_or_assign("CrossSectionAxis",       static_cast<int64_t>(Document.EditorOverlay.CrossSectionAxis));
        Overlay.insert_or_assign("CrossSectionOffsetM",    static_cast<double>(Document.EditorOverlay.CrossSectionOffsetMeters));
        Overlay.insert_or_assign("HeatmapChannel",         static_cast<int64_t>(Document.EditorOverlay.HeatmapChannel));
        Overlay.insert_or_assign("StreamColorMode",        static_cast<int64_t>(Document.EditorOverlay.StreamColorMode));
        Overlay.insert_or_assign("CustomStreamColorRgba",  static_cast<int64_t>(Document.EditorOverlay.CustomStreamColorRgba));
        Overlay.insert_or_assign("StreamRibbonThickness",  static_cast<double>(Document.EditorOverlay.StreamRibbonThickness));
        Overlay.insert_or_assign("StreamEmissiveBoost",    static_cast<double>(Document.EditorOverlay.StreamEmissiveBoost));
        Overlay.insert_or_assign("StreamOpacity",          static_cast<double>(Document.EditorOverlay.StreamOpacity));
        Overlay.insert_or_assign("StreamCurveResolution",  static_cast<int64_t>(Document.EditorOverlay.StreamCurveResolution));
        Overlay.insert_or_assign("StreamLineCount",        static_cast<int64_t>(Document.EditorOverlay.StreamLineCount));
        Payload.DomainManifest.insert_or_assign(".EditorOverlaySettings", std::move(Overlay));
    }
#endif

    if (Document.EmbedGpuPackedCache && !Document.ForceSources.empty())
    {
        // Section 1: 64B-aligned FORCES_PACKED (ForceSourceShaderEntry[])
        BinarySectionBlock ForcesBlock{};
        ForcesBlock.SectionIdentifier = "FORCES_PACKED";
        ForcesBlock.SubFormatTag      = ".ForceSource";

        std::vector<SpatialVector3> WaypointsLut;
        for (const ForceSourceRecordEntry& Entry : Document.ForceSources)
        {
            const uint32_t StartWp = static_cast<uint32_t>(WaypointsLut.size());
            const uint32_t CountWp = std::min<uint32_t>(Entry.Motion.WaypointCount, MaxForceMotionWaypoints);
            for (uint32_t K = 0u; K < CountWp; ++K)
            {
                WaypointsLut.push_back(Entry.Motion.Waypoints[K]);
            }
            const ForceSourceShaderEntry Packed = PackForceSourceForShader(Entry, 0.0, StartWp);
            AppendPodBytes(ForcesBlock.PayloadBytes, Packed);
        }
        Payload.BinarySections.push_back(std::move(ForcesBlock));

        // Section 2: SPLINE_LUT (pre-sampled uniform phase coordinates)
        if (!WaypointsLut.empty())
        {
            BinarySectionBlock SplineBlock{};
            SplineBlock.SectionIdentifier = "SPLINE_LUT";
            SplineBlock.SubFormatTag      = ".MotionPath";
            for (const SpatialVector3& Wp : WaypointsLut)
            {
                AppendPodBytes(SplineBlock.PayloadBytes, Wp);
            }
            Payload.BinarySections.push_back(std::move(SplineBlock));
        }

        // Section 3: FORMULA_IR (precompiled FormulaBytecodeProgram for active formula entries)
        BinarySectionBlock FormulaBlock{};
        FormulaBlock.SectionIdentifier = "FORMULA_IR";
        FormulaBlock.SubFormatTag      = ".FormulaKernel";
        for (const ForceSourceRecordEntry& Entry : Document.ForceSources)
        {
            if (Entry.FormulaEnabled && !Entry.FormulaSourceText.empty())
            {
                const FormulaCompileOutcome Compiled = CompileFieldFormula(Entry.FormulaSourceText);
                if (Compiled.Compiled)
                {
                    AppendPodBytes(FormulaBlock.PayloadBytes, Entry.UniqueToken);
                    AppendPodBytes(FormulaBlock.PayloadBytes, Compiled.Program);
                }
            }
        }
        if (!FormulaBlock.PayloadBytes.empty())
        {
            Payload.BinarySections.push_back(std::move(FormulaBlock));
        }
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool VectorFieldEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const VectorFieldSpaceDocument&    Document,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(TargetPath, BuildPayload(Document, OptionalRevision), OutDiagnostic);
}

bool VectorFieldDecoder::ExtractDocument(
    const SpaceContainerPayload& Payload,
    VectorFieldSpaceDocument&    OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::VectorFieldSpace)
    {
        OutDiagnostic = "Expected .VectorFieldSpace container category.";
        return false;
    }

    OutDocument               = VectorFieldSpaceDocument{};
    OutDocument.DocumentTitle = Payload.DocumentTitle;

    if (const toml::table* Header = Payload.DomainManifest[".VectorFieldHeader"].as_table())
    {
        OutDocument.CoordinateSystem  = (*Header)["CoordinateSystem"].value_or(std::string{ "RightHandZUp" });
        OutDocument.CascadeResolution = static_cast<uint32_t>((*Header)["CascadeResolution"].value_or<int64_t>(16));
    }

    DeserializeInlineForceSourcesFromManifest(Payload.DomainManifest, OutDocument.ForceSources, OutDocument.LinkedAssetPaths);

#if FRONTIER_EDITOR_ENABLED
    if (const toml::table* Overlay = Payload.DomainManifest[".EditorOverlaySettings"].as_table())
    {
        OutDocument.HasEditorOverlay                         = true;
        OutDocument.EditorOverlay.Enabled                    = (*Overlay)["Enabled"].value_or(true);
        OutDocument.EditorOverlay.ShowHeatmap                = (*Overlay)["ShowHeatmap"].value_or(true);
        OutDocument.EditorOverlay.ShowVectors                = (*Overlay)["ShowVectors"].value_or(true);
        OutDocument.EditorOverlay.ShowStreamlines            = (*Overlay)["ShowStreamlines"].value_or(true);
        OutDocument.EditorOverlay.ShowBounds                 = (*Overlay)["ShowBounds"].value_or(true);
        OutDocument.EditorOverlay.ShowMotionCurves           = (*Overlay)["ShowMotionCurves"].value_or(true);
        OutDocument.EditorOverlay.CrossSectionAxis           = static_cast<CrossSectionPlaneAxis>((*Overlay)["CrossSectionAxis"].value_or<int64_t>(0));
        OutDocument.EditorOverlay.CrossSectionOffsetMeters   = static_cast<float>((*Overlay)["CrossSectionOffsetM"].value_or(8.0));
        OutDocument.EditorOverlay.HeatmapChannel             = static_cast<HeatmapChannelMode>((*Overlay)["HeatmapChannel"].value_or<int64_t>(0));
        OutDocument.EditorOverlay.StreamColorMode            = static_cast<StreamlineColorMode>((*Overlay)["StreamColorMode"].value_or<int64_t>(1));
        OutDocument.EditorOverlay.CustomStreamColorRgba      = static_cast<uint32_t>((*Overlay)["CustomStreamColorRgba"].value_or<int64_t>(0x68DCFFFFu));
        OutDocument.EditorOverlay.StreamRibbonThickness      = static_cast<float>((*Overlay)["StreamRibbonThickness"].value_or(0.62));
        OutDocument.EditorOverlay.StreamEmissiveBoost        = static_cast<float>((*Overlay)["StreamEmissiveBoost"].value_or(1.0));
        OutDocument.EditorOverlay.StreamOpacity              = static_cast<float>((*Overlay)["StreamOpacity"].value_or(0.92));
        OutDocument.EditorOverlay.StreamCurveResolution      = static_cast<uint32_t>((*Overlay)["StreamCurveResolution"].value_or<int64_t>(40));
        OutDocument.EditorOverlay.StreamLineCount            = static_cast<uint32_t>((*Overlay)["StreamLineCount"].value_or<int64_t>(180));
    }
#endif

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool VectorFieldDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    VectorFieldSpaceDocument&    OutDocument,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDocument(Payload, OutDocument, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
