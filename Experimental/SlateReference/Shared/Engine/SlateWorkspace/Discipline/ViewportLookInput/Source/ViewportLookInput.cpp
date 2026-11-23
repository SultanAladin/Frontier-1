//============================================================================================================================================
//                                                    VIEWPORTLOOKINPUT.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ViewportLookInput/Api/ViewportLookInput.h"

namespace Slate
{

bool IsViewportLookNavigationKey(char Character)
{
    switch (Character)
    {
        case 'w':
        case 'W':
        case 'a':
        case 'A':
        case 's':
        case 'S':
        case 'd':
        case 'D':
        case 'e':
        case 'E':
        case 'q':
        case 'Q':
            return true;
    }
    return false;
}

TextInputCondition FilterViewportLookTextInput(const TextInputCondition& Incoming,
                                               bool LookHeld)
{
    if (!LookHeld || Incoming.IntakeCount == 0u)
        return Incoming;

    TextInputCondition Filtered = Incoming;
    Filtered.IntakeCount = 0u;

    for (std::uint32_t Index = 0u; Index < Incoming.IntakeCount; ++Index)
    {
        const char Character = Incoming.Intake[Index];
        if (IsViewportLookNavigationKey(Character))
            continue;
        if (Filtered.IntakeCount + 1u >= TextInputCondition::IntakeLimit)
            break;
        Filtered.Intake[Filtered.IntakeCount++] = Character;
    }

    Filtered.Intake[Filtered.IntakeCount] = '\0';
    return Filtered;
}

} // namespace Slate
