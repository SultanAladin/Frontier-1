/*============================================================================================================================================
                                                            NOTIFICATIONSEQUENCE.H
============================================================================================================================================*/
// 🧩 Bounded semantic notification history, top-right live toast stack, and Quick Controls notification stream

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>
#include <deque>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class NotificationSeverity : uint8_t
{
    Notice  = 0, // [-] - Neutral informational status alert
    Success = 1, // [-] - Successful persistence or command confirmation
    Warning = 2, // [-] - Non-fatal caution alert
    Error   = 3, // [-] - Validation or persistence failure alert
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct NotificationPreferences
{
    bool  RateOverlayEnabled   = false; // [-] - Render top-right refresh-rate indicator
    bool  AppliedAlertsEnabled = true;  // [-] - Broadcast toast when preferences are saved
    bool  ErrorAlertsEnabled   = true;  // [-] - Broadcast toast when validation/save fails
    float AlertHoldSeconds     = 3.5f;  // [s] - Toast visibility duration [1.0..10.0]

    [[nodiscard]] constexpr bool operator==(const NotificationPreferences&) const noexcept = default;
};

struct NotificationEntry
{
    uint64_t             Token            = 0u;                           // [idx] - Monotonic notification identifier
    NotificationSeverity Severity         = NotificationSeverity::Notice; // [-]   - Visual severity level
    std::string          Title            = "";                           // [-]   - Primary toast heading
    std::string          Description      = "";                           // [-]   - Wrapped body text
    double               RemainingSeconds = 3.5;                          // [s]   - Remaining lifetime before auto-dismiss
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class NotificationSequence
{
public:
    static constexpr std::size_t HistoryCapacity = 64u; // [count] - Bounded notification stream retention
    static constexpr std::size_t PendingCapacity = 32u; // [count] - Bounded live toast queue capacity
    static constexpr std::size_t VisibleCapacity = 3u;  // [count] - Simultaneous top-right toasts

    NotificationSequence() noexcept = default;

    uint64_t Broadcast(
        NotificationSeverity Severity,
        const std::string&   Title,
        const std::string&   Description,
        double               HoldSeconds         = 3.5,
        bool                 PresentationEnabled = true);

    void Integrate(
        double      DeltaSeconds,
        std::size_t VisibleCount = VisibleCapacity,
        uint64_t    PausedToken  = 0u) noexcept;

    bool Dismiss(uint64_t Token) noexcept;
    void ClearHistory() noexcept;
    void ClearPending() noexcept;

    [[nodiscard]] const std::deque<NotificationEntry>& QueryHistory() const noexcept;
    [[nodiscard]] const std::deque<NotificationEntry>& QueryPending() const noexcept;

    [[nodiscard]] static float QueryBannerHeight(
        PixelSpace&              Surface,
        float                    Width,
        const NotificationEntry& Entry);

    [[nodiscard]] static bool ConstructBanner(
        PixelSpace&              Surface,
        Rectangle2D              Bounds,
        const NotificationEntry& Entry,
        const ControlPointer&    Pointer,
        bool                     DismissButtonEnabled);

    void ConstructToastOverlay(
        PixelSpace&           Surface,
        Rectangle2D           ViewportLogicalBounds,
        const ControlPointer& Pointer,
        double                DeltaSeconds);

private:
    std::deque<NotificationEntry> HistoryEntries {}; // [-] - Retained stream entries
    std::deque<NotificationEntry> PendingEntries {}; // [-] - Active/queued top-right toasts
    uint64_t                      NextToken      = 1u; // [idx] - Monotonic entry token counter
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void NotificationSequence::ClearHistory() noexcept
{
    HistoryEntries.clear();
}

inline void NotificationSequence::ClearPending() noexcept
{
    PendingEntries.clear();
}

inline const std::deque<NotificationEntry>& NotificationSequence::QueryHistory() const noexcept
{
    return HistoryEntries;
}

inline const std::deque<NotificationEntry>& NotificationSequence::QueryPending() const noexcept
{
    return PendingEntries;
}

} // namespace Frontier
