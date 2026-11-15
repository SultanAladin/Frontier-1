/*============================================================================================================================================
                                                         DIFFERENTIALWORLDENCODER.CPP
============================================================================================================================================*/
// 🧩 Sector-relative 24-bit XYZ quantization, smallest-three 32-bit quaternions, baseline delta codec, slerp, and 64-bit alignment digest

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/DifferentialWorldEncoder.h"
#include "Engine/NetworkingSpace/PacketSerializer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::NetworkingSpace
{

namespace
{

constexpr float InverseSqrtTwo = 0.70710678118654752440f;

void AppendSigned24(std::vector<uint8_t>& Buffer, int32_t Signed24)
{
    const uint32_t Magnitude = static_cast<uint32_t>(std::clamp(Signed24, -8388607, 8388607) + 8388607);
    Buffer.push_back(static_cast<uint8_t>(Magnitude & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Magnitude >> 8u) & 0xFFu));
    Buffer.push_back(static_cast<uint8_t>((Magnitude >> 16u) & 0xFFu));
}

[[nodiscard]] bool ReadSigned24(std::span<const uint8_t> Source, std::size_t& Cursor, int32_t& OutSigned24) noexcept
{
    if (Cursor + 3u > Source.size())
    {
        return false;
    }
    const uint32_t Magnitude = static_cast<uint32_t>(Source[Cursor]) |
                               (static_cast<uint32_t>(Source[Cursor + 1u]) << 8u) |
                               (static_cast<uint32_t>(Source[Cursor + 2u]) << 16u);
    Cursor += 3u;
    if (Magnitude > 16777214u)
    {
        return false;
    }
    OutSigned24 = static_cast<int32_t>(Magnitude) - 8388607;
    return true;
}

[[nodiscard]] int16_t QuantizeSymmetric16(float Quantity, float MaxMagnitude) noexcept
{
    const float Normalized = std::clamp(Quantity / MaxMagnitude, -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(Normalized * 32767.0f));
}

[[nodiscard]] float DequantizeSymmetric16(int16_t Code, float MaxMagnitude) noexcept
{
    return (static_cast<float>(Code) / 32767.0f) * MaxMagnitude;
}

[[nodiscard]] UnitQuaternion NormalizeQuaternion(const UnitQuaternion& Input) noexcept
{
    const float LengthSq = Input.X * Input.X + Input.Y * Input.Y + Input.Z * Input.Z + Input.W * Input.W;
    if (LengthSq <= 1.0e-12f)
    {
        return UnitQuaternion{ 0.0f, 0.0f, 0.0f, 1.0f };
    }
    const float InvLen = 1.0f / std::sqrt(LengthSq);
    return UnitQuaternion{ Input.X * InvLen, Input.Y * InvLen, Input.Z * InvLen, Input.W * InvLen };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             QUATERNION & SPATIAL QUANTIZATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t PackSmallestThreeQuaternion(const UnitQuaternion& Orientation) noexcept
{
    const UnitQuaternion Unit = NormalizeQuaternion(Orientation);
    std::array<float, 4u> Components = { Unit.X, Unit.Y, Unit.Z, Unit.W };

    uint32_t LargestIndex = 0u;
    float    LargestAbs   = std::fabs(Components[0]);
    for (uint32_t Index = 1u; Index < 4u; ++Index)
    {
        const float CandidateAbs = std::fabs(Components[Index]);
        if (CandidateAbs > LargestAbs)
        {
            LargestAbs   = CandidateAbs;
            LargestIndex = Index;
        }
    }

    if (Components[LargestIndex] < 0.0f)
    {
        for (float& Component : Components)
        {
            Component = -Component;
        }
    }

    std::array<uint32_t, 3u> Codes{};
    uint32_t WriteSlot = 0u;
    for (uint32_t Index = 0u; Index < 4u; ++Index)
    {
        if (Index == LargestIndex)
        {
            continue;
        }
        const float Clamped    = std::clamp(Components[Index], -InverseSqrtTwo, InverseSqrtTwo);
        const float Normalized = (Clamped + InverseSqrtTwo) / (2.0f * InverseSqrtTwo);
        Codes[WriteSlot++]     = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::lround(Normalized * 1023.0f)), 0, 1023));
    }

    return (LargestIndex << 30u) | (Codes[0] << 20u) | (Codes[1] << 10u) | Codes[2];
}

