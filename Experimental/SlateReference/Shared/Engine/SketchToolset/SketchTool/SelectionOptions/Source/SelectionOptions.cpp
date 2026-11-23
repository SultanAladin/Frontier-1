//============================================================================================================================================
//                                                         SELECTIONOPTIONS.CPP
//============================================================================================================================================
// 📝 The options are a plain declaration with `constexpr` accessors, so there is nothing to define out of
//    line. This translation unit compiles the header on its own and states the invariants the widget and
//    the picker both depend on, where breaking one cannot compile rather than merely failing a proof.

#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"

namespace Slate
{

namespace
{
    constexpr SelectionOptions Standing = {};

    static_assert(Standing.Element == SelectionElement::Vertex,
                  "a fresh Select tool picks vertices, the smallest thing on the sketch");
    static_assert(Standing.Admits(SelectionElement::Vertex),
                  "and admits the element it stands at");

    // 🔴 THE POINT OF THE MODE. With `Vertex` standing an edge is not a candidate at all, however near
    //    the pointer falls to it. A mode that merely PREFERRED its element would still hand back a curve
    //    whenever no vertex was close, which is the luck-of-the-draw behaviour being removed.
    static_assert(!Standing.Admits(SelectionElement::Edge),
                  "and admits NOTHING else, so a near edge cannot win over a wanted vertex");
    static_assert(!Standing.Admits(SelectionElement::Face),
                  "nor a face");

    constexpr SelectionOptions Loose = { SelectionElement::Edge, 1000.0f };
    static_assert(Loose.ResolvedTolerance() == SelectionOptions::ToleranceMaximum,
                  "a tolerance beyond the declared range is held at the bound, never honoured");

    constexpr SelectionOptions Tight = { SelectionElement::Edge, -5.0f };
    static_assert(Tight.ResolvedTolerance() == SelectionOptions::ToleranceMinimum,
                  "and a negative tolerance cannot make the picker unusable");

    static_assert(SelectionElementText(SelectionElement::Vertex)[0] == 'V',
                  "every mode names itself for the widget");
    static_assert(SelectionElementText(SelectionElement::Edge)[0] == 'E', "");
    static_assert(SelectionElementText(SelectionElement::Face)[0] == 'F', "");
}

}   // namespace Slate
