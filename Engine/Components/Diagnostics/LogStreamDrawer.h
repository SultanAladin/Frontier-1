/*============================================================================================================================================
                                                              LOGSTREAMDRAWER.H
============================================================================================================================================*/
// 🧩 Reusable collapsible diagnostic log stream drawer with channel filter chips, timestamped semantic badges & scroll offset

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LogChannelCategory : uint8_t
{
    All      = 0u, // [-] - Show every diagnostic channel
    Physics  = 1u, // [-] - Physics & fracture solver telemetry
    Render   = 2u, // [-] - GPU compute & raytracing telemetry
    Wind     = 3u, // [-] - Ambient wind & anemometer probes
    Viewport = 4u, // [-] - Camera, gizmo & overlay viewport events
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LogStreamEntry
{
    std::string        TimestampText { "00:00:00" }; // [-] - HH:MM:SS timestamp
    LogChannelCategory Channel       = LogChannelCategory::Physics;
    std::string        Message       {};                           // [-] - Diagnostic summary text
};

struct LogStreamDrawerSession
{
    LogChannelCategory ActiveFilter = LogChannelCategory::All;
    float              ScrollY      = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class LogStreamDrawer
{
public:
    static constexpr float RecommendedHeightPx = 112.0f; // [px] - Canonical open drawer height below ruler

    [[nodiscard]] static const char* QueryChannelTag(LogChannelCategory Channel) noexcept;
    [[nodiscard]] static ColorRgba   QueryChannelAccent(LogChannelCategory Channel) noexcept;

    static void Construct(
        PixelSpace&                      Surface,
        Rectangle2D                      Bounds,
        std::span<const LogStreamEntry> Records,
        LogStreamDrawerSession&          Session,
        const ControlPointer&            Pointer,
        float                            Opacity = 1.0f) noexcept;
};

} // namespace Frontier
