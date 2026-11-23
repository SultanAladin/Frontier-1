//============================================================================================================================================
//                                                        BOOLEANSOLVER.CPP
//============================================================================================================================================

#include "SlateShape/Operation/BooleanSolver/Api/BooleanSolver.h"

namespace Slate
{

bool BooleanSpecification::Declared() const
{
    if (OperandSet.size() < 2u)
        return false;

    for (const BooleanOperand& Operand : OperandSet)
        if (!Operand.Declared())
            return false;

    return true;
}

BooleanDisposition EvaluateBoolean(const BooleanSpecification& Declared)
{
    if (Declared.OperandSet.empty())
        return BooleanDisposition::NotRequested;
    if (!Declared.Declared())
        return BooleanDisposition::InvalidSpecification;
    return BooleanDisposition::ImplementationAbsent;
}

} // namespace Slate
