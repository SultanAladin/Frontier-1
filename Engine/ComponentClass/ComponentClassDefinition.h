/*============================================================================================================================================
                                                        COMPONENTCLASSDEFINITION.H
============================================================================================================================================*/
// 🧩 Top-level ComponentClass asset encapsulating title, simulation role, revision, baked RGBA thumbnail & ComponentInstanceStorage

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceStorage.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ComponentClassArchetype : uint8_t
{
    StaticMechanicalProp = 0u, // [-] - Static or constrained mechanical prop
    Driveable    = 1u, // [-] - Controlled multi-wheel vehicle class
    DynamicRig   = 2u, // [-] - Articulated physics rig / emitter assembly
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ComponentClassDefinition
{
public:
    std::string              Title                { "UntitledComponentClass" };   // [-]     - Display class title
    ComponentClassArchetype       ClassArchetype            = ComponentClassArchetype::StaticMechanicalProp; // [-] - Simulation role
    uint64_t                 RevisionNumber       = 1ull;                         // [count] - Monotonic edit revision
    uint64_t                 CompiledRevision     = 0ull;                         // [count] - Last compiled revision
    bool                     CompiledDirty        = true;                         // [-]     - True when edits occurred since Compile
    uint32_t                 BakedIconWidth       = 0u;                           // [px]    - Thumbnail width (128 px when baked)
    uint32_t                 BakedIconHeight      = 0u;                           // [px]    - Thumbnail height (128 px when baked)
    std::vector<uint8_t>     BakedIconRgba8       {};                             // [rgba8] - Transparent studio specimen thumbnail
    ComponentInstanceStorage Storage              {};                             // [-]     - Contiguous typed instance arrays

    void TouchRevision() noexcept;
    void FinalizeCompiledRevision() noexcept;

    [[nodiscard]] static const char* QueryArchetypeLabel(ComponentClassArchetype Archetype) noexcept;
};

} // namespace Frontier::ComponentClass
