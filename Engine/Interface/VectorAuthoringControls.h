/*============================================================================================================================================
                                                             VECTORAUTHORINGCONTROLS.H
============================================================================================================================================*/
// 🧩 Shared rounded authoring controls, semantic SVG glyphs, tooltips and observable interaction extents for native workspaces.

#pragma once
#include "ControlKit.h"
#include "Engine/ContentInterchange/VectorAuthoringArrayPlacement.h"
#include <functional>
#include <span>
#include <array>
#include <string_view>

namespace Frontier
{
class VectorAuthoringControlSurface
{
public:
    float Scale = 1;
    float ControlHeight = 30;
    std::function<void(std::string_view, PlaneExtent)> RegisterExtent;
    bool Button(
        const char       *Caption,
        float             Width = 0,
        bool              Selected = false,
        std::string_view  Path = {},
        bool              Capsule = false);
    bool Toggle(const char *Caption, bool &Enabled);
    bool Number(
        const char *Caption,
        float      &Quantity,
        float       Minimum,
        float       Maximum);
    bool Range(
        const char *Caption,
        float      &Quantity,
        float       Minimum,
        float       Maximum);
    bool Dropdown(const char *Caption, int &Selection, std::span<const char *const> Captions);
    bool Colour(const char *Caption, std::array<float, 4> &Spectrum);
    bool RenderColourSourcePicker(std::array<float, 4> &Spectrum);
    void Tooltip(const char *Caption, const char *Description);
    void BeginCard(const char *Caption, bool Raised = false, bool Subordinate = false);
    void EndCard();
    bool Array(
        VectorAuthoringArraySpecification &Specification,
        size_t                     LayerCount,
        size_t                     SelectionCount,
        size_t                     Limit,
        bool                       Protected);
private:
    void Register(const char *Caption);
};
}
