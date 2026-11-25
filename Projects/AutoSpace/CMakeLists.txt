# ============================================================================================================================================
#                                                     PROJECTS/AUTOSPACE/CMAKELISTS.TXT
# ============================================================================================================================================
# 🧩 Standalone project build target for AutoSpace (isolated from Engine/; links FrontierEngine with zero Editor UI)

add_library(AutoSpaceProject STATIC
    Source/AutoSpacePolicies.cpp
    Source/AutoSpaceLifecycle.cpp
)
target_include_directories(AutoSpaceProject PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(AutoSpaceProject PUBLIC
    FrontierEngine
)
target_compile_features(AutoSpaceProject PUBLIC cxx_std_20)

add_executable(AutoSpace
    Source/AutoSpaceLaunch.cpp
)
target_link_libraries(AutoSpace PRIVATE AutoSpaceProject)
target_compile_features(AutoSpace PRIVATE cxx_std_20)
