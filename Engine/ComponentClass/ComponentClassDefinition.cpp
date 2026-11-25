/*============================================================================================================================================
                                                       COMPONENTCLASSDEFINITION.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentClassDefinition revision tracking and role labels

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"

namespace Frontier::ComponentClass
{

void ComponentClassDefinition::TouchRevision() noexcept
{
    ++RevisionNumber;
    CompiledDirty = true;
}

void ComponentClassDefinition::FinalizeCompiledRevision() noexcept
{
    CompiledRevision = RevisionNumber;
    CompiledDirty    = false;
}

const char* ComponentClassDefinition::QueryArchetypeLabel(ComponentClassArchetype Archetype) noexcept
{
    switch (Archetype)
    {
        case ComponentClassArchetype::StaticMechanicalProp: return "AssemblyProp";
        case ComponentClassArchetype::Driveable:    return "Driveable";
        case ComponentClassArchetype::DynamicRig:   return "DynamicRig";
    }
    return "AssemblyProp";
}

} // namespace Frontier::ComponentClass