UnitQuaternion UnpackSmallestThreeQuaternion(uint32_t PackedQuaternion) noexcept
{
    const uint32_t LargestIndex = (PackedQuaternion >> 30u) & 0x03u;
    const std::array<uint32_t, 3u> Codes = {
        (PackedQuaternion >> 20u) & 0x3FFu,
        (PackedQuaternion >> 10u) & 0x3FFu,
        PackedQuaternion & 0x3FFu
    };

    std::array<float, 4u> Components{};
    float SumSquares   = 0.0f;
    uint32_t ReadSlot  = 0u;
    for (uint32_t Index = 0u; Index < 4u; ++Index)
    {
        if (Index == LargestIndex)
        {
            continue;
        }
        const float Reconstructed = (static_cast<float>(Codes[ReadSlot++]) / 1023.0f) * (2.0f * InverseSqrtTwo) - InverseSqrtTwo;
        Components[Index]         = Reconstructed;
        SumSquares               += Reconstructed * Reconstructed;
    }

    Components[LargestIndex] = std::sqrt(std::max(0.0f, 1.0f - SumSquares));
    return NormalizeQuaternion(UnitQuaternion{ Components[0], Components[1], Components[2], Components[3] });
}

UnitQuaternion SlerpUnitQuaternion(const UnitQuaternion& Start, const UnitQuaternion& Target, float Alpha) noexcept
{
    const float ClampedAlpha = std::clamp(Alpha, 0.0f, 1.0f);
    const UnitQuaternion Left = NormalizeQuaternion(Start);
    UnitQuaternion Right      = NormalizeQuaternion(Target);

    float Cosine = Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z + Left.W * Right.W;
    if (Cosine < 0.0f)
    {
        Cosine  = -Cosine;
        Right.X = -Right.X;
        Right.Y = -Right.Y;
        Right.Z = -Right.Z;
        Right.W = -Right.W;
    }

    if (Cosine > 0.9995f)
    {
        return NormalizeQuaternion(UnitQuaternion{
            Left.X + ClampedAlpha * (Right.X - Left.X),
            Left.Y + ClampedAlpha * (Right.Y - Left.Y),
            Left.Z + ClampedAlpha * (Right.Z - Left.Z),
            Left.W + ClampedAlpha * (Right.W - Left.W)
        });
    }

    const float Theta    = std::acos(std::clamp(Cosine, -1.0f, 1.0f));
    const float SinTheta = std::sin(Theta);
    const float WeightA  = std::sin((1.0f - ClampedAlpha) * Theta) / SinTheta;
    const float WeightB  = std::sin(ClampedAlpha * Theta) / SinTheta;

    return NormalizeQuaternion(UnitQuaternion{
        WeightA * Left.X + WeightB * Right.X,
        WeightA * Left.Y + WeightB * Right.Y,
        WeightA * Left.Z + WeightB * Right.Z,
        WeightA * Left.W + WeightB * Right.W
    });
}

