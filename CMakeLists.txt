cmake_minimum_required(VERSION 3.21)
project(FrontierUIFoundation LANGUAGES C CXX)

find_package(Git QUIET)
set(FRONTIER_BUILD_COMMIT "local")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE FRONTIER_BUILD_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE FRONTIER_GIT_RESULT)
    if(NOT FRONTIER_GIT_RESULT EQUAL 0)
        set(FRONTIER_BUILD_COMMIT "source")
    endif()
endif()

# UI-first port with a separately validated Vulkan CAD offscreen unit; native viewport integration remains explicit.
option(FRONTIER_HOST_VERIFICATION "Build local verification tools on a non-Windows editing host" OFF)
if(NOT WIN32 AND NOT FRONTIER_HOST_VERIFICATION)
    message(FATAL_ERROR "Windows is the delivery target. Use host verification only for local checks.")
endif()
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareUI.py" --verify
    RESULT_VARIABLE DEPENDENCY_STATUS)
if(NOT DEPENDENCY_STATUS EQUAL 0)
    message(FATAL_ERROR "Run python Automation/PrepareUI.py before configuring; configure never downloads.")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareFonts.py" --verify
    RESULT_VARIABLE FONT_STATUS)
if(NOT FONT_STATUS EQUAL 0)
    message(FATAL_ERROR "Prepare fonts first: pip install -r Automation/FontRequirements.txt; python Automation/PrepareFonts.py")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()
include(Automation/ThorVGSoftware.cmake)
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
add_library(FrontierImGui STATIC
    "${IMGUI_ROOT}/imgui.cpp" "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp" "${IMGUI_ROOT}/imgui_widgets.cpp")
target_include_directories(FrontierImGui PUBLIC "${IMGUI_ROOT}")
# The pattern gallery replays tens of thousands of cached thumbnail triangles per
# card into one window draw list, exceeding the 16-bit 64K vertex ceiling. Both
# shipped backends (stock Vulkan/DX11) support 32-bit indices. Defined here rather
# than in the vendored imconfig.h so CI-recovered dependencies match local builds.
target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
add_library(FrontierTextFiles STATIC Engine/Interface/TextFileReplacement.cpp)
target_include_directories(FrontierTextFiles PUBLIC "${CMAKE_SOURCE_DIR}")
# 📝 Dependency-free disk cache for baked gallery thumbnail atlases (Pattern + Material). Kept as its
# own unit (rather than folded into FrontierInterface) because the Material side lives in
# FrontierSurfaceRasterization, which does not otherwise depend on FrontierInterface; both link this
# instead of each compiling a private copy of the same translation unit into separate static archives.
add_library(FrontierThumbnailAtlasCache STATIC Engine/Interface/ThumbnailAtlasCache.cpp)
target_include_directories(FrontierThumbnailAtlasCache PUBLIC "${CMAKE_SOURCE_DIR}")
add_library(FrontierMaterialLibrary STATIC Engine/ContentInterchange/MaterialLibrary.cpp
    Engine/ContentInterchange/MaterialRecipeProfiles.cpp)
target_include_directories(FrontierMaterialLibrary PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierMaterialLibrary PUBLIC FrontierSurfaceRasterization FrontierTextFiles)
add_library(FrontierVectorAuthoring STATIC
    Engine/ContentInterchange/VectorAuthoringDocument.cpp
    Engine/ContentInterchange/VectorAuthoringEditing.cpp
    Engine/ContentInterchange/VectorAuthoringArrayPlacement.cpp
    Engine/ContentInterchange/VectorAuthoringProperties.cpp
    Engine/ContentInterchange/VectorAuthoringSelectionEditing.cpp
    Engine/ContentInterchange/CubicPathEditing.cpp
    Engine/ContentInterchange/StampTextOutline.cpp
    Engine/ContentInterchange/StampVectorImport.cpp
    Engine/Render/VectorSurfaceCompilation.cpp
    Engine/ContentInterchange/SurfaceMapEncoding.cpp)
