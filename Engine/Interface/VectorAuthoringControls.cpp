/*============================================================================================================================================
                                                            VECTORAUTHORINGCONTROLS.CPP
============================================================================================================================================*/
// 🧩 Reuse the themed ControlKit switch/slider while matching the approved rounded vector field and disclosure surfaces.

#include "VectorAuthoringControls.h"
#include "GlyphSpace.h"
#include "DropdownControls.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Frontier
{
namespace
{
ImVec4 ConvertSpectrum(ColorQuad Spectrum)
{
    return {Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha};
}

//------------------------------------------------------------------------------------------------------------------------
//                                             COLOUR PALETTE AND GENERATOR SOURCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Hex values mirror Experimental/TextureStudio/src/MaterialControls.jsx's `palettes` table so a
// swatch picked here or in the browser authoring tool reads as the same colour either side.
struct NamedColourPalette { const char *Name; std::array<const char *, 5> Hex; };
constexpr std::array<NamedColourPalette, 4> AuthoringColourPalettes{{
    {"Spectrum",        {"#c6db89", "#59bea3", "#ab8bc9", "#d17b83", "#82a3d5"}},
    {"Vivid spectrum",  {"#ec5667", "#50c38c", "#ae67e5", "#5999ee", "#edc25c"}},
    {"Warm metallic",   {"#f0cb8a", "#d99a5d", "#b97046", "#e6bf83", "#e7d6a9"}},
    {"Cool metallic",   {"#93bbdc", "#a49cdc", "#6ed0c5", "#c0cedd", "#789cab"}},
}};

void ParseHexColour(const char *Hex, float &Red, float &Green, float &Blue) noexcept
{
    const auto Component = [&](int Index) { char Byte[3]{Hex[Index], Hex[Index + 1], 0}; return float(strtol(Byte, nullptr, 16)) / 255.f; };
    Red = Component(1); Green = Component(3); Blue = Component(5);
}

// 📝 Named colour generators mirror Experimental/TextureStudio/src/colourGenerators.js: each derives
// a single authored colour from the current one (plus a seed for the randomised entries) rather than
// a spatial mask — procedural mask generators remain the separate CHANNEL_GENERATORS toolkit.
enum class AuthoringColourGenerator : uint32_t { Complementary, HarmonicHue, Analogous, ProceduralNoise, Count };
constexpr std::array<const char *, size_t(AuthoringColourGenerator::Count)> AuthoringColourGeneratorNames{
    "Complementary", "Harmonic hue", "Analogous", "Procedural noise"};

// 📝 Mulberry32: a tiny deterministic 32-bit generator so a repeated seed reproduces the same suggestion.
float NextColourGeneratorSample(uint32_t &State) noexcept
{
    State += 0x6D2B79F5u;
    uint32_t Mixed = State;
    Mixed = (Mixed ^ (Mixed >> 15)) * (Mixed | 1u);
    Mixed ^= Mixed + (Mixed ^ (Mixed >> 7)) * (Mixed | 61u);
    return float((Mixed ^ (Mixed >> 14)) >> 0) / 4294967296.f;
}

ImVec4 GenerateColourSuggestion(AuthoringColourGenerator Generator, const std::array<float, 4> &Spectrum, uint32_t Seed) noexcept
{
    float Hue, Saturation, Value;
    ImGui::ColorConvertRGBtoHSV(Spectrum[0], Spectrum[1], Spectrum[2], Hue, Saturation, Value);
    uint32_t State = Seed;
    switch (Generator)
    {
        case AuthoringColourGenerator::Complementary: Hue = std::fmod(Hue + 0.5f, 1.f); break;
        case AuthoringColourGenerator::HarmonicHue:   Hue = std::fmod(Hue + 0.38196601125f, 1.f); break;                        // 📝 Golden-angle hue rotation
        case AuthoringColourGenerator::Analogous:     Hue = std::fmod(Hue + (20.f + NextColourGeneratorSample(State) * 25.f) / 360.f, 1.f); break;
        case AuthoringColourGenerator::ProceduralNoise: default:
            Hue = NextColourGeneratorSample(State);
            Saturation = 0.35f + NextColourGeneratorSample(State) * 0.5f;
            Value = 0.45f + NextColourGeneratorSample(State) * 0.45f;
            break;
    }
    float Red, Green, Blue;
    ImGui::ColorConvertHSVtoRGB(Hue, Saturation, Value, Red, Green, Blue);
    return {Red, Green, Blue, Spectrum[3]};
}
}

bool VectorAuthoringControlSurface::RenderColourSourcePicker(std::array<float, 4> &Spectrum)
{
    bool Changed = false;
    ImGui::TextDisabled("Palette");
    for (const auto &Entry : AuthoringColourPalettes)
    {
        ImGui::TextDisabled("%s", Entry.Name);
        for (size_t Index = 0; Index < Entry.Hex.size(); ++Index)
        {
            if (Index) ImGui::SameLine();
            ImGui::PushID(int(Index));
            float Red, Green, Blue;
            ParseHexColour(Entry.Hex[Index], Red, Green, Blue);
            if (ImGui::ColorButton(Entry.Hex[Index], ImVec4(Red, Green, Blue, 1), ImGuiColorEditFlags_NoAlpha, ImVec2(18 * Scale, 18 * Scale)))
            {
                Spectrum[0] = Red; Spectrum[1] = Green; Spectrum[2] = Blue;
                Changed = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Generate");
    for (size_t Index = 0; Index < AuthoringColourGeneratorNames.size(); ++Index)
    {
        ImGui::PushID(int(Index) + 100);
        const auto Suggestion = GenerateColourSuggestion(AuthoringColourGenerator(Index), Spectrum, uint32_t(Index) * 97u + 1u);
        ImGui::ColorButton("##Preview", Suggestion, ImGuiColorEditFlags_NoAlpha, ImVec2(16 * Scale, 16 * Scale));
        ImGui::SameLine();
        if (ImGui::Selectable(AuthoringColourGeneratorNames[Index]))
        {
            const auto Applied = GenerateColourSuggestion(AuthoringColourGenerator(Index), Spectrum,
                uint32_t(ImGui::GetTime() * 1000.0) ^ (uint32_t(Index) * 97u + 1u));
            Spectrum[0] = Applied.x; Spectrum[1] = Applied.y; Spectrum[2] = Applied.z;
            Changed = true;
        }
        ImGui::PopID();
    }
    return Changed;
}

void VectorAuthoringControlSurface::Register(const char *Caption)
{
    if (!RegisterExtent) return;
    const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    RegisterExtent(Caption, {Minimum.x, Minimum.y, Maximum.x, Maximum.y});
}

bool VectorAuthoringControlSurface::Button(
    const char       *Caption,
    float             Width,
    bool              Selected,
    std::string_view  Path,
    bool              Capsule)
{
    const auto &Palette = ControlKit::Palette();
    const float Height = ControlHeight * Scale;
    if (Width == 0) Width = std::max(Height, ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption)).x + 24 * Scale);
    const float Rounding = Capsule || !Path.empty() ? Height * .5f : std::min(6 * Scale, Height * .25f);
    const auto Position = ImGui::GetCursorScreenPos();
    const bool Pressed = ImGui::InvisibleButton(Caption, ImVec2(Width, Height));
    Register(Caption);
    const auto Colour = Selected ? Palette.Selected : ImGui::IsItemHovered() ? Palette.Raised : Palette.Inset;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Colour)), Rounding);
    Ink->AddRect(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Selected ? Palette.TextFaint : Palette.Stroke)), Rounding);
    if (!Path.empty())
    {
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(Surface, Path, {Position.x + (Width - 17 * Scale) * .5f, Position.y + 6 * Scale, 17 * Scale, 1.5f, Palette.TextDim});
    }
    else
    {
        const auto Extent = ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption));
        Ink->AddText(ImVec2(Position.x + (Width - Extent.x) * .5f, Position.y + (Height - Extent.y) * .5f), ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Caption, ImGui::FindRenderedTextEnd(Caption));
    }
    return Pressed;
}

