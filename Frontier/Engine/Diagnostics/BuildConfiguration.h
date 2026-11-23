/*==========================================================================================================================================
                                                     COMPILE-TIME BUILD-FLAVOUR POLICY
                                     ------------------------------------------------------------------
                               Exactly one of Editor / Development / Shipping profiles is active per binary.
==========================================================================================================================================*/

// 🧩 Compile-time build-flavour policy: exactly one of Editor / Development / Shipping profiles is active per binary.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROFILE SELECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Profile macros keep the upstream build spelling (FRONTIER_DEVELOPMENT_PROFILE / FRONTIER_SHIPPING_PROFILE are
//    what the Windows Build.bat files pass to cl). A binary that defines none of them defaults to Development.
#if !defined(FRONTIER_EDITOR_PROFILE) && !defined(FRONTIER_DEVELOPMENT_PROFILE) && !defined(FRONTIER_SHIPPING_PROFILE)
    #define FRONTIER_DEVELOPMENT_PROFILE 1
#endif

#ifdef FRONTIER_EDITOR_PROFILE
    #define FRONTIER_PROFILE_EDITOR_VALUE 1
#else
    #define FRONTIER_PROFILE_EDITOR_VALUE 0
#endif

#ifdef FRONTIER_DEVELOPMENT_PROFILE
    #define FRONTIER_PROFILE_DEVELOPMENT_VALUE 1
#else
    #define FRONTIER_PROFILE_DEVELOPMENT_VALUE 0
#endif

#ifdef FRONTIER_SHIPPING_PROFILE
    #define FRONTIER_PROFILE_SHIPPING_VALUE 1
#else
    #define FRONTIER_PROFILE_SHIPPING_VALUE 0
#endif

static_assert(FRONTIER_PROFILE_EDITOR_VALUE + FRONTIER_PROFILE_DEVELOPMENT_VALUE + FRONTIER_PROFILE_SHIPPING_VALUE == 1,
              "Exactly one Frontier build profile must be active");

// 📝 Retained from the upstream WorkspaceBuildConfiguration precedent: the developer console exists outside Shipping
//    unless a build overrides it explicitly.
#ifndef FRONTIER_DEVELOPMENT_CONSOLE
    #ifdef FRONTIER_SHIPPING_PROFILE
        #define FRONTIER_DEVELOPMENT_CONSOLE 0
    #else
        #define FRONTIER_DEVELOPMENT_CONSOLE 1
    #endif
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct BuildConfiguration
{
    static constexpr bool EditorEnabled      = FRONTIER_PROFILE_EDITOR_VALUE != 0;      // [-] - Full editor application
    static constexpr bool DevelopmentEnabled = FRONTIER_PROFILE_DEVELOPMENT_VALUE != 0; // [-] - Developer build of a project
    static constexpr bool ShippingEnabled    = FRONTIER_PROFILE_SHIPPING_VALUE != 0;    // [-] - Release build, no editor machinery

    // 📝 Editor-only machinery (SceneDirectory registration, gizmos, consoles, this Log Stream panel) keys off this
    //    one predicate; Shipping binaries contain none of it.
    static constexpr bool EditorMachineryEnabled  = EditorEnabled || DevelopmentEnabled; // [-] - Gates every editor-only code path
    static constexpr bool DevelopmentConsoleEnabled = FRONTIER_DEVELOPMENT_CONSOLE != 0; // [-] - Mirrors the upstream console policy
};

} // namespace Frontier
