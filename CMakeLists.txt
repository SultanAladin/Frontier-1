#============================================================================================================================================
#                                                           CMAKELISTS.TXT
#============================================================================================================================================
# 🧩 Root ISO C++20 build configuration enforcing Editor, Development, and Shipping profiles across Windows, Linux, and Android.

cmake_minimum_required(VERSION 3.21)
project(Frontier LANGUAGES C CXX)

include(CTest)

#------------------------------------------------------------------------------------------------------------------------
#                                                LANGUAGE & PROFILE POLICY
#------------------------------------------------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FRONTIER_PROFILE "Development" CACHE STRING "Active Frontier build profile (Editor, Development, Shipping)")
set_property(CACHE FRONTIER_PROFILE PROPERTY STRINGS Editor Development Shipping)

if(FRONTIER_PROFILE STREQUAL "Editor")
    set(FRONTIER_PROFILE_EDITOR_DEF      1)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Development")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 1)
    set(FRONTIER_PROFILE_SHIPPING_DEF    0)
elseif(FRONTIER_PROFILE STREQUAL "Shipping")
    set(FRONTIER_PROFILE_EDITOR_DEF      0)
    set(FRONTIER_PROFILE_DEVELOPMENT_DEF 0)
    set(FRONTIER_PROFILE_SHIPPING_DEF    1)
else()
    message(FATAL_ERROR "Invalid FRONTIER_PROFILE='${FRONTIER_PROFILE}'. Choose Editor, Development, or Shipping.")
endif()

message(STATUS "Frontier active profile: ${FRONTIER_PROFILE}")

#------------------------------------------------------------------------------------------------------------------------
#                                          PINNED DEPENDENCY & PATCH VERIFICATION
#------------------------------------------------------------------------------------------------------------------------

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Automation/PrepareDependencies.py" --verify
    RESULT_VARIABLE FRONTIER_DEP_STATUS
)
if(NOT FRONTIER_DEP_STATUS EQUAL 0)
    message(FATAL_ERROR "Dependencies or ImGui patches unverified. Run: python3 Automation/PrepareDependencies.py")
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                             COMPILER WARNING & PROFILE FLAGS
#------------------------------------------------------------------------------------------------------------------------

add_library(FrontierCompileSettings INTERFACE)
target_compile_features(FrontierCompileSettings INTERFACE cxx_std_20)
target_compile_definitions(FrontierCompileSettings INTERFACE
    FRONTIER_PROFILE_EDITOR=${FRONTIER_PROFILE_EDITOR_DEF}
    FRONTIER_PROFILE_DEVELOPMENT=${FRONTIER_PROFILE_DEVELOPMENT_DEF}
    FRONTIER_PROFILE_SHIPPING=${FRONTIER_PROFILE_SHIPPING_DEF}
)

if(MSVC)
    target_compile_options(FrontierCompileSettings INTERFACE /utf-8 /W4 /permissive-)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE /O2 /DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE /Zi)
    endif()
else()
    target_compile_options(FrontierCompileSettings INTERFACE -Wall -Wextra -Wpedantic)
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        target_compile_options(FrontierCompileSettings INTERFACE -O3 -DNDEBUG)
    else()
        target_compile_options(FrontierCompileSettings INTERFACE -O1 -g)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                     PATCHED IMGUI (EXCLUDED FROM SHIPPING RUNTIME)
#------------------------------------------------------------------------------------------------------------------------

if(NOT FRONTIER_PROFILE STREQUAL "Shipping" OR BUILD_TESTING)
    set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/ExternalPackages/imgui")
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        add_library(FrontierImGui STATIC EXCLUDE_FROM_ALL
            "${IMGUI_ROOT}/imgui.cpp"
            "${IMGUI_ROOT}/imgui_draw.cpp"
            "${IMGUI_ROOT}/imgui_tables.cpp"
            "${IMGUI_ROOT}/imgui_widgets.cpp"
        )
    else()
        add_library(FrontierImGui STATIC
            "${IMGUI_ROOT}/imgui.cpp"
            "${IMGUI_ROOT}/imgui_draw.cpp"
            "${IMGUI_ROOT}/imgui_tables.cpp"
            "${IMGUI_ROOT}/imgui_widgets.cpp"
        )
    endif()
    target_include_directories(FrontierImGui PUBLIC "${IMGUI_ROOT}")
    target_compile_definitions(FrontierImGui PUBLIC "ImDrawIdx=unsigned int")
    if(NOT MSVC)
        target_compile_options(FrontierImGui PRIVATE -w)
    endif()
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                         DESKTOP GLFW 3.4 BACKEND (NON-ANDROID)
#------------------------------------------------------------------------------------------------------------------------

