#============================================================================================================================================
#                                                           VECTORCOVERAGE.CMAKE
#============================================================================================================================================
# 🧩 Build the shared GPU coverage unit and explicit shader/readback verification alongside existing surface ownership.

target_sources(FrontierSurfaceRasterization PRIVATE
    Engine/Render/VectorCoverageRasterization.cpp Engine/Render/VectorAuthoringCoverageRendering.cpp
    Engine/Render/VectorSelectionEvaluation.cpp Engine/Render/VectorCoverageTileBinning.cpp)
set(VECTOR_COVERAGE_ARCHIVE "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorCoverageCompute.spv")
set(VECTOR_SELECTION_ARCHIVE "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorSelectionCompute.spv")
set(VECTOR_SURFACE_ARCHIVE "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorSurfaceCompute.spv")
if(FRONTIER_SLANGC)
    foreach(ENTRY VectorCoverageCompute VectorSelectionCompute)
        add_custom_command(OUTPUT "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${ENTRY}.spv"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
            COMMAND "${FRONTIER_SLANGC}" "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/VectorCoverage.slang"
                -entry ${ENTRY} -stage compute -target spirv -profile spirv_1_3
                -o "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${ENTRY}.spv"
            DEPENDS Engine/Render/Shader/VectorCoverage.slang VERBATIM)
    endforeach()
elseif(FRONTIER_SLANG_WASM)
    add_custom_command(OUTPUT "${VECTOR_COVERAGE_ARCHIVE}" "${VECTOR_SELECTION_ARCHIVE}"
        COMMAND "${FRONTIER_NODE}" "${CMAKE_SOURCE_DIR}/Automation/CompileVectorCoverage.mjs"
            "${FRONTIER_SLANG_WASM}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS Engine/Render/Shader/VectorCoverage.slang Automation/CompileVectorCoverage.mjs
        BYPRODUCTS "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorCompilation.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorCoverageCompute.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorSelectionCompute.json" VERBATIM)
endif()
if(FRONTIER_SLANGC)
    add_custom_command(OUTPUT "${VECTOR_SURFACE_ARCHIVE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        COMMAND "${FRONTIER_SLANGC}" "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/VectorSurface.slang"
            -entry VectorSurfaceCompute -stage compute -target spirv -profile spirv_1_3 -o "${VECTOR_SURFACE_ARCHIVE}"
        DEPENDS Engine/Render/Shader/VectorSurface.slang Engine/Render/Shader/VectorCoverage.slang VERBATIM)
elseif(FRONTIER_SLANG_WASM)
    add_custom_command(OUTPUT "${VECTOR_SURFACE_ARCHIVE}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}/VectorSurfaceCompilation.json"
        COMMAND "${FRONTIER_NODE}" "${CMAKE_SOURCE_DIR}/Automation/CompileVectorSurface.mjs"
            "${FRONTIER_SLANG_WASM}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS Automation/CompileVectorSurface.mjs Engine/Render/Shader/VectorSurface.slang
            Engine/Render/Shader/VectorCoverage.slang "${FRONTIER_SLANG_WASM}" VERBATIM)
endif()
if(FRONTIER_SLANGC OR FRONTIER_SLANG_WASM)
    add_custom_target(FrontierVectorCoverageShaders ALL DEPENDS "${VECTOR_COVERAGE_ARCHIVE}" "${VECTOR_SELECTION_ARCHIVE}" "${VECTOR_SURFACE_ARCHIVE}")
    if(TARGET TextureStudio)
        add_dependencies(TextureStudio FrontierVectorCoverageShaders)
        install(FILES "${VECTOR_COVERAGE_ARCHIVE}" "${VECTOR_SELECTION_ARCHIVE}" "${VECTOR_SURFACE_ARCHIVE}" DESTINATION EngineContent/ShaderArchives/Surface)
    endif()
