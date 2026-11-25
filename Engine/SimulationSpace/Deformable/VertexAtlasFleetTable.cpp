/*============================================================================================================================================
                                                  VERTEXATLASFLEETTABLE.CPP
============================================================================================================================================*/

#include "Engine/SimulationSpace/Deformable/VertexAtlasFleetTable.h"
#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

uint32_t VertexAtlasFleetTable::RegisterClipFromFile(
    const std::filesystem::path& atlasPath,
    std::string* outError)
{
    if (ClipCount_ >= kMaxAtlasClips)
    {
        if (outError) *outError = "VertexAtlasFleetTable clip table is full.";
        return UINT32_MAX;
    }

    Codecs::Simulation::VertexAtlasDocumentModel doc {};
    if (!Codecs::Simulation::VertexAtlasCodec::DecodeFromFile(atlasPath, doc, outError))
    {
        return UINT32_MAX;
    }
    return RegisterClip(std::move(doc.Atlas));
}

uint32_t VertexAtlasFleetTable::RegisterClip(GeometrySpace::Deformation::VertexMotionAtlas clip)
{
    if (ClipCount_ >= kMaxAtlasClips || !clip.Ready())
    {
        return UINT32_MAX;
    }
    const uint32_t slot = ClipCount_++;
    Clips_[slot] = std::move(clip);
    return slot;
}

uint32_t VertexAtlasFleetTable::SpawnInstance(
    uint32_t clipSlot,
    const std::array<float, 3>& worldPosMeters,
    float uniformScale,
    float playbackRate,
    float cullDistanceMeters) noexcept
{
    if (clipSlot >= ClipCount_ || InstanceCount_ >= kMaxFleetInstances)
    {
        return UINT32_MAX;
    }

    const uint32_t idx = InstanceCount_++;
    VertexAtlasInstanceGpuRecord& rec = GpuRecords_[idx];
    rec = {};
    rec.WorldPosAndScale = { worldPosMeters[0], worldPosMeters[1], worldPosMeters[2], std::max(0.01f, uniformScale) };
    rec.RotationAndPhase = { 0.0f, 0.0f, 0.0f, std::max(0.0f, playbackRate) };
    rec.ClipSliceAndLod  = { clipSlot, 0u, (Clips_[clipSlot].SliceCount > 1u ? 1u : 0u), 0u };
    rec.BlendAndStrain   = { 0.0f, 1.0f, 0.0f, 0.0f };
    SliceCursors_[idx]   = 0.0f;
    CullDistances_[idx]  = std::max(1.0f, cullDistanceMeters);
    return idx;
}

VertexAtlasFleetStepSummary VertexAtlasFleetTable::StepFleet(
    float deltaTimeSeconds,
    const std::array<float, 3>& viewerPosMeters) noexcept
{
    VertexAtlasFleetStepSummary summary {};
    summary.ActiveInstances = InstanceCount_;
    ++TickCounter_;

    const float dt = std::max(0.0f, deltaTimeSeconds);
    for (uint32_t i = 0u; i < InstanceCount_; ++i)
    {
        VertexAtlasInstanceGpuRecord& rec = GpuRecords_[i];
        const uint32_t clipSlot = rec.ClipSliceAndLod[0];
        if (clipSlot >= ClipCount_) continue;

        const float dx = rec.WorldPosAndScale[0] - viewerPosMeters[0];
        const float dy = rec.WorldPosAndScale[1] - viewerPosMeters[1];
        const float dz = rec.WorldPosAndScale[2] - viewerPosMeters[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        rec.BlendAndStrain[2] = dist;

        const float cullDist = CullDistances_[i];
        if (dist > cullDist)
        {
            rec.ClipSliceAndLod[3] = 2u; // Culled
            ++summary.CulledCount;
            continue;
        }

        const bool isHalf = (dist > cullDist * 0.5f);
        rec.ClipSliceAndLod[3] = isHalf ? 1u : 0u;
        if (isHalf)
        {
            ++summary.HalfRateCount;
            if ((TickCounter_ & 1ull) != 0ull) continue;
        }
        else
        {
            ++summary.FullRateCount;
        }

        const auto& clip = Clips_[clipSlot];
        const float sliceCountF = static_cast<float>(clip.SliceCount);
        const float stepSlices  = dt * clip.SlicesPerSecond * rec.RotationAndPhase[3] * (isHalf ? 2.0f : 1.0f);
        float cursor = std::fmod(SliceCursors_[i] + stepSlices, sliceCountF);
        if (cursor < 0.0f) cursor += sliceCountF;
        SliceCursors_[i] = cursor;

        const uint32_t s0   = static_cast<uint32_t>(std::floor(cursor)) % clip.SliceCount;
        const uint32_t s1   = (s0 + 1u) % clip.SliceCount;
        const float    frac = cursor - std::floor(cursor);

        rec.ClipSliceAndLod[1] = s0;
        rec.ClipSliceAndLod[2] = s1;
        rec.BlendAndStrain[0]  = frac;
        rec.RotationAndPhase[2] = clip.SlicePoses[s0].SpinAngleRad;
    }

    return summary;
}

VertexAtlasSamplePoint VertexAtlasFleetTable::SampleInstanceVertexMeters(
    uint32_t instanceSlot,
    uint32_t vertexOrdinal) const noexcept
{
    VertexAtlasSamplePoint out {};
    if (instanceSlot >= InstanceCount_) return out;

    const auto& rec      = GpuRecords_[instanceSlot];
    const uint32_t cSlot = rec.ClipSliceAndLod[0];
    if (cSlot >= ClipCount_) return out;

    const auto& clip = Clips_[cSlot];
    if (vertexOrdinal >= clip.VertexCount) return out;

    const uint32_t s0   = rec.ClipSliceAndLod[1] % clip.SliceCount;
    const uint32_t s1   = rec.ClipSliceAndLod[2] % clip.SliceCount;
    const float    frac = rec.BlendAndStrain[0];
    const float    scl  = rec.WorldPosAndScale[3];

    const size_t off0 = clip.TexelOffset(vertexOrdinal, s0);
    const size_t off1 = clip.TexelOffset(vertexOrdinal, s1);
    if (off0 + 3u >= clip.PositionStrainRgba.size() || off1 + 3u >= clip.PositionStrainRgba.size())
    {
        return out;
    }

    const float lx = clip.PositionStrainRgba[off0 + 0u] * (1.0f - frac) + clip.PositionStrainRgba[off1 + 0u] * frac;
    const float ly = clip.PositionStrainRgba[off0 + 1u] * (1.0f - frac) + clip.PositionStrainRgba[off1 + 1u] * frac;
    const float lz = clip.PositionStrainRgba[off0 + 2u] * (1.0f - frac) + clip.PositionStrainRgba[off1 + 2u] * frac;
    const float st = clip.PositionStrainRgba[off0 + 3u] * (1.0f - frac) + clip.PositionStrainRgba[off1 + 3u] * frac;

    out.WorldPosMeters = {
        rec.WorldPosAndScale[0] + lx * scl,
        rec.WorldPosAndScale[1] + ly * scl,
        rec.WorldPosAndScale[2] + lz * scl
    };
    out.Strain = st;
    out.Valid  = true;
    return out;
}

void VertexAtlasFleetTable::ClearAll() noexcept
{
    for (uint32_t i = 0u; i < ClipCount_; ++i)
    {
        Clips_[i] = {};
    }
    ClipCount_     = 0u;
    InstanceCount_ = 0u;
    TickCounter_   = 0ull;
}

} // namespace Frontier::SimulationSpace::Deformable
