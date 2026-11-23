cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FrontierPathReplay LANGUAGES CXX)
endif()

# 📝 Typed shader inputs do not provide Vulkan submission or editor integration.
add_library(FrontierMediumRadiancePayload STATIC MediumRadiancePayload.cpp)
target_compile_features(FrontierMediumRadiancePayload PUBLIC cxx_std_17)
target_include_directories(FrontierMediumRadiancePayload PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierMediumRadiancePayload PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierMediumRadiancePayload PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Float32 source distribution is shared by surface and fog transport; submission remains separate.
add_library(FrontierRadiantFacetDistribution STATIC RadiantFacetDistribution.cpp)
target_compile_features(FrontierRadiantFacetDistribution PUBLIC cxx_std_17)
target_include_directories(FrontierRadiantFacetDistribution PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierRadiantFacetDistribution PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierRadiantFacetDistribution PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Reusable bounded replay-domain intake; shader submission and editor wiring are separate work.
add_library(FrontierPathReplayDomain STATIC PathReplayDomain.cpp)
target_compile_features(FrontierPathReplayDomain PUBLIC cxx_std_20)
target_include_directories(FrontierPathReplayDomain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierPathReplayDomain PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierPathReplayDomain PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Optional caller-owned Vulkan path-replay recorder; pipelines, descriptors and queue remain external.
if(FRONTIER_RENDERING_VULKAN_HEADERS)
    add_library(FrontierPathReplayVulkan STATIC PathReplayVulkan.cpp)
    target_compile_features(FrontierPathReplayVulkan PUBLIC cxx_std_17)
    target_include_directories(FrontierPathReplayVulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${FRONTIER_RENDERING_VULKAN_HEADERS}")
    if(MSVC)
        target_compile_options(FrontierPathReplayVulkan PRIVATE /utf-8 /W4)
    else()
        target_compile_options(FrontierPathReplayVulkan PRIVATE -Wall -Wextra -Werror)
    endif()
endif()

# 📝 Matched face-flux gas projection parameters; shader submission and advection remain separate.
add_library(FrontierGasProjectionFootprint INTERFACE)
target_compile_features(FrontierGasProjectionFootprint INTERFACE cxx_std_17)
target_include_directories(FrontierGasProjectionFootprint INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 📝 Typed plume input footprints and validation, without a renderer or GPU submission.
add_library(FrontierPlumeLatticeFootprint STATIC PlumeLatticeFootprint.cpp)
target_compile_features(FrontierPlumeLatticeFootprint PUBLIC cxx_std_17)
target_include_directories(FrontierPlumeLatticeFootprint PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierPlumeLatticeFootprint PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierPlumeLatticeFootprint PRIVATE -Wall -Wextra -Werror)
endif()
