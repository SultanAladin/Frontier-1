#============================================================================================================================================
#                                                       PROJECTS/UNTITLED/CMAKELISTS.TXT
#============================================================================================================================================
# 🧩 Reference standalone Frontier project: tight zero-Editor runtime executable (links FrontierEngine only — zero FrontierImGui/Editor)

add_library(UntitledRuntimeLibrary STATIC
    Source/UntitledPolicies.cpp
    Source/UntitledLifecycle.cpp
)

target_include_directories(UntitledRuntimeLibrary PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(UntitledRuntimeLibrary PUBLIC
    FrontierEngine
    FrontierCompileSettings
)

target_compile_definitions(UntitledRuntimeLibrary PUBLIC
    UNTITLED_PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(Untitled
    Source/Main.cpp
)

target_link_libraries(Untitled PRIVATE
    UntitledRuntimeLibrary
)