target_include_directories(FrontierVectorAuthoring PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_include_directories(FrontierVectorAuthoring PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
target_link_libraries(FrontierVectorAuthoring PUBLIC thorvg_static)
add_library(FrontierTextureStudioUI STATIC UI/TextureStudio/MaterialLibraryPanel.cpp UI/TextureStudio/PatternWorkspacePanel.cpp
    UI/TextureStudio/StampWorkspacePanel.cpp
    UI/TextureStudio/PatternThumbnailRaster.cpp UI/TextureStudio/VectorMaskControls.cpp Engine/Interface/VectorAuthoringControls.cpp)
target_link_libraries(FrontierTextureStudioUI PUBLIC FrontierMaterialLibrary FrontierInterface FrontierVectorAuthoring)
target_compile_definitions(FrontierTextureStudioUI PUBLIC FRONTIER_EDITOR=1)
add_library(FrontierInterface STATIC
    Engine/Interface/IconArt.cpp Engine/Interface/IconPresentation.cpp
    Engine/Interface/PixelSpace.cpp Engine/Interface/GlyphSpace.cpp
    Engine/Interface/ControlKit.cpp Engine/Interface/TextEntryState.cpp
    Engine/Interface/ThemeStructure.cpp Engine/Interface/VectorCodec.cpp
    Engine/Interface/ContentDirectory.cpp Engine/Interface/FontCodec.cpp
    Engine/Interface/TypefaceRegistry.cpp Engine/Interface/AppearanceInspector.cpp
    Engine/Interface/AppearanceCodec.cpp Engine/Interface/MotionIntegrator.cpp
    Engine/Interface/PanelExpansionTab.cpp Engine/Interface/ConfigurationInspector.cpp
    Engine/Interface/ConfigurationStructure.cpp Engine/Interface/InputPreferencesCodec.cpp
    Engine/Interface/MonitorPlacement.cpp Engine/Interface/PanelDockspaceMonitor.cpp
    Engine/Interface/PanelDockspace.cpp Engine/Interface/PanelDockspaceLayout.cpp Engine/Interface/NotificationSequence.cpp Engine/Interface/ConfirmationDialogue.cpp
    Engine/ContentInterchange/ProjectSpaceCodec.cpp)
target_include_directories(FrontierInterface PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include")
target_link_libraries(FrontierInterface PUBLIC FrontierImGui thorvg_static FrontierTextFiles FrontierThumbnailAtlasCache)
add_library(FrontierEditorUI STATIC UI/Editor/ProjectBrowserPanel.cpp UI/Editor/FoundationPanel.cpp UI/Editor/EditorWorkspace.cpp UI/Editor/ControlCentrePanel.cpp UI/Editor/ContentBrowserPanel.cpp)
target_link_libraries(FrontierEditorUI PUBLIC FrontierInterface)
target_compile_definitions(FrontierEditorUI PUBLIC FRONTIER_EDITOR=1)

# GLFW owns windows/input on every platform; DX11 is only the legacy Windows-only exhibit's display
# adapter, not the primary renderer, and never needs GLFW built differently per platform.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(ExternalPackages/glfw EXCLUDE_FROM_ALL)

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE FRONTIER_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)

# TextureStudio ships on both Windows and Linux; Vulkan + GLFW own presentation on each, and the
# shared WindowsApplication.cpp/VulkanPresentation.cpp plumbing is platform-neutral.
add_executable(TextureStudio Experimental/TextureStudio/Native/TextureStudioApplication.cpp
    Engine/Interface/WindowsApplication.cpp Engine/Interface/VulkanPresentation.cpp
    "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp")
set_target_properties(TextureStudio PROPERTIES OUTPUT_NAME TextureWorkspace)
target_include_directories(TextureStudio PRIVATE "${IMGUI_ROOT}/backends"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_compile_definitions(TextureStudio PRIVATE FRONTIER_UI_VULKAN=1 VK_NO_PROTOTYPES GLFW_INCLUDE_NONE
    FRONTIER_BUILD_COMMIT=\"${FRONTIER_BUILD_COMMIT}\" FRONTIER_BUILD_REVISION="${FRONTIER_BUILD_REVISION}")
target_link_libraries(TextureStudio PRIVATE FrontierEditorUI FrontierTextureStudioUI glfw)
if(NOT WIN32 AND BUILD_TESTING)
    # 📝 Host-verification builds put every plain CTest executable in this same flat binary
    # directory; a bundled EngineContent copy sitting beside them would shadow the real,
    # executable-relative repository content those tests deliberately verify against (see
    # ControlCentreValidation). Windows packaging is unaffected: it is untouched by this branch.
    set_target_properties(TextureStudio PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/TextureStudio")
endif()
add_custom_command(TARGET TextureStudio POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/EngineContent/MaterialArchives/TextureStudio"
        "$<TARGET_FILE_DIR:TextureStudio>/EngineContent/MaterialArchives/TextureStudio")
add_custom_command(TARGET TextureStudio POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_SOURCE_DIR}/EngineContent" "$<TARGET_FILE_DIR:TextureStudio>/EngineContent")
install(TARGETS TextureStudio RUNTIME DESTINATION .)
install(DIRECTORY ExternalPackages/vulkan-headers/LICENSES DESTINATION Notices/Vulkan-Headers)
install(DIRECTORY EngineContent DESTINATION .)
install(FILES "${IMGUI_ROOT}/LICENSE.txt" DESTINATION Notices RENAME ImGui-LICENSE.txt)
install(FILES ExternalPackages/glfw/LICENSE.md DESTINATION Notices RENAME GLFW-LICENSE.md)
install(FILES ExternalPackages/tomlpp/LICENSE DESTINATION Notices RENAME TomlPlusPlus-LICENSE.txt)
install(FILES ExternalPackages/stb/LICENSE DESTINATION Notices RENAME Stb-LICENSE.txt)
install(FILES ExternalPackages/thorvg/LICENSE DESTINATION Notices RENAME ThorVG-LICENSE.txt)
install(FILES Engine/Interface/Lucide.NOTICE Documentation/SourceNotices.md DESTINATION Notices)

if(WIN32)
    target_compile_definitions(TextureStudio PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(TextureStudio PRIVATE comdlg32 shell32)
    if(MSVC)
        target_link_options(TextureStudio PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
    # Vulkan is the editor UI default. GLFW + the pinned ImGui Vulkan adapter own secondary-window presentation.
    add_executable(FrontierUI Exhibits/UIFoundation/Code/WindowsUI.cpp
        Engine/Interface/WindowsApplication.cpp
        Engine/Interface/VulkanPresentation.cpp
        "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp")
    target_include_directories(FrontierUI PRIVATE "${IMGUI_ROOT}/backends"
        "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
    target_compile_definitions(FrontierUI PRIVATE FRONTIER_UI_VULKAN=1 VK_NO_PROTOTYPES GLFW_INCLUDE_NONE)
    target_link_libraries(FrontierUI PRIVATE FrontierEditorUI glfw)
    add_executable(Frontier Experimental/Frontier/Native/WindowsFrontier.cpp
        Engine/Interface/WindowsApplication.cpp Engine/Interface/VulkanPresentation.cpp
        "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp")
    target_include_directories(Frontier PRIVATE "${IMGUI_ROOT}/backends"
        "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
    target_compile_definitions(Frontier PRIVATE FRONTIER_UI_VULKAN=1 VK_NO_PROTOTYPES GLFW_INCLUDE_NONE
        FRONTIER_BUILD_COMMIT=\"${FRONTIER_BUILD_COMMIT}\")
    target_link_libraries(Frontier PRIVATE FrontierEditorUI FrontierSurfaceRasterization glfw ole32)
    if(MSVC)
        enable_language(RC)
        foreach(WORKSPACE_TARGET Frontier TextureStudio)
            if(WORKSPACE_TARGET STREQUAL "TextureStudio")
                set(WORKSPACE_APPLICATION "TextureWorkspace")
            else()
                set(WORKSPACE_APPLICATION "Frontier")
            endif()
            configure_file(Automation/WindowsApplicationVersion.rc.in
                "${CMAKE_BINARY_DIR}/${WORKSPACE_APPLICATION}Version.rc" @ONLY)
            target_sources(${WORKSPACE_TARGET} PRIVATE "${CMAKE_BINARY_DIR}/${WORKSPACE_APPLICATION}Version.rc")
        endforeach()
    endif()
    # Retain the original DX11 exhibit without making DX11 a dependency of the Vulkan editor.
    add_executable(FrontierUIDX11 Exhibits/UIFoundation/Code/WindowsUI.cpp
        Engine/Interface/WindowsApplication.cpp
        "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp" "${IMGUI_ROOT}/backends/imgui_impl_dx11.cpp")
    target_include_directories(FrontierUIDX11 PRIVATE "${IMGUI_ROOT}/backends")
    target_compile_definitions(FrontierUIDX11 PRIVATE FRONTIER_UI_VULKAN=0)
    if(NOT DEFINED FRONTIER_D3DCOMPILER)
        set(FRONTIER_D3DCOMPILER d3dcompiler)
    endif()
    target_link_libraries(FrontierUIDX11 PRIVATE FrontierEditorUI glfw d3d11 dxgi ${FRONTIER_D3DCOMPILER} dwmapi)
    foreach(UI_TARGET FrontierUI FrontierUIDX11 Frontier)
        target_compile_definitions(${UI_TARGET} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
            FRONTIER_BUILD_REVISION="${FRONTIER_BUILD_REVISION}")
        target_link_libraries(${UI_TARGET} PRIVATE comdlg32 shell32)
        if(MSVC)
            target_link_options(${UI_TARGET} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        endif()
        add_custom_command(TARGET ${UI_TARGET} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/EngineContent" "$<TARGET_FILE_DIR:${UI_TARGET}>/EngineContent")
    endforeach()
    install(TARGETS FrontierUI FrontierUIDX11 Frontier RUNTIME DESTINATION .)
endif()

include(CTest)
include(Automation/SurfaceRasterization.cmake)
include(Automation/VectorCoverage.cmake)
if(BUILD_TESTING)
    find_program(FRONTIER_PATTERN_IMPORT_NODE NAMES node)
    if(FRONTIER_PATTERN_IMPORT_NODE)
        set(FRONTIER_PATTERN_IMPORT_TOML "${CMAKE_BINARY_DIR}/WebPatternImport/Pattern.vector.toml")
        add_executable(VectorAuthoringImportValidation Exhibits/TextureStudio/Code/VectorAuthoringImportValidation.cpp)
        target_link_libraries(VectorAuthoringImportValidation PRIVATE FrontierVectorAuthoring)
        add_test(NAME WebPatternNativeImportFixture COMMAND "${FRONTIER_PATTERN_IMPORT_NODE}"
            "${CMAKE_SOURCE_DIR}/Automation/ConvertWebPattern.mjs"
            "${CMAKE_SOURCE_DIR}/Automation/Fixtures/PatternNativeImport.json"
            "${FRONTIER_PATTERN_IMPORT_TOML}")
        add_test(NAME WebPatternNativeImportValidation COMMAND VectorAuthoringImportValidation
            "${FRONTIER_PATTERN_IMPORT_TOML}")
        set_tests_properties(WebPatternNativeImportValidation PROPERTIES DEPENDS WebPatternNativeImportFixture)
        set_tests_properties(WebPatternNativeImportFixture WebPatternNativeImportValidation PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()
    add_executable(MaterialPreviewConfigurationValidation Exhibits/TextureStudio/Code/MaterialPreviewConfigurationValidation.cpp)
    target_link_libraries(MaterialPreviewConfigurationValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME MaterialPreviewConfigurationValidation COMMAND MaterialPreviewConfigurationValidation
        "${CMAKE_BINARY_DIR}/MaterialPreviewConfigurationChecks.json")
    set_tests_properties(MaterialPreviewConfigurationValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(MaterialWorkspaceEvidence Exhibits/TextureStudio/Code/MaterialWorkspaceEvidence.cpp)
    target_link_libraries(MaterialWorkspaceEvidence PRIVATE FrontierTextureStudioUI ${CMAKE_DL_LIBS})
    add_executable(MaterialAtlasValidation Exhibits/TextureStudio/Code/MaterialAtlasValidation.cpp)
    target_link_libraries(MaterialAtlasValidation PRIVATE FrontierMaterialLibrary ${CMAKE_DL_LIBS})
    add_executable(MaterialAtlasDiskCacheValidation Exhibits/TextureStudio/Code/MaterialAtlasDiskCacheValidation.cpp)
    target_link_libraries(MaterialAtlasDiskCacheValidation PRIVATE FrontierMaterialLibrary ${CMAKE_DL_LIBS})
    if(TARGET FrontierSurfaceShaders)
        add_dependencies(MaterialAtlasDiskCacheValidation FrontierSurfaceShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME MaterialAtlasDiskCacheValidation COMMAND MaterialAtlasDiskCacheValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_SOURCE_DIR}/EngineContent/MaterialArchives/TextureStudio" "${CMAKE_BINARY_DIR}/MaterialAtlasDiskCacheEvidence")
            set_tests_properties(MaterialAtlasDiskCacheValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
    endif()
    add_executable(IconDomainValidation Exhibits/TextureStudio/Code/IconDomainValidation.cpp)
    target_link_libraries(IconDomainValidation PRIVATE FrontierInterface)
    add_test(NAME IconDomainValidation COMMAND IconDomainValidation "${CMAKE_BINARY_DIR}/IconDomainChecks.json")
    set_tests_properties(IconDomainValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(AutomotivePaintValidation Exhibits/TextureStudio/Code/AutomotivePaintValidation.cpp)
    target_link_libraries(AutomotivePaintValidation PRIVATE FrontierMaterialLibrary ${CMAKE_DL_LIBS})
    if(TARGET FrontierSurfaceShaders)
        add_dependencies(AutomotivePaintValidation FrontierSurfaceShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME AutomotivePaintValidation COMMAND AutomotivePaintValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_SOURCE_DIR}/EngineContent/MaterialArchives/TextureStudio" "${CMAKE_BINARY_DIR}/AutomotivePaintEvidence")
            set_tests_properties(AutomotivePaintValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
    endif()
    add_executable(PatternLibraryPresetValidation Exhibits/TextureStudio/Code/PatternLibraryPresetValidation.cpp)
    target_link_libraries(PatternLibraryPresetValidation PRIVATE FrontierVectorAuthoring)
    add_test(NAME PatternLibraryPresetValidation COMMAND PatternLibraryPresetValidation)
    set_tests_properties(PatternLibraryPresetValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(VectorSelectionValidation Exhibits/TextureStudio/Code/VectorSelectionValidation.cpp)
    target_link_libraries(VectorSelectionValidation PRIVATE FrontierVectorAuthoring)
    add_test(NAME VectorSelectionValidation COMMAND VectorSelectionValidation)
    set_tests_properties(VectorSelectionValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampTextOutlineValidation Exhibits/TextureStudio/Code/StampTextOutlineValidation.cpp)
    target_link_libraries(StampTextOutlineValidation PRIVATE FrontierVectorAuthoring)
    add_test(NAME StampTextOutlineValidation COMMAND StampTextOutlineValidation)
    set_tests_properties(StampTextOutlineValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampVectorImportValidation Exhibits/TextureStudio/Code/StampVectorImportValidation.cpp)
    target_link_libraries(StampVectorImportValidation PRIVATE FrontierVectorAuthoring)
    add_test(NAME StampVectorImportValidation COMMAND StampVectorImportValidation)
    set_tests_properties(StampVectorImportValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampWorkspacePanelConstructionValidation Exhibits/TextureStudio/Code/StampWorkspacePanelConstructionValidation.cpp)
    target_link_libraries(StampWorkspacePanelConstructionValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME StampWorkspacePanelConstructionValidation COMMAND StampWorkspacePanelConstructionValidation)
    set_tests_properties(StampWorkspacePanelConstructionValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampTextGestureValidation Exhibits/TextureStudio/Code/StampTextGestureValidation.cpp)
    target_link_libraries(StampTextGestureValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME StampTextGestureValidation COMMAND StampTextGestureValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/FontArchives/DMSans/DMSans-Regular.ttf")
    set_tests_properties(StampTextGestureValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(PatternLibraryGalleryValidation Exhibits/TextureStudio/Code/PatternLibraryGalleryValidation.cpp)
    target_link_libraries(PatternLibraryGalleryValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME PatternLibraryGalleryValidation COMMAND PatternLibraryGalleryValidation)
    set_tests_properties(PatternLibraryGalleryValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(PatternThumbnailAtlasValidation Exhibits/TextureStudio/Code/PatternThumbnailAtlasValidation.cpp)
    target_link_libraries(PatternThumbnailAtlasValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME PatternThumbnailAtlasValidation COMMAND PatternThumbnailAtlasValidation)
    set_tests_properties(PatternThumbnailAtlasValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(PatternAtlasDiskCacheValidation Exhibits/TextureStudio/Code/PatternAtlasDiskCacheValidation.cpp)
    target_link_libraries(PatternAtlasDiskCacheValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME PatternAtlasDiskCacheValidation COMMAND PatternAtlasDiskCacheValidation "${CMAKE_BINARY_DIR}/pattern-atlas-cache-evidence")
    set_tests_properties(PatternAtlasDiskCacheValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampTextThumbnailContrastValidation Exhibits/TextureStudio/Code/StampTextThumbnailContrastValidation.cpp)
    target_link_libraries(StampTextThumbnailContrastValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME StampTextThumbnailContrastValidation COMMAND StampTextThumbnailContrastValidation)
    set_tests_properties(StampTextThumbnailContrastValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(StampTextLiveFitValidation Exhibits/TextureStudio/Code/StampTextLiveFitValidation.cpp)
    target_link_libraries(StampTextLiveFitValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME StampTextLiveFitValidation COMMAND StampTextLiveFitValidation)
    set_tests_properties(StampTextLiveFitValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(WorkspaceZoomValidation Exhibits/TextureStudio/Code/WorkspaceZoomValidation.cpp)
    target_link_libraries(WorkspaceZoomValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME WorkspaceZoomValidation COMMAND WorkspaceZoomValidation)
    set_tests_properties(WorkspaceZoomValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(MaterialLibraryValidation Exhibits/TextureStudio/Code/MaterialLibraryValidation.cpp)
    target_link_libraries(MaterialLibraryValidation PRIVATE FrontierTextureStudioUI)
    add_test(NAME MaterialLibraryValidation COMMAND MaterialLibraryValidation
        "${CMAKE_SOURCE_DIR}/Exhibits/Renderer/Materials" "${CMAKE_BINARY_DIR}/MaterialLibraryEvidence")
    set_tests_properties(MaterialLibraryValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(MaterialProceduralBake Exhibits/TextureStudio/Code/MaterialProceduralBake.cpp)
    target_include_directories(MaterialProceduralBake PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(MaterialProceduralBake PRIVATE FrontierMaterialLibrary)
    add_executable(EnvironmentBake Exhibits/TextureStudio/Code/EnvironmentBake.cpp)
    target_include_directories(EnvironmentBake PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(EnvironmentBake PRIVATE FrontierMaterialLibrary)
    add_executable(MaterialProceduralBakingValidation Exhibits/TextureStudio/Code/MaterialProceduralBakingValidation.cpp)
    target_include_directories(MaterialProceduralBakingValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(MaterialProceduralBakingValidation PRIVATE FrontierMaterialLibrary)
    add_test(NAME MaterialProceduralBakingValidation COMMAND MaterialProceduralBakingValidation)
    set_tests_properties(MaterialProceduralBakingValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(MaterialAuthoringValidation Exhibits/TextureStudio/Code/MaterialAuthoringValidation.cpp)
    target_link_libraries(MaterialAuthoringValidation PRIVATE FrontierMaterialLibrary)
    add_test(NAME MaterialAuthoringValidation COMMAND MaterialAuthoringValidation)
    set_tests_properties(MaterialAuthoringValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
if(BUILD_TESTING)
    add_executable(ProjectBrowserValidation Exhibits/ProjectBrowser/Code/ProjectBrowserValidation.cpp)
    target_link_libraries(ProjectBrowserValidation PRIVATE FrontierEditorUI)
    add_test(NAME ProjectBrowserValidation COMMAND ProjectBrowserValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/Defaults/BlankProject/Blank.projectspace"
        "${CMAKE_BINARY_DIR}/project-browser-validation")
    add_executable(MonitorPlacementValidation Exhibits/Docking/Code/MonitorPlacementValidation.cpp)
    target_link_libraries(MonitorPlacementValidation PRIVATE FrontierInterface)
    add_test(NAME MonitorPlacementValidation COMMAND MonitorPlacementValidation)
    foreach(EDITOR_MODE 0 1)
        add_executable(ActivationValidation${EDITOR_MODE} Exhibits/UIFoundation/Code/ActivationValidation.cpp)
        target_include_directories(ActivationValidation${EDITOR_MODE} PRIVATE "${CMAKE_SOURCE_DIR}")
        target_compile_definitions(ActivationValidation${EDITOR_MODE} PRIVATE FRONTIER_EDITOR=${EDITOR_MODE})
        add_test(NAME ActivationValidation${EDITOR_MODE} COMMAND ActivationValidation${EDITOR_MODE})
    endforeach()
    add_test(NAME FontCatalogueValidation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/Exhibits/ControlCentre/Code/FontCatalogueValidation.py")
    add_executable(InputPreferencesValidation Exhibits/ControlCentre/Code/InputPreferencesValidation.cpp)
    target_link_libraries(InputPreferencesValidation PRIVATE FrontierInterface)
    add_test(NAME InputPreferencesValidation COMMAND InputPreferencesValidation "${CMAKE_BINARY_DIR}/input-evidence")
    add_executable(PanelExpansionValidation Exhibits/ControlCentre/Code/PanelExpansionValidation.cpp)
    target_link_libraries(PanelExpansionValidation PRIVATE FrontierInterface)
    add_test(NAME PanelExpansionValidation COMMAND PanelExpansionValidation)
    add_executable(ContentDirectoryValidation Exhibits/ContentBrowser/Code/ContentDirectoryValidation.cpp)
    target_link_libraries(ContentDirectoryValidation PRIVATE FrontierInterface)
    add_test(NAME ContentDirectoryValidation COMMAND ContentDirectoryValidation "${CMAKE_BINARY_DIR}/content-directory-evidence")
    add_executable(ThumbnailAtlasCacheValidation Exhibits/TextureStudio/Code/ThumbnailAtlasCacheValidation.cpp)
    target_link_libraries(ThumbnailAtlasCacheValidation PRIVATE FrontierThumbnailAtlasCache)
    add_test(NAME ThumbnailAtlasCacheValidation COMMAND ThumbnailAtlasCacheValidation "${CMAKE_BINARY_DIR}/thumbnail-atlas-cache-evidence")
    add_executable(ContentBrowserValidation Exhibits/ContentBrowser/Code/ContentBrowserValidation.cpp)
    target_link_libraries(ContentBrowserValidation PRIVATE FrontierEditorUI)
    add_test(NAME ContentBrowserValidation COMMAND ContentBrowserValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/FontArchives" "${CMAKE_BINARY_DIR}/content-browser-evidence")
    add_executable(ControlCentreValidation Exhibits/ControlCentre/Code/ControlCentreValidation.cpp)
    target_link_libraries(ControlCentreValidation PRIVATE FrontierEditorUI)
    add_test(NAME ControlCentreValidation COMMAND ControlCentreValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/FontArchives" "${CMAKE_BINARY_DIR}/control-centre-evidence")
    add_executable(WorkspaceLayoutValidation Exhibits/Docking/Code/WorkspaceLayoutValidation.cpp)
    target_link_libraries(WorkspaceLayoutValidation PRIVATE FrontierEditorUI)
    add_test(NAME WorkspaceLayoutValidation COMMAND WorkspaceLayoutValidation
        "${CMAKE_SOURCE_DIR}/EngineContent" "${CMAKE_CURRENT_BINARY_DIR}/workspace-layout-evidence")
    add_executable(DockingValidation Exhibits/Docking/Code/DockingValidation.cpp)
    target_link_libraries(DockingValidation PRIVATE FrontierEditorUI)
    add_test(NAME DockingValidation COMMAND DockingValidation
        "${CMAKE_SOURCE_DIR}/EngineContent" "${CMAKE_BINARY_DIR}/docking-evidence")
    add_executable(NotificationValidation Exhibits/Notifications/Code/NotificationValidation.cpp)
    target_link_libraries(NotificationValidation PRIVATE FrontierEditorUI)
    add_test(NAME NotificationValidation COMMAND NotificationValidation
        "${CMAKE_SOURCE_DIR}/EngineContent" "${CMAKE_BINARY_DIR}/notification-evidence")
    add_executable(ProjectSpaceValidation Exhibits/ProjectSpace/Code/ProjectSpaceValidation.cpp)
    target_link_libraries(ProjectSpaceValidation PRIVATE FrontierEditorUI)
    add_test(NAME ProjectSpaceValidation COMMAND ProjectSpaceValidation
        "${CMAKE_SOURCE_DIR}/EngineContent" "${CMAKE_BINARY_DIR}/projectspace-evidence")
    add_executable(InterfaceValidation Exhibits/UIFoundation/Code/InterfaceValidation.cpp)
    target_link_libraries(InterfaceValidation PRIVATE FrontierEditorUI)
    add_test(NAME InterfaceValidation COMMAND InterfaceValidation
        "${CMAKE_SOURCE_DIR}/EngineContent/Icons" "${CMAKE_BINARY_DIR}/evidence")
endif()
