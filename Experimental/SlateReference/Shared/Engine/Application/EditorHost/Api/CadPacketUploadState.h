//============================================================================================================================================
//                                                   CADPACKETUPLOADSTATE.H
//============================================================================================================================================
// Upload bookkeeping for the shared CAD packet. The packet is uploaded once, then recorded through
// each viewport leaf's projection; this state deliberately does not own viewport projection data.

#pragma once

#include <cstddef>
#include <cstdint>
#include "Shared/WorkspaceCadPacket.slang.h"

namespace Slate
{

inline std::uint64_t FingerprintCadPacket(const WorkspaceCadPacket& Packet)
{
    // 🧩 The packet is rebuilt each tick, so its mutation counter is not a content identity.
    //    Uploading on that counter rewrites the mapped storage buffer every frame, including when
    //    the shape is unchanged. A stable content fingerprint lets the GPU buffer remain untouched
    //    on idle frames and removes the intermittent read/write race that presented as shape flicker.
    std::uint64_t Hash = 1469598103934665603ull;
    const auto Mix = [&](const void* Data, std::size_t Bytes)
    {
        const auto* BytesView = static_cast<const unsigned char*>(Data);
        for (std::size_t Index = 0u; Index < Bytes; ++Index)
        {
            Hash ^= static_cast<std::uint64_t>(BytesView[Index]);
            Hash *= 1099511628211ull;
        }
    };
    Mix(&Packet.SegmentCount, sizeof(Packet.SegmentCount));
    Mix(Packet.Segments, sizeof(Packet.Segments[0]) * Packet.SegmentCount);
    Mix(&Packet.FillCount, sizeof(Packet.FillCount));
    Mix(Packet.Fills, sizeof(Packet.Fills[0]) * Packet.FillCount);
    Mix(&Packet.MarkerCount, sizeof(Packet.MarkerCount));
    Mix(Packet.Markers, sizeof(Packet.Markers[0]) * Packet.MarkerCount);
    Mix(&Packet.MinimumAlong, sizeof(Packet.MinimumAlong));
    Mix(&Packet.MinimumAcross, sizeof(Packet.MinimumAcross));
    Mix(&Packet.MaximumAlong, sizeof(Packet.MaximumAlong));
    Mix(&Packet.MaximumAcross, sizeof(Packet.MaximumAcross));
    Mix(&Packet.ExtentStanding, sizeof(Packet.ExtentStanding));
    return Hash;
}

struct CadPacketUploadState
{
    std::uint64_t UploadedFingerprint = 0ull;
    bool          HasUploadedPacket = false;

    void Invalidate()
    {
        UploadedFingerprint = 0ull;
        HasUploadedPacket = false;
    }

    bool NeedsUpload(std::uint64_t PacketFingerprint) const
    {
        return !HasUploadedPacket || UploadedFingerprint != PacketFingerprint;
    }

    void MarkUploaded(std::uint64_t PacketFingerprint)
    {
        UploadedFingerprint = PacketFingerprint;
        HasUploadedPacket = true;
    }
};

} // namespace Slate
