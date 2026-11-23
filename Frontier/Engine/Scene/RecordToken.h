/*==========================================================================================================================================
                                                               RECORD TOKEN
                                     ------------------------------------------------------------------
                      Session-unique identity issued to every record at construction; never reused, never hand-made.
==========================================================================================================================================*/

// 🧩 Session-unique identity issued to every record at construction; never reused, never hand-made.

#pragma once

#include <atomic>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Opaque identity for one RecordEntry. Zero is the invalid token (no record); issued values increase monotonically
//    for the whole process, so a stale token can never alias a later record — lookups against it simply miss.
struct RecordToken
{
    uint64_t Value = 0;   // [-] - Issued value; 0 is invalid

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }

    friend constexpr bool operator==(RecordToken Left, RecordToken Right) noexcept { return Left.Value == Right.Value; }
    friend constexpr bool operator!=(RecordToken Left, RecordToken Right) noexcept { return Left.Value != Right.Value; }
    friend constexpr bool operator<(RecordToken Left, RecordToken Right) noexcept { return Left.Value < Right.Value; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      TOKEN ISSUER
//------------------------------------------------------------------------------------------------------------------------

// The single source of record tokens. Issue() is the only way a token comes into existence; RecordEntry's
// constructor calls it, so registration cannot be forgotten or forged elsewhere.
class TokenIssuer
{
  public:
    [[nodiscard]] static RecordToken Issue() noexcept
    {
        // 🧵 Relaxed is enough: uniqueness comes from the atomic increment, not from ordering against other data.
        static std::atomic<uint64_t> Next{1};   // [-] - Next value to issue; 0 is reserved as invalid
        return RecordToken{Next.fetch_add(1, std::memory_order_relaxed)};
    }

    TokenIssuer() = delete;
};

} // namespace Frontier
