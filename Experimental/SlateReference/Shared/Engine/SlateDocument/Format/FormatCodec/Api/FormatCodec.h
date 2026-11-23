//============================================================================================================================================
//                                                              FORMATCODEC.H
//============================================================================================================================================
// 🧩 Versioned document stream layout and its declared migrations — never a conditional inside a reader.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STREAM VERSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The document stream layout this build writes.
inline constexpr std::uint32_t CurrentStreamVersion = 1u;   // [-] - advanced whenever the layout changes

/// 🧩 The leading declaration of every document stream.
/// tag   nonallocating, nonthrowing
struct StreamHeading
{
    std::uint32_t  Signature      = 0u;   // [-] - fixed four bytes; a stream without them is not a document
    std::uint32_t  StreamVersion  = 0u;   // [-] - the version the stream was written at
    std::uint64_t  OwnerCount  = 0u;   // [-] - owners the stream carries
    std::uint64_t  ContentIndex = 0u;   // [B] - where the owner content begins
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      MIGRATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One declared transformation from a stream version to the one above it.
/// note  🔴 Migration is a declared transformation between two versions, never a conditional inside a
///       reader. Conditionals inside readers are how a format acquires cases nobody can enumerate.
/// tag   nonallocating, nonthrowing
struct DeclaredMigration
{
    std::uint32_t  FromVersion = 0u;   // [-] - the version the stream carries
    std::uint32_t  ToVersion   = 0u;   // [-] - exactly one greater than FromVersion
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CODEC
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Reads a document stream's heading and reports whether a migration path reaches the current version.
/// in    Heading  [-]  the heading as the stream carried it
/// out   Result  [-]  refuses with VersionUnmigratable when no declared chain reaches CurrentStreamVersion
/// note  A codec translates a stream and does nothing else. It does not condition what it decoded and does
///       not decide whether the result is fit to use.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<std::uint32_t> ResolveMigration(const StreamHeading& Heading);

}   // namespace Slate
