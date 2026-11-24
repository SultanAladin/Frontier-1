/*============================================================================================================================================
                                                 SIMULATIONREPLAYRECORDER.H
============================================================================================================================================*/
// 🧩 Phase L01: Zero-allocation deterministic multi-domain interactive action recorder & binary .SimReplay stream
//    persister (FNV-1a verified) guaranteeing bit-identical replay when paired with SimulationCheckpointBuffer rewind.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"
#include "Engine/SimulationSpace/SimulationInteractionBridge.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            REPLAY ACTION KINDS & RECORDS
//------------------------------------------------------------------------------------------------------------------------

enum class ReplayActionKind : uint8_t
{
    None               = 0u,
    BeginSoftBodyGrab  = 1u,
    MoveSoftBodyGrab   = 2u,
    ReleaseSoftBodyGrab = 3u,
    FluidBrushStroke   = 4u,
    RadialBlastImpulse = 5u,
    EnableSoftBody     = 6u,
    DisableSoftBody    = 7u
};

struct ReplayActionRecord
{
    uint64_t             TickIndex          = 0ull;
    ReplayActionKind     Kind               = ReplayActionKind::None;
    uint8_t              Padding_[3]        { 0u, 0u, 0u };
    uint32_t             InstanceId         = 0u;
    int32_t              PointIndex         = -1;
    std::array<float, 3> VectorA            { 0.0f, 0.0f, 0.0f }; // World target / brush centre / blast centre
    std::array<float, 3> VectorB            { 0.0f, 0.0f, 0.0f }; // Fluid velocity
    float                ScalarA            = 0.0f;               // Density / blast radius
    float                ScalarB            = 0.0f;               // Temperature / peak speed
    float                ScalarC            = 0.0f;               // Brush radius / fluid temp boost
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION REPLAY RECORDER
//------------------------------------------------------------------------------------------------------------------------

class SimulationReplayRecorder
{
public:
    static constexpr uint32_t kMaxRecordedActions = 1024u;
    static constexpr uint32_t kBinaryVersion      = 1u;

    /// Resets the tape and begins recording actions anchored to `initialCheckpointHash` (0 heap allocations).
    void BeginRecording(uint64_t initialCheckpointHash = 0ull) noexcept;
    void StopRecording() noexcept { Recording_ = false; }

    /// Switches into playback mode starting at record ordinal 0 (0 heap allocations).
    void BeginPlayback() noexcept;
    void StopPlayback() noexcept { Playing_ = false; }

    /// Appends an action record at `tickIndex` (0 heap allocations; returns false if not recording or tape full).
    [[nodiscard]] bool RecordAction(const ReplayActionRecord& record) noexcept;

    /// Convenience typed recording helpers (0 heap allocations).
    [[nodiscard]] bool RecordSoftBodyGrabBegin(uint64_t tickIndex, const SoftBodyGrabHandle& handle) noexcept;
    [[nodiscard]] bool RecordSoftBodyGrabMove(uint64_t tickIndex, const SoftBodyGrabHandle& handle, const std::array<float, 3>& targetMeters) noexcept;
    [[nodiscard]] bool RecordSoftBodyGrabRelease(uint64_t tickIndex, const SoftBodyGrabHandle& handle) noexcept;
    [[nodiscard]] bool RecordFluidBrushStroke(
        uint64_t tickIndex,
        const std::array<float, 3>& centreMeters,
        const std::array<float, 3>& velocityMps,
        float densityAdd,
        float temperatureAdd,
        float radiusMeters) noexcept;
    [[nodiscard]] bool RecordRadialBlast(
        uint64_t tickIndex,
        const std::array<float, 3>& centreMeters,
        float radiusMeters,
        float peakSpeedMps,
        float fluidTempBoost) noexcept;

    /// Dispatches all recorded actions whose TickIndex <= `currentTickIndex` and >= cursor in deterministic order (0 heap allocs).
    /// Returns the number of actions replayed during this call.
    [[nodiscard]] uint32_t ApplyPlaybackForTick(
        uint64_t currentTickIndex,
        SimulationCoordinatorExtension& coordinator) noexcept;

    /// Deterministic FNV-1a 64-bit checksum covering initialCheckpointHash + all recorded actions.
    [[nodiscard]] uint64_t ComputeTapeChecksum() const noexcept;

    /// Persists/restores the replay stream to/from a compact binary file with FNV-1a corruption detection.
    [[nodiscard]] bool SaveToFile(const std::filesystem::path& filePath, std::string* outError = nullptr) const;
    [[nodiscard]] bool LoadFromFile(const std::filesystem::path& filePath, std::string* outError = nullptr);

    void Clear() noexcept;

    [[nodiscard]] bool     IsRecording() const noexcept { return Recording_; }
    [[nodiscard]] bool     IsPlaying() const noexcept { return Playing_; }
    [[nodiscard]] uint32_t QueryActionCount() const noexcept { return ActionCount_; }
    [[nodiscard]] uint32_t QueryPlaybackCursor() const noexcept { return PlaybackCursor_; }
    [[nodiscard]] uint64_t QueryInitialCheckpointHash() const noexcept { return InitialCheckpointHash_; }

private:
    std::array<ReplayActionRecord, kMaxRecordedActions> Tape_                  {};
    uint32_t                                            ActionCount_           = 0u;
    uint32_t                                            PlaybackCursor_        = 0u;
    uint64_t                                            InitialCheckpointHash_ = 0ull;
    bool                                                Recording_             = false;
    bool                                                Playing_               = false;
};

} // namespace Frontier::SimulationSpace
