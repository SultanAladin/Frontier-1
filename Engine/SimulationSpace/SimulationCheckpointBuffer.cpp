/*============================================================================================================================================
                                               SIMULATIONCHECKPOINTBUFFER.CPP
============================================================================================================================================*/
// 🧩 Phase H02 Implementation of SimulationCheckpointBuffer (zero steady-state allocations on ring reuse + FNV-1a binary IO)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCheckpointBuffer.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Frontier::SimulationSpace
{

namespace
{

constexpr uint32_t kCheckpointMagic   = 0x4B435346u; // 'FSCK' (Frontier Simulation Checkpoint)
constexpr uint32_t kCheckpointVersion = 1u;

void HashBytesFnv1a(uint64_t& h, const void* data, size_t byteLen) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0u; i < byteLen; ++i)
    {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ull;
    }
}

template <typename T>
void WritePod(std::ofstream& out, const T& val)
{
    out.write(reinterpret_cast<const char*>(&val), static_cast<std::streamsize>(sizeof(T)));
}

template <typename T>
bool ReadPod(std::ifstream& in, T& val)
{
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&val), static_cast<std::streamsize>(sizeof(T))));
}

} // namespace

SimulationCheckpointBuffer::SimulationCheckpointBuffer(uint32_t ringCapacity)
{
    RingSlots_.resize(std::max(1u, ringCapacity));
}

void SimulationCheckpointBuffer::Clear() noexcept
{
    for (auto& slot : RingSlots_)
    {
        slot.Occupied = false;
    }
    NextWriteSlot_ = 0u;
    OccupiedCount_ = 0u;
}

uint64_t SimulationCheckpointBuffer::ComputeCheckpointHash(const SimulationCheckpointRecord& record) noexcept
{
    uint64_t h = 14695981039346656037ull;
    HashBytesFnv1a(h, &record.CompletedTicks, sizeof(record.CompletedTicks));
    HashBytesFnv1a(h, &record.AccumulatedSeconds, sizeof(record.AccumulatedSeconds));
    HashBytesFnv1a(h, &record.FluidSimTime, sizeof(record.FluidSimTime));
    HashBytesFnv1a(h, &record.FluidStepCount, sizeof(record.FluidStepCount));
    if (!record.FluidVelocityCells.empty())
    {
        HashBytesFnv1a(h, record.FluidVelocityCells.data(), record.FluidVelocityCells.size() * sizeof(std::array<float, 4>));
    }
    if (!record.FluidScalarCells.empty())
    {
        HashBytesFnv1a(h, record.FluidScalarCells.data(), record.FluidScalarCells.size() * sizeof(std::array<float, 4>));
    }
    if (!record.FluidPressureCells.empty())
    {
        HashBytesFnv1a(h, record.FluidPressureCells.data(), record.FluidPressureCells.size() * sizeof(std::array<float, 4>));
    }
    for (const auto& body : record.DeformableBodies)
    {
        HashBytesFnv1a(h, &body.InstanceId, sizeof(body.InstanceId));
        HashBytesFnv1a(h, &body.SteppedTickCount, sizeof(body.SteppedTickCount));
        HashBytesFnv1a(h, &body.SeveredCount, sizeof(body.SeveredCount));
        if (!body.Positions.empty())            HashBytesFnv1a(h, body.Positions.data(), body.Positions.size() * sizeof(float));
        if (!body.PrevPositions.empty())        HashBytesFnv1a(h, body.PrevPositions.data(), body.PrevPositions.size() * sizeof(float));
        if (!body.Velocities.empty())           HashBytesFnv1a(h, body.Velocities.data(), body.Velocities.size() * sizeof(float));
        if (!body.PointStrain.empty())          HashBytesFnv1a(h, body.PointStrain.data(), body.PointStrain.size() * sizeof(float));
        if (!body.DistanceSeveredFlags.empty()) HashBytesFnv1a(h, body.DistanceSeveredFlags.data(), body.DistanceSeveredFlags.size());
    }
    return h;
}

