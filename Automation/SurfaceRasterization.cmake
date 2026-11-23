#============================================================================================================================================
#                                                       SURFACERASTERIZATION.CMAKE
#============================================================================================================================================
# 🧩 Build the reusable Vulkan CAD unit and explicitly provisioned shader/readback validation, without automatic downloads.

# 📝 MaterialAuthoring rides this unit because SurfaceMaterial decodes the [Authoring] table; keeping the
# codec beside its only archive-level caller avoids a static-library cycle with FrontierMaterialLibrary.
add_library(FrontierSurfaceRasterization STATIC Engine/Render/SurfaceRasterization.cpp Engine/Render/SurfaceMaterial.cpp
    Engine/ContentInterchange/MaterialAuthoring.cpp
    Engine/Render/MaterialThumbnailAtlas.cpp Engine/Render/MaterialLibraryRendering.cpp Engine/Render/MaterialProceduralBaking.cpp
    Engine/Render/BrowserPreviewRendering.cpp Engine/Render/StbImageImplementation.cpp
    Engine/Render/StbImageWriteImplementation.cpp)
target_include_directories(FrontierSurfaceRasterization PUBLIC "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include")
target_include_directories(FrontierSurfaceRasterization PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include" "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
target_compile_definitions(FrontierSurfaceRasterization PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(FrontierSurfaceRasterization PUBLIC FrontierVectorAuthoring FrontierThumbnailAtlasCache)

# Windows/native Slang is preferred. The supplied WASM path is a portable local verification alternative, not vendored.
find_program(FRONTIER_SLANGC NAMES slangc)
set(FRONTIER_SLANG_WASM "" CACHE FILEPATH "Explicit portable Slang WASM JavaScript entry, if native slangc is absent")
set(FRONTIER_VULKAN_DRIVER "" CACHE FILEPATH "Explicit Vulkan loader/driver for offscreen CTest; empty leaves runtime test unregistered")
set(FRONTIER_SURFACE_SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/ShaderArchives/Surface")
set(SURFACE_SHADER_OUTPUTS)
foreach(SURFACE_SOURCE CadSurface CadEdge MatCapSurface PreviewPbrSurface MaterialSurface BrowserPreview BrowserDrafting)
    foreach(SURFACE_CATEGORY Vertex Fragment)
        set(SURFACE_ENTRY "${SURFACE_SOURCE}${SURFACE_CATEGORY}")
        list(APPEND SURFACE_SHADER_OUTPUTS "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${SURFACE_ENTRY}.spv")
        if(FRONTIER_SLANGC)
            string(TOLOWER "${SURFACE_CATEGORY}" SURFACE_CATEGORY_ARGUMENT)
            add_custom_command(OUTPUT "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${SURFACE_ENTRY}.spv"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                COMMAND "${FRONTIER_SLANGC}" "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/${SURFACE_SOURCE}.slang"
                    -entry "${SURFACE_ENTRY}" -stage "${SURFACE_CATEGORY_ARGUMENT}" -target spirv -profile spirv_1_3
                    -o "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${SURFACE_ENTRY}.spv"
                    -reflection-json "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${SURFACE_ENTRY}.json"
                DEPENDS "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/${SURFACE_SOURCE}.slang"
                    "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/SurfaceProjection.slang"
                    "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/ScatteringEvaluation.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/AutomotivePaint.slang"
                    "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/BrowserProjection.slang"
                BYPRODUCTS "${FRONTIER_SURFACE_SHADER_DIRECTORY}/${SURFACE_ENTRY}.json"
                VERBATIM)
        endif()
    endforeach()
endforeach()
if(FRONTIER_SLANGC)
    add_custom_target(FrontierSurfaceShaders ALL DEPENDS ${SURFACE_SHADER_OUTPUTS})
elseif(FRONTIER_SLANG_WASM)
    find_program(FRONTIER_NODE NAMES node REQUIRED)
    add_custom_command(OUTPUT ${SURFACE_SHADER_OUTPUTS}
        COMMAND "${FRONTIER_NODE}" "${CMAKE_SOURCE_DIR}/Automation/CompileSurfaceShaders.mjs"
            "${FRONTIER_SLANG_WASM}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
        DEPENDS "${CMAKE_SOURCE_DIR}/Automation/CompileSurfaceShaders.mjs" "${FRONTIER_SLANG_WASM}"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/CadSurface.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/CadEdge.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/SurfaceProjection.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/ScatteringEvaluation.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/AutomotivePaint.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/MatCapSurface.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/PreviewPbrSurface.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/MaterialSurface.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/BrowserProjection.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/BrowserPreview.slang"
            "${CMAKE_SOURCE_DIR}/Engine/Render/Shader/BrowserDrafting.slang"
        BYPRODUCTS "${FRONTIER_SURFACE_SHADER_DIRECTORY}/ShaderCompilation.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/CadSurfaceVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/CadSurfaceFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/CadEdgeVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/CadEdgeFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MatCapSurfaceVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MatCapSurfaceFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/PreviewPbrSurfaceVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/PreviewPbrSurfaceFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MaterialSurfaceVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/MaterialSurfaceFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BrowserPreviewVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BrowserPreviewFragment.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BrowserDraftingVertex.json"
            "${FRONTIER_SURFACE_SHADER_DIRECTORY}/BrowserDraftingFragment.json"
        VERBATIM)
    add_custom_target(FrontierSurfaceShaders ALL DEPENDS ${SURFACE_SHADER_OUTPUTS})
else()
    message(STATUS "Surface C++ unit builds; supply slangc or FRONTIER_SLANG_WASM for shader/readback validation")
endif()

if(BUILD_TESTING)
    add_executable(SurfaceGeometryValidation Exhibits/Renderer/Code/SurfaceGeometryValidation.cpp)
    target_link_libraries(SurfaceGeometryValidation PRIVATE FrontierSurfaceRasterization)
    add_test(NAME SurfaceGeometryValidation COMMAND SurfaceGeometryValidation)
    add_executable(SurfaceRasterizationValidation Exhibits/Renderer/Code/SurfaceRasterizationValidation.cpp)
    target_link_libraries(SurfaceRasterizationValidation PRIVATE FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    add_executable(MatCapRasterizationValidation Exhibits/Renderer/Code/MatCapRasterizationValidation.cpp)
    target_link_libraries(MatCapRasterizationValidation PRIVATE FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    add_executable(PreviewPbrRasterizationValidation Exhibits/Renderer/Code/PreviewPbrRasterizationValidation.cpp)
    target_link_libraries(PreviewPbrRasterizationValidation PRIVATE FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    add_executable(MaterialRasterizationValidation Exhibits/Renderer/Code/MaterialRasterizationValidation.cpp)
    target_link_libraries(MaterialRasterizationValidation PRIVATE FrontierSurfaceRasterization ${CMAKE_DL_LIBS})
    if(TARGET FrontierSurfaceShaders)
        add_dependencies(MaterialRasterizationValidation FrontierSurfaceShaders)
        add_dependencies(PreviewPbrRasterizationValidation FrontierSurfaceShaders)
        add_dependencies(MatCapRasterizationValidation FrontierSurfaceShaders)
        add_dependencies(SurfaceRasterizationValidation FrontierSurfaceShaders)
        if(FRONTIER_VULKAN_DRIVER)
            add_test(NAME MaterialRasterizationValidation COMMAND MaterialRasterizationValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_BINARY_DIR}/MaterialEvidence" "${CMAKE_SOURCE_DIR}/Exhibits/Renderer/Materials")
            add_test(NAME PreviewPbrRasterizationValidation COMMAND PreviewPbrRasterizationValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_BINARY_DIR}/PreviewPbrEvidence")
            add_test(NAME MatCapRasterizationValidation COMMAND MatCapRasterizationValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_BINARY_DIR}/MatCapRasterEvidence")
            add_test(NAME SurfaceRasterizationValidation COMMAND SurfaceRasterizationValidation
                "${FRONTIER_VULKAN_DRIVER}" "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
                "${CMAKE_BINARY_DIR}/SurfaceRasterEvidence")
        endif()
    elseif(FRONTIER_VULKAN_DRIVER)
        message(FATAL_ERROR "A Vulkan readback test requires explicitly available Slang shader compilation")
    endif()
endif()

if(TARGET TextureStudio AND TARGET FrontierSurfaceShaders)
    add_dependencies(TextureStudio FrontierSurfaceShaders)
    install(FILES ${SURFACE_SHADER_OUTPUTS} DESTINATION EngineContent/ShaderArchives/Surface)
endif()
if(TARGET TextureStudio AND TARGET FrontierSurfaceShaders)
    add_custom_command(TARGET TextureStudio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
            "$<TARGET_FILE_DIR:TextureStudio>/EngineContent/ShaderArchives/Surface")
endif()
if(TARGET Frontier AND TARGET FrontierSurfaceShaders)
    add_dependencies(Frontier FrontierSurfaceShaders)
    add_custom_command(TARGET Frontier POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FRONTIER_SURFACE_SHADER_DIRECTORY}"
            "$<TARGET_FILE_DIR:Frontier>/EngineContent/ShaderArchives/Surface")
endif()
