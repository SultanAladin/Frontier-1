//============================================================================================================================================
//                                                   PARAMETRICTOOLSSPECIFICATION.CPP
//============================================================================================================================================

#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"

namespace Slate
{

const char* ParametricToolDimensionText(ParametricToolDimension Subject)
{
    switch (Subject)
    {
        case ParametricToolDimension::Nothing: return "Nothing";
        case ParametricToolDimension::Vertex:  return "Vertex";
        case ParametricToolDimension::Edge:    return "Edge";
        case ParametricToolDimension::Wire:    return "Wire";
        case ParametricToolDimension::Face:    return "Face";
        case ParametricToolDimension::Shell:   return "Shell";
        case ParametricToolDimension::Solid:   return "Solid";
        case ParametricToolDimension::DimensionCount:
            return "Nothing";
    }
    return "Nothing";
}

} // namespace Slate