endif()
if(BUILD_TESTING)
    add_executable(VectorSurfaceValidation Exhibits/TextureStudio/Code/VectorSurfaceValidation.cpp)
    target_include_directories(VectorSurfaceValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(VectorSurfaceValidation PRIVATE FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    if(TARGET FrontierVectorCoverageShaders)
        add_dependencies(VectorSurfaceValidation FrontierVectorCoverageShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME VectorSurfaceValidation COMMAND VectorSurfaceValidation "${FRONTIER_VULKAN_DRIVER}"
                "${VECTOR_SURFACE_ARCHIVE}" "${CMAKE_BINARY_DIR}/VectorSurfaceEvidence")
            set_tests_properties(VectorSurfaceValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
    endif()
    add_executable(VectorCoverageValidation Exhibits/TextureStudio/Code/VectorCoverageValidation.cpp)
    target_include_directories(VectorCoverageValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(VectorCoverageValidation PRIVATE FrontierSurfaceRasterization FrontierVectorAuthoring ${CMAKE_DL_LIBS})
    if(TARGET FrontierVectorCoverageShaders)
        add_dependencies(VectorCoverageValidation FrontierVectorCoverageShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME VectorCoverageValidation COMMAND VectorCoverageValidation "${FRONTIER_VULKAN_DRIVER}"
                "${VECTOR_COVERAGE_ARCHIVE}" "${CMAKE_BINARY_DIR}/VectorCoverageEvidence")
            set_tests_properties(VectorCoverageValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
    endif()
    add_executable(VectorCoverageTileBinningValidation Exhibits/TextureStudio/Code/VectorCoverageTileBinningValidation.cpp)
    target_link_libraries(VectorCoverageTileBinningValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME VectorCoverageTileBinningValidation COMMAND VectorCoverageTileBinningValidation)
    add_executable(VectorCoverageTierHysteresisValidation Exhibits/TextureStudio/Code/VectorCoverageTierHysteresisValidation.cpp)
    target_link_libraries(VectorCoverageTierHysteresisValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME VectorCoverageTierHysteresisValidation COMMAND VectorCoverageTierHysteresisValidation)
    add_executable(VectorCoverageZoomResolutionValidation Exhibits/TextureStudio/Code/VectorCoverageZoomResolutionValidation.cpp)
    target_link_libraries(VectorCoverageZoomResolutionValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME VectorCoverageZoomResolutionValidation COMMAND VectorCoverageZoomResolutionValidation)
    add_executable(VectorCoverageIdleResubmissionValidation Exhibits/TextureStudio/Code/VectorCoverageIdleResubmissionValidation.cpp)
    target_link_libraries(VectorCoverageIdleResubmissionValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME VectorCoverageIdleResubmissionValidation COMMAND VectorCoverageIdleResubmissionValidation)
    add_executable(VectorCoverageCurveFlatteningValidation Exhibits/TextureStudio/Code/VectorCoverageCurveFlatteningValidation.cpp)
    target_link_libraries(VectorCoverageCurveFlatteningValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME VectorCoverageCurveFlatteningValidation COMMAND VectorCoverageCurveFlatteningValidation)
    add_executable(PatternWorkspaceValidation Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp)
    target_include_directories(PatternWorkspaceValidation PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
    target_link_libraries(PatternWorkspaceValidation PRIVATE FrontierTextureStudioUI FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    if(TARGET FrontierVectorCoverageShaders)
        add_dependencies(PatternWorkspaceValidation FrontierVectorCoverageShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME PatternWorkspaceValidation COMMAND PatternWorkspaceValidation "${FRONTIER_VULKAN_DRIVER}"
                "${VECTOR_COVERAGE_ARCHIVE}" "${CMAKE_SOURCE_DIR}/EngineContent/FontArchives/DMSans/DMSans-Regular.ttf"
                "${CMAKE_BINARY_DIR}/PatternWorkspaceEvidence")
            set_tests_properties(PatternWorkspaceValidation PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
    endif()
endif()
