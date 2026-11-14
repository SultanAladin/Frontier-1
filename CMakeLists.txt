#============================================================================================================================================
#                                                           CMAKELISTS.TXT
#============================================================================================================================================
# 🧩 Root ISO C++20 build configuration enforcing Editor, Development, and Shipping profiles across Windows, Linux, and Android.

cmake_minimum_required(VERSION 3.21)
project(Frontier LANGUAGES C CXX)

include(CTest)

#------------------------------------------------------------------------------------------------------------------------
#                                                LANGUAGE & PROFILE POLICY
#------------------------------------------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRONTIER_PROFILE "Development" CACHE STRING "Active Frontier build profile (Editor, Development, Shipping)")
set_property(CACHE FRONTIER_PROFILE PROPERTY STRINGS Editor Development Shipping)

if(FRONTIER_PROFILE STREQUAL "Editor")
    set(FRONTIER_PROFILE_EDITOR_DEF      1)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Development")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 1)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Shipping")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    1)
else()
    message(FATAL_ERROR "Invalid FRONTIER_PROFILE='${FRONTIER_PROFILE}'. Choose Editor, Development, or Shipping.")
endif()

message(STATUS "Frontier active profile: ${FRONTIER_PROFILE}")

#------------------------------------------------------------------------------------------------------------------------
#                                          PINNED DEPENDENCY & PATCH VERIFICATION
#------------------------------------------------------------------------------------------------------------------------

find_package(Threads REQUIRED)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareDependencies.py" --verify
    RESULT_VARIABLE FRONTIER_DEP_STATUS
)
if(NOT FRONTIER_DEP_STATUS EQUAL 0)
    message(FATAL_ERROR "Dependencies or ImGui patches unverified. Run: python3 Automation/PrepareDependencies.py")
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                             COMPILER WARNING & PROFILE FLAGS
#------------------------------------------------------------------------------------------------------------------------

add_library(FrontierCompileSettings INTERFACE)
target_compile_features(FrontierCompileSettings INTERFACE cxx_std_20)
target_compile_definitions(FrontierCompileSettings INTERFACE
    FRONTIER_PROFILE_EDITOR=${FRONTIER_PROFILE_EDITOR_DEF}
    FRONTIER_PROFILE_DEVELOPMENT=${FRONTIER_PROFILE_DEVELOPMENT_DEF}
    FRONTIER_PROFILE_SHIPPING=${FRONTIER_PROFILE_SHIPPING_DEF}
)

if(MSVC)
    target_compile_options(FrontierCompileSettings INTERFACE /utf-8 /W4 /permissive-)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE /O2 /DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE /Zi)
    endif()
else()
    target_compile_options(FrontierCompileSettings INTERFACE -Wall -Wextra -Wpedantic)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE -O3 -DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE -O1 -g)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                     PATCHED IMGUI (EXCLUDED FROM SHIPPING RUNTIME)
#------------------------------------------------------------------------------------------------------------------------

if(NOT FRONTIER_PROFILE STREQUAL "Shipping" OR BUILD_TESTING)
    set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        add_library(FrontierImGui STATIC EXCLUDE_FROM_ALL
            "${IMGUI_ROOT}/imgui.cpp"
            "${IMGUI_ROOT}/imgui_draw.cpp"
            "${IMGUI_ROOT}/imgui_tables.cpp"
            "${IMGUI_ROOT}/imgui_widgets.cpp"
            "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp"
        )
    else()
        add_library(FrontierImGui STATIC
            "${IMGUI_ROOT}/imgui.cpp"
            "${IMGUI_ROOT}/imgui_draw.cpp"
            "${IMGUI_ROOT}/imgui_tables.cpp"
            "${IMGUI_ROOT}/imgui_widgets.cpp"
            "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp"
        )
    endif()
    target_include_directories(FrontierImGui PUBLIC
        "${IMGUI_ROOT}"
        "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include"
        "${CMAKE_SOURCE_DIR}/ExternalPackages/volk")
    target_compile_definitions(FrontierImGui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)
    target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
    if(NOT MSVC)
        target_compile_options(FrontierImGui PRIVATE -w)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                         DESKTOP GLFW 3.4 WINDOWING (NON-ANDROID)
