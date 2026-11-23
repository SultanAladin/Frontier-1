/*====================================================================================================================================
                                                    RECORDEDITORDESCRIPTION.H
====================================================================================================================================*/
// 🧩 Editor-only description a record builds for itself: outliner grouping and inspector fields.

#pragma once

// 📝 This header is empty outside the editor profile. A record's constructor builds one of these
//    descriptions under the same condition and hands it to the SceneDirectory; shipping binaries
//    contain neither the description nor any field pointers into record settings.
#if defined(FRONTIER_EDITOR_PROFILE)

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One numeric inspector field, bound directly to a live setting in the owning record. The bounds
//    and units for the environment records are transcribed from the pinned VehicleHMI inspector
//    ranges (CloudRanges / EnvironmentDefaults at 6afb6f0); editing through Target edits the scene.
struct RecordEditorScalarField
{
    const char *Label  = "";       // [text] - Inspector row label
    const char *Unit   = "";       // [text] - Unit suffix shown in the number field
    double      Minimum = 0.0;     // [-] - Slider lower bound, in the field's unit
    double      Maximum = 0.0;     // [-] - Slider upper bound, in the field's unit
    double      Step    = 0.0;     // [-] - Slider / input step, in the field's unit
    int32_t     Digits  = 2;       // [digits] - Fractional digits in the number field
    double     *Target  = nullptr; // [-] - Live setting in the owning record; never owned here
};

// 📝 One inspector property switch, bound directly to a live setting in the owning record.
struct RecordEditorSwitch
{
    const char *Label  = "";       // [text] - Switch caption
    bool       *Target = nullptr;  // [-] - Live setting in the owning record; never owned here
};

// 📝 Everything the Outliner and Inspector need for one record, built by that record's own
//    editor-conditional constructor code. Group, glyph and colour spellings are the pinned HTML
//    values (EditorShell.js GroupOrder, InspectorPages.js entity table at 6afb6f0).
struct RecordEditorDescription
{
    const char *StableId    = "";  // [text] - Pinned HTML entity id, e.g. "sun"
    const char *Group       = "";  // [text] - Pinned HTML outliner group, e.g. "Environment"
    const char *DisplayName = "";  // [text] - Row and inspector header name
    const char *Category    = "";  // [text] - Inspector eyebrow type, e.g. "Directional light"
    const char *Glyph       = "";  // [text] - Pinned HTML glyph name
    const char *Colour      = "";  // [text] - Row accent colour, "#rrggbb"
    const char *Description = "";  // [text] - Inspector header description
    bool       *Enabled     = nullptr; // [-] - Live enabled setting; nullptr means always enabled

    std::vector<RecordEditorScalarField> ScalarFields; // [-] - Inspector sliders, in display order
    std::vector<RecordEditorSwitch>      Switches;     // [-] - Inspector property switches, in display order
};

} // namespace Frontier

#endif // FRONTIER_EDITOR_PROFILE