QuantizedSpatialRecord QuantizeEntityKinematics(const ReplicatedEntityKinematics& Entity) noexcept
{
    QuantizedSpatialRecord Record{};
    Record.EntityIdentifier = Entity.EntityIdentifier;

    const std::array<double, 3u> WorldCoords = {
        static_cast<double>(Entity.PositionMeters.X),
        static_cast<double>(Entity.PositionMeters.Y),
        static_cast<double>(Entity.PositionMeters.Z)
    };

    const double Span     = static_cast<double>(WorldSectorSpanMeters);
    const double HalfSpan = Span * 0.5;

    for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
    {
        const double ClampedWorld = std::clamp(WorldCoords[Axis], -32767.0 * Span, 32767.0 * Span);
        const int16_t Sector      = static_cast<int16_t>(std::floor((ClampedWorld + HalfSpan) / Span));
        const double LocalMeters  = std::clamp(ClampedWorld - static_cast<double>(Sector) * Span, -HalfSpan, HalfSpan);
        const int32_t Signed24    = static_cast<int32_t>(std::llround((LocalMeters / HalfSpan) * 8388607.0));

        Record.SectorIndices[Axis] = Sector;
        Record.LocalSigned24[Axis] = std::clamp(Signed24, -8388607, 8388607);
    }

    Record.SmallestThreeQuaternion = PackSmallestThreeQuaternion(Entity.Orientation);

    Record.LinearVelocitySigned16[0]  = QuantizeSymmetric16(Entity.LinearVelocityMetersPerSec.X, MaxQuantizedLinearSpeedMps);
    Record.LinearVelocitySigned16[1]  = QuantizeSymmetric16(Entity.LinearVelocityMetersPerSec.Y, MaxQuantizedLinearSpeedMps);
    Record.LinearVelocitySigned16[2]  = QuantizeSymmetric16(Entity.LinearVelocityMetersPerSec.Z, MaxQuantizedLinearSpeedMps);

    Record.AngularVelocitySigned16[0] = QuantizeSymmetric16(Entity.AngularVelocityRadPerSec.X, MaxQuantizedAngularSpeedRadPs);
    Record.AngularVelocitySigned16[1] = QuantizeSymmetric16(Entity.AngularVelocityRadPerSec.Y, MaxQuantizedAngularSpeedRadPs);
    Record.AngularVelocitySigned16[2] = QuantizeSymmetric16(Entity.AngularVelocityRadPerSec.Z, MaxQuantizedAngularSpeedRadPs);

    return Record;
}

