/*============================================================================================================================================
                                                         PARTICLEINTERCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleInterchange delegating colour to ColourInterchange and 3D surfaces to GeometryInterchange

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Interchange/ParticleInterchange.h"

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/Interchange/ColourInterchange.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetCatalogue.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Frontier::Interchange
{

namespace
{

std::string LowerAscii(std::string_view Input)
{
    std::string Out(Input);
    for (char& Ch : Out) Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    return Out;
}

bool ExtractJsonNumber(std::string_view Json, std::string_view Key, double& OutValue) noexcept
{
    const std::string Needle = "\"" + std::string(Key) + "\"";
    std::size_t Pos = Json.find(Needle);
    if (Pos == std::string_view::npos) return false;
    Pos = Json.find(':', Pos + Needle.size());
    if (Pos == std::string_view::npos) return false;
    ++Pos;
    while (Pos < Json.size() && std::isspace(static_cast<unsigned char>(Json[Pos]))) ++Pos;
    if (Pos >= Json.size()) return false;
    try
    {
        std::size_t Parsed = 0u;
        const std::string Slice(Json.substr(Pos, std::min<std::size_t>(48u, Json.size() - Pos)));
        OutValue = std::stod(Slice, &Parsed);
        return Parsed > 0u && std::isfinite(OutValue);
    }
    catch (...)
    {
        return false;
    }
}

bool ExtractJsonString(std::string_view Json, std::string_view Key, std::string& OutText)
{
    const std::string Needle = "\"" + std::string(Key) + "\"";
    std::size_t Pos = Json.find(Needle);
    if (Pos == std::string_view::npos) return false;
    Pos = Json.find(':', Pos + Needle.size());
    if (Pos == std::string_view::npos) return false;
    Pos = Json.find('"', Pos + 1u);
    if (Pos == std::string_view::npos) return false;
    ++Pos;
    std::string Acc;
    while (Pos < Json.size() && Json[Pos] != '"')
    {
        if (Json[Pos] == '\\' && (Pos + 1u) < Json.size())
        {
            ++Pos;
        }
        Acc.push_back(Json[Pos++]);
    }
    OutText = std::move(Acc);
    return true;
}

bool ExtractJsonVec3(std::string_view Json, std::string_view Key, float OutVec[3]) noexcept
{
    const std::string Needle = "\"" + std::string(Key) + "\"";
    std::size_t Pos = Json.find(Needle);
    if (Pos == std::string_view::npos) return false;
    Pos = Json.find('[', Pos + Needle.size());
    if (Pos == std::string_view::npos) return false;
    const std::size_t End = Json.find(']', Pos + 1u);
    if (End == std::string_view::npos) return false;
    std::string Body(Json.substr(Pos + 1u, End - Pos - 1u));
    for (char& C : Body) if (C == ',') C = ' ';
    std::istringstream Iss(Body);
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    if (!(Iss >> X >> Y >> Z)) return false;
    OutVec[0] = X; OutVec[1] = Y; OutVec[2] = Z;
    return true;
}

} // namespace

ExternalParticleFormat ParticleInterchange::DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept
{
    const std::string Ext = LowerAscii(FilePath.extension().string());
    if (Ext == ".json") return ExternalParticleFormat::HtmlStudioJson;
    if (Ext == ".toml" || Ext == ".handoff") return ExternalParticleFormat::HandoffPacketToml;
    if (Ext == ".obj")  return ExternalParticleFormat::WavefrontSurface;
    if (Ext == ".gltf") return ExternalParticleFormat::GltfSurface;
    return ExternalParticleFormat::Unknown;
}

void ParticleInterchange::ConvertYUpToRightHandZUp(const float SrcYUp[3], float DstZUp[3]) noexcept
{
    // HTML Three.js Right-Hand +Y Up -> Frontier Right-Hand +Z Up (+90 deg rotation about +X)
    const float X = SrcYUp[0], Y = SrcYUp[1], Z = SrcYUp[2];
    DstZUp[0] = X;
    DstZUp[1] = -Z;
    DstZUp[2] = Y;
}

void ParticleInterchange::ConvertRightHandZUpToYUp(const float SrcZUp[3], float DstYUp[3]) noexcept
{
    const float X = SrcZUp[0], Y = SrcZUp[1], Z = SrcZUp[2];
    DstYUp[0] = X;
    DstYUp[1] = Z;
    DstYUp[2] = -Y;
}

bool ParticleInterchange::ImportHtmlStudioRigJson(
    std::string_view            JsonText,
    ParticleSpace::ParticleRig& OutRig,
    std::string&                OutDiagnostic,
    bool                        ConvertAxisToZUp)
{
    if (JsonText.find('{') == std::string_view::npos)
    {
        OutDiagnostic = "ParticleInterchange rejected input: missing JSON object delimiter.";
        return false;
    }

    OutRig = ParticleSpace::ConstructDefaultStudioRig();

    std::string StrVal;
    if (ExtractJsonString(JsonText, "id", StrVal) && !StrVal.empty())
    {
        OutRig.Identifier = "particle.preset." + StrVal;
    }
    if (ExtractJsonString(JsonText, "name", StrVal) && !StrVal.empty())
    {
        OutRig.DisplayName = StrVal;
    }
    if (ExtractJsonString(JsonText, "vfxCategory", StrVal) && !StrVal.empty())
    {
        OutRig.CategoryLabel = StrVal;
    }
    if (ExtractJsonString(JsonText, "colorBy", StrVal) && !StrVal.empty())
    {
        OutRig.TintDriver.Channel = ParticleSpace::ParseScalarChannelKey(StrVal);
    }

    double DVal = 0.0;
    if (ExtractJsonNumber(JsonText, "capacity", DVal))     OutRig.SlotCapacity      = static_cast<uint32_t>(std::clamp(DVal, 16.0, 250000.0));
    if (ExtractJsonNumber(JsonText, "rate", DVal))         OutRig.ReleaseRatePerSec = static_cast<float>(std::max(0.0, DVal));
    if (ExtractJsonNumber(JsonText, "spread", DVal))       OutRig.Kinematics.SpreadRadians   = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "speedMin", DVal))     OutRig.Kinematics.SpeedMinMps     = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "speedMax", DVal))     OutRig.Kinematics.SpeedMaxMps     = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "lifeMin", DVal))      OutRig.Kinematics.LifeMinSeconds  = static_cast<float>(std::max(0.05, DVal));
    if (ExtractJsonNumber(JsonText, "lifeMax", DVal))      OutRig.Kinematics.LifeMaxSeconds  = static_cast<float>(std::max(0.05, DVal));
    if (ExtractJsonNumber(JsonText, "sizeStart", DVal))    OutRig.Kinematics.SizeBirthMeters = static_cast<float>(std::max(0.002, DVal));
    if (ExtractJsonNumber(JsonText, "sizeEnd", DVal))      OutRig.Kinematics.SizeDeathMeters = static_cast<float>(std::max(0.002, DVal));
    if (ExtractJsonNumber(JsonText, "sizeScale", DVal))    OutRig.Kinematics.SizeMultiplier  = static_cast<float>(std::max(0.05, DVal));
    if (ExtractJsonNumber(JsonText, "gravity", DVal))      OutRig.Dynamics.Ballistic.GravityScale       = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "drag", DVal))         OutRig.Dynamics.Ballistic.AerodynamicDrag    = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "buoyancy", DVal))     OutRig.Dynamics.Ballistic.BuoyancyMps2       = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "bounce", DVal))       OutRig.Dynamics.Ballistic.FloorRestitution   = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "flutter", DVal))      OutRig.Dynamics.Ballistic.FlutterAmplitude   = static_cast<float>(DVal);
    if (ExtractJsonNumber(JsonText, "windCoupling", DVal)) OutRig.Dynamics.ForceCouple.WindCouplingRate = static_cast<float>(DVal);

    float OrigVec[3] = { 0.0f, 1.5f, 0.0f };
    if (ExtractJsonVec3(JsonText, "origin", OrigVec))
    {
        if (ConvertAxisToZUp) ConvertYUpToRightHandZUp(OrigVec, OrigVec);
        ParticleSpace::AnalyticPointOrigin Pt {};
        for (int K = 0; K < 3; ++K) Pt.CenterMeters[K] = OrigVec[K];
        OutRig.Origin = Pt;
    }

    float DirVec[3] = { 0.0f, 1.0f, 0.0f };
    if (ExtractJsonVec3(JsonText, "dir", DirVec))
    {
        if (ConvertAxisToZUp) ConvertYUpToRightHandZUp(DirVec, DirVec);
        for (int K = 0; K < 3; ++K) OutRig.Kinematics.LaunchDirection[K] = DirVec[K];
    }

    if (ExtractJsonNumber(JsonText, "curlMode", DVal) && DVal > 0.5)
    {
        ParticleSpace::CurlManifoldSpec Curl {};
        Curl.ManifoldMode = static_cast<uint8_t>(std::clamp(DVal, 1.0, 5.0));
        if (ExtractJsonNumber(JsonText, "curlFreq", DVal))    Curl.FrequencyPerM   = static_cast<float>(DVal);
        if (ExtractJsonNumber(JsonText, "curlAmp", DVal))     Curl.DriveSpeedMps   = static_cast<float>(DVal);
        if (ExtractJsonNumber(JsonText, "curlRate", DVal))    Curl.EvolutionRateHz = static_cast<float>(DVal);
        if (ExtractJsonNumber(JsonText, "shellRadius", DVal)) Curl.ShellRadiusM    = static_cast<float>(DVal);
        OutRig.Dynamics.Curl = Curl;
    }
    else
    {
        OutRig.Dynamics.Curl.reset();
    }

    if (ExtractJsonNumber(JsonText, "fireworkMode", DVal) && DVal > 0.5)
    {
        ParticleSpace::FireworkOrigin Fw {};
        if (ExtractJsonNumber(JsonText, "fireworkShape", DVal))
        {
            Fw.Shape = static_cast<ParticleSpace::FireworkShapeCategory>(static_cast<uint8_t>(DVal) % static_cast<uint8_t>(ParticleSpace::FireworkShapeCategory::Count));
        }
        if (ExtractJsonString(JsonText, "fireworkText", StrVal)) Fw.SkyTextMessage = StrVal;
        if (ExtractJsonNumber(JsonText, "fireworkScale", DVal))  Fw.PatternRadius  = static_cast<float>(std::max(0.5, DVal));
        for (int K = 0; K < 3; ++K) Fw.ShellOrigin[K] = OrigVec[K];
        OutRig.Origin = std::move(Fw);
    }

    // Delegate colour block directly to Engine/Interchange/ColourInterchange!
    std::string ColDiag;
    ColourSpace::ColourSourceProfile ImportedColour {};
    if (ColourInterchange::ImportHtmlStudioJsonBlock(JsonText, ImportedColour, ColDiag))
    {
        OutRig.ColourProfile = std::move(ImportedColour);
    }

    OutDiagnostic = "Imported HTML Particle Studio snapshot into ParticleRig '" + OutRig.DisplayName + "'.";
    return true;
}

std::string ParticleInterchange::ExportHtmlStudioRigJson(
    const ParticleSpace::ParticleRig& Rig,
    bool                              ConvertAxisFromZUp)
{
    float Orig[3] = { 0.0f, 1.5f, 0.0f };
    if (const auto* Pt = std::get_if<ParticleSpace::AnalyticPointOrigin>(&Rig.Origin))
        for (int K = 0; K < 3; ++K) Orig[K] = Pt->CenterMeters[K];
    else if (const auto* Surf = std::get_if<ParticleSpace::SurfaceAnchorOrigin>(&Rig.Origin))
        for (int K = 0; K < 3; ++K) Orig[K] = Surf->CenterMeters[K];
    else if (const auto* Fw = std::get_if<ParticleSpace::FireworkOrigin>(&Rig.Origin))
        for (int K = 0; K < 3; ++K) Orig[K] = Fw->ShellOrigin[K];

    float Dir[3] = { Rig.Kinematics.LaunchDirection[0], Rig.Kinematics.LaunchDirection[1], Rig.Kinematics.LaunchDirection[2] };
    if (ConvertAxisFromZUp)
    {
        ConvertRightHandZUpToYUp(Orig, Orig);
        ConvertRightHandZUpToYUp(Dir, Dir);
    }

    // Embed canonical ColourInterchange HTML JSON colour block directly
    std::string ColourJson = ColourInterchange::ExportHtmlStudioJsonBlock(Rig.ColourProfile);
    // Strip outer braces from ColourJson so we merge seamlessly into one flat HTML Studio JSON snapshot
    const std::size_t FirstBrace = ColourJson.find('{');
    const std::size_t LastBrace  = ColourJson.rfind('}');
    std::string ColourBody;
    if (FirstBrace != std::string::npos && LastBrace != std::string::npos && LastBrace > FirstBrace)
    {
        ColourBody = ColourJson.substr(FirstBrace + 1u, LastBrace - FirstBrace - 1u);
    }

    std::ostringstream Out;
    Out << std::fixed << std::setprecision(4);
    Out << "{\n";
    Out << "  \"id\": \"" << Rig.Identifier << "\",\n";
    Out << "  \"name\": \"" << Rig.DisplayName << "\",\n";
    Out << "  \"vfxCategory\": \"" << Rig.CategoryLabel << "\",\n";
    Out << "  \"capacity\": " << Rig.SlotCapacity << ",\n";
    Out << "  \"rate\": " << Rig.ReleaseRatePerSec << ",\n";
    Out << "  \"origin\": [" << Orig[0] << ", " << Orig[1] << ", " << Orig[2] << "],\n";
    Out << "  \"dir\": [" << Dir[0] << ", " << Dir[1] << ", " << Dir[2] << "],\n";
    Out << "  \"spread\": " << Rig.Kinematics.SpreadRadians << ",\n";
    Out << "  \"speedMin\": " << Rig.Kinematics.SpeedMinMps << ",\n";
    Out << "  \"speedMax\": " << Rig.Kinematics.SpeedMaxMps << ",\n";
    Out << "  \"lifeMin\": " << Rig.Kinematics.LifeMinSeconds << ",\n";
    Out << "  \"lifeMax\": " << Rig.Kinematics.LifeMaxSeconds << ",\n";
    Out << "  \"sizeStart\": " << Rig.Kinematics.SizeBirthMeters << ",\n";
    Out << "  \"sizeEnd\": " << Rig.Kinematics.SizeDeathMeters << ",\n";
    Out << "  \"sizeScale\": " << Rig.Kinematics.SizeMultiplier << ",\n";
    Out << "  \"gravity\": " << Rig.Dynamics.Ballistic.GravityScale << ",\n";
    Out << "  \"drag\": " << Rig.Dynamics.Ballistic.AerodynamicDrag << ",\n";
    Out << "  \"buoyancy\": " << Rig.Dynamics.Ballistic.BuoyancyMps2 << ",\n";
    Out << "  \"bounce\": " << Rig.Dynamics.Ballistic.FloorRestitution << ",\n";
    Out << "  \"flutter\": " << Rig.Dynamics.Ballistic.FlutterAmplitude << ",\n";
    Out << "  \"windCoupling\": " << Rig.Dynamics.ForceCouple.WindCouplingRate << ",\n";
    Out << "  \"colorBy\": \"" << ParticleSpace::QueryScalarChannelKey(Rig.TintDriver.Channel) << "\",\n";
    Out << "  \"curlMode\": " << (Rig.Dynamics.Curl.has_value() ? static_cast<int>(Rig.Dynamics.Curl->ManifoldMode) : 0);
    if (Rig.Dynamics.Curl.has_value())
    {
        Out << ",\n  \"curlFreq\": " << Rig.Dynamics.Curl->FrequencyPerM;
        Out << ",\n  \"curlAmp\": " << Rig.Dynamics.Curl->DriveSpeedMps;
        Out << ",\n  \"curlRate\": " << Rig.Dynamics.Curl->EvolutionRateHz;
        Out << ",\n  \"shellRadius\": " << Rig.Dynamics.Curl->ShellRadiusM;
    }
    if (!ColourBody.empty())
    {
        Out << ",\n" << ColourBody << "\n";
    }
    else
    {
        Out << "\n";
    }
    Out << "}\n";
    return Out.str();
}

std::string ParticleInterchange::EncodeHandoffPacketToml(const ParticleHandoffPacket& Packet)
{
    toml::table Root {};
    toml::table H {};
    H.insert("PresetId",        Packet.PresetId);
    H.insert("RigName",         Packet.RigName);
    H.insert("SourceWorkspace", Packet.SourceWorkspace);
    if (Packet.SlotCapacity.has_value()) H.insert("SlotCapacity", static_cast<int64_t>(*Packet.SlotCapacity));
    if (Packet.ReleaseRate.has_value())  H.insert("ReleaseRate",  static_cast<double>(*Packet.ReleaseRate));
    if (Packet.GravityScale.has_value()) H.insert("GravityScale", static_cast<double>(*Packet.GravityScale));
    if (Packet.WindCoupling.has_value()) H.insert("WindCoupling", static_cast<double>(*Packet.WindCoupling));
    Root.insert("ParticleHandoff", std::move(H));

    if (Packet.ColourOverride.has_value())
    {
        Root.insert("ColourOverride", Codecs::ColourEncoder::SerializeSourceToManifest(*Packet.ColourOverride));
    }

    std::ostringstream Oss;
    Oss << Root << "\n";
    return Oss.str();
}

bool ParticleInterchange::DecodeHandoffPacketToml(
    std::string_view       TomlText,
    ParticleHandoffPacket& OutPacket,
    std::string&           OutDiagnostic)
{
    try
    {
        const toml::table Parsed = toml::parse(TomlText);
        const auto* H = Parsed["ParticleHandoff"].as_table();
        if (!H)
        {
            OutDiagnostic = "Missing [ParticleHandoff] section in handoff packet.";
            return false;
        }
        OutPacket = {};
        if (const auto V = (*H)["PresetId"].value<std::string>())        OutPacket.PresetId        = *V;
        if (const auto V = (*H)["RigName"].value<std::string>())         OutPacket.RigName         = *V;
        if (const auto V = (*H)["SourceWorkspace"].value<std::string>()) OutPacket.SourceWorkspace = *V;
        if (const auto V = (*H)["SlotCapacity"].value<int64_t>())        OutPacket.SlotCapacity    = static_cast<uint32_t>(*V);
        if (const auto V = (*H)["ReleaseRate"].value<double>())          OutPacket.ReleaseRate     = static_cast<float>(*V);
        if (const auto V = (*H)["GravityScale"].value<double>())         OutPacket.GravityScale    = static_cast<float>(*V);
        if (const auto V = (*H)["WindCoupling"].value<double>())         OutPacket.WindCoupling    = static_cast<float>(*V);

        if (const auto* ColTbl = Parsed["ColourOverride"].as_table())
        {
            ColourSpace::ColourSourceProfile Prof {};
            std::string ColDiag;
            if (Codecs::ColourDecoder::ExtractSourceFromManifest(*ColTbl, Prof, ColDiag))
            {
                OutPacket.ColourOverride = std::move(Prof);
            }
        }
        return true;
    }
    catch (const toml::parse_error& Err)
    {
        OutDiagnostic = std::string("Handoff TOML parse failed: ") + Err.what();
        return false;
    }
}

ParticleSpace::ParticleRig ParticleInterchange::MaterializeHandoffRig(const ParticleHandoffPacket& Packet)
{
    ParticleSpace::ParticleRig Rig = ParticleSpace::ConstructDefaultStudioRig();
    if (const auto* Preset = ParticleSpace::FindParticlePresetById(Packet.PresetId))
    {
        Rig = ParticleSpace::InstantiateRigFromPreset(*Preset);
    }
    if (!Packet.RigName.empty())         Rig.DisplayName = Packet.RigName;
    if (Packet.SlotCapacity.has_value()) Rig.SlotCapacity = *Packet.SlotCapacity;
    if (Packet.ReleaseRate.has_value())  Rig.ReleaseRatePerSec = *Packet.ReleaseRate;
    if (Packet.GravityScale.has_value()) Rig.Dynamics.Ballistic.GravityScale = *Packet.GravityScale;
    if (Packet.WindCoupling.has_value()) Rig.Dynamics.ForceCouple.WindCouplingRate = *Packet.WindCoupling;
    if (Packet.ColourOverride.has_value()) Rig.ColourProfile = *Packet.ColourOverride;
    return Rig;
}

std::vector<ParticleSpace::ParticleAnchorSample> ParticleInterchange::SampleAnchorsFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork&      Network,
    const ParticleSpace::SurfaceAnchorOrigin& TransformOrigin,
    uint32_t                                  SampleCount,
    uint32_t                                  Seed)
{
    std::vector<ParticleSpace::SurfaceTriangleInput> Triangles;
    Triangles.reserve(Network.Faces.size() * 2u);

    std::vector<uint32_t> LoopVerts;
    for (const auto& F : Network.Faces)
    {
        if (F.CornerCount < 3u || F.BoundaryLoopEdgeIndex >= Network.Edges.size()) continue;
        LoopVerts.clear();
        uint32_t CurEdge = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount && CurEdge < Network.Edges.size(); ++Step)
        {
            const uint32_t VIdx = Network.Edges[CurEdge].OriginVertexIndex;
            if (VIdx < Network.Vertices.size()) LoopVerts.push_back(VIdx);
            CurEdge = Network.Edges[CurEdge].NextEdgeIndex;
            if (CurEdge == F.BoundaryLoopEdgeIndex) break;
        }
        if (LoopVerts.size() < 3u) continue;

        const auto& V0 = Network.Vertices[LoopVerts[0]].Axes;
        for (std::size_t K = 1u; K + 1u < LoopVerts.size(); ++K)
        {
            const auto& V1 = Network.Vertices[LoopVerts[K]].Axes;
            const auto& V2 = Network.Vertices[LoopVerts[K + 1u]].Axes;
            Triangles.push_back(ParticleSpace::SurfaceTriangleInput {
                { V0.X, V0.Y, V0.Z },
                { V1.X, V1.Y, V1.Z },
                { V2.X, V2.Y, V2.Z }
            });
        }
    }

    return ParticleSpace::SampleTriangleSurfaceAnchors(Triangles, TransformOrigin, SampleCount, Seed);
}

bool ParticleInterchange::ImportSurfaceAnchorsFromExternalFile(
    const std::filesystem::path&                      SourcePath,
    const ParticleSpace::SurfaceAnchorOrigin&         TransformOrigin,
    uint32_t                                          SampleCount,
    uint32_t                                          Seed,
    std::vector<ParticleSpace::ParticleAnchorSample>& OutAnchors,
    std::string&                                      OutDiagnostic)
{
    GeometrySpace::ConstructedSampleAsset Asset {};
    if (!GeometryInterchange::ImportAssetFromFile(SourcePath, Asset, OutDiagnostic))
    {
        return false;
    }
    OutAnchors = SampleAnchorsFromPolygonNetwork(Asset.Network, TransformOrigin, SampleCount, Seed);
    if (OutAnchors.empty())
    {
        OutDiagnostic = "Imported geometry contained zero sampleable polygon faces.";
        return false;
    }
    OutDiagnostic = "Sampled " + std::to_string(OutAnchors.size()) + " area-weighted surface anchors from " + SourcePath.filename().string();
    return true;
}

} // namespace Frontier::Interchange
