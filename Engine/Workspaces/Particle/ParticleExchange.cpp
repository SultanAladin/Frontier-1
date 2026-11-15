/*============================================================================================================================================
                                                             PARTICLEEXCHANGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleExchange live cross-workspace TOML handoff serialization & preset rig instantiation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleExchange.h"

#include "Engine/Codecs/ColourCodec.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"

#include <sstream>
#include <toml++/toml.hpp>

namespace Frontier::Workspaces
{

std::string ParticleExchange::EncodeHandoffPacketToml(const ParticleHandoffPacket& Packet)
{
    toml::table Root {};
    toml::table Handoff {};
    Handoff.insert("PresetId",        Packet.PresetId);
    Handoff.insert("RigName",         Packet.RigName);
    Handoff.insert("SourceWorkspace", Packet.SourceWorkspace);
    if (Packet.SlotCapacity.has_value()) Handoff.insert("SlotCapacity", static_cast<int64_t>(*Packet.SlotCapacity));
    if (Packet.ReleaseRate.has_value())  Handoff.insert("ReleaseRate",  static_cast<double>(*Packet.ReleaseRate));
    if (Packet.GravityScale.has_value()) Handoff.insert("GravityScale", static_cast<double>(*Packet.GravityScale));
    if (Packet.WindCoupling.has_value()) Handoff.insert("WindCoupling", static_cast<double>(*Packet.WindCoupling));
    Root.insert("ParticleHandoff", std::move(Handoff));

    if (Packet.ColourOverride.has_value())
    {
        Root.insert("ColourOverride", Codecs::ColourEncoder::SerializeSourceToManifest(*Packet.ColourOverride));
    }

    std::ostringstream Stream;
    Stream << Root << "\n";
    return Stream.str();
}

bool ParticleExchange::DecodeHandoffPacketToml(
    std::string_view       TomlText,
    ParticleHandoffPacket& OutPacket,
    std::string&           OutDiagnostic)
{
    try
    {
        const toml::table Parsed = toml::parse(TomlText);
        const auto* Handoff = Parsed["ParticleHandoff"].as_table();
        if (Handoff == nullptr)
        {
            OutDiagnostic = "Missing [ParticleHandoff] section in handoff packet.";
            return false;
        }
        OutPacket = {};
        if (const auto Entry = (*Handoff)["PresetId"].value<std::string>())        OutPacket.PresetId        = *Entry;
        if (const auto Entry = (*Handoff)["RigName"].value<std::string>())         OutPacket.RigName         = *Entry;
        if (const auto Entry = (*Handoff)["SourceWorkspace"].value<std::string>()) OutPacket.SourceWorkspace = *Entry;
        if (const auto Entry = (*Handoff)["SlotCapacity"].value<int64_t>())        OutPacket.SlotCapacity    = static_cast<uint32_t>(*Entry);
        if (const auto Entry = (*Handoff)["ReleaseRate"].value<double>())          OutPacket.ReleaseRate     = static_cast<float>(*Entry);
        if (const auto Entry = (*Handoff)["GravityScale"].value<double>())         OutPacket.GravityScale    = static_cast<float>(*Entry);
        if (const auto Entry = (*Handoff)["WindCoupling"].value<double>())         OutPacket.WindCoupling    = static_cast<float>(*Entry);

        if (const auto* ColSection = Parsed["ColourOverride"].as_table())
        {
            ColourSpace::ColourSourceProfile Profile {};
            std::string ColDiag;
            if (Codecs::ColourDecoder::ExtractSourceFromManifest(*ColSection, Profile, ColDiag))
            {
                OutPacket.ColourOverride = std::move(Profile);
            }
        }
        return true;
    }
    catch (const toml::parse_error& Failure)
    {
        OutDiagnostic = std::string("Handoff TOML parse failed: ") + Failure.what();
        return false;
    }
}

ParticleSpace::ParticleRig ParticleExchange::MaterializeHandoffRig(const ParticleHandoffPacket& Packet)
{
    ParticleSpace::ParticleRig Rig = ParticleSpace::ConstructDefaultStudioRig();
    if (const auto* Preset = ParticleSpace::FindParticlePresetById(Packet.PresetId))
    {
        Rig = ParticleSpace::InstantiateRigFromPreset(*Preset);
    }
    if (!Packet.RigName.empty())           Rig.DisplayName = Packet.RigName;
    if (Packet.SlotCapacity.has_value())   Rig.SlotCapacity = *Packet.SlotCapacity;
    if (Packet.ReleaseRate.has_value())    Rig.ReleaseRatePerSec = *Packet.ReleaseRate;
    if (Packet.GravityScale.has_value())   Rig.Dynamics.Ballistic.GravityScale = *Packet.GravityScale;
    if (Packet.WindCoupling.has_value())   Rig.Dynamics.ForceCouple.WindCouplingRate = *Packet.WindCoupling;
    if (Packet.ColourOverride.has_value()) Rig.ColourProfile = *Packet.ColourOverride;
    return Rig;
}

} // namespace Frontier::Workspaces
