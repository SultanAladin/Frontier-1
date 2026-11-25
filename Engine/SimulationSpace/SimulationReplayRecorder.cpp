/*============================================================================================================================================
                                                SIMULATIONREPLAYRECORDER.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/SimulationReplayRecorder.h"

#include <cstring>
#include <fstream>

namespace Frontier::SimulationSpace
{

namespace
{

constexpr char     kMagic[4]    = { 'F', 'S', 'R', 'P' };
constexpr uint64_t kFnvOffset64 = 14695981039346656037ull;
constexpr uint64_t kFnvPrime64  = 1099511628211ull;

inline void HashBytes(uint64_t& h, const void* data, size_t bytes) noexcept
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0u; i < bytes; ++i)
    {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime64;
    }
}

struct ReplayBinaryHeader
{
    char     Magic[4];
    uint32_t Version;
    uint32_t ActionCount;
    uint32_t Reserved;
    uint64_t InitialCheckpointHash;
    uint64_t TapeChecksum;
};

static_assert(sizeof(ReplayBinaryHeader) == 32u, "ReplayBinaryHeader must remain 32 bytes");

} // namespace

void SimulationReplayRecorder::BeginRecording(uint64_t initialCheckpointHash) noexcept
{
    ActionCount_           = 0u;
    PlaybackCursor_        = 0u;
    InitialCheckpointHash_ = initialCheckpointHash;
    Recording_             = true;
    Playing_               = false;
}

void SimulationReplayRecorder::BeginPlayback() noexcept
{
    Recording_      = false;
    Playing_        = true;
    PlaybackCursor_ = 0u;
}

void SimulationReplayRecorder::Clear() noexcept
{
    ActionCount_           = 0u;
    PlaybackCursor_        = 0u;
    InitialCheckpointHash_ = 0ull;
    Recording_             = false;
    Playing_               = false;
}

bool SimulationReplayRecorder::RecordAction(const ReplayActionRecord& record) noexcept
{
    if (!Recording_ || ActionCount_ >= kMaxRecordedActions)
    {
        return false;
    }
    Tape_[ActionCount_++] = record;
    return true;
}

bool SimulationReplayRecorder::RecordSoftBodyGrabBegin(uint64_t tickIndex, const SoftBodyGrabHandle& handle) noexcept
{
    if (!handle.Valid) return false;
    ReplayActionRecord rec {};
    rec.TickIndex  = tickIndex;
    rec.Kind       = ReplayActionKind::BeginSoftBodyGrab;
    rec.InstanceId = handle.InstanceId;
    rec.PointIndex = handle.PointIndex;
    rec.VectorA    = handle.HitWorldMeters;
    return RecordAction(rec);
}

bool SimulationReplayRecorder::RecordSoftBodyGrabMove(
    uint64_t tickIndex,
    const SoftBodyGrabHandle& handle,
    const std::array<float, 3>& targetMeters) noexcept
{
    if (!handle.Valid) return false;
    ReplayActionRecord rec {};
    rec.TickIndex  = tickIndex;
    rec.Kind       = ReplayActionKind::MoveSoftBodyGrab;
    rec.InstanceId = handle.InstanceId;
    rec.PointIndex = handle.PointIndex;
    rec.VectorA    = targetMeters;
    return RecordAction(rec);
}

bool SimulationReplayRecorder::RecordSoftBodyGrabRelease(uint64_t tickIndex, const SoftBodyGrabHandle& handle) noexcept
{
    if (!handle.Valid) return false;
    ReplayActionRecord rec {};
    rec.TickIndex  = tickIndex;
    rec.Kind       = ReplayActionKind::ReleaseSoftBodyGrab;
    rec.InstanceId = handle.InstanceId;
    rec.PointIndex = handle.PointIndex;
    return RecordAction(rec);
}

bool SimulationReplayRecorder::RecordFluidBrushStroke(
    uint64_t tickIndex,
    const std::array<float, 3>& centreMeters,
    const std::array<float, 3>& velocityMps,
    float densityAdd,
    float temperatureAdd,
    float radiusMeters) noexcept
{
    ReplayActionRecord rec {};
    rec.TickIndex = tickIndex;
    rec.Kind      = ReplayActionKind::FluidBrushStroke;
    rec.VectorA   = centreMeters;
    rec.VectorB   = velocityMps;
    rec.ScalarA   = densityAdd;
    rec.ScalarB   = temperatureAdd;
    rec.ScalarC   = radiusMeters;
    return RecordAction(rec);
}

bool SimulationReplayRecorder::RecordRadialBlast(
    uint64_t tickIndex,
    const std::array<float, 3>& centreMeters,
    float radiusMeters,
    float peakSpeedMps,
    float fluidTempBoost) noexcept
{
    ReplayActionRecord rec {};
    rec.TickIndex = tickIndex;
    rec.Kind      = ReplayActionKind::RadialBlastImpulse;
    rec.VectorA   = centreMeters;
    rec.ScalarA   = radiusMeters;
    rec.ScalarB   = peakSpeedMps;
    rec.ScalarC   = fluidTempBoost;
    return RecordAction(rec);
}

uint32_t SimulationReplayRecorder::ApplyPlaybackForTick(
    uint64_t currentTickIndex,
    SimulationCoordinatorExtension& coordinator) noexcept
{
    if (!Playing_) return 0u;

    uint32_t replayed = 0u;
    while (PlaybackCursor_ < ActionCount_ && Tape_[PlaybackCursor_].TickIndex <= currentTickIndex)
    {
        const ReplayActionRecord& rec = Tape_[PlaybackCursor_++];
        switch (rec.Kind)
        {
            case ReplayActionKind::BeginSoftBodyGrab:
            {
                SoftBodyGrabHandle h {};
                h.InstanceId     = rec.InstanceId;
                h.PointIndex     = rec.PointIndex;
                h.HitWorldMeters = rec.VectorA;
                h.Valid          = true;
                (void)SimulationInteractionBridge::BeginSoftBodyGrab(coordinator, h);
                ++replayed;
                break;
            }
            case ReplayActionKind::MoveSoftBodyGrab:
            {
                SoftBodyGrabHandle h {};
                h.InstanceId = rec.InstanceId;
                h.PointIndex = rec.PointIndex;
                h.Valid      = true;
                (void)SimulationInteractionBridge::MoveSoftBodyGrab(coordinator, h, rec.VectorA);
                ++replayed;
                break;
            }
            case ReplayActionKind::ReleaseSoftBodyGrab:
            {
                SoftBodyGrabHandle h {};
                h.InstanceId = rec.InstanceId;
                h.PointIndex = rec.PointIndex;
                h.Valid      = true;
                SimulationInteractionBridge::ReleaseSoftBodyGrab(coordinator, h);
                ++replayed;
                break;
            }
            case ReplayActionKind::FluidBrushStroke:
            {
                (void)SimulationInteractionBridge::InjectFluidBrushStroke(
                    coordinator, rec.VectorA, rec.VectorB, rec.ScalarA, rec.ScalarB, rec.ScalarC);
                ++replayed;
                break;
            }
            case ReplayActionKind::RadialBlastImpulse:
            {
                (void)SimulationInteractionBridge::InjectRadialBlastImpulse(
                    coordinator, rec.VectorA, rec.ScalarA, rec.ScalarB, rec.ScalarC);
                ++replayed;
                break;
            }
            case ReplayActionKind::EnableSoftBody:
            case ReplayActionKind::DisableSoftBody:
            {
                if (auto* inst = coordinator.AccessDeformables().AccessWorldRegistry().FindMutable(rec.InstanceId))
                {
                    inst->Enabled = (rec.Kind == ReplayActionKind::EnableSoftBody);
                    ++replayed;
                }
                break;
            }
            case ReplayActionKind::None:
            default:
                break;
        }
    }

    if (PlaybackCursor_ >= ActionCount_)
    {
        Playing_ = false;
    }
    return replayed;
}

uint64_t SimulationReplayRecorder::ComputeTapeChecksum() const noexcept
{
    uint64_t h = kFnvOffset64;
    HashBytes(h, &InitialCheckpointHash_, sizeof(InitialCheckpointHash_));
    HashBytes(h, &ActionCount_, sizeof(ActionCount_));
    for (uint32_t i = 0u; i < ActionCount_; ++i)
    {
        const ReplayActionRecord& r = Tape_[i];
        HashBytes(h, &r.TickIndex, sizeof(r.TickIndex));
        HashBytes(h, &r.Kind, sizeof(r.Kind));
        HashBytes(h, &r.InstanceId, sizeof(r.InstanceId));
        HashBytes(h, &r.PointIndex, sizeof(r.PointIndex));
        HashBytes(h, r.VectorA.data(), sizeof(r.VectorA));
        HashBytes(h, r.VectorB.data(), sizeof(r.VectorB));
        HashBytes(h, &r.ScalarA, sizeof(r.ScalarA));
        HashBytes(h, &r.ScalarB, sizeof(r.ScalarB));
        HashBytes(h, &r.ScalarC, sizeof(r.ScalarC));
    }
    return h;
}

bool SimulationReplayRecorder::SaveToFile(const std::filesystem::path& filePath, std::string* outError) const
{
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        if (outError) *outError = "Failed to open replay file for writing: " + filePath.string();
        return false;
    }

    ReplayBinaryHeader hdr {};
    std::memcpy(hdr.Magic, kMagic, sizeof(kMagic));
    hdr.Version               = kBinaryVersion;
    hdr.ActionCount           = ActionCount_;
    hdr.Reserved              = 0u;
    hdr.InitialCheckpointHash = InitialCheckpointHash_;
    hdr.TapeChecksum          = ComputeTapeChecksum();

    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (ActionCount_ > 0u)
    {
        out.write(reinterpret_cast<const char*>(Tape_.data()),
                  static_cast<std::streamsize>(sizeof(ReplayActionRecord) * ActionCount_));
    }
    if (!out.good())
    {
        if (outError) *outError = "Stream write error while persisting replay tape.";
        return false;
    }
    return true;
}

bool SimulationReplayRecorder::LoadFromFile(const std::filesystem::path& filePath, std::string* outError)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open())
    {
        if (outError) *outError = "Failed to open replay file for reading: " + filePath.string();
        return false;
    }

    ReplayBinaryHeader hdr {};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in.good() || std::memcmp(hdr.Magic, kMagic, sizeof(kMagic)) != 0)
    {
        if (outError) *outError = "Invalid replay stream magic header.";
        return false;
    }
    if (hdr.Version != kBinaryVersion || hdr.ActionCount > kMaxRecordedActions)
    {
        if (outError) *outError = "Unsupported replay version or action count overflow.";
        return false;
    }

    std::array<ReplayActionRecord, kMaxRecordedActions> staged {};
    if (hdr.ActionCount > 0u)
    {
        in.read(reinterpret_cast<char*>(staged.data()),
                static_cast<std::streamsize>(sizeof(ReplayActionRecord) * hdr.ActionCount));
        if (!in.good())
        {
            if (outError) *outError = "Truncated replay payload.";
            return false;
        }
    }

    // Verify FNV-1a checksum before committing loaded tape
    SimulationReplayRecorder candidate {};
    candidate.InitialCheckpointHash_ = hdr.InitialCheckpointHash;
    candidate.ActionCount_           = hdr.ActionCount;
    for (uint32_t i = 0u; i < hdr.ActionCount; ++i)
    {
        candidate.Tape_[i] = staged[i];
    }
    if (candidate.ComputeTapeChecksum() != hdr.TapeChecksum)
    {
        if (outError) *outError = "Replay tape FNV-1a checksum mismatch (corrupted data).";
        return false;
    }

    *this = candidate;
    return true;
}

} // namespace Frontier::SimulationSpace
