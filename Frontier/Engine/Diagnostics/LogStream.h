/*==========================================================================================================================================
                                                           PUBLIC LOG WRITING API
                                     ------------------------------------------------------------------
                              Engine and user code write into one ring store that the Log Stream panel reads.
==========================================================================================================================================*/

// 🧩 Public log writing API: engine and user code write into one ring store that the Log Stream panel reads.

#pragma once

#include "BuildConfiguration.h"
#include "LogCategories.h"
#include "LogRingStore.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] LogRingStore& LogStore();   // Process-wide store (LogRingStore::Capacity entries)

// Appends every subsequent entry to a session file as well as the ring. Empty path disables the file sink.
// Present in every profile; Shipping keeps the file sink while the panel and console compile out.
void ConfigureLogFile(const std::filesystem::path& Path);

// Core writers. Category is a registry token (see RegisterLogCategory / LogCategories().QueryByName).
void WriteLog(uint32_t Category, LogSeverity Severity, std::string_view Message, std::string_view Source = {});
void WriteLog(uint32_t Category, LogSeverity Severity, LogVerbosity Verbosity, std::string_view Message, std::string_view Source = {});

// printf-style formatting straight into the fixed slot buffer. Truncates safely at the slot edge.
void WriteLogFormat(uint32_t Category, LogSeverity Severity, LogVerbosity Verbosity, std::string_view Source, const char* Format, ...);

// Category-handle conveniences (the shape user code declares once and keeps).
inline void WriteLog(const LogCategory& Category, LogSeverity Severity, std::string_view Message, std::string_view Source = {})
{
    WriteLog(Category.Token, Severity, LogVerbosity::Log, Message, Source);
}

inline void WriteLog(const LogCategory& Category, LogSeverity Severity, LogVerbosity Verbosity,
                     std::string_view Message, std::string_view Source = {})
{
    WriteLog(Category.Token, Severity, Verbosity, Message, Source);
}

// Builds "basename:line" source text in a thread-local buffer (valid until the next call on the same thread).
[[nodiscard]] const char* LogSourceText(const char* File, int Line) noexcept;

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                         MACROS
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Macro arguments are not evaluated when a macro compiles out (Shipping verbose). Never hide side effects in them.
#define FRONTIER_LOG(Category, Severity, Message) \
    ::Frontier::WriteLog((Category), (Severity), ::Frontier::LogVerbosity::Log, (Message), ::Frontier::LogSourceText(__FILE__, __LINE__))

#define FRONTIER_LOG_FORMAT(Category, Severity, Format, ...) \
    ::Frontier::WriteLogFormat(::Frontier::LogCategoryTokenOf(Category), (Severity), ::Frontier::LogVerbosity::Log,    \
                               ::Frontier::LogSourceText(__FILE__, __LINE__), (Format), __VA_ARGS__)

#if FRONTIER_PROFILE_SHIPPING_VALUE
    #define FRONTIER_LOG_VERBOSE(Category, Message) ((void)0)
#else
    #define FRONTIER_LOG_VERBOSE(Category, Message) \
        ::Frontier::WriteLog((Category), ::Frontier::LogSeverity::Info, ::Frontier::LogVerbosity::Verbose,              \
                             (Message), ::Frontier::LogSourceText(__FILE__, __LINE__))
#endif