void VectorAuthoringControlSurface::Tooltip(const char *Caption, const char *Description)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsItemFocused()) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 10 * Scale));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ConvertSpectrum(ControlKit::Palette().Raised));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240 * Scale);
    ImGui::TextUnformatted(Caption);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Description);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

bool VectorAuthoringControlSurface::Toggle(const char *Caption, bool &Enabled)
{
    const auto Position = ImGui::GetCursorScreenPos();
    const float Width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(Caption, nullptr, true).x + 47 * Scale);
    const bool Changed = ImGui::InvisibleButton(Caption, ImVec2(Width, 28 * Scale));
    Register(Caption);
    if (Changed) Enabled = !Enabled;
    PixelSpace Surface;
    const float SwitchScale = .66f * Scale;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, SwitchScale);
    (void)ControlKit::Switch(Surface, Position.x / SwitchScale, (Position.y + 5 * Scale) / SwitchScale, Enabled, ControlPointer{}, ImGui::GetStyle().Alpha);
    ImGui::GetWindowDrawList()->AddText(ImVec2(Position.x + 47 * Scale, Position.y + 6 * Scale), ImGui::GetColorU32(ImGuiCol_Text), Caption, ImGui::FindRenderedTextEnd(Caption));
    return Changed;
}

bool VectorAuthoringControlSurface::Number(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum)
{
    ImGui::PushID(Caption);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    const bool Changed = ImGui::InputFloat("##Quantity", &Quantity, 0, 0, "%.3g");
    Register(Caption);
    if (Changed) Quantity = std::isfinite(Quantity) ? std::clamp(Quantity, Minimum, Maximum) : Minimum;
    ImGui::EndGroup();
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Range(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum)
{
    bool Changed = Number(Caption, Quantity, Minimum, Maximum);
    ImGui::PushID(Caption);
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(48.f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("Range", ImVec2(Width, 28 * Scale));
    const auto &Input = ImGui::GetIO();
    const bool Active = ImGui::IsItemActive();
    const ControlPointer Pointer{Input.MousePos.x, Input.MousePos.y, Input.MouseDown[0], ImGui::IsItemActivated(), ImGui::IsMouseReleased(0), Active || ImGui::IsItemHovered(), Input.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, Input.DisplaySize.x, Input.DisplaySize.y, 1);
    float Replacement = Quantity;
    (void)ControlKit::Slider(Surface, {Origin.x, Origin.y, Origin.x + Width, Origin.y + 28 * Scale}, Minimum, Maximum, Quantity, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 28 * Scale, 26 * Scale);
    if (Active && Replacement != Quantity) { Quantity = Replacement; Changed = true; }
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Dropdown(const char *Caption, int &Selection, std::span<const char *const> Captions)
{
    ImGui::PushID(Caption);
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    bool Changed = false;
    if (Captions.empty()) { ImGui::PopID(); return false; }
    if (DropdownControlSurface::BeginSelection("##Selection", Captions[size_t(std::clamp(Selection, 0, int(Captions.size()) - 1))], Scale))
    {
        for (size_t Index = 0; Index < Captions.size(); ++Index)
        {
            if (DropdownControlSurface::Option(Captions[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
            }
        }
        DropdownControlSurface::EndSelection();
    }
    Register(Caption);
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Colour(const char *Caption, std::array<float, 4> &Spectrum)
{
    ImGui::PushID(Caption);
    ImGui::TextDisabled("%s", Caption);

    const auto &Palette = ControlKit::Palette();

    // 📝 Collapsible "colorbar" (swatch chip + hex label + caret) that expands INLINE into an SV
    // box + a SEPARATE hue/alpha bar + a hex field — matching the HTML reference's `.colorbar` /
    // `.picker` pair (LayerInspector.js's BuildColourField) instead of ImGui's native hue-wheel
    // popup. Open/closed state is read from ImGui's own window state storage keyed on this
    // Caption's ID-stack path, so (unlike the HTML's DOM, which BuildColourField's OpenPickers/
    // BeginScrub machinery exists to protect from a full Refresh() rebuild) it just survives every
    // frame the normal ImGui way — there is nothing here that tears the widget down mid-interaction.
    const ImGuiID OpenId = ImGui::GetID("ColourOpen");
    ImGuiStorage *Storage = ImGui::GetStateStorage();
    bool Open = Storage->GetBool(OpenId, false);

    const float Height = 26 * Scale;
    const float Width  = std::max(ImGui::GetContentRegionAvail().x - 30 * Scale, Height * 3);
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ColourBar", ImVec2(Width, Height));
    const bool Hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) { Open = !Open; Storage->SetBool(OpenId, Open); }
    Register(Caption);

    auto      *Ink      = ImGui::GetWindowDrawList();
    const float Rounding = Height * .5f;
    const ImVec2 BarMax(Position.x + Width, Position.y + Height);
    Ink->AddRectFilled(Position, BarMax, ImGui::GetColorU32(ConvertSpectrum(Hovered ? Palette.Raised : Palette.Inset)), Rounding);
    if (Open) Ink->AddRect(Position, BarMax, ImGui::GetColorU32(ConvertSpectrum(Palette.StrokeStrong)), Rounding, 0, 1 * Scale);

    const float  ChipRadius = Height * .29f;
    const ImVec2 ChipCentre(Position.x + 6 * Scale + ChipRadius, Position.y + Height * .5f);
    const ImVec4 Swatch(std::clamp(Spectrum[0], 0.f, 1.f), std::clamp(Spectrum[1], 0.f, 1.f),
                        std::clamp(Spectrum[2], 0.f, 1.f), std::clamp(Spectrum[3], 0.f, 1.f));
    Ink->AddCircleFilled(ChipCentre, ChipRadius, ImGui::GetColorU32(Swatch), 20);
    Ink->AddCircle(ChipCentre, ChipRadius, ImGui::GetColorU32(ImVec4(0, 0, 0, .35f)), 20, 1 * Scale);

    char Hex[16];
    std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X", int(std::round(Swatch.x * 255.f)),
        int(std::round(Swatch.y * 255.f)), int(std::round(Swatch.z * 255.f)));
    Ink->AddText(ImVec2(ChipCentre.x + ChipRadius + 7 * Scale, Position.y + (Height - ImGui::GetFontSize()) * .5f),
        ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Hex);

    // 📝 A small drawn triangle rather than a glyph, so the caret needs no icon-font/atlas lookup:
    // it points down while closed and flips to point up while the picker is expanded.
    const ImVec2 CaretCentre(Position.x + Width - Height * .5f, Position.y + Height * .5f);
    const float  CaretHalf   = 3.5f * Scale;
    const ImU32  CaretColour = ImGui::GetColorU32(ConvertSpectrum(Palette.TextDim));
    if (Open)
        Ink->AddTriangleFilled(ImVec2(CaretCentre.x - CaretHalf, CaretCentre.y + CaretHalf * .5f),
            ImVec2(CaretCentre.x + CaretHalf, CaretCentre.y + CaretHalf * .5f),
            ImVec2(CaretCentre.x, CaretCentre.y - CaretHalf * .7f), CaretColour);
    else
        Ink->AddTriangleFilled(ImVec2(CaretCentre.x - CaretHalf, CaretCentre.y - CaretHalf * .5f),
            ImVec2(CaretCentre.x + CaretHalf, CaretCentre.y - CaretHalf * .5f),
            ImVec2(CaretCentre.x, CaretCentre.y + CaretHalf * .7f), CaretColour);

    bool Changed = false;
    if (Open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * Scale, 8 * Scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ConvertSpectrum(Palette.Panel));
        ImGui::PushStyleColor(ImGuiCol_Border, ConvertSpectrum(Palette.StrokeStrong));
        ImGui::BeginChild("ColourPicker", ImVec2(0, 0),
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetNextItemWidth(-1);
        // 📝 Square SV plane + a SEPARATE horizontal hue bar (PickerHueBar, not the native hue
        // wheel) + a single hex field (DisplayHex, no RGB/HSV cycle button) — the same three
        // pieces as the HTML's `.svbox` + `.barstack > .huebar` + `.hexrow`. NoSidePreview/
        // NoSmallPreview/NoOptions drop ImGui's own preview swatches and format menu since the
        // closed bar's chip already is the preview.
        if (ImGui::ColorPicker4("##Picker", Spectrum.data(),
                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoLabel |
                ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar |
                ImGuiColorEditFlags_DisplayHex))
            Changed = true;
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    // 📝 Additive palette/generator source — not part of the HTML reference, but an existing
    // Frontier affordance nothing asked to remove, so it stays as a secondary "..." button beside
    // the bar rather than folding into (or replacing) the redesigned picker above.
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("ColourSourcePopup");
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 12 * Scale));
    if (ImGui::BeginPopup("ColourSourcePopup"))
    {
        if (RenderColourSourcePicker(Spectrum)) Changed = true;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);

    ImGui::PopID();
    return Changed;
}

void VectorAuthoringControlSurface::BeginCard(const char *Caption, bool Raised, bool Subordinate)
{
    ImGui::PushID(Caption);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 14 * Scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ConvertSpectrum(Raised ? ControlKit::Palette().Raised : Subordinate ? ControlKit::Palette().Panel : ControlKit::Palette().Inset));
    ImGui::BeginChild("Card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted(Caption);
    ImGui::Spacing();
}

void VectorAuthoringControlSurface::EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 10 * Scale));
}

