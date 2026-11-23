/*==========================================================================================================================================
                                                         DIAGNOSTICJOURNAL.H
==========================================================================================================================================*/
// 🧩 Persist bounded, timestamped application diagnostics without material contents or credentials.

#pragma once
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{
class DiagnosticJournal
{
  public:
    static std::string EncodePath(const std::filesystem::path &Filename) noexcept
    {
        try
        {
            const auto Text = Filename.u8string();
            return std::string(Text.begin(), Text.end());
        }
        catch (...) { return "[unrepresentable diagnostic path]"; }
    }
    static void Initialize(std::string_view Application)
    {
        std::lock_guard<std::mutex> Exclusion(Synchronization);
        try
        {
            std::filesystem::path Directory;
#ifdef _WIN32
            if (const auto *Local = _wgetenv(L"LOCALAPPDATA")) Directory = std::filesystem::path(Local) / "Frontier/Logs";
#else
            if (const auto *Local = std::getenv("XDG_STATE_HOME")) Directory = std::filesystem::path(Local) / "Frontier/Logs";
#endif
            if (Directory.empty()) Directory = std::filesystem::temp_directory_path() / "Frontier/Logs";
            std::filesystem::create_directories(Directory);
            std::vector<std::filesystem::directory_entry> Archives;
            for (const auto &Archive : std::filesystem::directory_iterator(Directory))
                if (Archive.is_regular_file() && Archive.path().filename().string().starts_with(std::string(Application) + "-"))
                    Archives.push_back(Archive);
            std::sort(Archives.begin(), Archives.end(), [](const auto &First, const auto &Second)
            {
                return First.last_write_time() > Second.last_write_time();
            });
            for (size_t Index = 7; Index < Archives.size(); ++Index) std::filesystem::remove(Archives[Index].path());
            const auto Epoch = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            Filename = Directory / (std::string(Application) + "-" + std::to_string(Epoch) + ".log");
            Stream.open(Filename, std::ios::out | std::ios::trunc);
            Origin = std::chrono::steady_clock::now();
            Written = 0;
            if (!Stream) std::cerr << "Diagnostic file could not be opened: " << Filename << '\n';
        }
        catch (const std::exception &Failure)
        {
            std::cerr << "Diagnostic initialization failed: " << Failure.what() << '\n';
        }
    }
    static void Append(std::string_view Category, std::string_view Description) noexcept
    {
        try
        {
            std::lock_guard<std::mutex> Exclusion(Synchronization);
            if (!Stream || Written >= 8 * 1024 * 1024) return;
            const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - Origin).count();
            const std::string Text(Description.substr(0, 8192));
            Stream << Elapsed << "ms [" << Category.substr(0, 64) << "] ";
            for (char Character : Text) Stream << (Character == '\n' || Character == '\r' ? ' ' : Character);
            Stream << '\n';
            Written += Text.size() + Category.size() + 32;
            if (Written >= 8 * 1024 * 1024) Stream << "Diagnostic byte ceiling reached; further entries suppressed.\n";
            Stream.flush();
        }
        catch (...) {}
    }
    static std::filesystem::path QueryFilename()
    {
        std::lock_guard<std::mutex> Exclusion(Synchronization);
        return Filename;
    }

  private:
    inline static std::mutex Synchronization;
    inline static std::ofstream Stream;
    inline static std::filesystem::path Filename;
    inline static std::chrono::steady_clock::time_point Origin;
    inline static size_t Written = 0;
};
}
