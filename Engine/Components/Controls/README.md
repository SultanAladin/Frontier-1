# 🧩 `Engine/Components/Controls/`

> **TL;DR**: Reusable capsule buttons, weight/scope chips, bordered circular icon buttons, segmented bars, and quick discs (38 source/header/shader files under `Engine/Components/Controls/`).

---

## Files

| File | Purpose |
| :--- | :--- |
| `CapsuleButton.cpp` | Reusable capsule buttons, chips, circular icon buttons, segmented controls, and quick-control circular tiles. |
| `CapsuleButton.h` | Reusable capsule buttons, weight/scope chips, bordered circular icon buttons, segmented bars, and quick discs |
| `ColourRampEditor.cpp` | Implementation of reusable ColourRampEditor with smooth multi-stop strip, draggable stop handles, mode/wrap/curve/space toggles & preset bar |
| `ColourRampEditor.h` | Reusable multi-stop ColourRamp / LinearFill / SolidPigment / QuantizedRamp / BlackbodyThermal UI editor with ContentBrowser drop-slot support |
| `ColourSwatchPicker.cpp` | Implementation of reusable ColourSwatchPicker (2D S/L field, Hue spectrum strip, Alpha bar, Kelvin quick bar & WCAG readout) |
| `ColourSwatchPicker.h` | Reusable Linear/sRGB/HSL/Kelvin colour swatch picker with WCAG contrast readout & ContentBrowser .ColourSwatch drop-slot support |
| `ConfirmationDialogue.cpp` | Foreground modal confirmation dialogue rendering Rounded (28px) and SemanticHeader (8px) layouts above all trays |
| `ConfirmationDialogue.h` | Reusable foreground modal confirmation dialogue supporting Rounded (28px) and SemanticHeader (8px) layouts |
| `ConstraintTilePicker.cpp` | Implementation of ConstraintTilePicker 4x2 grid with crisp geometric icons for all 8 physics constraint kinds |
| `ConstraintTilePicker.h` | 4x2 interactive constraint kind tile picker (Fixed, Hinge, Slider, Ball, Spring, Spin, Tether, Dock) for Details inspector |
| `ControlAppearance.cpp` | Shared control color set, smoothstep cross-fade, rounded outlines, and section layout primitives |
| `ControlAppearance.h` | Shared control color set, layout metrics, pointer contact primitives, and section containers |
| `ControlTooltip.cpp` | Directional, light/dark, and split-header diagnostic tooltip implementation |
| `ControlTooltip.h` | Directional, light/dark, and split-header diagnostic tooltips with icons, metric tokens, and suggestion buttons |
| `DropdownSelector.cpp` | Reusable Studio Controls rounded dropdown selector button, option sheet, and directive sheet |
| `DropdownSelector.h` | Reusable Studio Controls rounded dropdown selector button, option sheet, and directive sheet |
| `ExclusiveSelector.cpp` | Reusable rounded toggle switch implementations. |
| `ExclusiveSelector.h` | Reusable rounded toggle switches (standard 46x26 bordered and compact 48x24 borderless) |
| `ExecutionModeStrip.cpp` | Implementation of reusable ExecutionModeStrip (Edit / Simulate / Play-Pause pill strip with optional Rewind trigger) |
| `ExecutionModeStrip.h` | Reusable Edit / Simulate / Play-Pause execution phase strip with optional Rewind trigger for studio viewports |
| `NotificationSequence.cpp` | Semantic toast banners, word-wrapped descriptions, and Quick Controls notification stream |
| `NotificationSequence.h` | Bounded semantic notification history, top-right live toast stack, and Quick Controls notification stream |
| `ScalarCurveEditor.cpp` | Implementation of reusable ScalarCurveEditor (0..1 Hermite-smoothed spline, draggable handles & quick preset curve chips) |
| `ScalarCurveEditor.h` | Reusable 0..1 piecewise-smooth scalar spline editor for size-over-life, opacity-over-life, wind falloff & force attenuation |
| `ScalarSlider.cpp` | Neutral-track scalar slider and two-cell readout capsule implementation. |
| `ScalarSlider.h` | Reusable scalar slider and split readout capsule ("[ 100 \| % ]" / "[ 16 \| px ]") |
| `SimulationPlaybackControls.cpp` | Implementation of reusable SimulationPlaybackControls (circular Play/Pause, Reset, SingleStep, Timecode, TimeScale & Burst trigger) |
| `SimulationPlaybackControls.h` | Reusable 52px simulation playback transport strip (Reset, Play/Pause, SingleStep, Timecode, TimeScale, Live indicator & Burst trigger) |
| `SimulationTimeRuler.cpp` | Implementation of reusable SimulationTimeRuler (graduated ticks, shaded active track strip & scrubbable playhead needle) |
| `SimulationTimeRuler.h` | Reusable scrubbable simulation & animation time ruler with step readout, graduated ticks, active track strip & playhead needle |
| `SimulationTimelineFooter.cpp` | Implementation of reusable SimulationTimelineFooter (transport controls, clock, scrubbable kinetic curve, speed strip & loop toggle) |
| `SimulationTimelineFooter.h` | Reusable playback footer strip (To-Start, Play/Pause, Step, Clock, Scrubbable Kinetic Curve & Event Ticks, Speed Strip, Loop) |
| `TextInputField.cpp` | UTF-8 character-safe caret movement, selection highlight, and clipped single-line text input control. |
| `TextInputField.h` | Reusable UTF-8 text editing buffer and single-line input field control |
| `ThemePreviewTile.cpp` | Exact 1:1 miniature window theme preview tiles and circular accent/semantic swatches. |
| `ThemePreviewTile.h` | Reusable 4:3 miniature OS window theme preview tile and circular accent/semantic color swatch controls |
| `Vector3CapsuleInput.cpp` | Implementation of Vector3CapsuleInput RGB X/Y/Z capsules matching HtmlReference Transform rows |
| `Vector3CapsuleInput.h` | RGB X/Y/Z axis-tinted vector capsule input row with scrub-drag, click-type edit, uniform link lock, and reset trigger |

## Namespaces & Primary Types

- **Namespaces**: `Frontier`, `CapsuleButton`, `Frontier::ColourRampEditor`, `ColourRampEditor`, `Frontier::ColourSwatchPicker`, `ColourSwatchPicker`
- **Primary Types**: `ButtonTone`, `ViewportNavigationDirective`, `ButtonDescriptor`, `ViewportNavigationCapsuleSpec`, `CapsuleReadoutSegment`, `ColourRampEditorSession`, `ColourRampEditorLayout`, `ColourSwatchPickerSession`, `DialogueSeverity`, `DialogueLayoutStyle`, `DialogueResponse`, `ConfirmationSpecification`, `ConfirmationDialogue`, `ConstraintTilePicker`, `DeferredTooltipEntry`, `ControlMetrics`, `TooltipDirection`, `TooltipSeverity` (+43 more)

## Usage

```cpp
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/ColourSwatchPicker.h"

using namespace Frontier;
ButtonTone Instance {};
```

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
