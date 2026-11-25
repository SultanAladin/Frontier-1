/*============================================================================================================================================
                                                      DEFAULTCONTENTGENERATORMAIN.CPP
============================================================================================================================================*/
// 🧩 Standalone FrontierDefaultContentGenerator executable populating & verifying Frontier's zero-abbreviation Content/ .Space repository

#include "Engine/Tools/DefaultContentGenerator.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int ArgumentCount, char** ArgumentValues)
{
    std::filesystem::path RepositoryRoot = FRONTIER_REPO_ROOT_DIR;
    for (int Idx = 1; Idx < ArgumentCount; ++Idx)
    {
        const std::string_view Arg(ArgumentValues[Idx]);
        if (Arg.starts_with("--repo-root="))
        {
            RepositoryRoot = std::filesystem::path(Arg.substr(12u));
        }
    }

    Frontier::Tools::GeneratedContentReport Report{};
    std::string                             Diagnostic{};
    if (!Frontier::Tools::DefaultContentGenerator::GenerateRepositoryContent(RepositoryRoot, Report, Diagnostic))
    {
        std::fprintf(stderr, "[FrontierDefaultContentGenerator] Generation failed: %s\n", Diagnostic.c_str());
        return 1;
    }

    uint32_t VerifiedCount = 0u;
    if (!Frontier::Tools::DefaultContentGenerator::VerifyRepositoryContent(RepositoryRoot, VerifiedCount, Diagnostic))
    {
        std::fprintf(stderr, "[FrontierDefaultContentGenerator] Verification failed: %s\n", Diagnostic.c_str());
        return 1;
    }

    std::printf(
        "[FrontierDefaultContentGenerator] Generated & verified %u .Space containers (%zu bytes) under %s/Content.\n",
        VerifiedCount,
        Report.TotalPayloadBytes,
        RepositoryRoot.generic_string().c_str());
    return 0;
}
