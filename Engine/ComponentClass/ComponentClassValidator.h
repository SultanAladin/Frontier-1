/*============================================================================================================================================
                                                        COMPONENTCLASSVALIDATOR.H
============================================================================================================================================*/
// 🧩 Structural and physics validation diagnostics for ComponentClass definitions prior to compilation or world spawning

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DiagnosticSeverity : uint8_t
{
    Warning = 0u, // [-] - Non-blocking advisory issue
    Error   = 1u, // [-] - Blocking compile error
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ClassDiagnosticIssue
{
    DiagnosticSeverity     Severity    = DiagnosticSeverity::Warning; // [-] - Warning or Error
    ComponentInstanceToken TargetToken {};                          // [id] - Affected component instance
    std::string            Summary     {};                          // [-] - Actionable diagnostic sentence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentClassValidator
{
public:
    using AssetExistsPredicate = std::function<bool(std::string_view AssetIdentifier)>;

    [[nodiscard]] static std::vector<ClassDiagnosticIssue> ValidateClass(
        const ComponentClassDefinition& Definition,
        const AssetExistsPredicate&     QueryAssetCompiled = nullptr);

    [[nodiscard]] static bool QueryHasBlockingErrors(const std::vector<ClassDiagnosticIssue>& Issues) noexcept;
};

} // namespace Frontier::ComponentClass
