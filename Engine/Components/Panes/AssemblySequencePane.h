/*============================================================================================================================================
                                                          ASSEMBLYSEQUENCEPANE.H
============================================================================================================================================*/
// 🧩 Interactive pan/zoom Assembly Sequence canvas visualizing ComponentClass hierarchy & physics construction flow with Bezier wires

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AssemblyBlockPosition
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct AssemblySequenceViewport
{
    Point2D                                     PanOffsetPx   { 40.0f, 52.0f };
    float                                       ZoomScale     = 1.0f;
    bool                                        Panning       = false;
    uint32_t                                    DraggedBlockId = 0u;
    uint64_t                                    SyncedRevision = 0ull;
    std::unordered_map<uint32_t, AssemblyBlockPosition> BlockOffsets {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class AssemblySequencePane
{
public:
    static ComponentClass::ComponentInstanceToken ConstructWithInput(
        PixelSpace&                                     Surface,
        Rectangle2D                                     Bounds,
        const ComponentClass::ComponentClassDefinition& ClassDef,
        ComponentClass::ComponentInstanceToken          SelectedToken,
        AssemblySequenceViewport&                       Viewport,
        const PaneInputReading&                         Input) noexcept;
};

} // namespace Frontier
