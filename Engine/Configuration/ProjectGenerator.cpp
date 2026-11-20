/*============================================================================================================================================
                                                             PROJECTGENERATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of ProjectGenerator producing standalone C++20 Frontier projects with 3 CMake targets and SessionOutput/

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/ProjectGenerator.h"
#include "Engine/Configuration/ProjectSpaceRepository.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Frontier::Configuration
{

namespace
{

[[nodiscard]] bool WriteTextArtifact(
    const std::filesystem::path&        TargetPath,
    std::string_view                    Contents,
    std::vector<std::filesystem::path>& OutFiles,
    std::string&                        Diagnostic)
{
    std::error_code DirErr {};
    if (TargetPath.has_parent_path())
    {
        std::filesystem::create_directories(TargetPath.parent_path(), DirErr);
        if (DirErr)
        {
            Diagnostic = "Failed to create directory: " + TargetPath.parent_path().string();
            return false;
        }
    }

    std::ofstream Stream(TargetPath, std::ios::binary | std::ios::trunc);
    if (!Stream.is_open())
    {
        Diagnostic = "Failed to write file: " + TargetPath.string();
        return false;
    }
    Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
    if (!Stream.good())
    {
        Diagnostic = "I/O write failure on: " + TargetPath.string();
        return false;
    }
    OutFiles.push_back(TargetPath);
    return true;
}

[[nodiscard]] std::string NormalizeCMakePath(const std::filesystem::path& RawPath)
{
    std::string Text = RawPath.generic_string();
    return Text;
}

void DeploySharedBinariesIfPresent(
    const std::filesystem::path& SourceBinariesDir,
    const std::filesystem::path& TargetBinariesDir) noexcept
{
    std::error_code Ec {};
    if (SourceBinariesDir.empty() || !std::filesystem::exists(SourceBinariesDir, Ec))
    {
        return;
    }

    std::filesystem::create_directories(TargetBinariesDir, Ec);
    for (const auto& DirEntry : std::filesystem::directory_iterator(SourceBinariesDir, Ec))
    {
        if (Ec)
        {
            break;
        }
        const auto& Src = DirEntry.path();
        const std::string Name = Src.filename().string();
        if (DirEntry.is_regular_file(Ec) && Name.find("Frontier") == 0u)
        {
            std::filesystem::copy_file(
                Src,
                TargetBinariesDir / Src.filename(),
                std::filesystem::copy_options::overwrite_existing,
                Ec);
        }
        else if (DirEntry.is_directory(Ec) && Name == "CompiledShaders")
        {
            std::filesystem::copy(
                Src,
                TargetBinariesDir / "CompiledShaders",
                std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive,
                Ec);
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             PROJECTGENERATOR IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

std::string ProjectGenerator::SanitizePascalIdentifier(std::string_view RawTitle)
{
    std::string Result {};
    Result.reserve(RawTitle.size() + 4u);

    bool CapitalizeNext = true;
    for (char RawCh : RawTitle)
    {
        const auto UCh = static_cast<unsigned char>(RawCh);
        if (std::isalnum(UCh))
        {
            if (Result.empty() && std::isdigit(UCh))
            {
                Result.append("Project");
            }
            Result.push_back(CapitalizeNext ? static_cast<char>(std::toupper(UCh)) : RawCh);
            CapitalizeNext = false;
        }
        else
        {
            CapitalizeNext = true;
        }
    }

    if (Result.empty())
    {
        Result = "FrontierProject";
    }
    return Result;
}

std::string ProjectGenerator::FormatUppercaseMacroPrefix(std::string_view PascalIdentifier)
{
    std::string Out {};
    Out.reserve(PascalIdentifier.size() + 4u);
    for (std::size_t Idx = 0u; Idx < PascalIdentifier.size(); ++Idx)
    {
        const auto UCh = static_cast<unsigned char>(PascalIdentifier[Idx]);
        if (Idx > 0u && std::isupper(UCh) && std::islower(static_cast<unsigned char>(PascalIdentifier[Idx - 1u])))
        {
            Out.push_back('_');
        }
        Out.push_back(static_cast<char>(std::toupper(UCh)));
    }
    if (Out.empty())
    {
        Out = "FRONTIER_PROJECT";
    }
    return Out;
}

ProjectScaffoldOutcome ProjectGenerator::GenerateProject(const ProjectScaffoldSpecification& Specification)
{
    ProjectScaffoldOutcome Outcome {};
    Outcome.PascalIdentifier = SanitizePascalIdentifier(Specification.DisplayName);

    if (Specification.DestinationDirectory.empty())
    {
        Outcome.Diagnostic = "Destination directory is required to scaffold a Frontier project.";
        return Outcome;
    }

    const std::string& Proj = Outcome.PascalIdentifier;
    const std::string  MacroPrefix = FormatUppercaseMacroPrefix(Proj);

    Outcome.ProjectRootDirectory = Specification.DestinationDirectory / Proj;
    Outcome.ProjectSpaceFile     = Outcome.ProjectRootDirectory / (Proj + ProjectSpaceRepository::ContainerSuffix);
    Outcome.CMakeListsFile       = Outcome.ProjectRootDirectory / "CMakeLists.txt";
    Outcome.MainWorldFile        = Outcome.ProjectRootDirectory / "Content" / "Worlds" / "MainWorld.WorldSpace";
    Outcome.MainUvFile           = Outcome.ProjectRootDirectory / "Content" / "UV" / "MainSurface.UVSpace";

    std::error_code DirErr {};
    std::filesystem::create_directories(Outcome.ProjectRootDirectory / "Binaries" / "Win64", DirErr);
    std::filesystem::create_directories(Outcome.ProjectRootDirectory / "SessionOutput", DirErr);
    if (DirErr)
    {
        Outcome.Diagnostic = "Unable to create project root directory: " + Outcome.ProjectRootDirectory.string();
        return Outcome;
    }

    // 1. Write <ProjectName>.projectspace via canonical ProjectSpaceRepository
    ProjectSpaceDocument SpaceDoc {};
    SpaceDoc.ProjectTitle     = Specification.DisplayName.empty() ? Proj : Specification.DisplayName;
    SpaceDoc.Appearance.Theme = Specification.InitialTheme;
    if (!ProjectSpaceRepository::Save(Outcome.ProjectSpaceFile, SpaceDoc, Outcome.Diagnostic))
    {
        return Outcome;
    }
    Outcome.GeneratedFiles.push_back(Outcome.ProjectSpaceFile);

    // 2. Write Engine/<ProjectName>.h
    {
        std::ostringstream Header {};
        Header << "// Generated Frontier Project Umbrella Header (" << Proj << ")\n"
               << "#pragma once\n\n"
               << "#include \"Engine/Lifecycle/" << Proj << "Application.h\"\n"
               << "#include \"Engine/SimulationSpace/" << Proj << "WorldSetup.h\"\n"
               << "#if !" << MacroPrefix << "_SHIPPING_BUILD\n"
               << "#include \"Engine/UserInterface/" << Proj << "ViewportOverlay.h\"\n"
               << "#endif\n";
        if (!WriteTextArtifact(Outcome.ProjectRootDirectory / "Engine" / (Proj + ".h"), Header.str(), Outcome.GeneratedFiles, Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 3. Write Engine/SimulationSpace/<ProjectName>WorldSetup.{h,cpp}
    {
        std::ostringstream SimH {};
        SimH << "#pragma once\n\n"
             << "namespace " << Proj << "::SimulationSpace\n{\n\n"
             << "struct WorldSimulationSession\n{\n"
             << "    double ElapsedSeconds = 0.0;\n"
             << "    void AdvanceStep(double DeltaSeconds) noexcept;\n"
             << "};\n\n"
             << "} // namespace " << Proj << "::SimulationSpace\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "SimulationSpace" / (Proj + "WorldSetup.h"),
                SimH.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }

        std::ostringstream SimCpp {};
        SimCpp << "#include \"Engine/SimulationSpace/" << Proj << "WorldSetup.h\"\n\n"
               << "namespace " << Proj << "::SimulationSpace\n{\n\n"
               << "void WorldSimulationSession::AdvanceStep(double DeltaSeconds) noexcept\n{\n"
               << "    ElapsedSeconds += DeltaSeconds;\n"
               << "}\n\n"
               << "} // namespace " << Proj << "::SimulationSpace\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "SimulationSpace" / (Proj + "WorldSetup.cpp"),
                SimCpp.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 4. Write Engine/UserInterface/<ProjectName>ViewportOverlay.{h,cpp}
    {
        std::ostringstream UiH {};
        UiH << "#pragma once\n\n"
            << "namespace " << Proj << "::UserInterface\n{\n\n"
            << "struct WorkspaceOverlayHost\n{\n"
            << "    double LastOverlayDeltaSeconds = 0.0;\n"
            << "    void TickOverlay(double DeltaSeconds) noexcept;\n"
            << "};\n\n"
            << "} // namespace " << Proj << "::UserInterface\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "UserInterface" / (Proj + "ViewportOverlay.h"),
                UiH.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }

        std::ostringstream UiCpp {};
        UiCpp << "#include \"Engine/UserInterface/" << Proj << "ViewportOverlay.h\"\n\n"
              << "namespace " << Proj << "::UserInterface\n{\n\n"
              << "void WorkspaceOverlayHost::TickOverlay(double DeltaSeconds) noexcept\n{\n"
              << "    LastOverlayDeltaSeconds = DeltaSeconds;\n"
              << "}\n\n"
              << "} // namespace " << Proj << "::UserInterface\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "UserInterface" / (Proj + "ViewportOverlay.cpp"),
                UiCpp.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 5. Write Engine/Lifecycle/<ProjectName>Application.{h,cpp} (with main() for all 3 targets)
    {
        std::ostringstream AppH {};
        AppH << "#pragma once\n\n"
             << "#include <cstdint>\n\n"
             << "namespace " << Proj << "::Lifecycle\n{\n\n"
             << "[[nodiscard]] int RunProjectApplication(int ArgumentCount, char** ArgumentVector);\n\n"
             << "} // namespace " << Proj << "::Lifecycle\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "Lifecycle" / (Proj + "Application.h"),
                AppH.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }

        std::ostringstream AppCpp {};
        AppCpp << "#include \"Engine/" << Proj << ".h\"\n"
               << "#include \"Engine/Lifecycle/ProjectExecutionRunner.h\"\n\n"
               << "#include <cstdlib>\n"
               << "#include <cstring>\n\n"
               << "namespace " << Proj << "::Lifecycle\n{\n\n"
               << "namespace\n{\n\n"
               << "struct ProjectRuntimeSession\n{\n"
               << "    SimulationSpace::WorldSimulationSession Simulation {};\n"
               << "#if !" << MacroPrefix << "_SHIPPING_BUILD\n"
               << "    UserInterface::WorkspaceOverlayHost Overlay {};\n"
               << "#endif\n"
               << "};\n\n"
               << "void DispatchSimulationStep(double DeltaSeconds, void* Payload)\n{\n"
               << "    if (auto* Session = static_cast<ProjectRuntimeSession*>(Payload))\n"
               << "    {\n"
               << "        Session->Simulation.AdvanceStep(DeltaSeconds);\n"
               << "    }\n"
               << "}\n\n"
               << "#if !" << MacroPrefix << "_SHIPPING_BUILD\n"
               << "void DispatchOverlayStep(double DeltaSeconds, void* Payload)\n{\n"
               << "    if (auto* Session = static_cast<ProjectRuntimeSession*>(Payload))\n"
               << "    {\n"
               << "        Session->Overlay.TickOverlay(DeltaSeconds);\n"
               << "    }\n"
               << "}\n"
               << "#endif\n\n"
               << "} // namespace\n\n"
               << "int RunProjectApplication(int ArgumentCount, char** ArgumentVector)\n{\n"
               << "    ProjectRuntimeSession Session {};\n"
               << "    FrontierProjectLaunchDescriptor Launch {};\n"
               << "    Launch.StructureVersion = 1u;\n"
               << "    Launch.ProjectTitle     = \"" << Proj << "\";\n"
               << "    Launch.ProjectSpacePath = \"" << Proj << ".projectspace\";\n"
               << "    Launch.StartupWorldPath = \"Content/Worlds/MainWorld.WorldSpace\";\n"
               << "    Launch.StartupUvPath    = \"Content/UV/MainSurface.UVSpace\";\n"
               << "    Launch.NotifySimulationStep = &DispatchSimulationStep;\n"
               << "    Launch.UserPayload          = &Session;\n\n"
               << "    for (int Idx = 1; Idx < ArgumentCount; ++Idx)\n"
               << "    {\n"
               << "        if (ArgumentVector[Idx] != nullptr && std::strcmp(ArgumentVector[Idx], \"--verify-ticks\") == 0)\n"
               << "        {\n"
               << "            Launch.BoundedTickCount = 4u;\n"
               << "        }\n"
               << "        else if (ArgumentVector[Idx] != nullptr && std::strcmp(ArgumentVector[Idx], \"--uv\") == 0)\n"
               << "        {\n"
               << "            Launch.PreferUvWorkspace = 1u;\n"
               << "        }\n"
               << "    }\n\n"
               << "#if " << MacroPrefix << "_SHIPPING_BUILD\n"
               << "    return FrontierRunShippingProjectLoop(&Launch);\n"
               << "#elif " << MacroPrefix << "_EDITOR_BUILD\n"
               << "    Launch.NotifyUserInterfaceStep = &DispatchOverlayStep;\n"
               << "    return FrontierRunEditorProjectLoop(&Launch);\n"
               << "#else\n"
               << "    Launch.NotifyUserInterfaceStep = &DispatchOverlayStep;\n"
               << "    return FrontierRunDevelopmentProjectLoop(&Launch);\n"
               << "#endif\n"
               << "}\n\n"
               << "} // namespace " << Proj << "::Lifecycle\n\n"
               << "int main(int argc, char** argv)\n{\n"
               << "    return " << Proj << "::Lifecycle::RunProjectApplication(argc, argv);\n"
               << "}\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Engine" / "Lifecycle" / (Proj + "Application.cpp"),
                AppCpp.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 6. Write Content/Worlds/MainWorld.WorldSpace, Content/UV/MainSurface.UVSpace, and Shaders/MaterialSpace/<ProjectName>Surface.slang
    {
        std::ostringstream WorldDoc {};
        WorldDoc << "# Frontier .WorldSpace Container\n"
                 << "Project = \"" << Proj << "\"\n"
                 << "ScenePreset = \"CornellStudioGallery\"\n"
                 << "RenderTier = \"Tier1GtxCalibrated\"\n"
                 << "UpAxis = \"+Z\"\n";
        if (!WriteTextArtifact(Outcome.MainWorldFile, WorldDoc.str(), Outcome.GeneratedFiles, Outcome.Diagnostic))
        {
            return Outcome;
        }

        std::ostringstream UvDoc {};
        UvDoc << "# Frontier .UVSpace Container\n"
              << "Project = \"" << Proj << "\"\n"
              << "DefaultUdim = 1001\n"
              << "TexelResolution = 2048\n";
        if (!WriteTextArtifact(Outcome.MainUvFile, UvDoc.str(), Outcome.GeneratedFiles, Outcome.Diagnostic))
        {
            return Outcome;
        }

        std::ostringstream SlangDoc {};
        SlangDoc << "// " << Proj << " OpenPBR Surface Shader (.slang)\n"
                 << "struct " << Proj << "SurfaceSample\n{\n"
                 << "    float3 BaseColor;\n"
                 << "    float  SpecularRoughness;\n"
                 << "    float  Metalness;\n"
                 << "};\n\n"
                 << Proj << "SurfaceSample EvaluateDefault" << Proj << "Surface()\n{\n"
                 << "    " << Proj << "SurfaceSample Out;\n"
                 << "    Out.BaseColor         = float3(0.82, 0.80, 0.76);\n"
                 << "    Out.SpecularRoughness = 0.32;\n"
                 << "    Out.Metalness         = 0.0;\n"
                 << "    return Out;\n"
                 << "}\n";
        if (!WriteTextArtifact(
                Outcome.ProjectRootDirectory / "Shaders" / "MaterialSpace" / (Proj + "Surface.slang"),
                SlangDoc.str(),
                Outcome.GeneratedFiles,
                Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 7. Write .gitignore (ignoring build/ and SessionOutput/, never Saved/)
    {
        const std::string GitIgnore =
            "build/\n"
            "build-*/\n"
            "SessionOutput/\n"
            "*.user\n";
        if (!WriteTextArtifact(Outcome.ProjectRootDirectory / ".gitignore", GitIgnore, Outcome.GeneratedFiles, Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    // 8. Write standalone 3-target CMakeLists.txt (<ProjectName>, <ProjectName>Editor, <ProjectName>Shipping)
    {
        const std::string DefaultEngineRoot = Specification.FrontierRootPath.empty()
                                                  ? "${CMAKE_CURRENT_SOURCE_DIR}/.."
                                                  : NormalizeCMakePath(Specification.FrontierRootPath);
        const std::string DefaultBinDir     = Specification.FrontierBinariesPath.empty()
                                                  ? "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/Win64"
                                                  : NormalizeCMakePath(Specification.FrontierBinariesPath);

        std::ostringstream CMake {};
        CMake << "cmake_minimum_required(VERSION 3.24)\n"
              << "project(" << Proj << " VERSION 0.1.0 LANGUAGES CXX)\n\n"
              << "set(CMAKE_CXX_STANDARD 20)\n"
              << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
              << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
              << "set(FRONTIER_ENGINE_ROOT \"" << DefaultEngineRoot << "\" CACHE PATH \"Path to Frontier Engine headers\")\n"
              << "set(FRONTIER_BINARIES_DIR \"" << DefaultBinDir << "\" CACHE PATH \"Path to Frontier shared binaries (Binaries/Win64)\")\n\n"
              << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/Binaries/Win64\")\n\n"
              << "find_library(FRONTIER_ENGINE_SHARED_LIB NAMES FrontierEngine PATHS \"${FRONTIER_BINARIES_DIR}\" NO_DEFAULT_PATH REQUIRED)\n"
              << "find_library(FRONTIER_WORKSPACE_UI_SHARED_LIB NAMES FrontierWorkspaceUi PATHS \"${FRONTIER_BINARIES_DIR}\" NO_DEFAULT_PATH REQUIRED)\n\n"
              << "set(" << MacroPrefix << "_CORE_SOURCES\n"
              << "    Engine/Lifecycle/" << Proj << "Application.cpp\n"
              << "    Engine/SimulationSpace/" << Proj << "WorldSetup.cpp\n"
              << ")\n\n"
              << "set(" << MacroPrefix << "_UI_SOURCES\n"
              << "    Engine/UserInterface/" << Proj << "ViewportOverlay.cpp\n"
              << ")\n\n"
              << "# 1. Development Target (" << Proj << ")\n"
              << "add_executable(" << Proj << " ${" << MacroPrefix << "_CORE_SOURCES} ${" << MacroPrefix << "_UI_SOURCES})\n"
              << "target_include_directories(" << Proj << " PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\" \"${FRONTIER_ENGINE_ROOT}\")\n"
              << "target_compile_definitions(" << Proj << " PRIVATE " << MacroPrefix << "_DEVELOPMENT_BUILD=1 FRONTIER_CONSUME_ENGINE_DLL=1 FRONTIER_CONSUME_WORKSPACE_UI_DLL=1)\n"
              << "target_link_libraries(" << Proj << " PRIVATE \"${FRONTIER_ENGINE_SHARED_LIB}\" \"${FRONTIER_WORKSPACE_UI_SHARED_LIB}\")\n\n"
              << "# 2. Editor Target (" << Proj << "Editor)\n"
              << "add_executable(" << Proj << "Editor ${" << MacroPrefix << "_CORE_SOURCES} ${" << MacroPrefix << "_UI_SOURCES})\n"
              << "target_include_directories(" << Proj << "Editor PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\" \"${FRONTIER_ENGINE_ROOT}\")\n"
              << "target_compile_definitions(" << Proj << "Editor PRIVATE " << MacroPrefix << "_EDITOR_BUILD=1 FRONTIER_CONSUME_ENGINE_DLL=1 FRONTIER_CONSUME_WORKSPACE_UI_DLL=1)\n"
              << "target_link_libraries(" << Proj << "Editor PRIVATE \"${FRONTIER_ENGINE_SHARED_LIB}\" \"${FRONTIER_WORKSPACE_UI_SHARED_LIB}\")\n\n"
              << "# 3. Shipping Target (" << Proj << "Shipping — zero Editor / Workspace UI link dependencies)\n"
              << "add_executable(" << Proj << "Shipping ${" << MacroPrefix << "_CORE_SOURCES})\n"
              << "target_include_directories(" << Proj << "Shipping PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\" \"${FRONTIER_ENGINE_ROOT}\")\n"
              << "target_compile_definitions(" << Proj << "Shipping PRIVATE " << MacroPrefix << "_SHIPPING_BUILD=1 FRONTIER_CONSUME_ENGINE_DLL=1)\n"
              << "target_link_libraries(" << Proj << "Shipping PRIVATE \"${FRONTIER_ENGINE_SHARED_LIB}\")\n\n"
              << "if(UNIX AND NOT APPLE)\n"
              << "    foreach(TGT " << Proj << " " << Proj << "Editor " << Proj << "Shipping)\n"
              << "        set_target_properties(${TGT} PROPERTIES BUILD_RPATH \"${FRONTIER_BINARIES_DIR}\")\n"
              << "    endforeach()\n"
              << "endif()\n";
        if (!WriteTextArtifact(Outcome.CMakeListsFile, CMake.str(), Outcome.GeneratedFiles, Outcome.Diagnostic))
        {
            return Outcome;
        }
    }

    DeploySharedBinariesIfPresent(Specification.FrontierBinariesPath, Outcome.ProjectRootDirectory / "Binaries" / "Win64");

    Outcome.Succeeded  = true;
    Outcome.Diagnostic = "Scaffolded " + Proj + " (" + std::to_string(Outcome.GeneratedFiles.size()) + " files)";
    return Outcome;
}

} // namespace Frontier::Configuration
