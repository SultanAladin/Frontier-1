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
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(FRONTIER_PROFILE "Editor" CACHE STRING "Active Frontier build profile (Editor, Development, Shipping)")
set_property(CACHE FRONTIER_PROFILE PROPERTY STRINGS Editor Development Shipping)

option(FRONTIER_ENABLE_EOS_SDK    "Compile proprietary Epic Online Services transport adapter instead of open-source stub" OFF)
option(FRONTIER_ENABLE_PHOTON_SDK "Compile proprietary Photon Realtime C++ transport adapter instead of open-source stub" OFF)

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
    target_compile_definitions(FrontierCompileSettings INTERFACE
        _ENABLE_EXTENDED_ALIGNED_STORAGE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(FrontierCompileSettings INTERFACE /utf-8 /W4 /permissive- /bigobj)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE $<$<NOT:$<CONFIG:Debug>>:/O2> /DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE $<$<NOT:$<CONFIG:Debug>>:/O2> /Zi)
    endif()
else()
    target_compile_options(FrontierCompileSettings INTERFACE -Wall -Wextra -Wpedantic)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE -O3 -DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE -O2 -g)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                     PATCHED IMGUI (EXCLUDED FROM SHIPPING RUNTIME)
#------------------------------------------------------------------------------------------------------------------------

