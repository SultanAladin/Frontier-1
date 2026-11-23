//============================================================================================================================================
//                                                           ECOMOWNERSHIP.H
//============================================================================================================================================
// 📦 Dormant Epic catalog-ownership token query. With no catalog item IDs configured the query
// completes immediately as Dormant and Photon proceeds without an ownership token (the dashboard
// currently checks nothing). When IDs are configured (EOS_CATALOG_ITEM_IDS), the signed ownership
// JWT is fetched for Photon's Epic authentication. The token is copied out, never logged.

#pragma once
#include "EpicExchange.h"
#include <cstddef>

namespace Networking
{
enum class OwnershipState
{
    Dormant,  // No catalog IDs configured; no SDK call was made.
    Querying, // Async ownership-token query in flight.
    Ready,    // Ownership token is available via CopyOwnershipToken.
    Failed,   // Query failed; Photon may still proceed without the token.
};

struct OwnershipStatus
{
    OwnershipState State = OwnershipState::Dormant;
    char Reading[256]{};
    unsigned CatalogCount = 0;
};

bool StartOwnershipQuery(const char* const* CatalogItemIds, unsigned Count, DiagnosticReception Reception) noexcept;
void StopOwnershipQuery() noexcept;
// Copies the ownership JWT. Valid only while Ready; returns false otherwise.
bool CopyOwnershipToken(char* Out, std::size_t Capacity) noexcept;
OwnershipStatus InspectOwnershipStatus() noexcept;
}
