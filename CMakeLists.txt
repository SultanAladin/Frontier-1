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
    target_compile_options(FrontierCompileSettings INTERFACE /utf-8 /W4 /permissive-)
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
    Engine/Components/Controls/ColourRampEditor.cpp
    Engine/Components/Controls/ColourSwatchPicker.cpp
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
    Engine/Workspaces/Texturing/PatternStampLayerStackPane.cpp
    Engine/Workspaces/Texturing/PatternStampToolHeaderPane.cpp
    Engine/Workspaces/Texturing/PatternStampInspectorPane.cpp
    Engine/Workspaces/Texturing/VectorViewportInteractor.cpp
    Engine/Workspaces/Texturing/PatternStampWorkspaceHost.cpp
    Engine/Workspaces/Texturing/BakingTargetsPane.cpp
    Engine/Workspaces/Texturing/BakingViewportPane.cpp
    Engine/Workspaces/Texturing/BakingInspectorPane.cpp
    Engine/Workspaces/Texturing/BakingWorkspaceHost.cpp
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
frontier_compile_slang_shader(StretchRelaxCompute.slang               ComputeEntry  compute  StretchRelaxCompute.comp.spv)
frontier_compile_slang_shader(ConcaveSkylineCompute.slang             ComputeEntry  compute  ConcaveSkylineCompute.comp.spv)
frontier_compile_slang_shader(CameraVisualEffects.slang               ComputeEntry  compute  CameraVisualEffects.comp.spv)
frontier_compile_slang_shader(WorldViewportRadiometricCompute.slang   ComputeEntry  compute  WorldViewportRadiometricCompute.comp.spv)

set(FRONTIER_MODULAR_SHADER_DIR "${CMAKE_SOURCE_DIR}/Engine/Shaders")

function(frontier_compile_modular_slang_shader DOMAIN_REL_PATH ENTRY_NAME SHADER_UNIT OUT_SPV_NAME)
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    file(GLOB DOMAIN_SLANG_DEPS
        "${FRONTIER_MODULAR_SHADER_DIR}/MaterialSpace/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/Visibility/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/DirectLighting/*.slang"
        "${FRONTIER_MODULAR_SHADER_DIR}/GlobalIllumination/*.slang"
    )
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${FRONTIER_MODULAR_SHADER_DIR}/${DOMAIN_REL_PATH}"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/MaterialSpace"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/Visibility"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/DirectLighting"
            -I "${FRONTIER_MODULAR_SHADER_DIR}/GlobalIllumination"
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
frontier_compile_modular_slang_shader(Visibility/DepthPyramidDownsample.slang                    DepthPyramidComputeEntry        compute  DepthPyramidDownsample.comp.spv)
frontier_compile_modular_slang_shader(Visibility/ClusterVisibilityCull.slang                     ClusterCullComputeEntry         compute  ClusterVisibilityCull.comp.spv)
frontier_compile_modular_slang_shader(Visibility/HardwareVisibilityRasterization.slang           VisibilityVertexEntry           vertex   HardwareVisibilityRasterization.vert.spv)
frontier_compile_modular_slang_shader(Visibility/HardwareVisibilityRasterization.slang           VisibilityFragmentEntry         fragment HardwareVisibilityRasterization.frag.spv)
frontier_compile_modular_slang_shader(Visibility/MicroFaceSoftwareRasterization.slang            MicroFaceComputeEntry           compute  MicroFaceSoftwareRasterization.comp.spv)
frontier_compile_modular_slang_shader(Visibility/AnalyticBarycentricResolve.slang                AnalyticResolveComputeEntry     compute  AnalyticBarycentricResolve.comp.spv)
frontier_compile_modular_slang_shader(DirectLighting/SolarFrustumShadow.slang                    SolarShadowComputeEntry         compute  SolarFrustumShadow.comp.spv)
frontier_compile_modular_slang_shader(DirectLighting/ContactClipShadow.slang                     ContactClipComputeEntry         compute  ContactClipShadow.comp.spv)
frontier_compile_modular_slang_shader(DirectLighting/PlaneAwareShadowFilter.slang                PlaneFilterComputeEntry         compute  PlaneAwareShadowFilter.comp.spv)
frontier_compile_modular_slang_shader(GlobalIllumination/OctahedralIrradianceLattice.slang       IrradianceLatticeComputeEntry   compute  OctahedralIrradianceLattice.comp.spv)
frontier_compile_modular_slang_shader(GlobalIllumination/SignedDistanceConeIllumination.slang    SdfConeGiComputeEntry           compute  SignedDistanceConeIllumination.comp.spv)
frontier_compile_modular_slang_shader(GlobalIllumination/SpecularAndGlassRaytrace.slang          SpecularRaytraceComputeEntry    compute  SpecularAndGlassRaytrace.comp.spv)

