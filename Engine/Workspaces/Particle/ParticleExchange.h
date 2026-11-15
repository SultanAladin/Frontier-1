/*============================================================================================================================================
                                                              PARTICLEEXCHANGE.H
============================================================================================================================================*/
// 🧩 Cross-workspace particle handoff exchange encoding, decoding, & rig instantiation (cleanly separated from external ParticleInterchange)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/ParticleSpace/ParticleRig.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleHandoffPacket
{
    std::string                                     PresetId         { "object-surface-pour" };
    std::string                                     RigName          { "Object Surface Pour Ribbons" };
    std::string                                     SourceWorkspace  { "Frontier.ParticleWorkspace" };
    std::optional<uint32_t>                         SlotCapacity     {};
    std::optional<float>                            ReleaseRate      {};
    std::optional<float>                            GravityScale     {};
    std::optional<float>                            WindCoupling     {};
    std::optional<ColourSpace::ColourSourceProfile> ColourOverride   {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ParticleExchange
{
public:
    [[nodiscard]] static std::string                EncodeHandoffPacketToml(const ParticleHandoffPacket& Packet);
    [[nodiscard]] static bool                       DecodeHandoffPacketToml(
        std::string_view       TomlText,
        ParticleHandoffPacket& OutPacket,
        std::string&           OutDiagnostic);
    [[nodiscard]] static ParticleSpace::ParticleRig MaterializeHandoffRig(const ParticleHandoffPacket& Packet);
};

} // namespace Frontier::Workspaces
