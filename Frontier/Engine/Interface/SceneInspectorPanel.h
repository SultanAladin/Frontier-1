/*====================================================================================================================================
                                                    SCENEINSPECTORPANEL.H
====================================================================================================================================*/
// 🧩 Inspector host: draws the description the selected record built for itself.

#pragma once

namespace Frontier
{

class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR HOST
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draws SceneDirectoryPanel::QueryInspectorModel inside the current ImGui window. The layout
//    metrics are transcribed from the pinned HTML inspector (Style/FrontierEditor.css at 6afb6f0):
//    44 px crumb bar, 20/16 px content padding, 19 px object header, 9 px eyebrow, an enabled pill
//    and one card of labelled sliders with number fields. Every control writes through the field
//    pointers in the record's own editor description, so an edit edits the live scene setting.
//    Without the interface SDK this compiles to an empty stub so portable builds keep linking.
void ConstructSceneInspectorInterface(SceneDirectoryPanel &Panel);

} // namespace Frontier