if(NOT ANDROID)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    if(UNIX AND NOT APPLE)
        include(CheckIncludeFile)
        check_include_file("X11/Xlib.h" FRONTIER_HAS_X11_HEADERS)
        check_include_file("wayland-client.h" FRONTIER_HAS_WAYLAND_HEADERS)
        find_program(FRONTIER_WAYLAND_SCANNER NAMES wayland-scanner)

        if(FRONTIER_HAS_X11_HEADERS)
            set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        endif()

        if(FRONTIER_HAS_WAYLAND_HEADERS AND FRONTIER_WAYLAND_SCANNER)
            set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
    endif()

    add_subdirectory(ExternalPackages/glfw EXCLUDE_FROM_ALL)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                               ENGINE RUNTIME LIBRARY
#------------------------------------------------------------------------------------------------------------------------

set(FRONTIER_UI_COMPONENT_SOURCES
    Engine/Components/PixelSpace.cpp
    Engine/Components/VectorIcons/VectorIconCatalog.cpp
    Engine/Components/Appearance/ThemeCatalog.cpp
    Engine/Components/Appearance/TypefaceCatalog.cpp
    Engine/Components/Motion/SpringIntegrator.cpp
    Engine/Components/Notch/PanelExpansionTab.cpp
    Engine/Components/Controls/ControlStyle.cpp
    Engine/Components/Controls/ScalarSlider.cpp
    Engine/Components/Controls/BooleanSwitch.cpp
    Engine/Components/Controls/CapsuleButton.cpp
    Engine/Components/Controls/ThemePreviewTile.cpp
    Engine/Components/Controls/DropdownSelector.cpp
    Engine/Components/Controls/TextInputField.cpp
    Engine/Components/Controls/ConfirmationDialogue.cpp
    Engine/Components/Controls/NotificationSequence.cpp
    Engine/Components/Docking/WorkspaceTab.cpp
    Engine/Components/Docking/WorkspaceDockContainer.cpp
    Engine/Components/ControlCentre/ControlCentrePanel.cpp
    Engine/Configuration/InputSettings.cpp
    Engine/Configuration/ProjectSpaceArchive.cpp
)

set(FRONTIER_ENGINE_SOURCES
    Engine/Input/InputExchange.cpp
    Engine/Input/InputExtension.cpp
    Engine/Windowing/PlatformWindowStub.cpp
)
if(NOT ANDROID)
    list(APPEND FRONTIER_ENGINE_SOURCES Engine/Windowing/PlatformWindowDesktop.cpp)
endif()
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    list(APPEND FRONTIER_ENGINE_SOURCES ${FRONTIER_UI_COMPONENT_SOURCES})
endif()

add_library(FrontierEngine STATIC ${FRONTIER_ENGINE_SOURCES})

target_include_directories(FrontierEngine PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/vulkan-headers/include"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/volk"
    "${CMAKE_SOURCE_DIR}/ExternalPackages/tomlpp/include"
)
target_link_libraries(FrontierEngine PUBLIC FrontierCompileSettings)
if(NOT FRONTIER_PROFILE STREQUAL "Shipping")
    target_link_libraries(FrontierEngine PUBLIC FrontierImGui)
endif()
if(NOT ANDROID)
    target_include_directories(FrontierEngine PUBLIC "${CMAKE_SOURCE_DIR}/ExternalPackages/glfw/include")
    target_link_libraries(FrontierEngine PRIVATE glfw)
endif()

#------------------------------------------------------------------------------------------------------------------------
#                                         VALIDATION EXHIBITS (CTEST TARGETS)
#------------------------------------------------------------------------------------------------------------------------

if(BUILD_TESTING)
    function(frontier_register_exhibit EXHIBIT_NAME EXHIBIT_SOURCE)
        add_executable(${EXHIBIT_NAME} ${EXHIBIT_SOURCE} ${ARGN})
        target_include_directories(${EXHIBIT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/ExternalPackages/stb")
        target_compile_definitions(${EXHIBIT_NAME} PRIVATE "FRONTIER_REPO_ROOT_DIR=\"${CMAKE_SOURCE_DIR}\"")
        target_link_libraries(${EXHIBIT_NAME} PRIVATE FrontierEngine FrontierImGui)
        add_test(NAME ${EXHIBIT_NAME} COMMAND ${EXHIBIT_NAME})
    endfunction()

    set(FRONTIER_SHIPPING_UI_SOURCES "")
    if(FRONTIER_PROFILE STREQUAL "Shipping")
        set(FRONTIER_SHIPPING_UI_SOURCES ${FRONTIER_UI_COMPONENT_SOURCES})
    endif()

    frontier_register_exhibit(InputValidation         Exhibits/InputValidation.cpp         ${FRONTIER_SHIPPING_UI_SOURCES})
    frontier_register_exhibit(WindowValidation        Exhibits/WindowValidation.cpp        ${FRONTIER_SHIPPING_UI_SOURCES})
    frontier_register_exhibit(DockingValidation       Exhibits/DockingValidation.cpp       ${FRONTIER_SHIPPING_UI_SOURCES})
    frontier_register_exhibit(ControlCentreValidation Exhibits/ControlCentreValidation.cpp ${FRONTIER_SHIPPING_UI_SOURCES})
endif()
