#============================================================================================================================================
#                                                        THORVGVECTOR.CMAKE
#============================================================================================================================================
# 🧩 Build ThorVG 1.0.0 (6715f99ac1) symbolic frontend (SVG DOM + TTF/OTF typography + RenderMethod SPI) for Frontier's Vulkan GPU Vector rasterizer.

find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT DEFINED FRONTIER_ROOT)
    set(FRONTIER_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(THORVG_SRC "${FRONTIER_ROOT}/ExternalPackages/thorvg")
set(THORVG_STAGING "${CMAKE_BINARY_DIR}/thorvg_staged")
file(MAKE_DIRECTORY "${THORVG_STAGING}")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${FRONTIER_ROOT}/Automation/StageThorVG.py"
            "${THORVG_SRC}" "${THORVG_STAGING}/tvgInitializer.cpp"
    RESULT_VARIABLE THORVG_STAGED_RESULT
)
if(NOT THORVG_STAGED_RESULT EQUAL 0)
    message(FATAL_ERROR "ThorVG staging failed; run python3 Automation/PrepareDependencies.py")
endif()

file(WRITE "${THORVG_STAGING}/config.h"
    "#pragma once\n"
    "#define THORVG_VERSION_STRING \"1.0.0\"\n"
    "#define THORVG_SVG_LOADER_SUPPORT 1\n"
    "#define THORVG_TTF_LOADER_SUPPORT 1\n"
    "#define THORVG_THREAD_SUPPORT 1\n"
    "#define THORVG_FILE_IO_SUPPORT 1\n"
)

set(THORVG_PUBLIC_INCLUDES "${THORVG_SRC}/inc")
set(THORVG_PRIVATE_INCLUDES
    "${THORVG_STAGING}"
    "${THORVG_SRC}/src/common"
    "${THORVG_SRC}/src/renderer"
    "${THORVG_SRC}/src/loaders/svg"
    "${THORVG_SRC}/src/loaders/ttf"
    "${THORVG_SRC}/src/loaders/raw"
)
set(THORVG_SOURCES "${THORVG_STAGING}/tvgInitializer.cpp")

foreach(SUBDIR src/common src/renderer src/loaders/svg src/loaders/ttf src/loaders/raw)
    file(GLOB THORVG_SUBDIR_SOURCES CONFIGURE_DEPENDS "${THORVG_SRC}/${SUBDIR}/*.cpp")
    list(REMOVE_ITEM THORVG_SUBDIR_SOURCES "${THORVG_SRC}/src/renderer/tvgInitializer.cpp")
    list(APPEND THORVG_SOURCES ${THORVG_SUBDIR_SOURCES})
endforeach()

add_library(thorvg_static STATIC ${THORVG_SOURCES})
target_include_directories(thorvg_static
    PUBLIC  ${THORVG_PUBLIC_INCLUDES}
    PRIVATE ${THORVG_PRIVATE_INCLUDES}
)
target_compile_definitions(thorvg_static PUBLIC TVG_STATIC PRIVATE TVG_BUILD NOMINMAX)
set_target_properties(thorvg_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(thorvg_static PUBLIC cxx_std_14)

if(NOT MSVC)
    target_compile_options(thorvg_static PRIVATE -w)
endif()

set(THORVG_VECTOR_SPI_INCLUDES
    ${THORVG_PRIVATE_INCLUDES}
    ${THORVG_PUBLIC_INCLUDES}
)

if(NOT WIN32 AND NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(thorvg_static PUBLIC Threads::Threads)
endif()