ReplicatedEntityKinematics DequantizeEntityKinematics(const QuantizedSpatialRecord& Quantized) noexcept
{
    ReplicatedEntityKinematics Entity{};
    Entity.EntityIdentifier = Quantized.EntityIdentifier;

    const double Span     = static_cast<double>(WorldSectorSpanMeters);
    const double HalfSpan = Span * 0.5;

    auto ReconstructAxis = [&](std::size_t Axis) noexcept -> float
    {
        const double SectorOriginMeters = static_cast<double>(Quantized.SectorIndices[Axis]) * Span;
        const double LocalMeters        = (static_cast<double>(Quantized.LocalSigned24[Axis]) / 8388607.0) * HalfSpan;
        return static_cast<float>(SectorOriginMeters + LocalMeters);
    };

    Entity.PositionMeters.X = ReconstructAxis(0u);
    Entity.PositionMeters.Y = ReconstructAxis(1u);
    Entity.PositionMeters.Z = ReconstructAxis(2u);
    Entity.Orientation      = UnpackSmallestThreeQuaternion(Quantized.SmallestThreeQuaternion);

    Entity.LinearVelocityMetersPerSec.X = DequantizeSymmetric16(Quantized.LinearVelocitySigned16[0], MaxQuantizedLinearSpeedMps);
    Entity.LinearVelocityMetersPerSec.Y = DequantizeSymmetric16(Quantized.LinearVelocitySigned16[1], MaxQuantizedLinearSpeedMps);
    Entity.LinearVelocityMetersPerSec.Z = DequantizeSymmetric16(Quantized.LinearVelocitySigned16[2], MaxQuantizedLinearSpeedMps);

    Entity.AngularVelocityRadPerSec.X = DequantizeSymmetric16(Quantized.AngularVelocitySigned16[0], MaxQuantizedAngularSpeedRadPs);
    Entity.AngularVelocityRadPerSec.Y = DequantizeSymmetric16(Quantized.AngularVelocitySigned16[1], MaxQuantizedAngularSpeedRadPs);
    Entity.AngularVelocityRadPerSec.Z = DequantizeSymmetric16(Quantized.AngularVelocitySigned16[2], MaxQuantizedAngularSpeedRadPs);

    return Entity;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           DIFFERENTIAL SNAPSHOT ENCODING
//------------------------------------------------------------------------------------------------------------------------

bool EncodeWorldSnapshotDelta(
    uint32_t                                    BaselineTick,
    std::span<const ReplicatedEntityKinematics> BaselineEntities,
    uint32_t                                    TargetTick,
    std::span<const ReplicatedEntityKinematics> TargetEntities,
    std::vector<uint8_t>&                       OutDeltaBytes,
    uint32_t&                                   OutChangedEntityCount)
{
    OutDeltaBytes.clear();
    OutChangedEntityCount = 0u;

    if (TargetEntities.size() > 65535u || BaselineEntities.size() > 65535u)
    {
        return false;
    }

    std::vector<QuantizedSpatialRecord> BaselineQuantized;
    BaselineQuantized.reserve(BaselineEntities.size());
    for (const ReplicatedEntityKinematics& Entity : BaselineEntities)
    {
        BaselineQuantized.push_back(QuantizeEntityKinematics(Entity));
    }

    std::vector<QuantizedSpatialRecord> ChangedRecords;
    for (const ReplicatedEntityKinematics& TargetEntity : TargetEntities)
    {
        const QuantizedSpatialRecord Candidate = QuantizeEntityKinematics(TargetEntity);
        bool UnchangedInBaseline               = false;
        for (const QuantizedSpatialRecord& PriorRecord : BaselineQuantized)
        {
            if (PriorRecord.EntityIdentifier == Candidate.EntityIdentifier)
            {
                UnchangedInBaseline = (PriorRecord == Candidate);
                break;
            }
        }
        if (!UnchangedInBaseline)
        {
            ChangedRecords.push_back(Candidate);
        }
    }

    std::vector<uint32_t> RemovedIdentifiers;
    for (const QuantizedSpatialRecord& PriorRecord : BaselineQuantized)
    {
        bool PresentInTarget = false;
        for (const ReplicatedEntityKinematics& TargetEntity : TargetEntities)
        {
            if (TargetEntity.EntityIdentifier == PriorRecord.EntityIdentifier)
            {
                PresentInTarget = true;
                break;
            }
        }
        if (!PresentInTarget)
        {
            RemovedIdentifiers.push_back(PriorRecord.EntityIdentifier);
        }
    }

    // Wire layout: 'F','W','D','1' + BaselineTick32 + TargetTick32 + ChangedCount16 + RemovedCount16 + Records + RemovedIds
    OutDeltaBytes.push_back(static_cast<uint8_t>('F'));
    OutDeltaBytes.push_back(static_cast<uint8_t>('W'));
    OutDeltaBytes.push_back(static_cast<uint8_t>('D'));
    OutDeltaBytes.push_back(static_cast<uint8_t>('1'));
    AppendLittleEndian32(OutDeltaBytes, BaselineTick);
    AppendLittleEndian32(OutDeltaBytes, TargetTick);
    AppendLittleEndian16(OutDeltaBytes, static_cast<uint16_t>(ChangedRecords.size()));
    AppendLittleEndian16(OutDeltaBytes, static_cast<uint16_t>(RemovedIdentifiers.size()));

    // Each changed entity occupies exact 35 bytes (4B id + 6B sector + 9B local24 + 4B quat + 6B linVel + 6B angVel)
    for (const QuantizedSpatialRecord& Record : ChangedRecords)
    {
        AppendLittleEndian32(OutDeltaBytes, Record.EntityIdentifier);
        for (int16_t Sector : Record.SectorIndices)
        {
            AppendLittleEndian16(OutDeltaBytes, static_cast<uint16_t>(Sector));
        }
        for (int32_t Local24 : Record.LocalSigned24)
        {
            AppendSigned24(OutDeltaBytes, Local24);
        }
        AppendLittleEndian32(OutDeltaBytes, Record.SmallestThreeQuaternion);
        for (int16_t LinearVel : Record.LinearVelocitySigned16)
        {
            AppendLittleEndian16(OutDeltaBytes, static_cast<uint16_t>(LinearVel));
        }
        for (int16_t AngularVel : Record.AngularVelocitySigned16)
        {
            AppendLittleEndian16(OutDeltaBytes, static_cast<uint16_t>(AngularVel));
        }
    }

    for (uint32_t RemovedId : RemovedIdentifiers)
    {
        AppendLittleEndian32(OutDeltaBytes, RemovedId);
    }

    OutChangedEntityCount = static_cast<uint32_t>(ChangedRecords.size());
    return true;
}

bool DecodeWorldSnapshotDelta(
    std::span<const ReplicatedEntityKinematics> BaselineEntities,
    std::span<const uint8_t>                    DeltaBytes,
    uint32_t&                                   OutBaselineTick,
    uint32_t&                                   OutTargetTick,
    std::vector<ReplicatedEntityKinematics>&    OutReconstructedEntities)
{
    if (DeltaBytes.size() < 16u ||
        DeltaBytes[0] != static_cast<uint8_t>('F') ||
        DeltaBytes[1] != static_cast<uint8_t>('W') ||
        DeltaBytes[2] != static_cast<uint8_t>('D') ||
        DeltaBytes[3] != static_cast<uint8_t>('1'))
    {
        return false;
    }

    std::size_t Cursor    = 4u;
    uint16_t ChangedCount = 0u;
    uint16_t RemovedCount = 0u;
    if (!ReadLittleEndian32(DeltaBytes, Cursor, OutBaselineTick) ||
        !ReadLittleEndian32(DeltaBytes, Cursor, OutTargetTick) ||
        !ReadLittleEndian16(DeltaBytes, Cursor, ChangedCount) ||
        !ReadLittleEndian16(DeltaBytes, Cursor, RemovedCount))
    {
        return false;
    }

    // Seed reconstructed list with quantized-canonicalized baseline entities so host & client bit-align identically
    OutReconstructedEntities.clear();
    OutReconstructedEntities.reserve(BaselineEntities.size() + ChangedCount);
    for (const ReplicatedEntityKinematics& PriorEntity : BaselineEntities)
    {
        OutReconstructedEntities.push_back(DequantizeEntityKinematics(QuantizeEntityKinematics(PriorEntity)));
    }

    for (uint16_t Index = 0u; Index < ChangedCount; ++Index)
    {
        QuantizedSpatialRecord Record{};
        if (!ReadLittleEndian32(DeltaBytes, Cursor, Record.EntityIdentifier))
        {
            return false;
        }
        for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
        {
            uint16_t RawSector = 0u;
            if (!ReadLittleEndian16(DeltaBytes, Cursor, RawSector))
            {
                return false;
            }
            Record.SectorIndices[Axis] = static_cast<int16_t>(RawSector);
        }
        for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
        {
            if (!ReadSigned24(DeltaBytes, Cursor, Record.LocalSigned24[Axis]))
            {
                return false;
            }
        }
        if (!ReadLittleEndian32(DeltaBytes, Cursor, Record.SmallestThreeQuaternion))
        {
            return false;
        }
        for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
        {
            uint16_t RawVel = 0u;
            if (!ReadLittleEndian16(DeltaBytes, Cursor, RawVel))
            {
                return false;
            }
            Record.LinearVelocitySigned16[Axis] = static_cast<int16_t>(RawVel);
        }
        for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
        {
            uint16_t RawAng = 0u;
            if (!ReadLittleEndian16(DeltaBytes, Cursor, RawAng))
            {
                return false;
            }
            Record.AngularVelocitySigned16[Axis] = static_cast<int16_t>(RawAng);
        }

        const ReplicatedEntityKinematics Reconstructed = DequantizeEntityKinematics(Record);
        bool UpdatedExisting = false;
        for (ReplicatedEntityKinematics& Existing : OutReconstructedEntities)
        {
            if (Existing.EntityIdentifier == Reconstructed.EntityIdentifier)
            {
                Existing        = Reconstructed;
                UpdatedExisting = true;
                break;
            }
        }
        if (!UpdatedExisting)
        {
            OutReconstructedEntities.push_back(Reconstructed);
        }
    }

    for (uint16_t Index = 0u; Index < RemovedCount; ++Index)
    {
        uint32_t RemovedId = 0u;
        if (!ReadLittleEndian32(DeltaBytes, Cursor, RemovedId))
        {
            return false;
        }
        std::erase_if(OutReconstructedEntities, [RemovedId](const ReplicatedEntityKinematics& Entry) noexcept
        {
            return Entry.EntityIdentifier == RemovedId;
        });
    }

    if (Cursor != DeltaBytes.size())
    {
        return false;
    }

    std::sort(OutReconstructedEntities.begin(), OutReconstructedEntities.end(),
        [](const ReplicatedEntityKinematics& Left, const ReplicatedEntityKinematics& Right) noexcept
        {
            return Left.EntityIdentifier < Right.EntityIdentifier;
        });
    return true;
}

uint64_t ComputeWorldAlignmentDigest(std::span<const ReplicatedEntityKinematics> Entities) noexcept
{
    std::vector<QuantizedSpatialRecord> SortedRecords;
    SortedRecords.reserve(Entities.size());
    for (const ReplicatedEntityKinematics& Entity : Entities)
    {
        SortedRecords.push_back(QuantizeEntityKinematics(Entity));
    }
    std::sort(SortedRecords.begin(), SortedRecords.end(),
        [](const QuantizedSpatialRecord& Left, const QuantizedSpatialRecord& Right) noexcept
        {
            return Left.EntityIdentifier < Right.EntityIdentifier;
        });

    uint64_t Digest = 14695981039346656037ull;
    auto MixByte = [&Digest](uint8_t Byte) noexcept
    {
        Digest ^= static_cast<uint64_t>(Byte);
        Digest *= 1099511628211ull;
    };
    auto MixUint32 = [&MixByte](uint32_t Word) noexcept
    {
        MixByte(static_cast<uint8_t>(Word & 0xFFu));
        MixByte(static_cast<uint8_t>((Word >> 8u) & 0xFFu));
        MixByte(static_cast<uint8_t>((Word >> 16u) & 0xFFu));
        MixByte(static_cast<uint8_t>((Word >> 24u) & 0xFFu));
    };

    for (const QuantizedSpatialRecord& Record : SortedRecords)
    {
        MixUint32(Record.EntityIdentifier);
        for (int16_t Sector : Record.SectorIndices) MixUint32(static_cast<uint16_t>(Sector));
        for (int32_t Local24 : Record.LocalSigned24) MixUint32(static_cast<uint32_t>(Local24));
        MixUint32(Record.SmallestThreeQuaternion);
        for (int16_t Vel : Record.LinearVelocitySigned16) MixUint32(static_cast<uint16_t>(Vel));
        for (int16_t Ang : Record.AngularVelocitySigned16) MixUint32(static_cast<uint16_t>(Ang));
    }

    return Digest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SPATIAL INTERPOLATION WINDOW
//------------------------------------------------------------------------------------------------------------------------

void SpatialInterpolationWindow::PushAuthoritativeSnapshot(const WorldSnapshotView& Snapshot)
{
    if (!History.empty() && Snapshot.TimestampSeconds <= History.back().TimestampSeconds)
    {
        return;
    }
    if (History.size() >= MaxSnapshotHistoryDepth)
    {
        History.erase(History.begin());
    }
    History.push_back(Snapshot);
}

void SpatialInterpolationWindow::ClearHistory() noexcept
{
    History.clear();
}

bool SpatialInterpolationWindow::SampleInterpolatedEntities(
    double                                   RenderTimestampSeconds,
    std::vector<ReplicatedEntityKinematics>& OutInterpolatedEntities) const
{
    OutInterpolatedEntities.clear();
    if (History.empty())
    {
        return false;
    }
    if (RenderTimestampSeconds <= History.front().TimestampSeconds || History.size() == 1u)
    {
        OutInterpolatedEntities = History.front().Entities;
        return true;
    }
    if (RenderTimestampSeconds >= History.back().TimestampSeconds)
    {
        OutInterpolatedEntities = History.back().Entities;
        return true;
    }

    std::size_t UpperIndex = 1u;
    while (UpperIndex < History.size() && History[UpperIndex].TimestampSeconds < RenderTimestampSeconds)
    {
        ++UpperIndex;
    }
    const WorldSnapshotView& Older = History[UpperIndex - 1u];
    const WorldSnapshotView& Newer = History[UpperIndex];
    const double SpanSeconds       = Newer.TimestampSeconds - Older.TimestampSeconds;
    const float  Alpha             = (SpanSeconds > 1.0e-9)
                                         ? static_cast<float>((RenderTimestampSeconds - Older.TimestampSeconds) / SpanSeconds)
                                         : 1.0f;

    OutInterpolatedEntities.reserve(Newer.Entities.size());
    for (const ReplicatedEntityKinematics& TargetEntity : Newer.Entities)
    {
        const ReplicatedEntityKinematics* PriorMatch = nullptr;
        for (const ReplicatedEntityKinematics& Candidate : Older.Entities)
        {
            if (Candidate.EntityIdentifier == TargetEntity.EntityIdentifier)
            {
                PriorMatch = &Candidate;
                break;
            }
        }

        if (PriorMatch == nullptr)
        {
            OutInterpolatedEntities.push_back(TargetEntity);
            continue;
        }

        ReplicatedEntityKinematics Blended{};
        Blended.EntityIdentifier     = TargetEntity.EntityIdentifier;
        Blended.PositionMeters.X     = PriorMatch->PositionMeters.X + Alpha * (TargetEntity.PositionMeters.X - PriorMatch->PositionMeters.X);
        Blended.PositionMeters.Y     = PriorMatch->PositionMeters.Y + Alpha * (TargetEntity.PositionMeters.Y - PriorMatch->PositionMeters.Y);
        Blended.PositionMeters.Z     = PriorMatch->PositionMeters.Z + Alpha * (TargetEntity.PositionMeters.Z - PriorMatch->PositionMeters.Z);
        Blended.Orientation          = SlerpUnitQuaternion(PriorMatch->Orientation, TargetEntity.Orientation, Alpha);
        Blended.LinearVelocityMetersPerSec.X = PriorMatch->LinearVelocityMetersPerSec.X + Alpha * (TargetEntity.LinearVelocityMetersPerSec.X - PriorMatch->LinearVelocityMetersPerSec.X);
        Blended.LinearVelocityMetersPerSec.Y = PriorMatch->LinearVelocityMetersPerSec.Y + Alpha * (TargetEntity.LinearVelocityMetersPerSec.Y - PriorMatch->LinearVelocityMetersPerSec.Y);
        Blended.LinearVelocityMetersPerSec.Z = PriorMatch->LinearVelocityMetersPerSec.Z + Alpha * (TargetEntity.LinearVelocityMetersPerSec.Z - PriorMatch->LinearVelocityMetersPerSec.Z);
        Blended.AngularVelocityRadPerSec.X   = PriorMatch->AngularVelocityRadPerSec.X + Alpha * (TargetEntity.AngularVelocityRadPerSec.X - PriorMatch->AngularVelocityRadPerSec.X);
        Blended.AngularVelocityRadPerSec.Y   = PriorMatch->AngularVelocityRadPerSec.Y + Alpha * (TargetEntity.AngularVelocityRadPerSec.Y - PriorMatch->AngularVelocityRadPerSec.Y);
        Blended.AngularVelocityRadPerSec.Z   = PriorMatch->AngularVelocityRadPerSec.Z + Alpha * (TargetEntity.AngularVelocityRadPerSec.Z - PriorMatch->AngularVelocityRadPerSec.Z);
        OutInterpolatedEntities.push_back(Blended);
    }

    return true;
}

} // namespace Frontier::NetworkingSpace
