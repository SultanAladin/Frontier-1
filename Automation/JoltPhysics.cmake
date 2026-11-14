#============================================================================================================================================
#                                                         JOLTPHYSICS.CMAKE
#============================================================================================================================================
# 🧩 Build JoltPhysics v5.6.0 (e77f175595) deterministically across Windows, Linux, and Android (x86_64 SSE4.2 and ARM64 NEON).

function(frontier_add_jolt)
    if(TARGET Jolt)
        return()
    endif()

    set(PHYSICS_REPO_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/JoltPhysics")
    if(NOT EXISTS "${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake")
        message(FATAL_ERROR "Missing ExternalPackages/JoltPhysics — run: python3 Automation/PrepareDependencies.py")
    endif()

    set(USE_ASSERTS OFF)
    set(DOUBLE_PRECISION OFF)
    set(CROSS_PLATFORM_DETERMINISTIC ON)
    set(OBJECT_LAYER_BITS 16)

    # 📝 Enable SSE4.1/SSE4.2 only on x86_64 targets; ARM64 (Android / Linux ARM) uses Jolt's native NEON path automatically.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        set(USE_SSE4_1 ON)
        set(USE_SSE4_2 ON)
    else()
        set(USE_SSE4_1 OFF)
        set(USE_SSE4_2 OFF)
    endif()

    set(USE_AVX OFF)
    set(USE_AVX2 OFF)
    set(USE_AVX512 OFF)
    set(USE_LZCNT OFF)
    set(USE_TZCNT OFF)
    set(USE_F16C OFF)
    set(USE_FMADD OFF)
    set(ENABLE_OBJECT_STREAM OFF)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
    set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
    set(PROFILER_IN_DISTRIBUTION OFF)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
    set(DISABLE_CUSTOM_ALLOCATOR OFF)
    set(USE_STD_VECTOR OFF)
    set(TRACK_BROADPHASE_STATS OFF)
    set(TRACK_NARROWPHASE_STATS OFF)
    set(JPH_TRACK_SIMULATION_STATS OFF)
    set(JPH_USE_DX12 OFF)
    set(JPH_USE_VK OFF)
    set(JPH_USE_MTL OFF)
    set(JPH_USE_CPU_COMPUTE OFF)
    set(JPH_BUILD_SHARED_LIBS OFF)

    include("${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake")
    if(NOT MSVC)
        target_compile_options(Jolt PRIVATE -w)
    endif()
endfunction()
