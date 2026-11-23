cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FrontierTyreContinuum LANGUAGES CXX)
endif()

# 📝 Standalone visual projection and Hex8 deformation; no vehicle dynamics or Jolt dependency.
add_library(FrontierTyreContinuum STATIC TyreContinuum.cpp TyreProjectionDevice.cpp TyreSolidDevice.cpp)
target_compile_features(FrontierTyreContinuum PUBLIC cxx_std_17)
target_include_directories(FrontierTyreContinuum PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(MSVC)
    target_compile_options(FrontierTyreContinuum PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierTyreContinuum PRIVATE -Wall -Wextra -Werror)
endif()

# 📝 Optional caller-owned Vulkan command recorder; the CPU tyre library has no Vulkan dependency.
if(FRONTIER_TYRE_VULKAN_HEADERS)
    add_library(FrontierTyreProjectionVulkan STATIC TyreProjectionVulkan.cpp)
    target_compile_features(FrontierTyreProjectionVulkan PUBLIC cxx_std_17)
    target_include_directories(FrontierTyreProjectionVulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${FRONTIER_TYRE_VULKAN_HEADERS}")
    if(MSVC)
        target_compile_options(FrontierTyreProjectionVulkan PRIVATE /utf-8 /W4)
    else()
        target_compile_options(FrontierTyreProjectionVulkan PRIVATE -Wall -Wextra -Werror)
    endif()
endif()
