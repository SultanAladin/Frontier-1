#============================================================================================================================================
#                                                       WINDOWSTOOLCHAIN.CMAKE
#============================================================================================================================================
# 🧩 Cross-compile Windows x64 binaries from Linux hosts using pinned Zig 0.13.0; native Windows builds use MSVC directly.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import pathlib, ziglang; print(pathlib.Path(ziglang.__file__).parent / 'zig')"
    OUTPUT_VARIABLE FRONTIER_ZIG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ZIG_LOCATE_STATUS
)
if(NOT ZIG_LOCATE_STATUS EQUAL 0 OR NOT EXISTS "${FRONTIER_ZIG}")
    message(FATAL_ERROR "Install Zig cross-toolchain first: python3 -m pip install ziglang==0.13.0")
endif()

set(CMAKE_C_COMPILER "${FRONTIER_ZIG}")
set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_C_FLAGS_INIT "-target x86_64-windows-gnu -static")
set(CMAKE_CXX_COMPILER "${FRONTIER_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-windows-gnu -static")

set(FRONTIER_ZIG_TOOLS "${CMAKE_CURRENT_BINARY_DIR}/zig-tools")
file(MAKE_DIRECTORY "${FRONTIER_ZIG_TOOLS}")
foreach(TOOL ar ranlib)
    file(WRITE "${FRONTIER_ZIG_TOOLS}/${TOOL}" "#!/bin/sh\nexec '${FRONTIER_ZIG}' ${TOOL} \"$@\"\n")
    file(CHMOD "${FRONTIER_ZIG_TOOLS}/${TOOL}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()

set(CMAKE_AR "${FRONTIER_ZIG_TOOLS}/ar" CACHE FILEPATH "Zig static library bundler" FORCE)
set(CMAKE_RANLIB "${FRONTIER_ZIG_TOOLS}/ranlib" CACHE FILEPATH "Zig static library symbol indexer" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_HAVE_LIBC_PTHREAD FALSE CACHE INTERNAL "Windows uses Win32 threads" FORCE)
set(CMAKE_HAVE_PTHREADS_CREATE FALSE CACHE INTERNAL "No pthreads library on target" FORCE)
set(CMAKE_HAVE_PTHREAD_CREATE FALSE CACHE INTERNAL "No pthread library on target" FORCE)
set(THREADS_HAVE_PTHREAD_ARG FALSE CACHE INTERNAL "No POSIX thread flags on target" FORCE)
