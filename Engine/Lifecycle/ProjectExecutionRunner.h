/*============================================================================================================================================
                                                           PROJECTEXECUTIONRUNNER.H
============================================================================================================================================*/
// 🧩 ABI-clean exported execution entrypoints for standalone generated C++20 Frontier projects (<ProjectName>, Editor, Shipping)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

#if defined(_WIN32)
    #if defined(FRONTIER_BUILDING_ENGINE_DLL)
        #define FRONTIER_ENGINE_API __declspec(dllexport)
    #elif defined(FRONTIER_CONSUME_ENGINE_DLL)
        #define FRONTIER_ENGINE_API __declspec(dllimport)
    #else
        #define FRONTIER_ENGINE_API
    #endif

    #if defined(FRONTIER_BUILDING_WORKSPACE_UI_DLL)
        #define FRONTIER_WORKSPACE_UI_API __declspec(dllexport)
    #elif defined(FRONTIER_CONSUME_WORKSPACE_UI_DLL)
        #define FRONTIER_WORKSPACE_UI_API __declspec(dllimport)
    #else
        #define FRONTIER_WORKSPACE_UI_API
    #endif
#else
    #define FRONTIER_ENGINE_API       __attribute__((visibility("default")))
    #define FRONTIER_WORKSPACE_UI_API __attribute__((visibility("default")))
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

extern "C"
{

using FrontierProjectTickProcedure = void (*)(double DeltaSeconds, void* UserPayload);

struct FrontierProjectLaunchDescriptor
{
    uint32_t                     StructureVersion    = 1u;      // [-]  - ABI structure revision (= 1)
    const char*                  ProjectTitle        = nullptr; // [-]  - Logical PascalCase project title
    const char*                  ProjectSpacePath    = nullptr; // [-]  - Path to <ProjectName>.projectspace
    const char*                  StartupWorldPath    = nullptr; // [-]  - Optional Content/Worlds/MainWorld.WorldSpace path
    const char*                  StartupUvPath       = nullptr; // [-]  - Optional Content/UV/MainSurface.UVSpace path
    uint32_t                     InitialWidthPx      = 1600u;   // [px] - Initial OS window width
    uint32_t                     InitialHeightPx     = 920u;    // [px] - Initial OS window height
    uint32_t                     BoundedTickCount        = 0u;      // [-]  - 0 = interactive OS window; >0 = bounded verification ticks
    uint32_t                     PreferUvWorkspace       = 0u;      // [-]  - 0 = Frontier World, 1 = UVWorkspace
    FrontierProjectTickProcedure NotifySimulationStep    = nullptr; // [-]  - Optional per-tick <ProjectName>::Lifecycle callback
    FrontierProjectTickProcedure NotifyUserInterfaceStep = nullptr; // [-]  - Optional per-tick <ProjectName>::UserInterface callback
    void*                        UserPayload             = nullptr; // [-]  - Opaque pointer forwarded to project callbacks
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  EXPORTED ENTRYPOINTS
//------------------------------------------------------------------------------------------------------------------------

// Exported by FrontierEngine (linked by <ProjectName>, <ProjectName>Editor, AND <ProjectName>Shipping — zero Editor/ImGui symbols)
FRONTIER_ENGINE_API int FrontierRunShippingProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor);

// Exported by FrontierWorkspaceUi (linked by <ProjectName> Development & <ProjectName>Editor — stripped from <ProjectName>Shipping)
FRONTIER_WORKSPACE_UI_API int FrontierRunDevelopmentProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor);
FRONTIER_WORKSPACE_UI_API int FrontierRunEditorProjectLoop(const FrontierProjectLaunchDescriptor* Descriptor);

} // extern "C"
