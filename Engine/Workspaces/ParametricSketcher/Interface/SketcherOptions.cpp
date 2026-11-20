/*====================================================================================================================================
                                                         SOLIDARCOPTIONS.CPP
====================================================================================================================================*/
// 🧩 Sketcher options (SketcherWorkspace.toml): window, viewport, interface, files, Vulkan driver

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOptions.h"

#include <toml++/toml.hpp>

#include <filesystem>
#include <sstream>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherOptions LoadSketcherOptions(const std::string& Path)
{
    SketcherOptions Options;
    if (Path.empty() || !std::filesystem::exists(Path)) return Options;
    toml::table Root;
    try
    {
        Root = toml::parse_file(Path);
    }
    catch (const toml::parse_error& Failure)
    {
        Options.Refusal = Path + ": " + std::string(Failure.description());
        return Options;
    }
    Options.WindowWidth         = static_cast<int>(Root["Window"]["Width"].value_or(int64_t{ Options.WindowWidth }));
    Options.WindowHeight        = static_cast<int>(Root["Window"]["Height"].value_or(int64_t{ Options.WindowHeight }));
    Options.EdgeStroke          = static_cast<float>(Root["Viewport"]["EdgeStroke"].value_or(double{ Options.EdgeStroke }));
    Options.GridEnabled         = Root["Viewport"]["Grid"].value_or(Options.GridEnabled);
    Options.OrthographicEnabled = Root["Viewport"]["Orthographic"].value_or(Options.OrthographicEnabled);
    Options.FieldOfView         = Root["Viewport"]["FieldOfView"].value_or(Options.FieldOfView);
    Options.InitialView         = Root["Viewport"]["InitialView"].value_or(Options.InitialView);
    Options.InitialLayout       = Root["Viewport"]["InitialLayout"].value_or(Options.InitialLayout);
    Options.FitImagesEnabled    = Root["Viewport"]["FitImages"].value_or(Options.FitImagesEnabled);
    Options.SurfaceTraceEnabled = Root["Tracing"]["Surface"].value_or(false);
    Options.FullXYZEnabled = Root["Tracing"]["FullXYZ"].value_or(false);
    Options.SourceEdgesEnabled = Root["Tracing"]["SourceEdges"].value_or(true);
    const std::string& View = Options.InitialView;
    if (!View.empty() && View != "Top" && View != "Bottom" && View != "Front" && View != "Back" && View != "Right" && View != "Left" && View != "Isometric")
        Options.Refusal = "Viewport.InitialView must be Top, Bottom, Front, Back, Right, Left or Isometric";
    if (!Options.InitialLayout.empty() && Options.InitialLayout != "Single" && Options.InitialLayout != "Quad")
        Options.Refusal = "Viewport.InitialLayout must be Single or Quad";
    Options.FontPath            = Root["Interface"]["FontPath"].value_or(Options.FontPath);
    Options.FontSize            = static_cast<float>(Root["Interface"]["FontSize"].value_or(double{ Options.FontSize }));
    Options.RecoveryPath        = Root["Files"]["RecoveryPath"].value_or(Options.RecoveryPath);
    Options.AutosaveDelay       = Root["Files"]["AutosaveDelay"].value_or(Options.AutosaveDelay);
    Options.DocumentPath        = Root["Files"]["DocumentPath"].value_or(Options.DocumentPath);
    Options.InsertFolder        = Root["Files"]["InsertFolder"].value_or(Options.InsertFolder);
    Options.CurvesPath          = Root["Files"]["CurvesPath"].value_or(Options.CurvesPath);
    Options.DriverPath          = Root["Vulkan"]["DriverPath"].value_or(Options.DriverPath);
    Options.ValidationEnabled   = Root["Vulkan"]["Validation"].value_or(Options.ValidationEnabled);
    return Options;
}

std::string EncodeSketcherOptions(const SketcherOptions& Options)
{
    const toml::table Root{
        { "Window", toml::table{ { "Width", Options.WindowWidth }, { "Height", Options.WindowHeight } } },
        { "Viewport", toml::table{ { "EdgeStroke", double(Options.EdgeStroke) }, { "Grid", Options.GridEnabled },
                                   { "Orthographic", Options.OrthographicEnabled }, { "FieldOfView", Options.FieldOfView },
                                   { "InitialView", Options.InitialView }, { "InitialLayout", Options.InitialLayout }, { "FitImages", Options.FitImagesEnabled } } },
                      { "Tracing", toml::table{ { "Surface", Options.SurfaceTraceEnabled }, { "FullXYZ", Options.FullXYZEnabled }, { "SourceEdges", Options.SourceEdgesEnabled } } },
        { "Interface", toml::table{ { "FontPath", Options.FontPath }, { "FontSize", double(Options.FontSize) } } },
        { "Files", toml::table{ { "RecoveryPath", Options.RecoveryPath }, { "AutosaveDelay", Options.AutosaveDelay }, { "DocumentPath", Options.DocumentPath }, { "CurvesPath", Options.CurvesPath }, { "InsertFolder", Options.InsertFolder } } },
        { "Vulkan", toml::table{ { "DriverPath", Options.DriverPath }, { "Validation", Options.ValidationEnabled } } },
    };
    std::ostringstream Stream;
    Stream << "# Sketcher options — every key is optional\n" << Root << "\n";
    return Stream.str();
}

} // namespace Frontier::Workspaces::Sketcher
