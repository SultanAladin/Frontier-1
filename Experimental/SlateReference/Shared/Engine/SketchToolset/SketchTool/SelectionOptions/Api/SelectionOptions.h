//============================================================================================================================================
//                                                          SELECTIONOPTIONS.H
//============================================================================================================================================
// 🧩 What the Select tool is currently willing to pick, and how forgivingly. The floating tool-options
//    widget edits this; the picker reads it. Nothing here draws, and nothing here knows a widget exists.
//
// 🔴 THE ARTIST ASKED FOR SPECIFIC SELECTION. The picker offered whatever happened to be nearest — a
//    curve, its endpoint or a Bezier handle, whichever won on distance — so reaching for an edge when a
//    vertex sat near it was a matter of luck and zoom. An element mode makes the request explicit: with
//    `Vertex` standing, a curve is not a candidate at all, however close the pointer falls to it.

#pragma once

#include <cstdint>

namespace Slate
{

/// 🧩 Transform manner requested on the gizmo.
enum class GizmoManner : std::uint32_t
{
    Move   = 0u,
    Rotate = 1u,
    Scale  = 2u
};

/// 🧩 Which kind of element a pick is allowed to return.
/// note  🔴 Five modes, each mapping onto what a sketch actually HAS and named the way an artist names it:
///          Vertex → an endpoint or a Bezier control handle (Point, Control)
///          Edge   → a curve between them (Curve)
///          Face   → a closed region the curves bound (Record)
///          Object → the whole shape or profile, however it was reached
///          Free   → whatever is nearest, whichever kind it turns out to be
/// note  🔴 `Free` WAS DELIBERATELY ABSENT AND IS NOW DELIBERATELY PRESENT. It was withheld because
///        "pick whatever is nearest" was the original defect — reaching for an edge and getting a vertex
///        because the vertex happened to be closer. That is only a defect when it is the ONLY behaviour.
///        As one mode among five, chosen deliberately, it is the fast path an artist wants when the
///        sketch is sparse and there is nothing to disambiguate. The defect was the absence of a choice,
///        not the existence of this behaviour.
/// note  📝 `Object` and `Face` differ in what they RETURN, not in what they hit. Face reports the region
///        under the pointer; Object reports the whole record that region belongs to, which is what the
///        artist means by "select the shape". They resolve alike when a record holds one profile.
enum class SelectionElement : std::uint32_t
{
    Vertex = 0u,
    Edge   = 1u,
    Face   = 2u,
    Object = 3u,
    Free   = 4u,

    ElementCount = 5u
};

/// 🧩 Static text naming one element mode, for the widget's segmented control.
/// cost  ✔️
constexpr const char* SelectionElementText(SelectionElement Declared)
{
    switch (Declared)
    {
        case SelectionElement::Vertex: return "Vertex";
        case SelectionElement::Edge:   return "Edge";
        case SelectionElement::Face:   return "Face";
        case SelectionElement::Object: return "Object";
        case SelectionElement::Free:   return "Free";
        case SelectionElement::ElementCount: break;
    }
    return "";
}

/// 🧩 Everything the Select tool is configured with, held in one place.
/// note  ⚠️ `Tolerance` is in PIXELS, not world units. A world tolerance is wrong at both ends of the
///        zoom range -- the same fault the closure tolerance had -- because what the artist means by
///        "near enough to click" is a distance on the screen they are looking at.
/// tag   guarantee, nonallocating, nonthrowing
struct SelectionOptions
{
    static constexpr float ToleranceMinimum = 1.0f;    // [px]
    static constexpr float ToleranceMaximum = 40.0f;   // [px]
    static constexpr float ToleranceDefault = 8.0f;    // [px]

    SelectionElement Element   = SelectionElement::Vertex;
    float            Tolerance = ToleranceDefault;   // [px] - how far a pick may reach

    // 🔴 SNAPPING IS ABSENT FROM THIS STRUCTURE, DELIBERATELY. The reference widget carries a
    //    "Snap to grid" switch and the artist asked, in as many words, not to add it: snapping is what a
    //    DRAWING tool does with a pointer position, and selection does not place anything. A switch here
    //    would imply the picker consults the snap catalogue, which it must not.

    /// 🧩 Whether a pick of this kind is admissible under the standing mode.
    /// note  📝 `Free` admits every kind, which is what makes it Free. Every other mode admits its own
    ///        kind and nothing else — the exactness the artist asked for.
    /// cost  ✔️
    constexpr bool Admits(SelectionElement Candidate) const
    {
        return Element == SelectionElement::Free || Candidate == Element;
    }

    /// 🧩 The tolerance, held inside its declared range whatever the caller stored.
    /// cost  ✔️
    constexpr float ResolvedTolerance() const
    {
        return Tolerance < ToleranceMinimum ? ToleranceMinimum
             : (Tolerance > ToleranceMaximum ? ToleranceMaximum : Tolerance);
    }
};

/// 🧩 Whether the transform gizmo is offered for the standing selection, and in what manner.
/// note  🔴 One widget, not two. The artist asked for selection and the gizmo to be the SAME floating
///        panel: choosing what to select and choosing what to do with it are one activity, and splitting
///        them across two panels would put the two halves of one decision in two places.
struct GizmoOptions
{
    bool Shown = true;   // [-] - whether the gizmo is drawn for a standing selection
    GizmoManner Manner = GizmoManner::Move;
};

}   // namespace Slate
