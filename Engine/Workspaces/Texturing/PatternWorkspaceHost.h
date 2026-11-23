/*============================================================================================================================================
                                                            PATTERNWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Step 2 Pattern Studio Workspace host (seamless tileable vector & modifier-stack UI chrome, zero 2D/PBR shader rendering).

#pragma once

#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"

namespace Frontier::Workspaces
{

class PatternWorkspaceHost : public PatternStampWorkspaceHost
{
public:
    PatternWorkspaceHost()
        : PatternStampWorkspaceHost(PatternStampWorkspaceMode::Pattern)
    {
    }
};

} // namespace Frontier::Workspaces
