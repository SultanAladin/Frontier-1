/*====================================================================================================================================
                                                          SOLIDARCOPTIONS.H
====================================================================================================================================*/
// 🧩 Sketcher options (SketcherWorkspace.toml): window, viewport, interface, files, Vulkan driver

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <string>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every field has a default, so a missing file or key is never an error; unknown keys are ignored.
struct SketcherOptions
{
    int         WindowWidth         = 1600;                                                  // [px]  - Initial window / capture width
    int         WindowHeight        = 900;                                                   // [px]  - Initial window / capture height
    float       EdgeStroke          = 1.5f;                                                  // [px]  - B-rep edge stroke
    bool        GridEnabled         = true;                                                  // [-]   - Ground grid
    bool        OrthographicEnabled = false;                                                 // [-]   - Start in parallel projection
    double      FieldOfView         = 0.7;                                                   // [rad] - Vertical perspective angle
    std::string InitialView         = {};                                                    // [-]   - Optional startup preset (Top/Bottom/Front/Back/Right/Left/Isometric)
    std::string InitialLayout       = {};                                                    // [-]   - Optional Single/Quad startup layout
    bool        SurfaceTraceEnabled = false;
    bool        FullXYZEnabled      = false;
    bool        SourceEdgesEnabled  = true;
    bool        FitImagesEnabled    = false;                                                 // [-]   - Frame visible reference planes at startup
    std::string FontPath            = {};                                                    // [-]   - UI font (empty: DM Sans from EngineContent)
    float       FontSize            = 15.0f;                                                 // [px]  - UI font size
    std::string RecoveryPath        = "_ClaudeScratch/autosave/SketcherWorkspace.solidarc"; // [-] - Autosave target
    double      AutosaveDelay       = 0.6;                                                   // [s]   - Quiet time before an autosave
    std::string DocumentPath        = "Bracket.solidarc";                               // [-]   - Default Save / Open path
    std::string CurvesPath          = "Experimental/VehicleSurfacing/Samples/SyntheticBonnet.toml"; // [-] - Default Import curves path
    std::string InsertFolder        = "Inserts";                                             // [-]   - Knife insert library (*.step)
    std::string DriverPath          = {};                                                    // [-]   - Vulkan ICD loaded directly (empty: loader)
    bool        ValidationEnabled   = false;                                                 // [-]   - Khronos validation layer when present
    std::string Refusal             = {};                                                    // [-]   - Parse failure of the file (defaults kept)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SketcherOptions LoadSketcherOptions(const std::string& Path);
[[nodiscard]] std::string     EncodeSketcherOptions(const SketcherOptions& Options);

} // namespace Frontier::Workspaces::Sketcher
