/*============================================================================================================================================
                                                        PROJECTEXECUTIONPOLICY.H
============================================================================================================================================*/
// 🧩 Declarative C++20 project execution policy governing fixed cadence, opt-in domains, worker channels, and step guardrails

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Configuration/BootConfigurationLoader.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class RuntimeDomainMask : uint32_t
{
    None           = 0u,
    WorkerChannels = 1u << 0u, // [-] - Opt-in parallel ExecutionChannels task workers
    AssetStreaming = 1u << 1u, // [-] - Opt-in storage-aware AssetStreamThread slice reader
    Physics        = 1u << 2u, // [-] - Opt-in rigid-body & vehicle dynamics stepping
    Audio          = 1u << 3u, // [-] - Opt-in spatial audio bus & mixer thread
    Networking     = 1u << 4u, // [-] - Opt-in NetworkingSpace transports & policy replication
    WorldRender    = 1u << 5u, // [-] - Opt-in world presentation & viewport rasterizer
};

constexpr RuntimeDomainMask operator|(RuntimeDomainMask Left, RuntimeDomainMask Right) noexcept
{
    return static_cast<RuntimeDomainMask>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

constexpr RuntimeDomainMask operator&(RuntimeDomainMask Left, RuntimeDomainMask Right) noexcept
{
    return static_cast<RuntimeDomainMask>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr bool QueryDomainEnabled(RuntimeDomainMask Mask, RuntimeDomainMask DomainBit) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(DomainBit)) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProjectExecutionPolicy
{
    uint32_t          SchemaVersion          = 1u;                                                // [ver]   - Policy document revision
    std::string       ProjectTitle           { "Untitled" };                                      // [-]     - Project display & window title
    double            FixedStepSeconds       = 1.0 / 60.0;                                        // [s]     - Deterministic simulation substep
    double            MaximumStepSeconds     = 0.100;                                             // [s]     - Spiral-of-death delta clamp
    uint32_t          MaximumSubstepsPerTick = 4u;                                                // [count] - Upper fixed substep cap per tick
    double            TargetStepBudgetMs     = 16.6667;                                           // [ms]    - Advisory tick duration budget
    uint32_t          WorkerChannelCount     = 2u;                                                // [count] - Opt-in ExecutionChannels lanes
    uint32_t          WindowWidthPixels      = 1280u;                                             // [px]    - Initial desktop window width
    uint32_t          WindowHeightPixels     = 720u;                                              // [px]    - Initial desktop window height
    bool              PreferHeadlessWindow   = true;                                              // [-]     - Prefer deterministic headless surface
    bool              SuspendWhenMinimized   = true;                                              // [-]     - Pause participants while iconified
    bool              EnableVerticalSync     = true;                                              // [-]     - Present synchronized with display
    RuntimeDomainMask EnabledDomains         = RuntimeDomainMask::WorkerChannels |                // [-]     - Opt-in subsystem activation bitmask
                                               RuntimeDomainMask::AssetStreaming;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ProjectExecutionPolicyCodec
{
public:
    [[nodiscard]] static ProjectExecutionPolicy ConstructFromBootConfiguration(
        const Serialization::ResolvedBootConfiguration& BootConfig) noexcept;

    static void ClampToSafeBounds(ProjectExecutionPolicy& Policy) noexcept;

    [[nodiscard]] static bool EncodeToDocument(
        const ProjectExecutionPolicy&     Policy,
        Serialization::DocumentContainer& OutDocument);

    [[nodiscard]] static bool DecodeFromDocument(
        const Serialization::DocumentContainer& Document,
        ProjectExecutionPolicy&                 OutPolicy,
        std::string&                            OutDiagnostic);

    [[nodiscard]] static bool SaveToFile(
        const std::filesystem::path&  TargetPath,
        const ProjectExecutionPolicy& Policy,
        std::string&                  OutDiagnostic);

    [[nodiscard]] static bool LoadFromFile(
        const std::filesystem::path& SourcePath,
        ProjectExecutionPolicy&      OutPolicy,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::Lifecycle
