/*==========================================================================================================================================
                                                                 LOG STREAM
                                     ------------------------------------------------------------------
                       Every write lands in the ring store first; the optional file sink mirrors it line by line for
                                                             retained sessions.
==========================================================================================================================================*/

// 🧩 Every write lands in the ring store first; the optional file sink mirrors it line by line for retained sessions.

#include "LogStream.h"

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::mutex   FileMutex;
std::ofstream FileSink;

// Formats epoch microseconds as UTC HH:MM:SS.mmm by arithmetic only (no locale, no thread-unsafe time calls).
void FormatClock(uint64_t WallMicros, char* Out, size_t OutSize) noexcept
{
    const uint64_t Millis  = (WallMicros / 1000) % 1000;
    const uint64_t Seconds = (WallMicros / 1000000) % 60;
    const uint64_t Minutes = (WallMicros / 60000000) % 60;
    const uint64_t Hours   = (WallMicros / 3600000000) % 24;
    std::snprintf(Out, OutSize, "%02llu:%02llu:%02llu.%03llu",
                  static_cast<unsigned long long>(Hours),
                  static_cast<unsigned long long>(Minutes),
                  static_cast<unsigned long long>(Seconds),
                  static_cast<unsigned long long>(Millis));
}

void AppendToFile(uint32_t Category, LogSeverity Severity, uint64_t WallMicros,
                  std::string_view Message, std::string_view Source) noexcept
{
    std::lock_guard<std::mutex> Lock(FileMutex);
    if (!FileSink.is_open())
    {
        return;
    }

    char Clock[16] = {};
    FormatClock(WallMicros, Clock, sizeof(Clock));

    const LogCategory* Resolved = LogCategories().Query(Category);
    const std::string  Name     = Resolved != nullptr ? Resolved->Name : std::string("Unknown");

    FileSink << '[' << Clock << "] [" << Name << "] [" << LogSeverityText(Severity) << "] " << Message;
    if (!Source.empty())
    {
        FileSink << " (" << Source << ')';
    }
    FileSink << '\n';
    FileSink.flush();
}

uint64_t EmitLogEntry(uint32_t Category, LogSeverity Severity, LogVerbosity Verbosity,
                      std::string_view Message, std::string_view Source) noexcept
{
    // 📝 Stamp once so the ring entry and the file line can never disagree about when this happened.
    const auto     Now    = std::chrono::system_clock::now().time_since_epoch();
    const uint64_t Micros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Now).count());
    const uint64_t Sequence = LogStore().WriteAt(Category, Severity, Verbosity, Message, Source, Micros);
    AppendToFile(Category, Severity, Micros, Message, Source);
    return Sequence;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LogRingStore& LogStore()
{
    static LogRingStore Store;
    return Store;
}

void ConfigureLogFile(const std::filesystem::path& Path)
{
    std::lock_guard<std::mutex> Lock(FileMutex);
    if (FileSink.is_open())
    {
        FileSink.close();
    }
    if (!Path.empty())
    {
        std::error_code Error;
        if (Path.has_parent_path())
        {
            std::filesystem::create_directories(Path.parent_path(), Error);
        }
        FileSink.open(Path, std::ios::app);
    }
}

void WriteLog(uint32_t Category, LogSeverity Severity, std::string_view Message, std::string_view Source)
{
    WriteLog(Category, Severity, LogVerbosity::Log, Message, Source);
}

void WriteLog(uint32_t Category, LogSeverity Severity, LogVerbosity Verbosity, std::string_view Message, std::string_view Source)
{
    EmitLogEntry(Category, Severity, Verbosity, Message, Source);
}

void WriteLogFormat(uint32_t Category, LogSeverity Severity, LogVerbosity Verbosity, std::string_view Source, const char* Format, ...)
{
    std::array<char, 256> Buffer{};

    va_list Arguments;
    va_start(Arguments, Format);
    std::vsnprintf(Buffer.data(), Buffer.size(), Format != nullptr ? Format : "", Arguments);
    va_end(Arguments);

    EmitLogEntry(Category, Severity, Verbosity, std::string_view(Buffer.data()), Source);
}

const char* LogSourceText(const char* File, int Line) noexcept
{
    thread_local std::array<char, 96> Buffer{};

    const char* Base = File != nullptr ? File : "?";
    for (const char* Cursor = Base; *Cursor != '\0'; ++Cursor)
    {
        if (*Cursor == '/' || *Cursor == '\\')
        {
            Base = Cursor + 1;
        }
    }
    std::snprintf(Buffer.data(), Buffer.size(), "%s:%d", Base, Line);
    return Buffer.data();
}

} // namespace Frontier
