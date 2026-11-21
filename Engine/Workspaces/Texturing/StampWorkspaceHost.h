/*============================================================================================================================================
                                                             STAMPWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Step 3 Stamp Studio Workspace host (transparent alpha cutout decal & modifier-stack UI chrome, zero 2D/PBR shader rendering).

#pragma once

#include "Engine/Workspaces/Texturing/PatternStampWorkspaceHost.h"

namespace Frontier::Workspaces
{

class StampWorkspaceHost : public PatternStampWorkspaceHost
{
public:
    StampWorkspaceHost()
        : PatternStampWorkspaceHost(PatternStampWorkspaceMode::Stamp)
    {
    }
};

} // namespace Frontier::Workspaces
