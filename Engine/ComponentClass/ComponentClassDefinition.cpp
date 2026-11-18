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

const char* ComponentClassDefinition::QueryRoleLabel(ComponentClassRole Role) noexcept
{
    switch (Role)
    {
        case ComponentClassRole::AssemblyProp: return "AssemblyProp";
        case ComponentClassRole::Driveable:    return "Driveable";
        case ComponentClassRole::DynamicRig:   return "DynamicRig";
    }
    return "AssemblyProp";
}

} // namespace Frontier::ComponentClass