void SimulationCheckpointBuffer::CaptureIntoSlot(
    const SimulationCoordinatorExtension& coordinator,
    SimulationCheckpointRecord& slot)
{
    const auto& telemetry = coordinator.InspectTelemetry();
    const auto& fluid     = coordinator.InspectFluidSimulation();

    slot.CompletedTicks      = telemetry.CompletedTicks;
    slot.AccumulatedSeconds  = telemetry.AccumulatedSeconds;
    slot.FluidSimTime        = fluid.SimulationTime;
    slot.FluidStepCount      = fluid.StepCount;
    slot.DeformableWorldTick = coordinator.InspectDeformables().InspectWorldRegistry().QueryWorldTickIndex();

    slot.FluidVelocityCells.assign(fluid.Velocity.Cells.begin(), fluid.Velocity.Cells.end());
    slot.FluidScalarCells.assign(fluid.State.Cells.begin(), fluid.State.Cells.end());
    slot.FluidPressureCells.assign(fluid.Pressure.Cells.begin(), fluid.Pressure.Cells.end());

    const auto instances = coordinator.InspectDeformables().InspectWorldRegistry().EnumerateInstances();
    slot.DeformableBodies.resize(instances.size());
    for (size_t i = 0u; i < instances.size(); ++i)
    {
        auto& dst       = slot.DeformableBodies[i];
        const auto& src = instances[i];
        dst.InstanceId       = src.InstanceId;
        dst.SteppedTickCount = src.SteppedTickCount;
        dst.SeveredCount     = src.Body.SeveredCount;
        dst.Positions.assign(src.Body.Positions.begin(), src.Body.Positions.end());
        dst.PrevPositions.assign(src.Body.PrevPositions.begin(), src.Body.PrevPositions.end());
        dst.Velocities.assign(src.Body.Velocities.begin(), src.Body.Velocities.end());
        dst.PointStrain.assign(src.Body.PointStrain.begin(), src.Body.PointStrain.end());
        dst.DistanceSeveredFlags.resize(src.Body.DistanceConstraints.size());
        for (size_t k = 0u; k < src.Body.DistanceConstraints.size(); ++k)
        {
            dst.DistanceSeveredFlags[k] = src.Body.DistanceConstraints[k].Severed ? 1u : 0u;
        }
    }

    slot.ContentHashFnv1a = ComputeCheckpointHash(slot);
    slot.Occupied         = true;
}

bool SimulationCheckpointBuffer::RestoreFromSlot(
    const SimulationCheckpointRecord& slot,
    SimulationCoordinatorExtension& coordinator)
{
    if (!slot.Occupied)
    {
        return false;
    }

    auto& fluid = coordinator.AccessFluidSimulation();
    if (fluid.Velocity.Cells.size() == slot.FluidVelocityCells.size())
    {
        std::copy(slot.FluidVelocityCells.begin(), slot.FluidVelocityCells.end(), fluid.Velocity.Cells.begin());
    }
    if (fluid.State.Cells.size() == slot.FluidScalarCells.size())
    {
        std::copy(slot.FluidScalarCells.begin(), slot.FluidScalarCells.end(), fluid.State.Cells.begin());
    }
    if (fluid.Pressure.Cells.size() == slot.FluidPressureCells.size())
    {
        std::copy(slot.FluidPressureCells.begin(), slot.FluidPressureCells.end(), fluid.Pressure.Cells.begin());
    }
    fluid.SimulationTime = slot.FluidSimTime;
    fluid.StepCount      = slot.FluidStepCount;

    auto& worldReg = coordinator.AccessDeformables().AccessWorldRegistry();
    for (const auto& savedBody : slot.DeformableBodies)
    {
        auto* inst = worldReg.FindMutable(savedBody.InstanceId);
        if (inst == nullptr) continue;
        inst->SteppedTickCount  = savedBody.SteppedTickCount;
        inst->Body.SeveredCount = savedBody.SeveredCount;
        if (inst->Body.Positions.size() == savedBody.Positions.size())
        {
            std::copy(savedBody.Positions.begin(), savedBody.Positions.end(), inst->Body.Positions.begin());
        }
        if (inst->Body.PrevPositions.size() == savedBody.PrevPositions.size())
        {
            std::copy(savedBody.PrevPositions.begin(), savedBody.PrevPositions.end(), inst->Body.PrevPositions.begin());
        }
        if (inst->Body.Velocities.size() == savedBody.Velocities.size())
        {
            std::copy(savedBody.Velocities.begin(), savedBody.Velocities.end(), inst->Body.Velocities.begin());
        }
        if (inst->Body.PointStrain.size() == savedBody.PointStrain.size())
        {
            std::copy(savedBody.PointStrain.begin(), savedBody.PointStrain.end(), inst->Body.PointStrain.begin());
        }
        if (inst->Body.DistanceConstraints.size() == savedBody.DistanceSeveredFlags.size())
        {
            for (size_t k = 0u; k < savedBody.DistanceSeveredFlags.size(); ++k)
            {
                inst->Body.DistanceConstraints[k].Severed = (savedBody.DistanceSeveredFlags[k] != 0u);
            }
        }
    }

    coordinator.RestoreClockState(slot.CompletedTicks, slot.AccumulatedSeconds);
    coordinator.AccessDeformables().AccessWorldRegistry().RestoreWorldTickIndex(slot.DeformableWorldTick);
    return true;
}