if(FRONTIER_PROFILE STREQUAL "Editor" OR BUILD_TESTING)
    set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
    if(NOT FRONTIER_PROFILE STREQUAL "Editor")
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
    Engine/Components/Controls/ColourRampEditor.cpp
    Engine/Components/Controls/ColourSwatchPicker.cpp
    Engine/Components/Controls/SimulationPlaybackControls.cpp
    Engine/Components/Controls/SimulationTimeRuler.cpp
    Engine/Components/Controls/SimulationTimelineFooter.cpp
    Engine/Components/Controls/ExecutionModeStrip.cpp
    Engine/Components/Controls/ScalarCurveEditor.cpp
    Engine/Components/Diagnostics/DiagnosticInfographicCanvas.cpp
    Engine/Components/Diagnostics/DiagnosticsOverlay.cpp
    Engine/Components/Diagnostics/LogStreamDrawer.cpp
    Engine/Components/Controls/ExclusiveSelector.cpp
    Engine/Components/Controls/CapsuleButton.cpp
    Engine/Components/Controls/ConstraintTilePicker.cpp
    Engine/Components/Controls/Vector3CapsuleInput.cpp
    Engine/Components/Controls/ThemePreviewTile.cpp
    Engine/Components/Controls/DropdownSelector.cpp
    Engine/Components/Controls/TextInputField.cpp
    Engine/Components/Controls/ConfirmationDialogue.cpp
    Engine/Components/Controls/NotificationSequence.cpp
    Engine/Components/Docking/WorkspaceTabStrip.cpp
    Engine/Components/Docking/WorkspaceDockContainer.cpp
    Engine/Components/Panes/WorkspaceAppearance.cpp
    Engine/Components/Panes/ExecutionSequencePane.cpp
    Engine/Components/Panes/OutlinerPanel.cpp
    Engine/Components/Panes/InspectorPanel.cpp
    Engine/Components/Panes/ViewportPanel.cpp
    Engine/Components/Panes/ViewportProjectionPane.cpp
    Engine/Components/Panes/DeterministicPathTracerPane.cpp
    Engine/Components/ControlCentre/ControlCentrePanel.cpp
    Engine/Components/ContentBrowser/ColourSwatchBrowserView.cpp
    Engine/Components/ContentBrowser/ThumbnailSheetRepository.cpp
    Engine/Components/ContentBrowser/ContentBrowserPanel.cpp
    Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.cpp
    Engine/Workspaces/ComponentEditor/ComponentTreePanel.cpp
    Engine/Workspaces/ComponentEditor/ComponentContentTray.cpp
    Engine/Workspaces/ComponentEditor/ComponentViewportHost.cpp
    Engine/Workspaces/ComponentEditor/ComponentDrawerPanel.cpp
    Engine/Workspaces/ComponentEditor/ComponentDetailsPanel.cpp
    Engine/Workspaces/ComponentEditor/ComponentWorkspaceHost.cpp
    Engine/Workspaces/UV/UVOutlinerPanel.cpp
    Engine/Workspaces/UV/UVInspectorPanel.cpp
    Engine/Workspaces/UV/UVViewportInteractor.cpp
    Engine/Workspaces/UV/UVViewportHost.cpp
    Engine/Workspaces/UV/UVWorkspaceHost.cpp
    Engine/Workspaces/Material/MaterialPreviewRenderer.cpp
    Engine/Workspaces/Material/MaterialWorkspaceSession.cpp
    Engine/Workspaces/Material/MaterialViewportHost.cpp
    Engine/Workspaces/Material/MaterialOutlinerPane.cpp
    Engine/Workspaces/Material/MaterialInspectorPane.cpp
    Engine/Workspaces/Material/MaterialWorkspaceHost.cpp
    Engine/Workspaces/Fracture/FractureViewportRenderer.cpp
    Engine/Workspaces/Fracture/FractureWorkspaceSession.cpp
    Engine/Workspaces/Fracture/FractureOutlinerPanel.cpp
    Engine/Workspaces/Fracture/FractureViewportHost.cpp
    Engine/Workspaces/Fracture/FractureInspectorPanel.cpp
    Engine/Workspaces/Fracture/FractureWorkspaceHost.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherIcons.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherStyleSheet.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherOutliner.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherOptions.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherNavigation.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherFileDialog.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherScript.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherOperationInput.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.cpp
    Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.cpp
    Engine/Workspaces/Texturing/PatternStampLayerStackPane.cpp
    Engine/Workspaces/Texturing/PatternStampToolHeaderPane.cpp
    Engine/Workspaces/Texturing/PatternStampInspectorPane.cpp
    Engine/Workspaces/Texturing/VectorViewportInteractor.cpp
    Engine/Workspaces/Texturing/PatternStampWorkspaceHost.cpp
    Engine/Workspaces/Texturing/BakingTargetsPane.cpp
    Engine/Workspaces/Texturing/BakingViewportPane.cpp
    Engine/Workspaces/Texturing/BakingInspectorPane.cpp
    Engine/Workspaces/Texturing/BakingWorkspaceHost.cpp
    Engine/Workspaces/Texturing/TexturePaintLayerStackPane.cpp
    Engine/Workspaces/Texturing/TexturePaintViewportPane.cpp
    Engine/Workspaces/Texturing/TexturePaintInspectorPane.cpp
    Engine/Workspaces/Texturing/TexturePaintWorkspaceHost.cpp
    Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.cpp
    Engine/GeometricRaster/Gizmo/WorldGizmoOverlayRasterization.cpp
    Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp
    Engine/Input/ImGuiInputAdapter.cpp
    Engine/Configuration/InputSettings.cpp
    Engine/Configuration/ProjectSpaceRepository.cpp
    Engine/Configuration/ProjectGenerator.cpp
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
set(FRONTIER_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/Engine/Shaders/GeometricRaster")
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
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/SkyViewTransmittanceCompute.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudNoise.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudDensity.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudTransport.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudAerialPerspective.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/VolumetricCloudCompute.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CelestialDiscStarCompute.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/LensFlareCompute.slang"
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
frontier_compile_slang_shader(WorldViewportBlit.slang         VertexEntry   vertex   WorldViewportBlit.vert.spv)
frontier_compile_slang_shader(WorldViewportBlit.slang         FragmentEntry fragment WorldViewportBlit.frag.spv)
frontier_compile_slang_shader(ImGuiOverlayRasterization.slang VertexEntry   vertex   ImGuiOverlayRasterization.vert.spv)
frontier_compile_slang_shader(ImGuiOverlayRasterization.slang FragmentEntry fragment ImGuiOverlayRasterization.frag.spv)
frontier_compile_slang_shader(SelectionMaskCompute.slang     ComputeEntry  compute  SelectionMaskCompute.comp.spv)
frontier_compile_slang_shader(ViewportPickReduction.slang    ComputeEntry  compute  ViewportPickReduction.comp.spv)
frontier_compile_slang_shader(UVDistortionInspection.slang   ComputeEntry  compute  UVDistortionInspection.comp.spv)
frontier_compile_slang_shader(StretchRelaxCompute.slang               ComputeEntry  compute  StretchRelaxCompute.comp.spv)
frontier_compile_slang_shader(ConcaveSkylineCompute.slang             ComputeEntry  compute  ConcaveSkylineCompute.comp.spv)
frontier_compile_slang_shader(CameraVisualEffects.slang               ComputeEntry  compute  CameraVisualEffects.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   ComputeEntry              compute  WorldViewportRadiometricCompute.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   CloudComputeEntry         compute  WorldViewportCloudCompute.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   DenoiseCompositeEntry     compute  WorldViewportDenoiseCompute.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   ShadowCascadeComputeEntry compute  WorldViewportShadowCascadeCompute.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   GlobalDistanceBakeEntry   compute  WorldViewportGlobalDistanceBake.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   GlobalRadiosityBakeEntry  compute  WorldViewportGlobalRadiosityBake.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   ViewportPathTraceEntry    compute  WorldViewportPathTraceCompute.comp.spv)

set(FRONTIER_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/Generated")
file(MAKE_DIRECTORY "${FRONTIER_GENERATED_INCLUDE_DIR}")
set(FRONTIER_PLANETARY_CLOUD_GENERATED "${FRONTIER_GENERATED_INCLUDE_DIR}/PlanetaryCloudGenerated.hpp")
add_custom_command(
    OUTPUT "${FRONTIER_PLANETARY_CLOUD_GENERATED}"
    COMMAND "${FRONTIER_SLANGC_BIN}"
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudCompute.slang"
        -I "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere"
        -entry CloudCaptureEntry
        -target cpp
        -fp-mode precise
        -O0
        -o "${FRONTIER_PLANETARY_CLOUD_GENERATED}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudNoise.slang"
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudDensity.slang"
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudTransport.slang"
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudAerialPerspective.slang"
        "${CMAKE_SOURCE_DIR}/Engine/Shaders/Atmosphere/CloudCompute.slang"
    COMMENT "[Slang->C++] Generating single-source PlanetaryCloudGenerated.hpp"
    VERBATIM
)
list(APPEND FRONTIER_SPV_OUTPUTS "${FRONTIER_PLANETARY_CLOUD_GENERATED}")

set(FRONTIER_MODULAR_SHADER_DIR "${CMAKE_SOURCE_DIR}/Engine/Shaders")

function(frontier_compile_modular_slang_shader DOMAIN_REL_PATH ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    file(GLOB DOMAIN_SLANG_DEPS
        "${FRONTIER_MODULAR_SHADER_DIR}/MaterialSpace/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/Visibility/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/DirectLighting/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/GlobalIllumination/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/Atmosphere/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/Colour/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/Compute/*.slang"
    )
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${FRONTIER_MODULAR_SHADER_DIR}/${DOMAIN_REL_PATH}"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/MaterialSpace"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/Visibility"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/DirectLighting"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/GlobalIllumination"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/Atmosphere"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/Colour"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/Compute"
            -I "${FRONTIER_MODULAR_SHADER_DIR}"
            -target spirv
            -profile spirv_1_3+spvGroupNonUniformArithmetic+spvInt64Atomics
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -O0
            -entry "${ENTRY_NAME}"
            -stage "${SHADER_UNIT}"
            -o "${OUT_SPV}"
        DEPENDS
            "${FRONTIER_MODULAR_SHADER_DIR}/${DOMAIN_REL_PATH}"
            ${DOMAIN_SLANG_DEPS}
        COMMENT "[Slang] Compiling ${DOMAIN_REL_PATH} (${SHADER_UNIT}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_modular_slang_shader(MaterialSpace/BounceLayerSummary.slang                     BounceSummaryComputeEntry       compute  BounceLayerSummary.comp.spv)
frontier_compile_modular_slang_shader(Colour/ColourRampSampling.slang                            SampleColourRampLutComputeEntry compute  ColourRampSampling.comp.spv)
frontier_compile_modular_slang_shader(MaterialSpace/OpenPbrSurfaceScattering.slang               ComputeEntry                    compute  OpenPbrSurfaceScattering.comp.spv)
frontier_compile_modular_slang_shader(MaterialSpace/DynamicMaterialResolve.slang                 DynamicStudioPreviewCompute     compute  DynamicMaterialResolve.comp.spv)
frontier_compile_modular_slang_shader(GeometricRaster/FractureSurfaceResolve.slang               FractureVertexEntry             vertex   FractureSurfaceResolve.vert.spv)
frontier_compile_modular_slang_shader(GeometricRaster/FractureSurfaceResolve.slang               FractureFragmentEntry           fragment FractureSurfaceResolve.frag.spv)
frontier_compile_modular_slang_shader(Visibility/DepthPyramidDownsample.slang                    DepthPyramidComputeEntry        compute  DepthPyramidDownsample.comp.spv)
frontier_compile_modular_slang_shader(Visibility/ClusterVisibilityCull.slang                     ClusterCullComputeEntry         compute  ClusterVisibilityCull.comp.spv)
frontier_compile_modular_slang_shader(Visibility/HardwareVisibilityRasterization.slang           VisibilityVertexEntry           vertex   HardwareVisibilityRasterization.vert.spv)
frontier_compile_modular_slang_shader(Visibility/HardwareVisibilityRasterization.slang           VisibilityFragmentEntry         fragment HardwareVisibilityRasterization.frag.spv)
frontier_compile_modular_slang_shader(Visibility/MicroFaceSoftwareRasterization.slang            MicroFaceComputeEntry           compute  MicroFaceSoftwareRasterization.comp.spv)
frontier_compile_modular_slang_shader(Visibility/AnalyticBarycentricResolve.slang                AnalyticResolveComputeEntry     compute  AnalyticBarycentricResolve.comp.spv)
frontier_compile_modular_slang_shader(DirectLighting/SolarFrustumShadow.slang                    SolarShadowComputeEntry         compute  SolarFrustumShadow.comp.spv)
frontier_compile_modular_slang_shader(DirectLighting/PlaneAwareShadowFilter.slang                PlaneFilterComputeEntry         compute  PlaneAwareShadowFilter.comp.spv)
frontier_compile_modular_slang_shader(GlobalIllumination/SpecularGlassRaytrace.slang          SpecularRaytraceComputeEntry    compute  SpecularGlassRaytrace.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/SkyViewTransmittanceCompute.slang              TransmittanceLutComputeEntry    compute  TransmittanceLutCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/SkyViewTransmittanceCompute.slang              MultiScatterLutComputeEntry     compute  MultiScatterLutCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/SkyViewTransmittanceCompute.slang              ComputeEntry                    compute  SkyViewTransmittanceCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/VolumetricCloudCompute.slang                   ComputeEntry                    compute  VolumetricCloudCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/VolumetricFogCompute.slang                     ComputeEntry                    compute  VolumetricFogCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/CelestialDiscStarCompute.slang                 ComputeEntry                    compute  CelestialDiscStarCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/StellarTileCullCompute.slang                 ClearStellarTilesEntry          compute  StellarTileClearCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/StellarTileCullCompute.slang                 ProjectCullStellarTilesEntry    compute  StellarTileCullCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/LensFlareBakeCompute.slang                     ComputeEntry                    compute  LensFlareBakeCompute.comp.spv)
frontier_compile_modular_slang_shader(Atmosphere/LensFlareCompute.slang                         ComputeEntry                    compute  LensFlareCompute.comp.spv)

function(frontier_compile_sketcher_slang_shader SLANG_FILE ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/Engine/Shaders/Sketcher/${SLANG_FILE}")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            -profile spirv_1_5
            -target spirv
            -emit-spirv-directly
            -O0
            -I "${CMAKE_SOURCE_DIR}"
            -entry "${ENTRY_NAME}"
            -stage "${SHADER_UNIT}"
            "${IN_SLANG}"
            -o "${OUT_SPV}"
        DEPENDS
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Sketcher/SketcherProjection.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Sketcher/SketcherMatCapLookup.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Sketcher/SketcherCadStroke.slang"
        COMMENT "[Slang] Compiling Sketcher/${SLANG_FILE} (${SHADER_UNIT}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_sketcher_slang_shader(SketcherSurface.slang    SketcherSurfaceVertex          vertex   SketcherSurface.vert.spv)
frontier_compile_sketcher_slang_shader(SketcherSurface.slang    SketcherSurfaceFragment        fragment SketcherSurface.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherEdge.slang       SketcherEdgeVertex             vertex   SketcherEdge.vert.spv)
frontier_compile_sketcher_slang_shader(SketcherEdge.slang       SketcherEdgeFragment           fragment SketcherEdge.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherEdge.slang       SketcherEdgeOccludedFragment   fragment SketcherEdgeOccluded.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherEdge.slang       SketcherEdgeIdentityFragment   fragment SketcherEdgeIdentity.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherGrid.slang       SketcherGridVertex             vertex   SketcherGrid.vert.spv)
frontier_compile_sketcher_slang_shader(SketcherGrid.slang       SketcherGridFragment           fragment SketcherGrid.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherImagePlane.slang SketcherImagePlaneVertex       vertex   SketcherImagePlane.vert.spv)
frontier_compile_sketcher_slang_shader(SketcherImagePlane.slang SketcherImagePlaneFragment     fragment SketcherImagePlane.frag.spv)
frontier_compile_sketcher_slang_shader(SketcherGizmo.slang      SketcherGizmoVertex            vertex   SketcherGizmo.vert.spv)
frontier_compile_sketcher_slang_shader(SketcherGizmo.slang      SketcherGizmoFragment          fragment SketcherGizmo.frag.spv)

function(frontier_compile_vector_slang_shader SLANG_REL_PATH ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/Engine/Shaders/Vector/${SLANG_REL_PATH}")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${IN_SLANG}"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders/Vector"
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
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Vector/VectorSharedLayouts.slang"
        COMMENT "[Slang] Compiling Vector/${SLANG_REL_PATH} (${SHADER_UNIT}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_vector_slang_shader(VectorModifierExpand.slang    ComputeEntry  compute  VectorModifierExpand.comp.spv)
frontier_compile_vector_slang_shader(VectorStencilFill.slang       VertexEntry   vertex   VectorStencilFill.vert.spv)
frontier_compile_vector_slang_shader(VectorStencilFill.slang       FragmentEntry fragment VectorStencilFill.frag.spv)
frontier_compile_vector_slang_shader(VectorShapeCover.slang        VertexEntry       vertex   VectorShapeCover.vert.spv)
frontier_compile_vector_slang_shader(VectorShapeCover.slang        FragmentEntry     fragment VectorShapeCover.frag.spv)
frontier_compile_vector_slang_shader(VectorShapeCover.slang        StrokeVertexEntry   vertex   VectorStrokeCover.vert.spv)
frontier_compile_vector_slang_shader(VectorShapeCover.slang        StrokeFragmentEntry fragment VectorStrokeCover.frag.spv)
frontier_compile_vector_slang_shader(VectorLayerStackCompute.slang ComputeEntry  compute  VectorLayerStackCompute.comp.spv)
frontier_compile_vector_slang_shader(VectorPbrSurfaceCompute.slang ComputeEntry  compute  VectorPbrSurfaceCompute.comp.spv)
frontier_compile_vector_slang_shader(VectorGuideOverlay.slang      VertexEntry   vertex   VectorGuideOverlay.vert.spv)
frontier_compile_vector_slang_shader(VectorGuideOverlay.slang      FragmentEntry fragment VectorGuideOverlay.frag.spv)

function(frontier_compile_baking_slang_shader SLANG_REL_PATH ENTRY_NAME SHADER_UNIT OUT_SPV_NAME USE_HW_RAYQUERY)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/Engine/Shaders/Baking/${SLANG_REL_PATH}")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    if(USE_HW_RAYQUERY)
        set(BAKE_SLANG_FLAGS
            -target spirv
            -profile spirv_1_4
            -capability spvRayQueryKHR
            "-DFRONTIER_USE_HW_RAY_QUERY=1"
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -O0)
    else()
        set(BAKE_SLANG_FLAGS
            -target spirv
            -profile spirv_1_3
            "-DFRONTIER_USE_HW_RAY_QUERY=0"
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -O0)
    endif()
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${IN_SLANG}"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders/Baking"
            -I "${FRONTIER_SHADER_SRC_DIR}"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders/DirectLighting"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            ${BAKE_SLANG_FLAGS}
            -entry "${ENTRY_NAME}"
            -stage "${SHADER_UNIT}"
            -o "${OUT_SPV}"
        DEPENDS
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Baking/BakeShaderLayouts.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/DirectLighting/WideTreeRayQuery.slang"
        COMMENT "[Slang] Compiling Baking/${SLANG_REL_PATH} (${SHADER_UNIT}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_baking_slang_shader(BakeLowUvRasterization.slang    VertexEntry   vertex   BakeLowUvRasterization.vert.spv    FALSE)
frontier_compile_baking_slang_shader(BakeLowUvRasterization.slang    FragmentEntry fragment BakeLowUvRasterization.frag.spv    FALSE)
frontier_compile_baking_slang_shader(BakeSurfaceTransferCompute.slang ComputeEntry compute  BakeSurfaceTransferCompute_GTX.comp.spv FALSE)
frontier_compile_baking_slang_shader(BakeSurfaceTransferCompute.slang ComputeEntry compute  BakeSurfaceTransferCompute_RTX.comp.spv TRUE)
frontier_compile_baking_slang_shader(BakeChannelResolveCompute.slang ComputeEntry  compute  BakeChannelResolveCompute.comp.spv FALSE)
frontier_compile_baking_slang_shader(BakeDilationCompute.slang       ComputeEntry  compute  BakeDilationCompute.comp.spv       FALSE)
frontier_compile_baking_slang_shader(BakeVolumeSdfCompute.slang      ComputeEntry  compute  BakeVolumeSdfCompute_GTX.comp.spv  FALSE)
frontier_compile_baking_slang_shader(BakeVolumeSdfCompute.slang      ComputeEntry  compute  BakeVolumeSdfCompute_RTX.comp.spv  TRUE)
frontier_compile_baking_slang_shader(ExtractSdfSectionCompute.slang  ComputeEntry  compute  ExtractSdfSectionCompute.comp.spv  FALSE)
frontier_compile_baking_slang_shader(BakeViewportPreview.slang       VertexEntry   vertex   BakeViewportPreview.vert.spv       FALSE)
frontier_compile_baking_slang_shader(BakeViewportPreview.slang       FragmentEntry fragment BakeViewportPreview.frag.spv       FALSE)

function(frontier_compile_component_editor_slang_shader SLANG_STEM ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/Engine/Shaders/ComponentEditor/${SLANG_STEM}.slang")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${IN_SLANG}"
            -I "${CMAKE_SOURCE_DIR}"
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
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/ComponentEditor/ComponentEditorShared.slang"
        COMMENT "[Slang] Compiling ComponentEditor/${SLANG_STEM}.slang (${SHADER_UNIT}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

foreach(CE_SHADER_STEM
    ComponentChannelInspection
    ComponentFieldHull
    ComponentLightVolume
    ComponentDotFloor
    ComponentConstraintVisual)
    frontier_compile_component_editor_slang_shader("${CE_SHADER_STEM}" "VertexMain"   "vertex"   "${CE_SHADER_STEM}.vert.spv")
    frontier_compile_component_editor_slang_shader("${CE_SHADER_STEM}" "FragmentMain" "fragment" "${CE_SHADER_STEM}.frag.spv")
endforeach()

function(frontier_compile_world_module_slang_shader SLANG_REL_PATH ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/Engine/Shaders/${SLANG_REL_PATH}")
    set(OUT_SPV  "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${IN_SLANG}"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders"
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
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/DirectLighting/WideTreeRayQuery.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/GlobalIllumination/QuadratureSequence.slang"
        COMMENT "[Slang] Compiling ${SLANG_REL_PATH} (${SHADER_UNIT}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_world_module_slang_shader(DirectLighting/RefitWideBoundingTreeCompute.slang            ComputeEntry           compute  RefitWideBoundingTreeCompute.comp.spv)
frontier_compile_world_module_slang_shader(DirectLighting/HybridConeShadowCompute.slang                 ComputeEntry           compute  HybridConeShadowCompute.comp.spv)
frontier_compile_world_module_slang_shader(DirectLighting/SdfEmissivePenumbraCompute.slang              ComputeEntry           compute  SdfEmissivePenumbraCompute.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/StokesContourIrradianceCompute.slang      ComputeEntry           compute  StokesContourIrradianceCompute.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/SurfelRadiosityLatticeCompute.slang       ComputeEntry           compute  SurfelRadiosityLatticeCompute.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DeterministicRadianceIntegration.slang    IntegrateRadiance      compute  DeterministicRadianceIntegration.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DeterministicRadianceIntegration.slang    ReprojectHistory       compute  DeterministicHistoryReproject.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DeterministicRadianceIntegration.slang    ResolveSurfaceFeatures compute  DeterministicSurfaceFeatures.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DeterministicRadianceIntegration.slang    ExtractQuadrature      compute  DeterministicQuadratureExtract.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/VisibilityTriangleRasterization.slang     VertexEntry            vertex   VisibilityTriangleRasterization.vert.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/VisibilityTriangleRasterization.slang     FragmentEntry          fragment VisibilityTriangleRasterization.frag.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DisplayReconstructionFilter.slang         GatherEstimate         compute  DisplayReconstructionGather.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DisplayReconstructionFilter.slang         FilterAtrous           compute  DisplayReconstructionAtrous.comp.spv)
frontier_compile_world_module_slang_shader(GlobalIllumination/DisplayToneTransform.slang                ComputeEntry           compute  DisplayToneTransform.comp.spv)

add_custom_target(FrontierShaders DEPENDS ${FRONTIER_SPV_OUTPUTS})

set(FRONTIER_ENGINE_SOURCES
    Engine/Lifecycle/ThoughtLog.cpp
    Engine/Configuration/EngineConfiguration.cpp
    Engine/Configuration/WorkspaceConfiguration.cpp
    Engine/MaterialSpace/SurfaceMaterial.cpp
    Engine/MaterialSpace/CompactMaterialRegister.cpp
    Engine/MaterialSpace/MaterialSurfaceLayer/MaterialSurfaceLayer.cpp
    Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.cpp
    Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.cpp
    Engine/MaterialSpace/EnergyCompensation/SheenCosineIntegral.cpp
    Engine/MaterialSpace/Baking/ProceduralChannelBaker.cpp
    Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.cpp
    Engine/GeometricRaster/Baking/GeometrySpecimenRasterizer.cpp
    Engine/MaterialSpace/Dynamic/DynamicParameterBlock.cpp
    Engine/MaterialSpace/Dynamic/DynamicMaterialRecipe.cpp
    Engine/MaterialSpace/Dynamic/DynamicMaterialCompiler.cpp
    Engine/MaterialSpace/Dynamic/DynamicMaterialRepository.cpp
    Engine/MaterialSpace/Dynamic/DynamicSurfaceResolver.cpp
    Engine/GeometricRaster/Visibility/DepthPyramidSequence.cpp
    Engine/GeometricRaster/Visibility/ClusterCullEncoder.cpp
    Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.cpp
    Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.cpp
    Engine/GeometricRaster/DirectLighting/SolarShadowSequence.cpp
    Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.cpp
    Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.cpp
    Engine/GeometricRaster/DirectLighting/SdfEmissivePenumbraSequence.cpp
    Engine/GeometricRaster/DirectLighting/PhotometricLightTree.cpp
    Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/SurfelRadiosityLatticeSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.cpp
    Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.cpp
    Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.cpp
    Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.cpp
    Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.cpp
    Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.cpp
    Engine/GeometricRaster/Atmosphere/VolumetricFogSequence.cpp
    Engine/GeometricRaster/Atmosphere/LensFlareSequence.cpp
    Engine/Components/Camera/CameraComponent.cpp
    Engine/Components/Camera/CameraLagComponent.cpp
    Engine/Components/Camera/VisualEffects/CameraOpticsEncoder.cpp
    Engine/GeometrySpace/Network/PolygonNetwork.cpp
    Engine/GeometrySpace/Network/GeometryInstance.cpp
    Engine/GeometrySpace/Network/FacePartitionRegister.cpp
    Engine/GeometrySpace/Primitives/AnalyticPrimitives.cpp
    Engine/GeometrySpace/Primitives/RevolutionSweepPrimitives.cpp
    Engine/GeometrySpace/Primitives/WatertightSewing.cpp
    Engine/GeometrySpace/Analysis/CurvatureAnalysis.cpp
    Engine/GeometrySpace/Analysis/StretchMeasurement.cpp
    Engine/GeometrySpace/Selection/ComponentSelectionMask.cpp
    Engine/GeometrySpace/Selection/GeometrySelection.cpp
    Engine/GeometrySpace/Selection/IslandFloodSelection.cpp
    Engine/GeometrySpace/Fracture/FractureMaterialRepository.cpp
    Engine/GeometrySpace/Fracture/FastRuntimeFracture.cpp
    Engine/GeometrySpace/Fracture/EngineeringBakedFracture.cpp
    Engine/GeometrySpace/Fracture/ConvexPartitioning.cpp
    Engine/GeometrySpace/Parametric/Arrangement/ArrangementAnalysis.cpp
    Engine/GeometrySpace/Parametric/Arrangement/JointSolver.cpp
    Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.cpp
    Engine/GeometrySpace/Parametric/Audit/IntersectionAudit.cpp
    Engine/GeometrySpace/Parametric/Audit/ModelAudit.cpp
    Engine/GeometrySpace/Parametric/Audit/TessellationAudit.cpp
    Engine/GeometrySpace/Parametric/Blend/BlendRepository.cpp
    Engine/GeometrySpace/Parametric/Blend/EdgeBlend.cpp
    Engine/GeometrySpace/Parametric/Blend/FairPatch.cpp
    Engine/GeometrySpace/Parametric/Blend/RollingBallGeometry.cpp
    Engine/GeometrySpace/Parametric/Boolean/BooleanRepository.cpp
    Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.cpp
    Engine/GeometrySpace/Parametric/Construction/BodyArrangement.cpp
    Engine/GeometrySpace/Parametric/Construction/BodyModifiers.cpp
    Engine/GeometrySpace/Parametric/Construction/BodyTransform.cpp
    Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.cpp
    Engine/GeometrySpace/Parametric/Containment/FaceDomain.cpp
    Engine/GeometrySpace/Parametric/Containment/WindingNumber.cpp
    Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.cpp
    Engine/GeometrySpace/Parametric/Drawing/DrawingExport.cpp
    Engine/GeometrySpace/Parametric/Drawing/DrawingProjection.cpp
    Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.cpp
    Engine/GeometrySpace/Parametric/Exchange/StepExchange.cpp
    Engine/GeometrySpace/Parametric/Exchange/StepReading.cpp
    Engine/GeometrySpace/Parametric/Exchange/StlExchange.cpp
    Engine/GeometrySpace/Parametric/Feature/BodyCut.cpp
    Engine/GeometrySpace/Parametric/Feature/FeatureRepository.cpp
    Engine/GeometrySpace/Parametric/Feature/FeatureTree.cpp
    Engine/GeometrySpace/Parametric/Feature/PathSweep.cpp
    Engine/GeometrySpace/Parametric/Feature/ProfileSweep.cpp
    Engine/GeometrySpace/Parametric/Feature/ReplayDigest.cpp
    Engine/GeometrySpace/Parametric/Feature/TopologicalNaming.cpp
    Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.cpp
    Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.cpp
    Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.cpp
    Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.cpp
    Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.cpp
    Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.cpp
    Engine/GeometrySpace/Parametric/Inspection/BlendEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/BooleanEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/FaceOperationEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/FeatureEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/IntersectionRepository.cpp
    Engine/GeometrySpace/Parametric/Inspection/IntersectionEncoding.cpp
    Engine/GeometrySpace/Parametric/Inspection/SpecimenRepository.cpp
    Engine/GeometrySpace/Parametric/Inspection/TessellationEncoding.cpp
    Engine/GeometrySpace/Parametric/Intersection/BernsteinPolynomial.cpp
    Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.cpp
    Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.cpp
    Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.cpp
    Engine/GeometrySpace/Parametric/Intersection/IsoParameterCurve.cpp
    Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.cpp
    Engine/GeometrySpace/Parametric/Local/FaceChange.cpp
    Engine/GeometrySpace/Parametric/Local/FaceOperationRepository.cpp
    Engine/GeometrySpace/Parametric/Local/FaceOperations.cpp
    Engine/GeometrySpace/Parametric/Local/RedundantConnectivity.cpp
    Engine/GeometrySpace/Parametric/Local/ConnectivityRebuild.cpp
    Engine/GeometrySpace/Parametric/Measurement/MassProperties.cpp
    Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.cpp
    Engine/GeometrySpace/Parametric/Measurement/PlaneSection.cpp
    Engine/GeometrySpace/Parametric/Precision/ExactPredicates.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchRepository.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchConstraining.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchCorner.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchEditing.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchRailDeform.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchProfile.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchRegion.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchSection.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchSolver.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchSpline.cpp
    Engine/GeometrySpace/Parametric/Sketch/SketchTrim.cpp
    Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.cpp
    Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.cpp
    Engine/GeometrySpace/Parametric/Surfacing/CurveNetwork.cpp
    Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.cpp
    Engine/GeometrySpace/Parametric/Surfacing/PanelJoining.cpp
    Engine/GeometrySpace/Parametric/Surfacing/SectionLoft.cpp
    Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.cpp
    Engine/GeometrySpace/Parametric/Surfacing/SurfaceDistanceCurve.cpp
    Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.cpp
    Engine/GeometrySpace/Parametric/Tessellation/ChartTriangulation.cpp
    Engine/GeometrySpace/Parametric/Connectivity/EulerEdits.cpp
    Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherCamera.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherGizmoSpace.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherPortableImage.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.cpp
    Engine/Workspaces/ParametricSketcher/Viewport/SketcherSurfaceTracing.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherCurveImport.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherKnife.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherMeasurement.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherScene.cpp
    Engine/Workspaces/ParametricSketcher/Document/SketcherSpecimen.cpp
    Engine/DistanceFieldSpace/LocalSignedDistanceVolume.cpp
    Engine/SpatialAcceleration/ClusterPyramid.cpp
    Engine/SpatialAcceleration/WideBoundingTree.cpp
    Engine/SpatialAcceleration/CompressedWideTree.cpp
    Engine/AudioSpace/AudioBusAccumulator.cpp
    Engine/AudioSpace/AudioExtension.cpp
    Engine/AudioSpace/AudioHardwareOutput.cpp
    Engine/AudioSpace/SpatialAcoustics.cpp
    Engine/AudioSpace/WaveformDecoder.cpp
    Engine/ColourSpace/TransferFunction.cpp
    Engine/ColourSpace/WhitePointAdaptation.cpp
    Engine/ColourSpace/ChromaticitySpace.cpp
    Engine/ColourSpace/SpectralDistribution.cpp
    Engine/ColourSpace/GamutCompression.cpp
    Engine/ColourSpace/ColourRampSampler.cpp
    Engine/ColourSpace/ColourPresetRepository.cpp
    Engine/ColourSpace/ColourAliasRepository.cpp
    Engine/ColourSpace/ColourDocument.cpp
    Engine/ColourSpace/ColourAssetRegister.cpp
    Engine/Interchange/ColourInterchange.cpp
    Engine/Interchange/GeometryInterchange.cpp
    Engine/Interchange/IndexedTriangleStreamInterchange.cpp
    Engine/GeometricRaster/Gizmo/TransformGizmo.cpp
    Engine/Components/Camera/ViewportProjection.cpp
    Engine/GeometricRaster/VulkanInterchange.cpp
    Engine/GeometricRaster/Compute/TimelineTransferRegister.cpp
    Engine/GeometricRaster/Compute/ComputeDispatchEncoder.cpp
    Engine/GeometricRaster/Picking/ViewportPickQuery.cpp
    Engine/GeometricRaster/UV/UVWorkspaceRasterizer.cpp
    Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.cpp
    Engine/GeometricRaster/Vector/VectorCanvasRasterizer.cpp
    Engine/GeometricRaster/Baking/StudyGeometryBuilder.cpp
    Engine/GeometricRaster/Baking/BakingAccelerationStructure.cpp
    Engine/GeometricRaster/Baking/GpuTextureBaker.cpp
    Engine/GeometricRaster/Baking/GpuSdfBaker.cpp
    Engine/GeometricRaster/Baking/TextureBakerRasterizer.cpp
    Engine/GeometricRaster/ComponentEditor/ComponentGizmoGeometry.cpp
    Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.cpp
    Engine/GeometricRaster/ComponentEditor/ComponentSpecimenBaker.cpp
    Engine/Workspaces/Texturing/PatternStampStackSession.cpp
    Engine/Workspaces/Texturing/BakingWorkspaceSession.cpp
    Engine/Workspaces/Texturing/TexturePaintSession.cpp
    Engine/Input/InputExchange.cpp
    Engine/Input/InputExtension.cpp
    Engine/Lifecycle/RuntimeLifecycle.cpp
    Engine/Lifecycle/ProjectExecutionRunner.cpp
    Engine/Workspaces/WorkspaceCapabilityRegister.cpp
    Engine/Windowing/PlatformWindowStub.cpp
    Engine/Threading/Sync/ThreadProgressionTracker.cpp
    Engine/Threading/Tasks/TaskChannel.cpp
    Engine/Threading/Tasks/ExecutionChannels.cpp
    Engine/Threading/Tasks/TaskScheduler.cpp
    Engine/Threading/Threads/Audio/AudioThread.cpp
    Engine/Threading/Threads/Workspace/WorkspaceThread.cpp
    Engine/Threading/Threads/Streaming/AssetStreamThread.cpp
    Engine/Codecs/AcousticCodec.cpp
    Engine/Codecs/SpaceContainerCodec.cpp
    Engine/Codecs/RevisionCodec.cpp
    Engine/Codecs/ProjectCodec.cpp
    Engine/Codecs/InputCodec.cpp
    Engine/Codecs/ColourCodec.cpp
    Engine/Codecs/GeometryCodec.cpp
    Engine/Codecs/FontCodec.cpp
    Engine/Codecs/UVCodec.cpp
    Engine/Codecs/ImageCodec.cpp
    Engine/Codecs/TextureCodec.cpp
    Engine/Codecs/MaterialCodec.cpp
    Engine/Codecs/VectorCodec.cpp
    Engine/Codecs/NetworkCodec.cpp
    Engine/Codecs/ComponentSpaceCodec.cpp
    Engine/Codecs/PhotometricProfile.cpp
    Engine/Codecs/PhotometrySpaceCodec.cpp
    Engine/Codecs/VolumetricSpaceCodec.cpp
    Engine/Codecs/RenderSpaceCodec.cpp
    Engine/Codecs/WorldSpaceCodec.cpp
    Engine/Codecs/ControlVehicleSurfaceDecoder.cpp
    Engine/Tools/DefaultContentGenerator.cpp
    Engine/ComponentClass/ComponentInstanceStorage.cpp
    Engine/ComponentClass/ComponentClassDefinition.cpp
    Engine/ComponentClass/ComponentClassValidator.cpp
    Engine/ComponentClass/ComponentWorldPlacement.cpp
    Engine/ComponentClass/ControlVehicleClassBuilder.cpp
    Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.cpp
    Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.cpp
    Engine/SimulationSpace/PhysicsSpace/CollisionDetection.cpp
    Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.cpp
    Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.cpp
    Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.cpp
    Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.cpp
    Engine/NetworkingSpace/PacketSerializer.cpp
    Engine/NetworkingSpace/NetworkEndpointRegister.cpp
    Engine/NetworkingSpace/DifferentialWorldEncoder.cpp
    Engine/NetworkingSpace/RemoteDirectiveRouter.cpp
    Engine/NetworkingSpace/NetworkingExtension.cpp
    Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.cpp
    Engine/NetworkingSpace/Policies/TransportSelectionPolicy.cpp
    Engine/NetworkingSpace/Policies/ReplicationPolicy.cpp
    Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.cpp
    Engine/NetworkingSpace/Transports/LoopbackTransport.cpp
    Engine/NetworkingSpace/Transports/SocketTransport.cpp
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
if(FRONTIER_ENABLE_EOS_SDK)
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/NetworkingSpace/Transports/EpicOnlineTransport.cpp)
else()
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/NetworkingSpace/Transports/EpicStubTransport.cpp)
endif()
if(FRONTIER_ENABLE_PHOTON_SDK)
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/NetworkingSpace/Transports/PhotonRealtimeTransport.cpp)
else()
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/NetworkingSpace/Transports/PhotonStubTransport.cpp)
endif()
if(FRONTIER_PROFILE STREQUAL "Editor")
    list(APPEND FRONTIER_ENGINE_SOURCES
        Engine/Editor/Explorer/ContentBrowser.cpp
        Engine/Editor/Explorer/ProjectBrowser.cpp
        Engine/Lifecycle/EditorLifecycle.cpp
    )
endif()
if(FRONTIER_PROFILE STREQUAL "Editor")
    list(APPEND FRONTIER_ENGINE_SOURCES ${FRONTIER_UI_COMPONENT_SOURCES})
endif()

include(Automation/ThorVGVector.cmake)
include(Automation/JoltPhysics.cmake)

set_source_files_properties(Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${THORVG_VECTOR_SPI_INCLUDES}")

add_library(FrontierEngine STATIC ${FRONTIER_ENGINE_SOURCES})
add_dependencies(FrontierEngine FrontierShaders thorvg_static Jolt)

target_include_directories(FrontierEngine PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${FRONTIER_GENERATED_INCLUDE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/volk"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/stb"
)
target_compile_definitions(FrontierEngine PUBLIC
    "FRONTIER_COMPILED_SHADER_DIR=\"${FRONTIER_COMPILED_SHADER_DIR}\""
    "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\""
)
target_link_libraries(FrontierEngine PUBLIC FrontierCompileSettings thorvg_static Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(FrontierEngine PRIVATE Jolt)
if(WIN32)
    target_link_libraries(FrontierEngine PUBLIC ws2_32)
endif()
if(FRONTIER_PROFILE STREQUAL "Editor")
    target_link_libraries(FrontierEngine PUBLIC FrontierImGui)
endif()
if(NOT ANDROID)
    target_include_directories(FrontierEngine PUBLIC "${CMAKE_SOURCE_DIR}/ExternalPackages/glfw/include")
    target_link_libraries(FrontierEngine PRIVATE glfw)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                         SHARED ENGINE & WORKSPACE UI BINARIES FOR GENERATED PROJECTS (Binaries/Win64)
#------------------------------------------------------------------------------------------------------------------------

set(FRONTIER_SHARED_BIN_DIR "${CMAKE_BINARY_DIR}/Binaries/Win64")

add_library(FrontierEngineRuntimeDll SHARED Engine/Lifecycle/ProjectExecutionRunner.cpp)
set_target_properties(FrontierEngineRuntimeDll PROPERTIES
    OUTPUT_NAME                "FrontierEngine"
    CXX_VISIBILITY_PRESET      "hidden"
    VISIBILITY_INLINES_HIDDEN  ON
    RUNTIME_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
)
target_compile_definitions(FrontierEngineRuntimeDll PRIVATE FRONTIER_BUILDING_ENGINE_DLL=1)
target_link_libraries(FrontierEngineRuntimeDll PRIVATE FrontierEngine)
if(UNIX AND NOT APPLE)
    target_link_options(FrontierEngineRuntimeDll PRIVATE "-Wl,--exclude-libs,ALL")
endif()

if(NOT ANDROID AND FRONTIER_PROFILE STREQUAL "Editor")
    add_library(FrontierWorkspaceUiDll SHARED Engine/Lifecycle/ProjectExecutionRunner.cpp)
    set_target_properties(FrontierWorkspaceUiDll PROPERTIES
        OUTPUT_NAME                "FrontierWorkspaceUi"
        CXX_VISIBILITY_PRESET      "hidden"
        VISIBILITY_INLINES_HIDDEN  ON
        RUNTIME_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY   "${FRONTIER_SHARED_BIN_DIR}"
    )
    target_compile_definitions(FrontierWorkspaceUiDll PRIVATE
        FRONTIER_BUILDING_WORKSPACE_UI_DLL=1
        FRONTIER_COMPILE_WORKSPACE_UI_RUNNER=1
        "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\""
    )
    target_link_libraries(FrontierWorkspaceUiDll PRIVATE FrontierEngine FrontierImGui)
    if(UNIX AND NOT APPLE)
        target_link_options(FrontierWorkspaceUiDll PRIVATE "-Wl,--exclude-libs,ALL")
    endif()
endif()

foreach(CFG_TYPE DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(FrontierEngineRuntimeDll PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
    )
    if(TARGET FrontierWorkspaceUiDll)
        set_target_properties(FrontierWorkspaceUiDll PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${CFG_TYPE} "${FRONTIER_SHARED_BIN_DIR}"
        )
    endif()
endforeach()

#------------------------------------------------------------------------------------------------------------------------
#                         DESKTOP APPLICATION EXECUTABLES (STANDALONE INDIVIDUAL WORKSPACES)
#------------------------------------------------------------------------------------------------------------------------

    if(NOT ANDROID AND FRONTIER_PROFILE STREQUAL "Editor")
        function(frontier_register_desktop_app APP_NAME LAUNCH_SOURCE)
            add_executable(${APP_NAME} ${LAUNCH_SOURCE})
            target_compile_definitions(${APP_NAME} PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
            target_link_libraries(${APP_NAME} PRIVATE FrontierEngine FrontierImGui)
            if(WIN32)
                target_link_options(${APP_NAME} PRIVATE "/STACK:8388608")
            endif()
        endfunction()

    frontier_register_desktop_app(Frontier           Engine/Lifecycle/FrontierHostLaunch.cpp)
    frontier_register_desktop_app(FrontierEditor     Engine/Lifecycle/FrontierEditorLaunch.cpp)
    frontier_register_desktop_app(FrontierUV         Engine/Lifecycle/FrontierUVLaunch.cpp)
    frontier_register_desktop_app(FrontierBaking      Engine/Lifecycle/FrontierBakingLaunch.cpp)
    frontier_register_desktop_app(FrontierPattern     Engine/Lifecycle/FrontierPatternLaunch.cpp)
    frontier_register_desktop_app(FrontierStamp       Engine/Lifecycle/FrontierStampLaunch.cpp)
    frontier_register_desktop_app(FrontierTexturePaint Engine/Lifecycle/FrontierTexturePaintLaunch.cpp)

    add_executable(FrontierDefaultContentGenerator Engine/Tools/DefaultContentGeneratorMain.cpp)
    target_compile_definitions(FrontierDefaultContentGenerator PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
    target_link_libraries(FrontierDefaultContentGenerator PRIVATE FrontierEngine)
    add_custom_target(FrontierGenerateDefaultContent
        COMMAND FrontierDefaultContentGenerator "--repo-root=${CMAKE_SOURCE_DIR}"
        DEPENDS FrontierDefaultContentGenerator
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating & verifying zero-abbreviation Content/ .Space repository"
    )
    frontier_register_desktop_app(FrontierMaterial    Engine/Lifecycle/FrontierMaterialLaunch.cpp)
    frontier_register_desktop_app(FrontierFracture    Engine/Lifecycle/FrontierFractureLaunch.cpp)
    frontier_register_desktop_app(FrontierSketcher    Engine/Lifecycle/FrontierSketcherLaunch.cpp)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                         VALIDATION EXHIBITS (CTEST TARGETS)
#------------------------------------------------------------------------------------------------------------------------

if(BUILD_TESTING)
    if(NOT FRONTIER_PROFILE STREQUAL "Editor")
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
        if(FRONTIER_PROFILE STREQUAL "Editor")
            target_link_libraries(${EXHIBIT_NAME} PRIVATE FrontierEngine FrontierImGui)
        else()
            target_link_libraries(${EXHIBIT_NAME} PRIVATE FrontierShippingExhibitUi FrontierEngine FrontierImGui)
        endif()
        add_test(NAME ${EXHIBIT_NAME} COMMAND ${EXHIBIT_NAME})
    endfunction()

    frontier_register_exhibit(ClusterLodBakeValidation      Exhibits/ClusterLodBakeValidation.cpp)
    frontier_register_exhibit(NaniteViewportVehicleValidation Exhibits/NaniteViewportVehicleValidation.cpp)
    frontier_register_exhibit(InputValidation             Exhibits/InputValidation.cpp)
    frontier_register_exhibit(CameraComponentValidation     Exhibits/CameraComponentValidation.cpp)
    frontier_register_exhibit(CameraEffectsValidation       Exhibits/CameraEffectsValidation.cpp)
    frontier_register_exhibit(WindowValidation            Exhibits/WindowValidation.cpp)
    frontier_register_exhibit(DockingValidation           Exhibits/DockingValidation.cpp)
    frontier_register_exhibit(ControlCentreValidation     Exhibits/ControlCentreValidation.cpp)
    frontier_register_exhibit(WorkspacePanesValidation    Exhibits/WorkspacePanesValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(LifecycleValidation            Exhibits/LifecycleValidation.cpp)
    endif()
    frontier_register_exhibit(WorkspaceRevisionValidation    Exhibits/WorkspaceRevisionValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(ContentBrowserValidation       Exhibits/ContentBrowserValidation.cpp)
    endif()
    frontier_register_exhibit(ColourSpaceValidation          Exhibits/ColourSpaceValidation.cpp)
    frontier_register_exhibit(ColourCodecInterchangeValidation Exhibits/ColourCodecInterchangeValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(ColourComponentsValidation     Exhibits/ColourComponentsValidation.cpp)
    endif()
    frontier_register_exhibit(PhysicsSpaceValidation              Exhibits/PhysicsSpaceValidation.cpp)
    frontier_register_exhibit(ComponentClassPhysicsValidation     Exhibits/ComponentClassPhysicsValidation.cpp)
    frontier_register_exhibit(FractureGeometryValidation          Exhibits/FractureGeometryValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(FractureWorkspaceUIValidation Exhibits/FractureWorkspaceUIValidation.cpp)
    endif()
    frontier_register_exhibit(GeometryNetworkValidation      Exhibits/GeometryNetworkValidation.cpp)
    frontier_register_exhibit(GeometryCompletionValidation   Exhibits/GeometryCompletionValidation.cpp)
    frontier_register_exhibit(MaterialScatteringValidation   Exhibits/MaterialScatteringValidation.cpp)
    frontier_register_exhibit(DynamicMaterialRecipeValidation Exhibits/DynamicMaterialRecipeValidation.cpp)
    frontier_register_exhibit(DynamicMaterialVulkanValidation Exhibits/DynamicMaterialVulkanValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(MaterialWorkspaceUIValidation   Exhibits/MaterialWorkspaceUIValidation.cpp)
    endif()
    frontier_register_exhibit(VisibilityRasterValidation     Exhibits/VisibilityRasterValidation.cpp)
    frontier_register_exhibit(SolarShadowValidation          Exhibits/SolarShadowValidation.cpp)
    frontier_register_exhibit(TieredWorldRenderValidation    Exhibits/TieredWorldRenderValidation.cpp)
    frontier_register_exhibit(AudioSpaceValidation           Exhibits/AudioSpaceValidation.cpp)
    frontier_register_exhibit(CelestialCloudsValidation      Exhibits/CelestialCloudsValidation.cpp)
    frontier_register_exhibit(AtmosphereAuditProof           Exhibits/AtmosphereAuditProof.cpp)
    frontier_register_exhibit(LensFlareValidation            Exhibits/LensFlareValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(DrawerGestureWorldValidation   Exhibits/DrawerGestureWorldValidation.cpp)
        frontier_register_exhibit(ComponentEditorUIValidation    Exhibits/ComponentEditorUIValidation.cpp)
    endif()
    frontier_register_exhibit(UVRasterPickingGizmoValidation    Exhibits/UVRasterPickingGizmoValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(UVWorkspaceUIValidation           Exhibits/UVWorkspaceUIValidation.cpp)
    endif()
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(PatternStampWorkspaceUIValidation Exhibits/PatternStampWorkspaceUIValidation.cpp)
    endif()
    frontier_register_exhibit(UVParameterizationValidation      Exhibits/UVParameterizationValidation.cpp)
    frontier_register_exhibit(UVInteractiveAuthoringValidation  Exhibits/UVInteractiveAuthoringValidation.cpp)
    frontier_register_exhibit(UVBoundaryValidation              Exhibits/UVBoundaryValidation.cpp)
    frontier_register_exhibit(UVAuthoringChaosValidation        Exhibits/UVAuthoringChaosValidation.cpp)
    frontier_register_exhibit(NetworkingSpaceValidation         Exhibits/NetworkingSpaceValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(ProjectBrowserValidation          Exhibits/ProjectBrowserValidation.cpp)
    endif()
    frontier_register_exhibit(VectorGraphicsGPUValidation       Exhibits/VectorGraphicsGPUValidation.cpp)
    frontier_register_exhibit(BakingGPUValidation               Exhibits/BakingGPUValidation.cpp)
    frontier_register_exhibit(DirectPbrWorldValidation          Exhibits/DirectPbrWorldValidation.cpp)
    frontier_register_exhibit(GtxSdfGiShadowValidation          Exhibits/GtxSdfGiShadowValidation.cpp)
    frontier_register_exhibit(GtxGlobalDistanceFieldViewportValidation Exhibits/GtxGlobalDistanceFieldViewportValidation.cpp)
    frontier_register_exhibit(DynamicClipmapNaniteViewportValidation Exhibits/DynamicClipmapNaniteViewportValidation.cpp)
    frontier_register_exhibit(SdfEmissivePenumbraValidation     Exhibits/SdfEmissivePenumbraValidation.cpp)
    frontier_register_exhibit(DeterministicPathTracerValidation Exhibits/DeterministicPathTracerValidation.cpp)
    frontier_register_exhibit(WindCloudsFogValidation           Exhibits/WindCloudsFogValidation.cpp)
    frontier_register_exhibit(SketcherKernelValidation          Exhibits/ParametricSketcher/SketcherKernelValidation.cpp)
    frontier_register_exhibit(SketcherDocumentValidation        Exhibits/ParametricSketcher/SketcherDocumentValidation.cpp)
    frontier_register_exhibit(SketcherViewportValidation        Exhibits/ParametricSketcher/SketcherViewportValidation.cpp)
    frontier_register_exhibit(ComponentEditorRenderValidation   Exhibits/ComponentEditorRenderValidation.cpp)
    frontier_register_exhibit(DataDrivenSpaceContentValidation  Exhibits/DataDrivenSpaceContentValidation.cpp)
    if(FRONTIER_PROFILE STREQUAL "Editor")
        frontier_register_exhibit(BakingWorkspaceUIValidation       Exhibits/BakingWorkspaceUIValidation.cpp)
        frontier_register_exhibit(TexturePaintWorkspaceUIValidation Exhibits/TexturePaintWorkspaceUIValidation.cpp)
        frontier_register_exhibit(SketcherWorkspaceValidation       Exhibits/ParametricSketcher/SketcherWorkspaceValidation.cpp)
    endif()

    if(NOT ANDROID AND FRONTIER_PROFILE STREQUAL "Editor")
        add_test(
            NAME FrontierUVExecutableSmoke
            COMMAND FrontierUV --frames=3 "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierUV_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierEditorExecutableSmoke
            COMMAND FrontierEditor --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierEditor_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierPatternExecutableSmoke
            COMMAND FrontierPattern --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierPattern_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierStampExecutableSmoke
            COMMAND FrontierStamp --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierStamp_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierBakingExecutableSmoke
            COMMAND FrontierBaking --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierBaking_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierTexturePaintExecutableSmoke
            COMMAND FrontierTexturePaint --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierTexturePaint_DesktopExecutable.png"
        )
        add_test(
            NAME FrontierMaterialExecutableSmoke
            COMMAND FrontierMaterial --frames=3
        )
        add_test(
            NAME FrontierFractureExecutableSmoke
            COMMAND FrontierFracture --frames=3
        )
        add_test(
            NAME FrontierSketcherExecutableSmoke
            COMMAND FrontierSketcher --frames=1
        )
    endif()
endif()
