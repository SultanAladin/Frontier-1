/*============================================================================================================================================
                                                          GAMEPLAYRULEPOLICY.H
============================================================================================================================================*/
// 🧩 Declarative C++20 gameplay & authority rule policy: fixed cadence, 64-bit world alignment hash, directive validation, and <128B wire sync

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Frontier::SimulationSpace::Policies
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DirectiveValidationOutcome : uint8_t
{
    Accepted                  = 0, // [-] - Directive satisfies ownership, bounds, finiteness, and rate ceilings
    RejectedNonFiniteValue    = 1, // [-] - NaN or +/-Inf detected in control axis
    RejectedOwnerMismatch     = 2, // [-] - Sender endpoint does not own target vehicle/actor
    RejectedSteerOutOfBounds  = 3, // [-] - |Steer| exceeds MaximumSteerMagnitude
    RejectedThrottleBounds    = 4, // [-] - Throttle outside [0, 1]
    RejectedBrakeBounds       = 5, // [-] - Brake outside [0, 1]
    RejectedRateLimitExceeded = 6, // [-] - Client directive frequency exceeds MaximumDirectiveRateHz
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldKinematicsDigestEntry
{
    uint32_t ActorIdentifier    = 0u;   // [id]    - Stable replicated actor identifier
    double   PositionMetersX    = 0.0;  // [m]     - World X coordinate (Z-up)
    double   PositionMetersY    = 0.0;  // [m]     - World Y coordinate (Z-up)
    double   PositionMetersZ    = 0.0;  // [m]     - World Z elevation
    float    VelocityMetersSecX = 0.0f; // [m/s]   - Linear velocity X
    float    VelocityMetersSecY = 0.0f; // [m/s]   - Linear velocity Y
    float    VelocityMetersSecZ = 0.0f; // [m/s]   - Linear velocity Z
    float    YawRadians         = 0.0f; // [rad]   - Planar heading angle
};

struct OwnerControlDirective
{
    uint32_t SenderEndpointIndex = 0u;    // [idx] - Network endpoint submitting directive
    uint32_t TargetActorId       = 0u;    // [id]  - Controlled actor/vehicle identifier
    uint64_t SimulationTick      = 0ull;  // [idx] - Monotonic fixed-step tick ordinal
    float    Steer               = 0.0f;  // [-]   - Normalized steering [-1..+1]
    float    Throttle            = 0.0f;  // [-]   - Normalized throttle [0..1]
    float    Brake               = 0.0f;  // [-]   - Normalized brake [0..1]
    uint8_t  GearAndHandbrake    = 0u;    // [-]   - Packed gear nibble + handbrake bit
};

struct GameplayRulePolicy
{
    static constexpr std::size_t WirePayloadBytes = 48u; // [B] - Fixed compact wire slab (< 128 B budget)

    uint32_t SchemaVersion               = 1u;        // [ver]   - Rule policy schema revision
    uint32_t FixedSimulationHz           = 120u;      // [Hz]    - Authoritative fixed simulation cadence
    uint32_t AlignmentHashIntervalTicks  = 60u;       // [ticks] - Interval between 64-bit desync verification hashes
    float    SpeedLimitKmh               = 120.0f;    // [km/h]  - Traffic enforcement speed ceiling
    float    RedSignalPenaltyScore       = 50.0f;     // [pts]   - Penalty accrued for running a red traffic signal
    float    CollisionImpulseThresholdNs = 12000.0f;  // [N*s]   - Impulse threshold triggering pursuit escalation
    uint32_t PursuitEscalationStages     = 5u;        // [count] - Maximum pursuit heat escalation stages
    float    MaximumSteerMagnitude       = 1.0f;      // [-]     - Upper absolute steer axis limit
    uint32_t MaximumDirectiveRateHz      = 240u;      // [Hz]    - Upper permitted client directive submission rate
    bool     JoinInProgressAllowed       = true;      // [-]     - Permit late-joining peers to sync baseline
    bool     HostMigrationEnabled        = true;      // [-]     - Permit deterministic host election on disconnect
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class GameplayRulePolicyEngine
{
public:
    static void ClampToSafeBounds(GameplayRulePolicy& Policy) noexcept;

    [[nodiscard]] static uint64_t ComputeWorldAlignmentHash(
        uint64_t                                   TickOrdinal,
        std::span<const WorldKinematicsDigestEntry> Entries) noexcept;

    [[nodiscard]] static bool VerifyWorldAlignmentHash(
        uint64_t                                   TickOrdinal,
        std::span<const WorldKinematicsDigestEntry> Entries,
        uint64_t                                   ExpectedDigest) noexcept;

    [[nodiscard]] static DirectiveValidationOutcome ValidateOwnerDirective(
        const GameplayRulePolicy&    Policy,
        const OwnerControlDirective& Directive,
        uint32_t                     AuthoritativeOwnerEndpoint,
        uint32_t                     ObservedClientRateHz) noexcept;

    [[nodiscard]] static bool EncodePolicyWireBytes(
        const GameplayRulePolicy&                                     Policy,
        std::array<std::byte, GameplayRulePolicy::WirePayloadBytes>&  OutWireBytes) noexcept;

    [[nodiscard]] static bool DecodePolicyWireBytes(
        std::span<const std::byte> WireBytes,
        GameplayRulePolicy&        OutPolicy,
        std::string&               OutDiagnostic);

    [[nodiscard]] static bool EncodeToDocument(
        const GameplayRulePolicy&         Policy,
        Serialization::DocumentContainer& OutDocument);

    [[nodiscard]] static bool DecodeFromDocument(
        const Serialization::DocumentContainer& Document,
        GameplayRulePolicy&                     OutPolicy,
        std::string&                            OutDiagnostic);
};

} // namespace Frontier::SimulationSpace::Policies