void SimulationCheckpointBuffer::PrewarmForCoordinator(const SimulationCoordinatorExtension& coordinator)
{
    for (auto& slot : RingSlots_)
    {
        CaptureIntoSlot(coordinator, slot);
        slot.Occupied = false;
    }
    NextWriteSlot_ = 0u;
    OccupiedCount_ = 0u;
}

uint64_t SimulationCheckpointBuffer::Capture(const SimulationCoordinatorExtension& coordinator)
{
    const uint32_t cap = static_cast<uint32_t>(RingSlots_.size());
    SimulationCheckpointRecord& slot = RingSlots_[NextWriteSlot_];
    CaptureIntoSlot(coordinator, slot);
    NextWriteSlot_ = (NextWriteSlot_ + 1u) % cap;
    if (OccupiedCount_ < cap) ++OccupiedCount_;
    return slot.ContentHashFnv1a;
}

const SimulationCheckpointRecord* SimulationCheckpointBuffer::InspectByOrdinal(uint32_t chronologicalOrdinal) const noexcept
{
    if (chronologicalOrdinal >= OccupiedCount_ || RingSlots_.empty())
    {
        return nullptr;
    }
    const uint32_t cap        = static_cast<uint32_t>(RingSlots_.size());
    const uint32_t oldestSlot = (OccupiedCount_ < cap) ? 0u : NextWriteSlot_;
    const uint32_t targetSlot = (oldestSlot + chronologicalOrdinal) % cap;
    return &RingSlots_[targetSlot];
}

bool SimulationCheckpointBuffer::RestoreByOrdinal(
    uint32_t chronologicalOrdinal,
    SimulationCoordinatorExtension& coordinator) const
{
    const SimulationCheckpointRecord* slot = InspectByOrdinal(chronologicalOrdinal);
    return (slot != nullptr) && RestoreFromSlot(*slot, coordinator);
}

bool SimulationCheckpointBuffer::RestoreLatest(SimulationCoordinatorExtension& coordinator) const
{
    if (OccupiedCount_ == 0u) return false;
    return RestoreByOrdinal(OccupiedCount_ - 1u, coordinator);
}

bool SimulationCheckpointBuffer::WriteCheckpointFile(
    const SimulationCheckpointRecord& record,
    const std::filesystem::path& filePath,
    std::string* outError)
{
    if (!record.Occupied)
    {
        if (outError != nullptr) *outError = "Cannot write unoccupied simulation checkpoint.";
        return false;
    }
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (outError != nullptr) *outError = "Failed to open checkpoint output file.";
        return false;
    }

    const uint64_t expectedHash = ComputeCheckpointHash(record);
    WritePod(out, kCheckpointMagic);
    WritePod(out, kCheckpointVersion);
    WritePod(out, expectedHash);
    WritePod(out, record.CompletedTicks);
    WritePod(out, record.AccumulatedSeconds);
    WritePod(out, record.FluidSimTime);
    WritePod(out, record.FluidStepCount);

    auto writeVec4 = [&](const std::vector<std::array<float, 4>>& v)
    {
        const uint32_t n = static_cast<uint32_t>(v.size());
        WritePod(out, n);
        if (n > 0u) out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(n * sizeof(std::array<float, 4>)));
    };
    writeVec4(record.FluidVelocityCells);
    writeVec4(record.FluidScalarCells);
    writeVec4(record.FluidPressureCells);

    const uint32_t bodyCount = static_cast<uint32_t>(record.DeformableBodies.size());
    WritePod(out, bodyCount);
    for (const auto& body : record.DeformableBodies)
    {
        WritePod(out, body.InstanceId);
        WritePod(out, body.SteppedTickCount);
        WritePod(out, body.SeveredCount);
        auto writeF = [&](const std::vector<float>& vf)
        {
            const uint32_t n = static_cast<uint32_t>(vf.size());
            WritePod(out, n);
            if (n > 0u) out.write(reinterpret_cast<const char*>(vf.data()), static_cast<std::streamsize>(n * sizeof(float)));
        };
        writeF(body.Positions);
        writeF(body.PrevPositions);
        writeF(body.Velocities);
        writeF(body.PointStrain);
        const uint32_t sevN = static_cast<uint32_t>(body.DistanceSeveredFlags.size());
        WritePod(out, sevN);
        if (sevN > 0u) out.write(reinterpret_cast<const char*>(body.DistanceSeveredFlags.data()), static_cast<std::streamsize>(sevN));
    }

    return static_cast<bool>(out);
}