bool VectorAuthoringControlSurface::Array(
    VectorAuthoringArraySpecification &Specification,
    size_t                     LayerCount,
    size_t                     SelectionCount,
    size_t                     Limit,
    bool                       Protected)
{
    ImGui::BeginDisabled(Protected);
    int Category = int(Specification.Category), Profile = int(Specification.ScaleProfile);
    const char *Layouts[]{"Linear", "Grid", "Circular / arc", "Radial rings"};
    if (Dropdown("Array layout", Category, Layouts))
    {
        Specification.Category = VectorAuthoringArrayCategory(Category);
        if (Category == 1 && Specification.StepY == 0) Specification.StepY = 80;
    }
    const auto Integral = [&](const char *Caption, uint32_t &Quantity, float Minimum, float Maximum)
    {
        float Replacement = float(Quantity);
        if (Number(Caption, Replacement, Minimum, Maximum)) Quantity = uint32_t(Replacement);
    };
    if (Category == 1)
    {
        Integral("Columns", Specification.Columns, 1, 8);
        Integral("Rows", Specification.Rows, 1, 8);
    }
    else Integral(Category >= 2 ? "Copies per ring" : "Copies", Specification.Count, 2, 64);
    if (Category >= 2)
    {
        Number("Radius", Specification.Radius, 0, 512);
        if (Category == 3)
        {
            Integral("Rings", Specification.Rings, 1, 8);
            Number("Ring spacing", Specification.RadiusStep, 1, 512);
        }
        Number("Centre X", Specification.CentreX, -512, 1024);
        Number("Centre Y", Specification.CentreY, -512, 1024);
        Number("Start angle", Specification.StartAngle, -360, 360);
        Number("Arc sweep", Specification.Sweep, -360, 360);
        Toggle("Rotate copies around centre", Specification.RotateCopies);
    }
    else
    {
        Number("Step X", Specification.StepX, -512, 512);
        Number("Step Y", Specification.StepY, -512, 512);
    }
    const char *Profiles[]{"Uniform", "Linear progression", "Circular wave", "Radial distance", "Seeded noise"};
    if (Dropdown("Scale profile", Profile, Profiles)) Specification.ScaleProfile = VectorAuthoringScaleProfile(Profile);
    Number("Start scale %", Specification.StartScale, 1, 400);
    if (Profile != 0) Number("End scale %", Specification.EndScale, 1, 400);
    if (Profile == 4) Integral("Noise seed", Specification.Seed, 0, 999999);
    ImGui::TextWrapped("Replaces the selection with editable copies, not a live modifier. Undo restores originals.");
    const auto Population = Category == 1 ? Specification.Columns * Specification.Rows : Specification.Count * (Category == 3 ? Specification.Rings : 1);
    const auto Total = LayerCount - SelectionCount + Population * SelectionCount;
    ImGui::TextDisabled("%zu / %zu document entries after generation", Total, Limit);
    ImGui::BeginDisabled(Total > Limit || Population < 2);
    const bool Generate = Button("Generate array", ImGui::GetContentRegionAvail().x);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return Generate;
}
}