function(frontier_compile_vectorfield_slang_stage SLANG_REL_PATH ENTRY_NAME STAGE_KIND OUT_SPV_NAME)
    set(IN_SLANG "${CMAKE_SOURCE_DIR}/${SLANG_REL_PATH}")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            -profile spirv_1_3
            -target spirv
            -emit-spirv-directly
            -O0
            -I "${CMAKE_SOURCE_DIR}"
            -entry "${ENTRY_NAME}"
            -stage "${STAGE_KIND}"
            "${IN_SLANG}"
            -o "${OUT_SPV}"
        DEPENDS
            "${IN_SLANG}"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/Compute/ForceFieldEvaluationShared.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/VectorField/VectorFieldRenderingShared.slang"
        COMMENT "[Slang] Compiling ${SLANG_REL_PATH} (${STAGE_KIND}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
        VERBATIM
    )
    set(FRONTIER_SPV_OUTPUTS ${FRONTIER_SPV_OUTPUTS} "${OUT_SPV}" PARENT_SCOPE)
endfunction()

frontier_compile_vectorfield_slang_stage(
    "Engine/Shaders/Compute/FieldLatticeCompute.slang"
    "ComputeFieldLatticeVoxel"
    "compute"
    "FieldLatticeCompute.spv"
)

function(frontier_compile_vector_slang_shader SLANG_REL_PATH ENTRY_NAME STAGE_KIND OUT_SPV_NAME)
    set(IN_SLANG "${FRONTIER_SHADER_SRC_DIR}/Vector/${SLANG_REL_PATH}")
    set(OUT_SPV "${FRONTIER_COMPILED_SHADER_DIR}/${OUT_SPV_NAME}")
    add_custom_command(
        OUTPUT "${OUT_SPV}"
        COMMAND "${FRONTIER_SLANGC_BIN}"
            "${IN_SLANG}"
            -I "${FRONTIER_SHADER_SRC_DIR}/Vector"
            -I "${FRONTIER_SHADER_SRC_DIR}"
            -target spirv
            -profile spirv_1_3
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -O0
            -entry "${ENTRY_NAME}"
            -stage "${STAGE_KIND}"
            -o "${OUT_SPV}"
        DEPENDS
            "${IN_SLANG}"
            "${FRONTIER_SHADER_SRC_DIR}/Vector/VectorSharedLayouts.slang"
        COMMENT "[Slang] Compiling Vector/${SLANG_REL_PATH} (${STAGE_KIND}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
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

function(frontier_compile_baking_slang_shader SLANG_REL_PATH ENTRY_NAME STAGE_KIND OUT_SPV_NAME USE_HW_RAYQUERY)
    set(IN_SLANG "${FRONTIER_SHADER_SRC_DIR}/Baking/${SLANG_REL_PATH}")
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
            -I "${FRONTIER_SHADER_SRC_DIR}/Baking"
            -I "${FRONTIER_SHADER_SRC_DIR}"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders/DirectLighting"
            -I "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            ${BAKE_SLANG_FLAGS}
            -entry "${ENTRY_NAME}"
            -stage "${STAGE_KIND}"
            -o "${OUT_SPV}"
        DEPENDS
            "${IN_SLANG}"
            "${FRONTIER_SHADER_SRC_DIR}/Baking/BakeShaderLayouts.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Shaders/DirectLighting/WideTreeRayQuery.slang"
        COMMENT "[Slang] Compiling Baking/${SLANG_REL_PATH} (${STAGE_KIND}:${ENTRY_NAME}) -> ${OUT_SPV_NAME}"
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

if(FRONTIER_PROFILE STREQUAL "Editor")
    foreach(VF_SHADER_STEM
        LitBackdropRasterization
        CrossSectionHeatmapRasterization
        InfluenceEnvelopeRasterization
        VectorArrowRasterization
        StreamlineRasterization
        ForcePathAndBeaconRasterization)
        frontier_compile_vectorfield_slang_stage(
            "Engine/Shaders/VectorField/${VF_SHADER_STEM}.slang"
            "VertexMain"
            "vertex"
            "${VF_SHADER_STEM}.vert.spv"
        )
        frontier_compile_vectorfield_slang_stage(
            "Engine/Shaders/VectorField/${VF_SHADER_STEM}.slang"
            "FragmentMain"
            "fragment"
            "${VF_SHADER_STEM}.frag.spv"
        )
    endforeach()
endif()

add_custom_target(FrontierShaders DEPENDS ${FRONTIER_SPV_OUTPUTS})

set(FRONTIER_ENGINE_SOURCES
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
    Engine/GeometricRaster/Visibility/DepthPyramidSequence.cpp
    Engine/GeometricRaster/Visibility/ClusterCullEncoder.cpp
    Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.cpp
    Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.cpp
    Engine/GeometricRaster/DirectLighting/SolarShadowSequence.cpp
    Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.cpp
    Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.cpp
    Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.cpp
    Engine/Components/Camera/CameraComponent.cpp
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
    Engine/SpatialAcceleration/ClusterPyramid.cpp
    Engine/SpatialAcceleration/WideBoundingTree.cpp
    Engine/DistanceFieldSpace/SignedDistanceVolume.cpp
    Engine/ColourSpace/TransferFunction.cpp
    Engine/ColourSpace/WhitePointAdaptation.cpp
    Engine/ColourSpace/ChromaticitySpace.cpp
    Engine/ColourSpace/SpectralDistribution.cpp
    Engine/ColourSpace/GamutCompression.cpp
    Engine/ColourSpace/ColourRampSampler.cpp
    Engine/ColourSpace/ColourPresetRepository.cpp
    Engine/ColourSpace/ColourCodex.cpp
    Engine/ColourSpace/ColourDocument.cpp
    Engine/ColourSpace/ColourExtension.cpp
    Engine/Interchange/ColourInterchange.cpp
    Engine/Interchange/GeometryInterchange.cpp
    Engine/Editor/Viewports/TransformGizmo.cpp
    Engine/Editor/Viewports/ViewportProjection.cpp
    Engine/GeometricRaster/VulkanInterchange.cpp
    Engine/GeometricRaster/Compute/TimelineTransferRegister.cpp
    Engine/GeometricRaster/Compute/ComputeDispatchEncoder.cpp
    Engine/GeometricRaster/Picking/ViewportPickQuery.cpp
    Engine/GeometricRaster/UV/UVWorkspaceRasterizer.cpp
    Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.cpp
    Engine/GeometricRaster/Vector/VectorCanvasRasterizer.cpp
    Engine/GeometricRaster/Baking/StudyGeometryBuilder.cpp
    Engine/GeometricRaster/Baking/BakingAccelerationStructure.cpp
    Engine/GeometricRaster/Baking/GpuTextureAndSdfBaker.cpp
    Engine/GeometricRaster/Baking/TextureBakerRasterizer.cpp
    Engine/Workspaces/Texturing/PatternStampStackSession.cpp
    Engine/Workspaces/Texturing/BakingWorkspaceSession.cpp
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
    Engine/Threading/Threads/Workspace/WorkspaceThread.cpp
    Engine/Threading/Threads/Streaming/AssetStreamThread.cpp
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
    Engine/Codecs/VectorFieldCodec.cpp
    Engine/Codecs/ForceModelPresetCodec.cpp
    Engine/SimulationSpace/ForceFields/ForceMotionPath.cpp
    Engine/SimulationSpace/ForceFields/VectorField.cpp
    Engine/SimulationSpace/ForceFields/FlowField.cpp
    Engine/SimulationSpace/ForceFields/TurbulenceField.cpp
    Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.cpp
    Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.cpp
    Engine/SimulationSpace/ForceFields/FieldLatticeCascade.cpp
    Engine/SimulationSpace/ForceFields/FieldLatticeVulkanExecutor.cpp
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
        Engine/Editor/Workspaces/Simulation/ForcePresetBrowserPanel.cpp
        Engine/Editor/Workspaces/Simulation/ForceInspectorView.cpp
        Engine/Editor/Workspaces/Simulation/VectorFieldOverlaySequence.cpp
        Engine/Editor/Workspaces/Simulation/VectorFieldViewportCoordinator.cpp
        Engine/Lifecycle/EditorLifecycle.cpp
    )
endif()
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    list(APPEND FRONTIER_ENGINE_SOURCES ${FRONTIER_UI_COMPONENT_SOURCES})
endif()

include(Automation/ThorVGVector.cmake)

set_source_files_properties(Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${THORVG_VECTOR_SPI_INCLUDES}")

add_library(FrontierEngine STATIC ${FRONTIER_ENGINE_SOURCES})
add_dependencies(FrontierEngine FrontierShaders thorvg_static)

target_include_directories(FrontierEngine PUBLIC
    "${CMAKE_SOURCE_DIR}"
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
if(WIN32)
    target_link_libraries(FrontierEngine PUBLIC ws2_32)
endif()
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
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

if(NOT ANDROID AND NOT FRONTIER_PROFILE STREQUAL "Shipping")
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
#                              DESKTOP APPLICATION EXECUTABLES (FRONTIER, FRONTIERUV & FRONTIEREDITOR)
#------------------------------------------------------------------------------------------------------------------------

if(NOT ANDROID AND NOT FRONTIER_PROFILE STREQUAL "Shipping")
    add_executable(Frontier Engine/Lifecycle/FrontierHostLaunch.cpp)
    target_compile_definitions(Frontier PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
    target_link_libraries(Frontier PRIVATE FrontierEngine FrontierImGui)

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
    frontier_register_exhibit(ColourSpaceValidation          Exhibits/ColourSpaceValidation.cpp)
    frontier_register_exhibit(ColourCodecInterchangeValidation Exhibits/ColourCodecInterchangeValidation.cpp)
    frontier_register_exhibit(ColourComponentsValidation     Exhibits/ColourComponentsValidation.cpp)
    frontier_register_exhibit(GeometryNetworkValidation      Exhibits/GeometryNetworkValidation.cpp)
    frontier_register_exhibit(GeometryCompletionValidation   Exhibits/GeometryCompletionValidation.cpp)
    frontier_register_exhibit(MaterialScatteringValidation   Exhibits/MaterialScatteringValidation.cpp)
    frontier_register_exhibit(VisibilityRasterValidation     Exhibits/VisibilityRasterValidation.cpp)
    frontier_register_exhibit(SolarShadowValidation          Exhibits/SolarShadowValidation.cpp)
    frontier_register_exhibit(TieredWorldRenderValidation    Exhibits/TieredWorldRenderValidation.cpp)
    frontier_register_exhibit(UVRasterPickingGizmoValidation    Exhibits/UVRasterPickingGizmoValidation.cpp)
    frontier_register_exhibit(UVWorkspaceUIValidation           Exhibits/UVWorkspaceUIValidation.cpp)
    frontier_register_exhibit(PatternStampWorkspaceUIValidation Exhibits/PatternStampWorkspaceUIValidation.cpp)
    frontier_register_exhibit(UVParameterizationValidation      Exhibits/UVParameterizationValidation.cpp)
    frontier_register_exhibit(UVInteractiveAuthoringValidation  Exhibits/UVInteractiveAuthoringValidation.cpp)
    frontier_register_exhibit(UVBoundaryValidation              Exhibits/UVBoundaryValidation.cpp)
    frontier_register_exhibit(UVAuthoringChaosValidation        Exhibits/UVAuthoringChaosValidation.cpp)
    frontier_register_exhibit(NetworkingSpaceValidation         Exhibits/NetworkingSpaceValidation.cpp)
    frontier_register_exhibit(VectorFieldValidation             Exhibits/VectorFieldValidation.cpp)
    frontier_register_exhibit(ProjectBrowserValidation          Exhibits/ProjectBrowserValidation.cpp)
    frontier_register_exhibit(VectorGraphicsGPUValidation       Exhibits/VectorGraphicsGPUValidation.cpp)
    frontier_register_exhibit(BakingGPUValidation               Exhibits/BakingGPUValidation.cpp)
    frontier_register_exhibit(BakingWorkspaceUIValidation       Exhibits/BakingWorkspaceUIValidation.cpp)

    if(NOT ANDROID AND NOT FRONTIER_PROFILE STREQUAL "Shipping")
        add_test(
            NAME FrontierUVExecutableSmoke
            COMMAND FrontierUV --frames=3 "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierUV_DesktopExecutable.png"
        )
        if(FRONTIER_PROFILE STREQUAL "Editor")
            add_test(
                NAME FrontierEditorExecutableSmoke
                COMMAND FrontierEditor --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/UV/FrontierEditor_DesktopExecutable.png"
            )
            add_test(
                NAME FrontierPatternExecutableSmoke
                COMMAND FrontierEditor --workspace=pattern --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierPattern_DesktopExecutable.png"
            )
            add_test(
                NAME FrontierStampExecutableSmoke
                COMMAND FrontierEditor --workspace=stamp --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierStamp_DesktopExecutable.png"
            )
            add_test(
                NAME FrontierBakingExecutableSmoke
                COMMAND FrontierEditor --workspace=baking --frames=3 --open-browser "--capture-png=${CMAKE_SOURCE_DIR}/Exhibits/Images/Texturing/FrontierBaking_DesktopExecutable.png"
            )
        endif()
    endif()
endif()