#------------------------------------------------------------------------------------------------------------------------

if(NOT ANDROID)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    if(UNIX AND NOT APPLE)
        include(CheckIncludeFile)
        check_include_file("X11/Xlib.h" FRONTIER_HAS_X11_HEADERS)
        check_include_file("wayland-client.h" FRONTIER_HAS_WAYLAND_HEADERS)
        find_program(FRONTIER_WAYLAND_SCANNER NAMES wayland-scanner)

        if(FRONTIER_HAS_X11_HEADERS)
            set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        endif()

        if(FRONTIER_HAS_WAYLAND_HEADERS AND FRONTIER_WAYLAND_SCANNER)
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
    endif()

    add_subdirectory(ExternalPackages/glfw EXCLUDE_FROM_ALL)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                               ENGINE RUNTIME LIBRARY
#------------------------------------------------------------------------------------------------------------------------

set(FRONTIER_UI_COMPONENT_SOURCES
    Engine/Components/PixelSpace.cpp
    Engine/Components/VectorIcons/VectorIconRepository.cpp
    Engine/Components/Appearance/ThemeRepository.cpp
    Engine/Components/Appearance/TypographyRepository.cpp
    Engine/Components/Motion/SpringIntegrator.cpp
    Engine/Components/Notch/PanelExpansionTab.cpp
    Engine/Components/Controls/ControlAppearance.cpp
    Engine/Components/Controls/ControlTooltip.cpp
    Engine/Components/Controls/ScalarSlider.cpp
    Engine/Components/Controls/ExclusiveSelector.cpp
    Engine/Components/Controls/CapsuleButton.cpp
    Engine/Components/Controls/ThemePreviewTile.cpp
    Engine/Components/Controls/DropdownSelector.cpp
    Engine/Components/Controls/TextInputField.cpp
    Engine/Components/Controls/ConfirmationDialogue.cpp
    Engine/Components/Controls/NotificationSequence.cpp
    Engine/Components/Docking/WorkspaceTabStrip.cpp
    Engine/Components/Docking/WorkspaceDockContainer.cpp
    Engine/Components/Panes/WorkspaceAppearance.cpp
    Engine/Components/Panes/OutlinerPanel.cpp
    Engine/Components/Panes/InspectorPanel.cpp
    Engine/Components/Panes/ViewportPanel.cpp
    Engine/Components/Panes/ViewportProjectionPane.cpp
    Engine/Components/ControlCentre/ControlCentrePanel.cpp
    Engine/Components/ContentBrowser/ColourCodex.cpp
    Engine/Components/ContentBrowser/ThumbnailSheetRepository.cpp
    Engine/Components/ContentBrowser/ContentBrowserPanel.cpp
    Engine/Workspaces/UV/UVOutlinerPanel.cpp
    Engine/Workspaces/UV/UVInspectorPanel.cpp
    Engine/Workspaces/UV/UVViewportInteractor.cpp
    Engine/Workspaces/UV/UVViewportHost.cpp
    Engine/Workspaces/UV/UVWorkspaceHost.cpp
    Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.cpp
    Engine/Input/ImGuiInputAdapter.cpp
    Engine/Configuration/InputSettings.cpp
    Engine/Configuration/ProjectSpaceRepository.cpp
    Engine/Lifecycle/WorkspaceLifecycle.cpp
)
if(NOT ANDROID)
    list(APPEND FRONTIER_UI_COMPONENT_SOURCES Engine/Lifecycle/DesktopWorkspaceLaunch.cpp)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                      SLANG -> SPIR-V SHADER COMPILATION PIPELINE
#------------------------------------------------------------------------------------------------------------------------

if(WIN32)
    set(FRONTIER_SLANGC_BIN "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc.exe")
else()
    set(FRONTIER_SLANGC_BIN "${CMAKE_SOURCE_DIR}/ExternalPackages/VulkanCompute/Slang/slangtorch/bin/slangc")
endif()
if(NOT EXISTS "${FRONTIER_SLANGC_BIN}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareShaderToolchain.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE FRONTIER_SHADER_TOOLCHAIN_STATUS
    )
endif()

