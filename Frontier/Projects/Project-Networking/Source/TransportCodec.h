//============================================================================================================================================
//                                                           TRANSPORTODEC.H
//============================================================================================================================================
// 📦 Engine-agnostic replication packet codec shared by every multiplayer transport.
// Wire format: 'F' 'P' '1' | version u8 | kind u8 | length u16BE | payload[length].
// Bounds-checked both ways; oversized or truncated input is refused, never read past.

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Networking
{
enum class ReplicationKind : std::uint8_t
{
    Probe = 0,   // Transport handshake probe; payload is a short ASCII tag, no identity.
    Snapshot = 1,// Engine state snapshot (engine-defined bytes).
    Input = 2,   // Engine input/command stream (engine-defined bytes).
};

struct PacketCodec
{
    static constexpr std::uint8_t WireVersion = 1;
    static constexpr std::size_t HeaderLength = 7;
    static constexpr std::size_t MaxPayload = 4096;
    static constexpr std::size_t MaxPacket = HeaderLength + MaxPayload;

    static bool ValidKind(std::uint8_t Kind) noexcept
    {
        return Kind <= static_cast<std::uint8_t>(ReplicationKind::Input);
    }

    // Encodes into Out (replaced). Returns false when the payload is too large.
    static bool Encode(ReplicationKind Kind, const std::uint8_t* Payload, std::size_t Length,
        std::vector<std::uint8_t>& Out) noexcept
    {
        if (Length > MaxPayload || (Length > 0 && !Payload))
            return false;
        Out.resize(HeaderLength + Length);
        Out[0] = 'F';
        Out[1] = 'P';
        Out[2] = '1';
        Out[3] = WireVersion;
        Out[4] = static_cast<std::uint8_t>(Kind);
        Out[5] = static_cast<std::uint8_t>((Length >> 8) & 0xFFu);
        Out[6] = static_cast<std::uint8_t>(Length & 0xFFu);
        if (Length > 0)
            std::memcpy(Out.data() + HeaderLength, Payload, Length);
        return true;
    }

    // Decodes in place: Kind + Payload/Lenght point into the caller's buffer.
    // Returns false on bad magic, version, kind, or length mismatch.
    static bool Decode(const std::uint8_t* Bytes, std::size_t Length, ReplicationKind& Kind,
        const std::uint8_t*& Payload, std::size_t& PayloadLength) noexcept
    {
        Kind = ReplicationKind::Probe;
        Payload = nullptr;
        PayloadLength = 0;
        if (!Bytes || Length < HeaderLength || Length > MaxPacket)
            return false;
        if (Bytes[0] != 'F' || Bytes[1] != 'P' || Bytes[2] != '1')
            return false;
        if (Bytes[3] != WireVersion || !ValidKind(Bytes[4]))
            return false;
        const std::size_t Declared = (static_cast<std::size_t>(Bytes[5]) << 8) | Bytes[6];
        if (Declared > MaxPayload || HeaderLength + Declared != Length)
            return false;
        Kind = static_cast<ReplicationKind>(Bytes[4]);
        Payload = Bytes + HeaderLength;
        PayloadLength = Declared;
        return true;
    }
};
}
