/*============================================================================================================================================
                                                    BUILDCONFIGURATION.H
============================================================================================================================================*/
// 🧩 Compile-time build-profile policy: enforces mutually exclusive Editor, Development, and Shipping profiles across all targets

#pragma once

#include <cstdint>

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROFILE EXCLUSIVITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Default to Development when invoked outside CMake so standalone translation-unit syntax checks remain valid.
#if !defined(FRONTIER_PROFILE_EDITOR) && !defined(FRONTIER_PROFILE_DEVELOPMENT) && !defined(FRONTIER_PROFILE_SHIPPING)
    #define FRONTIER_PROFILE_EDITOR      0
    #define FRONTIER_PROFILE_DEVELOPMENT 1
    #define FRONTIER_PROFILE_SHIPPING    0
#endif

#ifndef FRONTIER_PROFILE_EDITOR
    #define FRONTIER_PROFILE_EDITOR 0
#endif

#ifndef FRONTIER_PROFILE_DEVELOPMENT
    #define FRONTIER_PROFILE_DEVELOPMENT 0
#endif

#ifndef FRONTIER_PROFILE_SHIPPING
    #define FRONTIER_PROFILE_SHIPPING 0
#endif

static_assert(
    (FRONTIER_PROFILE_EDITOR + FRONTIER_PROFILE_DEVELOPMENT + FRONTIER_PROFILE_SHIPPING) == 1,
    "Exactly one Frontier build profile (FRONTIER_PROFILE_EDITOR, FRONTIER_PROFILE_DEVELOPMENT, or FRONTIER_PROFILE_SHIPPING) must equal 1."
);

#if FRONTIER_PROFILE_EDITOR
    #define FRONTIER_EDITOR_ENABLED      1
    #define FRONTIER_DIAGNOSTICS_ENABLED 1
#elif FRONTIER_PROFILE_DEVELOPMENT
    #define FRONTIER_EDITOR_ENABLED      0
    #define FRONTIER_DIAGNOSTICS_ENABLED 1
#else
    #define FRONTIER_EDITOR_ENABLED      0
    #define FRONTIER_DIAGNOSTICS_ENABLED 0
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BuildProfileCategory : uint8_t
{
    Editor      = 0, // [-] - Full authoring editor, workspaces, inspectors, and GPU overlays
    Development = 1, // [-] - Standalone runtime with diagnostics; runs locally without packaging
    Shipping    = 2, // [-] - Stripped production binary; packaged standalone archive
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BuildConfiguration
{
    static constexpr bool EditorProfileActive      = (FRONTIER_PROFILE_EDITOR != 0);      // [-] - True in Editor profile
    static constexpr bool DevelopmentProfileActive = (FRONTIER_PROFILE_DEVELOPMENT != 0); // [-] - True in Development profile
    static constexpr bool ShippingProfileActive    = (FRONTIER_PROFILE_SHIPPING != 0);    // [-] - True in Shipping profile

    static constexpr bool EditorIncluded           = (FRONTIER_EDITOR_ENABLED != 0);      // [-] - Gates Engine/Editor inclusion
    static constexpr bool DiagnosticsIncluded      = (FRONTIER_DIAGNOSTICS_ENABLED != 0); // [-] - Gates assertions, console, GPU debug lines
    static constexpr bool BinaryPackagingRequired  = ShippingProfileActive;               // [-] - True only when standalone packaging applies

    [[nodiscard]] static consteval BuildProfileCategory ActiveProfile() noexcept
    {
        if constexpr (EditorProfileActive)
        {
            return BuildProfileCategory::Editor;
        }
        else if constexpr (DevelopmentProfileActive)
        {
            return BuildProfileCategory::Development;
        }
        else
        {
            return BuildProfileCategory::Shipping;
        }
    }
};

} // namespace Frontier
