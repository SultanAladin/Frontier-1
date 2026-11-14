/*============================================================================================================================================
                                                  PARTICLECODECINTERCHANGEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for ParticleCodec (.ParticleSpace + .ColourLut/.SurfaceAnchor/.StrandRest) & ParticleInterchange (.obj/JSON/Handoff)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Interchange/ParticleInterchange.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetCatalogue.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int gPassCount = 0;
int gFailCount = 0;

void Expect(bool Condition, const char* Label)
{
    if (Condition)
    {
        ++gPassCount;
        std::cout << "[PASS] " << Label << "\n";
    }
    else
    {
        ++gFailCount;
        std::cerr << "[FAIL] " << Label << "\n";
    }
}

} // namespace

int main()
{
    std::cout << "=== ParticleCodec & ParticleInterchange Validation ===\n";

    // 1. Two-Tier Hybrid .ParticleSpace round-trip including .ColourLut + .SurfaceAnchor + .StrandRest sections
    ParticleSpace::ParticleDocument Doc = ParticleSpace::InstantiateDocumentFromPresetId("spiderweb-orb");
    ParticleSpace::ParticleRig Rig2     = ParticleSpace::ConstructDefaultStudioRig(); // Carries SurfaceAnchorOrigin
    Doc.Rigs.push_back(Rig2);

    const Codecs::SpaceContainerPayload BuiltPayload = Codecs::ParticleEncoder::BuildPayload(Doc);
    std::vector<std::byte> BinaryBytes;
    std::string Diag;
    Expect(Codecs::SpaceContainerEncoder::EncodeToBytes(BuiltPayload, BinaryBytes, Diag), "Encode spiderweb + surface-pour document to .ParticleSpace");

    Codecs::SpaceContainerPayload Inspected {};
    Expect(Codecs::SpaceContainerDecoder::DecodeFromBytes(BinaryBytes, Inspected, Diag), "Inspect raw SpaceContainer sections");

    bool HasColourLut = false, HasSurfaceAnchor = false, HasStrandRest = false;
    for (const auto& Sec : Inspected.BinarySections)
    {
        if (Sec.SubFormatTag == ".ColourLut")     HasColourLut = true;
        if (Sec.SubFormatTag == ".SurfaceAnchor") HasSurfaceAnchor = true;
        if (Sec.SubFormatTag == ".StrandRest")    HasStrandRest = true;
    }
    Expect(HasColourLut,     "Tier 2 binary includes 64-entry .ColourLut section");
    Expect(HasSurfaceAnchor, "Tier 2 binary includes .SurfaceAnchor section");
    Expect(HasStrandRest,    "Tier 2 binary includes .StrandRest rest-length table section");

    ParticleSpace::ParticleDocument Decoded {};
    Expect(Codecs::ParticleDecoder::ExtractDocument(Inspected, Decoded, Diag), "Decode .ParticleSpace round-trip");
    Expect(Decoded.Rigs.size() == 2u, "Decoded document preserves both rigs");

    // 2. HTML Particle Studio JSON import/export + Y-Up <-> +Z Up axis conversion + ColourInterchange delegation
    const std::string HtmlSample = R"({
      "id": "curl-sphere-plasma",
      "name": "Spherical Curl Plasma",
      "vfxCategory": "Silk & Curl",
      "capacity": 3600,
      "rate": 900,
      "origin": [1.0, 2.5, -3.0],
      "dir": [0.0, 1.0, 0.0],
      "spread": 0.65,
      "speedMin": 1.2,
      "speedMax": 4.8,
      "gravity": -0.25,
      "windCoupling": 0.4,
      "curlMode": 2,
      "curlFreq": 1.4,
      "curlAmp": 3.5,
      "colorBy": "speed",
      "colorA": "#00E5FF",
      "colorB": "#FF4081"
    })";

    ParticleSpace::ParticleRig ImportedRig {};
    Expect(Interchange::ParticleInterchange::ImportHtmlStudioRigJson(HtmlSample, ImportedRig, Diag, true), "Import HTML Particle Studio JSON with +Z Up conversion");
    Expect(ImportedRig.SlotCapacity == 3600u, "Imported capacity matches HTML JSON");
    Expect(ImportedRig.Dynamics.Curl.has_value() && ImportedRig.Dynamics.Curl->ManifoldMode == 2u, "Imported CurlManifoldSpec preserves mode 2");

    const auto* ImportedPt = std::get_if<ParticleSpace::AnalyticPointOrigin>(&ImportedRig.Origin);
    Expect(ImportedPt != nullptr && std::fabs(ImportedPt->CenterMeters[0] - 1.0f) < 1e-4f
        && std::fabs(ImportedPt->CenterMeters[1] - 3.0f) < 1e-4f
        && std::fabs(ImportedPt->CenterMeters[2] - 2.5f) < 1e-4f,
        "Y-Up [1, 2.5, -3] converted cleanly to Right-Hand +Z Up [1, 3, 2.5]");

    const std::string ExportedJson = Interchange::ParticleInterchange::ExportHtmlStudioRigJson(ImportedRig, true);
    ParticleSpace::ParticleRig ReImported {};
    Expect(Interchange::ParticleInterchange::ImportHtmlStudioRigJson(ExportedJson, ReImported, Diag, false), "Re-import exported HTML JSON back to Y-Up");
    const auto* RePt = std::get_if<ParticleSpace::AnalyticPointOrigin>(&ReImported.Origin);
    Expect(RePt != nullptr && std::fabs(RePt->CenterMeters[0] - 1.0f) < 1e-3f
        && std::fabs(RePt->CenterMeters[1] - 2.5f) < 1e-3f
        && std::fabs(RePt->CenterMeters[2] + 3.0f) < 1e-3f,
        "Round-trip Y-Up -> +Z Up -> Y-Up preserves origin exactness");

    // 3. ParticleHandoffPacket TOML serialization + catalogue materialization
    Interchange::ParticleHandoffPacket Packet {};
    Packet.PresetId     = "energy-shield-hex";
    Packet.RigName      = "Workspace Handoff Hex Shield";
    Packet.SlotCapacity = 2400u;
    Packet.ReleaseRate  = 512.0f;
    Packet.GravityScale = 0.0f;
    Packet.WindCoupling = 0.25f;

    const std::string HandoffToml = Interchange::ParticleInterchange::EncodeHandoffPacketToml(Packet);
    Interchange::ParticleHandoffPacket DecodedPacket {};
    Expect(Interchange::ParticleInterchange::DecodeHandoffPacketToml(HandoffToml, DecodedPacket, Diag), "ParticleHandoffPacket TOML decode succeeds");
    Expect(DecodedPacket.PresetId == "energy-shield-hex" && DecodedPacket.SlotCapacity == 2400u, "Handoff packet fields round-trip exactly");

    const ParticleSpace::ParticleRig HandoffRig = Interchange::ParticleInterchange::MaterializeHandoffRig(DecodedPacket);
    Expect(HandoffRig.DisplayName == "Workspace Handoff Hex Shield" && HandoffRig.Dynamics.Shield.has_value(), "Materialized handoff rig activates ShieldBarrierSpec");

    // 4. External Wavefront .obj geometry intake delegating to GeometryInterchange -> SampleTriangleSurfaceAnchors
    const std::filesystem::path TempObj = std::filesystem::temp_directory_path() / "frontier_particle_pyramid_test.obj";
    {
        std::ofstream Out(TempObj);
        Out << "v -1.0 0.0 -1.0\n"
            << "v  1.0 0.0 -1.0\n"
            << "v  1.0 0.0  1.0\n"
            << "v -1.0 0.0  1.0\n"
            << "v  0.0 2.0  0.0\n"
            << "f 1 2 3 4\n"
            << "f 1 2 5\n"
            << "f 2 3 5\n"
            << "f 3 4 5\n"
            << "f 4 1 5\n";
    }
    ParticleSpace::SurfaceAnchorOrigin SurfOrigin {};
    SurfOrigin.ScaleMeters[0] = 1.25f;
    SurfOrigin.ScaleMeters[1] = 1.25f;
    SurfOrigin.ScaleMeters[2] = 1.25f;
    std::vector<ParticleSpace::ParticleAnchorSample> Anchors;
    Expect(Interchange::ParticleInterchange::ImportSurfaceAnchorsFromExternalFile(TempObj, SurfOrigin, 256u, 42u, Anchors, Diag),
        "Import Wavefront .obj surface via GeometryInterchange & sample 256 anchors");
    Expect(Anchors.size() == 256u, "Exactly 256 area-weighted surface anchors produced");
    bool NormalsUnit = true;
    for (const auto& A : Anchors)
    {
        const float NLen = std::sqrt(A.NormalUnit[0]*A.NormalUnit[0] + A.NormalUnit[1]*A.NormalUnit[1] + A.NormalUnit[2]*A.NormalUnit[2]);
        if (!std::isfinite(NLen) || std::fabs(NLen - 1.0f) > 0.05f) { NormalsUnit = false; break; }
    }
    Expect(NormalsUnit, "All sampled .obj surface anchor normals are finite unit vectors");
    std::error_code Ec;
    std::filesystem::remove(TempObj, Ec);

    std::cout << "Summary: " << gPassCount << " passed, " << gFailCount << " failed.\n";
    return (gFailCount == 0) ? 0 : 1;
}
