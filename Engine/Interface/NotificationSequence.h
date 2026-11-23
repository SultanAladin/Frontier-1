/*==========================================================================================================================================
                                                        NOTIFICATIONSEQUENCE.H
==========================================================================================================================================*/
// 🧩 Bounded transient alerts, retained notification history, and shared semantic card geometry.

#pragma once

#include "ControlKit.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   NOTIFICATION STORAGE
//------------------------------------------------------------------------------------------------------------------------

enum class NotificationCategory : uint8_t
{
    Information,
    Success,
    Warning,
    Error
};

struct NotificationEntry
{
    uint64_t             Token    = 0;
    NotificationCategory Category = NotificationCategory::Information;
    std::string          Title;
    std::string          Description;
    double               RemainingSeconds = 0;
};

class NotificationSequence
{
  public:
    static constexpr size_t HistoryCapacity = 64;
    static constexpr size_t PendingCapacity = 16;
    static constexpr size_t VisibleCapacity = 3;

    uint64_t Broadcast(NotificationCategory Category, const std::string &Title, const std::string &Description,
                       double HoldSeconds, bool PresentationEnabled = true);
    void     Integrate(double Δτ, size_t VisibleCount, uint64_t PausedToken = 0) noexcept;
    bool     Reclaim(uint64_t Token) noexcept;
    void     ResetPresentation() noexcept
    {
        Pending.clear();
    }
    void ResetHistory() noexcept
    {
        History.clear();
    }
    [[nodiscard]] const std::deque<NotificationEntry> &QueryPending() const noexcept
    {
        return Pending;
    }
    [[nodiscard]] const std::deque<NotificationEntry> &QueryHistory() const noexcept
    {
        return History;
    }

  private:
    uint64_t                      NextToken = 1;
    std::deque<NotificationEntry> Pending;
    std::deque<NotificationEntry> History;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   SEMANTIC CARD GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

class NotificationCard
{
  public:
    [[nodiscard]] static float QueryHeight(PixelSpace &Surface, float Width, const NotificationEntry &Entry);
    // The host authenticates a matching press/release token before acting on the returned dismissal.
    [[nodiscard]] static bool Construct(PixelSpace &Surface, const PlaneExtent &Extent, const NotificationEntry &Entry,
                                        const ControlPointer &Pointer, bool DismissEnabled);
};

} // namespace Frontier