set(FRONTIER_COMPILED_SHADER_DIR "${CMAKE_BINARY_DIR}/CompiledShaders")
file(MAKE_DIRECTORY "${FRONTIER_COMPILED_SHADER_DIR}")
set(FRONTIER_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/Engine/GeometricRaster/Shaders")
set(FRONTIER_SPV_OUTPUTS "")

function(frontier_compile_slang_shader SLANG_FILE ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${FRONTIER_SHADER_SRC_DIR}/${SLANG_FILE}"
            -I "${FRONTIER_SHADER_SRC_DIR}"
            -target spirv
            -profile spirv_1_3
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -O0
            -entry "${ENTRY_NAME}"
            -stage "${SHADER_UNIT}"
            -o "${OUT_SPV}"
        DEPENDS
            "${FRONTIER_SHADER_SRC_DIR}/${SLANG_FILE}"
            "${FRONTIER_SHADER_SRC_DIR}/MatCapEvaluation.slang"
        COMMENT "[Slang] Compiling ${SLANG_FILE} (${SHADER_UNIT}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_slang_shader(SurfaceShading.slang           VertexEntry   vertex   SurfaceShading.vert.spv)
frontier_compile_slang_shader(SurfaceShading.slang           FragmentEntry fragment SurfaceShading.frag.spv)
frontier_compile_slang_shader(UVTileRasterization.slang      VertexEntry   vertex   UVTileRasterization.vert.spv)
frontier_compile_slang_shader(UVTileRasterization.slang      FragmentEntry fragment UVTileRasterization.frag.spv)
frontier_compile_slang_shader(GizmoRasterization.slang        VertexEntry   vertex   GizmoRasterization.vert.spv)
frontier_compile_slang_shader(GizmoRasterization.slang        FragmentEntry fragment GizmoRasterization.frag.spv)
frontier_compile_slang_shader(ImGuiOverlayRasterization.slang VertexEntry   vertex   ImGuiOverlayRasterization.vert.spv)
frontier_compile_slang_shader(ImGuiOverlayRasterization.slang FragmentEntry fragment ImGuiOverlayRasterization.frag.spv)
frontier_compile_slang_shader(SelectionMaskCompute.slang     ComputeEntry  compute  SelectionMaskCompute.comp.spv)
frontier_compile_slang_shader(ViewportPickReduction.slang    ComputeEntry  compute  ViewportPickReduction.comp.spv)
frontier_compile_slang_shader(UVDistortionInspection.slang   ComputeEntry  compute  UVDistortionInspection.comp.spv)
frontier_compile_slang_shader(StretchRelaxCompute.slang      ComputeEntry  compute  StretchRelaxCompute.comp.spv)
frontier_compile_slang_shader(ConcaveSkylineCompute.slang    ComputeEntry  compute  ConcaveSkylineCompute.comp.spv)
frontier_compile_slang_shader(CameraVisualEffects.slang      ComputeEntry  compute  CameraVisualEffects.comp.spv)

add_custom_target(FrontierShaders DEPENDS ${FRONTIER_SPV_OUTPUTS})

set(FRONTIER_ENGINE_SOURCES
    Engine/Configuration/EngineConfiguration.cpp
    Engine/Configuration/WorkspaceConfiguration.cpp
    Engine/Components/Camera/CameraComponent.cpp
    Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.cpp
    Engine/GeometrySpace/Network/PolygonNetwork.cpp
    Engine/GeometrySpace/Network/GeometryInstance.cpp
    Engine/GeometrySpace/Network/FacePartitionRegister.cpp
    Engine/GeometrySpace/Primitives/AnalyticPrimitives.cpp
    Engine/GeometrySpace/Selection/ComponentSelectionMask.cpp
    Engine/GeometrySpace/Selection/GeometrySelection.cpp
    Engine/Interchange/GeometryInterchange.cpp
    Engine/Editor/Viewports/TransformGizmo.cpp
    Engine/Editor/Viewports/ViewportProjection.cpp
    Engine/GeometricRaster/VulkanInterchange.cpp
    Engine/GeometricRaster/Compute/TimelineTransferRegister.cpp
    Engine/GeometricRaster/Compute/ComputeDispatchEncoder.cpp
    Engine/GeometricRaster/Picking/ViewportPickQuery.cpp
    Engine/GeometricRaster/UV/UVWorkspaceRasterizer.cpp
    Engine/Input/InputExchange.cpp
    Engine/Input/InputExtension.cpp
    Engine/Lifecycle/RuntimeLifecycle.cpp
    Engine/Windowing/PlatformWindowStub.cpp
    Engine/Threading/Sync/ThreadProgressionTracker.cpp
    Engine/Threading/Tasks/TaskChannel.cpp
    Engine/Threading/Tasks/ExecutionChannels.cpp
    Engine/Threading/Tasks/TaskScheduler.cpp
    Engine/Threading/Threads/Workspace/WorkspaceThread.cpp
    Engine/Threading/Threads/Streaming/AssetStreamThread.cpp
    Engine/Codecs/SpaceContainerCodec.cpp
    Engine/Codecs/RevisionCodec.cpp
    Engine/Codecs/ProjectCodec.cpp
    Engine/Codecs/InputCodec.cpp
    Engine/Codecs/ColourCodec.cpp
    Engine/Codecs/FontCodec.cpp
    Engine/Codecs/UVCodec.cpp
    Engine/Codecs/ImageCodec.cpp
    Engine/Codecs/TextureCodec.cpp
    Engine/Codecs/MaterialCodec.cpp
    Engine/Codecs/VectorCodec.cpp
    Engine/Workspaces/Revision/ContentRegion.cpp
    Engine/Workspaces/Revision/NonLinearHistory.cpp
    Engine/Workspaces/Revision/OperationRecorder.cpp
    Engine/Workspaces/Revision/CursorResolution.cpp
    Engine/Workspaces/Revision/RevisionTracker.cpp
    Engine/Workspaces/Revision/RevisionExtension.cpp
    Engine/Workspaces/WorkspaceDocument.cpp
    Engine/Workspaces/WorkspaceRegistry.cpp
    Engine/Workspaces/WorkspaceExchange.cpp
    Engine/Workspaces/WorkspaceExtension.cpp
    Engine/Workspaces/UV/Operations/UVAuthoringOperations.cpp
    Engine/Workspaces/UV/Solvers/SeamCutGenerator.cpp
    Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.cpp
    Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.cpp
    Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.cpp
    Engine/Workspaces/UV/Solvers/TexelDensityEqualizer.cpp
    Engine/Workspaces/UV/Solvers/UVProjectionSolvers.cpp
    Engine/Workspaces/UV/Solvers/UVIslandLayoutOperations.cpp
    Engine/Workspaces/UV/UVWorkspaceSession.cpp
)
if(NOT ANDROID)
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/Windowing/PlatformWindowDesktop.cpp)
endif()
if(FRONTIER_PROFILE STREQUAL "Editor")
    list(APPEND FRONTIER_ENGINE_SOURCES
        Engine/Editor/Explorer/ContentBrowser.cpp
        Engine/Lifecycle/EditorLifecycle.cpp
    )
endif()
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    list(APPEND FRONTIER_ENGINE_SOURCES ${FRONTIER_UI_COMPONENT_SOURCES})
endif()

add_library(FrontierEngine STATIC ${FRONTIER_ENGINE_SOURCES})
add_dependencies(FrontierEngine FrontierShaders)

target_include_directories(FrontierEngine PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/volk"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include"
)
target_compile_definitions(FrontierEngine PUBLIC
    "FRONTIER_COMPILED_SHADER_DIR=\"${FRONTIER_COMPILED_SHADER_DIR}\""
)
target_link_libraries(FrontierEngine PUBLIC FrontierCompileSettings Threads::Threads ${CMAKE_DL_LIBS})
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    target_link_libraries(FrontierEngine PUBLIC FrontierImGui)
endif()
if(NOT ANDROID)
    target_include_directories(FrontierEngine PUBLIC "${CMAKE_SOURCE_DIR}/ExternalPackages/glfw/include")
    target_link_libraries(FrontierEngine PRIVATE glfw)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                    DESKTOP APPLICATION EXECUTABLES (FRONTIERUV & FRONTIEREDITOR)
#------------------------------------------------------------------------------------------------------------------------

if(NOT ANDROID AND NOT FRONTIER_PROFILE STREQUAL "Shipping")
    add_executable(FrontierUV Engine/Lifecycle/FrontierUVLaunch.cpp)
    target_compile_definitions(FrontierUV PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
    target_link_libraries(FrontierUV PRIVATE FrontierEngine FrontierImGui)

    add_executable(FrontierEditor Engine/Lifecycle/FrontierEditorLaunch.cpp)
    target_compile_definitions(FrontierEditor PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
    target_link_libraries(FrontierEditor PRIVATE FrontierEngine FrontierImGui)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                         VALIDATION EXHIBITS (CTEST TARGETS)
#------------------------------------------------------------------------------------------------------------------------

if(BUILD_TESTING)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        add_library(FrontierShippingExhibitUi STATIC EXCLUDE_FROM_ALL ${FRONTIER_UI_COMPONENT_SOURCES})
        target_include_directories(FrontierShippingExhibitUi PUBLIC "${CMAKE_SOURCE_DIR}")
        target_link_libraries(FrontierShippingExhibitUi PUBLIC FrontierEngine FrontierImGui)
        if(NOT ANDROID)
            target_link_libraries(FrontierShippingExhibitUi PRIVATE glfw)
        endif()
    endif()

    function(frontier_register_exhibit EXHIBIT_NAME EXHIBIT_SOURCE)
        add_executable(${EXHIBIT_NAME} ${EXHIBIT_SOURCE} ${ARGN})
        target_include_directories(${EXHIBIT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_compile_definitions(${EXHIBIT_NAME} PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
        if(FRONTIER_PROFILE STREQUAL "Shipping")
            target_link_libraries(${EXHIBIT_NAME} PRIVATE FrontierShippingExhibitUi FrontierEngine FrontierImGui)
        else()
            target_link_libraries(${EXHIBIT_NAME} PRIVATE FrontierEngine FrontierImGui)
        endif()
        add_test(NAME ${EXHIBIT_NAME} COMMAND ${EXHIBIT_NAME})
    endfunction()

    frontier_register_exhibit(InputValidation             Exhibits/InputValidation.cpp)
    frontier_register_exhibit(CameraComponentValidation   Exhibits/CameraComponentValidation.cpp)
    frontier_register_exhibit(WindowValidation            Exhibits/WindowValidation.cpp)
    frontier_register_exhibit(DockingValidation           Exhibits/DockingValidation.cpp)
    frontier_register_exhibit(ControlCentreValidation     Exhibits/ControlCentreValidation.cpp)
    frontier_register_exhibit(WorkspacePanesValidation    Exhibits/WorkspacePanesValidation.cpp)
    frontier_register_exhibit(LifecycleValidation            Exhibits/LifecycleValidation.cpp)
    frontier_register_exhibit(WorkspaceRevisionValidation    Exhibits/WorkspaceRevisionValidation.cpp)
    frontier_register_exhibit(ContentBrowserValidation       Exhibits/ContentBrowserValidation.cpp)
    frontier_register_exhibit(GeometryNetworkValidation      Exhibits/GeometryNetworkValidation.cpp)
    frontier_register_exhibit(UVRasterPickingGizmoValidation    Exhibits/UVRasterPickingGizmoValidation.cpp)
    frontier_register_exhibit(UVWorkspaceUIValidation           Exhibits/UVWorkspaceUIValidation.cpp)
    frontier_register_exhibit(UVParameterizationValidation      Exhibits/UVParameterizationValidation.cpp)
    frontier_register_exhibit(UVInteractiveAuthoringValidation  Exhibits/UVInteractiveAuthoringValidation.cpp)
    frontier_register_exhibit(UVBoundaryValidation              Exhibits/UVBoundaryValidation.cpp)
    frontier_register_exhibit(UVAuthoringChaosValidation        Exhibits/UVAuthoringChaosValidation.cpp)

    if(NOT ANDROID AND NOT FRONTIER_PROFILE STREQUAL "Shipping")
        add_test(
            NAME FrontierUVExecutableSmoke
            COMMAND FrontierUV --frames=3 "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierUV_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierEditorExecutableSmoke
            COMMAND FrontierEditor --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierEditor_DesktopExecutable.png"
        )
    endif()
endif()
