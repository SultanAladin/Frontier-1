/*====================================================================================================================================
                                                        OPERATIONTRACE.H
====================================================================================================================================*/
// 🧩 Development trace of operations: what began, what it decided, how long it took (FRONTIER_DEVELOPMENT_TRACE builds)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// 📝 Usage (every macro compiles to nothing, arguments unevaluated, unless FRONTIER_DEVELOPMENT_TRACE is defined):
//
//        FRONTIER_TRACE_SCOPE("Blend", "BlendEdges · " + std::to_string(Edges.size()) + " edge(s)");
//        FRONTIER_TRACE("Blend", "corner " + Label + " refused: " + Refusal);
//
//    A scope writes a ▶ line on entry and a ◀ line with its duration on exit, indented by the nesting depth of its
//    thread. Every line is flushed: after a freeze or a crash the last ▶ without its ◀ names the operation that hung.
//    The application opens the file (OpenOperationTrace); without a file the lines still reach the in-memory tail
//    that the Sketcher trace panel shows.

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Starts writing to Path (truncated); an existing file is first renamed to <stem>.previous<extension>. False when the
// file cannot be created (the in-memory tail keeps working).
bool OpenOperationTrace(const std::filesystem::path& Path);
void CloseOperationTrace();

// The file being written (empty when none).
[[nodiscard]] std::filesystem::path OperationTracePath();

// One line: elapsed time since the first line, thread number, category, nesting indent, message.
void WriteOperationTrace(const char* Category, const std::string& Message);

// The newest Count lines (oldest first) and a counter that grows with every line written (the panel scrolls on change).
[[nodiscard]] std::vector<std::string> RecentOperationTrace(size_t Count);
[[nodiscard]] size_t                   OperationTraceRevision();

// Shortest round-trip-free text of a value for trace lines (%.6g).
[[nodiscard]] std::string TraceNumber(double Value);

// True when this build writes the trace (FRONTIER_DEVELOPMENT_TRACE defined for the library).
[[nodiscard]] bool OperationTraceCompiledCondition();

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSES
//------------------------------------------------------------------------------------------------------------------------

class OperationTraceScope
{
public:
    OperationTraceScope(const char* Category, std::string Message);
    ~OperationTraceScope();

    OperationTraceScope(const OperationTraceScope&)            = delete;
    OperationTraceScope& operator=(const OperationTraceScope&) = delete;

private:
    const char*                           Category;                                          // [-]  - Column tag ("Blend", "Document", …)
    std::string                           Message;                                           // [-]  - Operation and its arguments
    std::chrono::steady_clock::time_point Start;                                             // [s]  - Entry time
};

} // namespace Frontier::GeometrySpace::Parametric

//------------------------------------------------------------------------------------------------------------------------
//                                                    MACROS
//------------------------------------------------------------------------------------------------------------------------

#define FRONTIER_TRACE_JOIN_INNER(First, Second) First##Second
#define FRONTIER_TRACE_JOIN(First, Second)       FRONTIER_TRACE_JOIN_INNER(First, Second)

#ifdef FRONTIER_DEVELOPMENT_TRACE
#define FRONTIER_TRACE_SCOPE(Category, Message) \
    const ::Frontier::GeometrySpace::Parametric::OperationTraceScope FRONTIER_TRACE_JOIN(FrontierTraceScope, __LINE__)(Category, Message)
#define FRONTIER_TRACE(Category, Message) ::Frontier::GeometrySpace::Parametric::WriteOperationTrace(Category, Message)
#else
#define FRONTIER_TRACE_SCOPE(Category, Message) static_cast<void>(0)
#define FRONTIER_TRACE(Category, Message)       static_cast<void>(0)
#endif
