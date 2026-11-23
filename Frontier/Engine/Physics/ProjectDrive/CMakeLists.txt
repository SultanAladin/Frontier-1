cmake_minimum_required(VERSION 3.20)
project(FrontierProjectDriveCore LANGUAGES CXX)

# Standalone opt-in library. The editor and the existing HTML-demo VehicleSolver do not automatically link it.
add_library(FrontierProjectDrivePhysics STATIC
    Aerodynamics.cpp Drivetrain.cpp PacejkaMagicFormula.cpp TyreSlipDynamics.cpp
    VehicleGeometry.cpp VehicleSolver.cpp TyreVisualBridge.cpp ../TyreContinuum.cpp)
target_compile_features(FrontierProjectDrivePhysics PUBLIC cxx_std_17)
target_include_directories(FrontierProjectDrivePhysics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
if(MSVC)
    target_compile_options(FrontierProjectDrivePhysics PRIVATE /utf-8 /W4)
else()
    target_compile_options(FrontierProjectDrivePhysics PRIVATE -Wall -Wextra -Werror)
endif()