bool SimulationCheckpointBuffer::ReadCheckpointFile(
    const std::filesystem::path& filePath,
    SimulationCheckpointRecord& outRecord,
    std::string* outError)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in)
    {
        if (outError != nullptr) *outError = "Failed to open checkpoint input file.";
        return false;
    }

    uint32_t magic = 0u, version = 0u;
    uint64_t storedHash = 0ull;
    if (!ReadPod(in, magic) || !ReadPod(in, version) || !ReadPod(in, storedHash) ||
        magic != kCheckpointMagic || version != kCheckpointVersion)
    {
        if (outError != nullptr) *outError = "Invalid simulation checkpoint header or version.";
        return false;
    }

    SimulationCheckpointRecord rec {};
    if (!ReadPod(in, rec.CompletedTicks) || !ReadPod(in, rec.AccumulatedSeconds) ||
        !ReadPod(in, rec.FluidSimTime) || !ReadPod(in, rec.FluidStepCount))
    {
        if (outError != nullptr) *outError = "Truncated simulation checkpoint timing block.";
        return false;
    }

    auto readVec4 = [&](std::vector<std::array<float, 4>>& v) -> bool
    {
        uint32_t n = 0u;
        if (!ReadPod(in, n) || n > 16777216u) return false;
        v.resize(n);
        if (n == 0u) return true;
        return static_cast<bool>(in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(std::array<float, 4>))));
    };
    if (!readVec4(rec.FluidVelocityCells) || !readVec4(rec.FluidScalarCells) || !readVec4(rec.FluidPressureCells))
    {
        if (outError != nullptr) *outError = "Truncated simulation checkpoint fluid payload.";
        return false;
    }

    uint32_t bodyCount = 0u;
    if (!ReadPod(in, bodyCount) || bodyCount > 4096u)
    {
        if (outError != nullptr) *outError = "Invalid deformable body count in checkpoint.";
        return false;
    }
    rec.DeformableBodies.resize(bodyCount);
    for (uint32_t b = 0u; b < bodyCount; ++b)
    {
        auto& body = rec.DeformableBodies[b];
        if (!ReadPod(in, body.InstanceId) || !ReadPod(in, body.SteppedTickCount) || !ReadPod(in, body.SeveredCount)) return false;
        auto readF = [&](std::vector<float>& vf) -> bool
        {
            uint32_t n = 0u;
            if (!ReadPod(in, n) || n > 16777216u) return false;
            vf.resize(n);
            if (n == 0u) return true;
            return static_cast<bool>(in.read(reinterpret_cast<char*>(vf.data()), static_cast<std::streamsize>(n * sizeof(float))));
        };
        if (!readF(body.Positions) || !readF(body.PrevPositions) || !readF(body.Velocities) || !readF(body.PointStrain))
        {
            if (outError != nullptr) *outError = "Truncated deformable body payload in checkpoint.";
            return false;
        }
        uint32_t sevN = 0u;
        if (!ReadPod(in, sevN) || sevN > 16777216u) return false;
        body.DistanceSeveredFlags.resize(sevN);
        if (sevN > 0u && !in.read(reinterpret_cast<char*>(body.DistanceSeveredFlags.data()), static_cast<std::streamsize>(sevN)))
        {
            if (outError != nullptr) *outError = "Truncated deformable tear flags in checkpoint.";
            return false;
        }
    }

    const uint64_t computedHash = ComputeCheckpointHash(rec);
    if (computedHash != storedHash)
    {
        if (outError != nullptr) *outError = "FNV-1a integrity mismatch in simulation checkpoint file.";
        return false;
    }

    rec.ContentHashFnv1a = computedHash;
    rec.Occupied         = true;
    outRecord            = std::move(rec);
    return true;
}

} // namespace Frontier::SimulationSpace
