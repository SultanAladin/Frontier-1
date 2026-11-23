//============================================================================================================================================
//                                                          CODEXINTERCHANGE.H
//============================================================================================================================================
// 🧩 Seekable, versioned Codex stream translation with payload preservation and integrity verification.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

/// 🧩 The authored surface a Codex stream opens into while retaining one common binary arrangement.
enum class CodexProfile : std::uint32_t
{
    Workspace   = 0u,
    Pigment     = 1u,
    Enamel      = 2u,
    Textile     = 3u,
    Garment     = 4u,
    Canvas      = 5u,
    World       = 6u,
    Terrain     = 7u,
    Impulse     = 8u,
    Solid       = 9u,
    // ⚠️ The NAME changed when this vocabulary was retired; the VALUE 10u is written into every
    //    existing document and must never move.
    Parametric  = 10u,
    Sketch      = 11u,
    Assembly    = 12u,
    Fabrication = 13u,
    Machining   = 14u,
    Datum       = 15u,
    Involute    = 16u,
    Profile     = 17u,
    Section     = 18u
};

/// 🧩 One independently addressable Codex payload section.
struct CodexSection
{
    std::uint32_t              Code          = 0u;   // [-] - stable four-character section identity
    std::uint16_t              MajorVersion  = 0u;   // [-] - section schema compatibility
    std::uint16_t              MinorVersion  = 0u;   // [-] - section schema evolution
    std::uint64_t              Revision      = 0u;   // [-] - revision that introduced this payload
    std::vector<std::uint8_t>  Content       = {};   // [B] - retained verbatim, including unknown sections
};

/// 🧩 One complete in-memory Codex document, independent of its eventual file location.
struct CodexDocument
{
    CodexProfile               Profile          = CodexProfile::Workspace;   // [-] - intended authoring surface
    std::uint64_t              Identity         = 0u;                         // [-] - stable document identity
    std::uint64_t              CurrentRevision  = 0u;                         // [-] - newest committed revision
    std::vector<CodexSection>  Sections         = {};                         // [-] - known and unknown payloads alike
};

/// 🧩 Translates a complete Codex document to and from its seekable binary arrangement.
class CodexInterchange
{
public:

    static constexpr std::uint16_t MajorVersion = 1u;   // [-] - incompatible arrangement changes advance this
    static constexpr std::uint16_t MinorVersion = 0u;   // [-] - additive compatible arrangement changes advance this

    /// 🧩 Encodes a document as preamble, payload sections, index, and completion record.
    /// in    Document [-]  complete content to preserve
    /// out   Result   [B]  one seekable Codex stream
    /// err   refuses when section identities repeat or a payload cannot be represented
    Deliver<std::vector<std::uint8_t>> Encode(const CodexDocument& Document) const;

    /// 🧩 Decodes and validates one complete Codex stream while preserving every unrecognized section.
    /// in    Stream [B]  complete Codex bytes
    /// out   Result [-]  profile, identities, and retained payload sections
    /// err   refuses when signatures, positions, extents, or integrity digests disagree
    Deliver<CodexDocument> Decode(const std::vector<std::uint8_t>& Stream) const;

    /// 🧩 Opens the newest complete Codex stream among the standing file and interrupted-save recovery files.
    /// in    OriginPath [-]  `.codex` or specialized Codex path
    /// out   Result     [-]  the highest complete revision whose profile agrees with the suffix
    Deliver<CodexDocument> Open(const std::string& OriginPath) const;

    /// 🧩 Inscribes one complete Codex stream through a recoverable temporary and preceding-revision route.
    /// in    Document   [-]  complete document to preserve
    /// in    OriginPath [-]  profile-matching target path
    /// out   Result     [-]  refuses without replacing the last complete stream when writing fails
    Deliver<bool> Inscribe(const CodexDocument& Document, const std::string& OriginPath) const;

    /// 🧩 Resolves one supported Codex profile from a case-insensitive file suffix.
    /// out   Result [-]  refuses for a suffix that names no Codex profile
    static Deliver<CodexProfile> ProfileOf(const std::string& OriginPath);
};

}   // namespace Slate
