// 🧩 StackRail chrome, TreeRow, IdentityHeader: the kit set the SceneDirectory consumes (interface SDK only).

#include "Engine/Interface/StackRailControls.h"

// This translation unit compiles only with the interface SDK present; without it, it is empty so portable
// builds keep linking (same contract as the Log Stream presentation).
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/TypefaceRegistry.h"

#include <cmath>

namespace Frontier
{

using Theme = KitTheme;

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

KitFaces ResolveKitFaces() noexcept
{
    KitFaces Faces;
    Faces.Light = Faces.Regular = Faces.Medium = Faces.SemiBold = ImGui::GetFont();
    TypefaceRegistry* Registry = TypefaceRegistry::QueryCurrent();
    if (Registry == nullptr)
    {
        return Faces;
    }
    const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
    if (Family < 0)
    {
        return Faces;
    }
    const auto Grab = [Registry, Family](FontWeightCategory Weight) -> ImFont* {
        const TypefaceFace* Face = Registry->Resolve(static_cast<uint32_t>(Family), Weight);
        return Face != nullptr ? static_cast<ImFont*>(Face->Handle) : nullptr;
    };
    if (ImFont* Face = Grab(FontWeightCategory::Light))    Faces.Light    = Face;
    if (ImFont* Face = Grab(FontWeightCategory::Regular))  Faces.Regular  = Face;
    if (ImFont* Face = Grab(FontWeightCategory::Medium))   Faces.Medium   = Face;
    if (ImFont* Face = Grab(FontWeightCategory::SemiBold)) Faces.SemiBold = Face;
    return Faces;
}

float KitTextWidth(ImFont* Font, float Size, const char* Begin, const char* End) noexcept
{
    return Font->CalcTextSizeA(Size, 3.402823466e+38f, 0.0f, Begin, End).x;
}

void KitDrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Begin,
                 const char* End)
{
    Ink->AddText(Font, Size, Position, Colour, Begin, End);
}

float KitDrawTracked(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 Position, ImU32 Colour, const char* Text,
                     float Tracking)
{
    float X = Position.x;
    for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
    {
        Ink->AddText(Font, Size, ImVec2(X, Position.y), Colour, Cursor, Cursor + 1);
        X += KitTextWidth(Font, Size, Cursor, Cursor + 1) + Tracking;
    }
    return X - Position.x - (Text[0] != '\0' ? Tracking : 0.0f);
}

bool KitTarget(const char* Id, ImVec2 Minimum, ImVec2 Size, bool& Hovered, const char* Tooltip)
{
    ImGui::SetCursorScreenPos(Minimum);
    const bool Clicked = ImGui::InvisibleButton(Id, Size);
    Hovered            = ImGui::IsItemHovered();
    if (Hovered && Tooltip != nullptr)
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
    return Clicked;
}

void KitPillSurface(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, ImU32 Background, ImU32 Border, float Radius)
{
    if ((Background >> 24) != 0)
    {
        Ink->AddRectFilled(Minimum, Maximum, Background, Radius);
    }
    if ((Border >> 24) != 0)
    {
        Ink->AddRect(Minimum, Maximum, Border, Radius);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTROLS
//------------------------------------------------------------------------------------------------------------------------

bool DrawStackRailHead(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* Title,
                       const char* CountText)
{
    ImDrawList* Ink       = ImGui::GetWindowDrawList();
    const float CentreY   = Minimum.y + 18.0f;   // [px] - 36 px head row
    const float Right     = Minimum.x + Width;

    // Stack glyph tile.
    Ink->AddRectFilled(ImVec2(Minimum.x, CentreY - 15.0f), ImVec2(Minimum.x + 30.0f, CentreY + 15.0f),
                       Theme::Surface2, 9.0f);
    Ink->AddRect(ImVec2(Minimum.x, CentreY - 15.0f), ImVec2(Minimum.x + 30.0f, CentreY + 15.0f), Theme::Line, 9.0f);
    for (int Bar = 0; Bar < 3; ++Bar)
    {
        const float Y = CentreY - 7.0f + static_cast<float>(Bar) * 6.0f;
        Ink->AddRectFilled(ImVec2(Minimum.x + 8.0f, Y), ImVec2(Minimum.x + 22.0f, Y + 2.6f), Theme::Mint, 1.3f);
    }

    const float TitleX = Minimum.x + 30.0f + 14.0f;
    KitDrawText(Ink, Faces.Medium, 21.0f, ImVec2(TitleX, CentreY - 10.5f), Theme::Text, Title);

    // HeadCount pill.
    const float CountW = KitTextWidth(Faces.Regular, 12.5f, CountText);
    const float PillW  = 14.0f + 7.0f + 8.0f + CountW + 14.0f;
    const float PillX  = TitleX + KitTextWidth(Faces.Medium, 21.0f, Title) + 14.0f;
    KitPillSurface(Ink, ImVec2(PillX, CentreY - 12.0f), ImVec2(PillX + PillW, CentreY + 12.0f), Theme::Surface2,
                   Theme::Line, 999.0f);
    Ink->AddCircleFilled(ImVec2(PillX + 17.5f, CentreY), 3.5f, Theme::Mint);
    KitDrawText(Ink, Faces.Regular, 12.5f, ImVec2(PillX + 29.0f, CentreY - 6.25f), Theme::Muted, CountText);

    // AddHost button.
    bool       Hovered = false;
    const bool Clicked = KitTarget("##StackRailAdd", ImVec2(Right - 30.0f, CentreY - 15.0f), ImVec2(30.0f, 30.0f),
                                   Hovered, "Add object");
    KitPillSurface(Ink, ImVec2(Right - 30.0f, CentreY - 15.0f), ImVec2(Right, CentreY + 15.0f),
                   Hovered ? Theme::Surface3 : Theme::Surface2, Theme::Line, 9.0f);
    const ImU32 PlusColour = Hovered ? Theme::Text : Theme::Muted;
    Ink->AddRectFilled(ImVec2(Right - 15.0f - 6.0f, CentreY - 1.3f), ImVec2(Right - 15.0f + 6.0f, CentreY + 1.3f),
                       PlusColour, 1.3f);
    Ink->AddRectFilled(ImVec2(Right - 15.0f - 1.3f, CentreY - 6.0f), ImVec2(Right - 15.0f + 1.3f, CentreY + 6.0f),
                       PlusColour, 1.3f);
    return Clicked;
}

void DrawStackRailFoot(const KitFaces& Faces, ImVec2 Minimum, float Width, const char* LeftText,
                       const char* RightText)
{
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + Width, Minimum.y + 1.0f), Theme::LineSoft);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x, Minimum.y + 8.0f), Theme::Faint, LeftText);
    const float RightW = KitTextWidth(Faces.Regular, 11.5f, RightText);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x + Width - RightW, Minimum.y + 8.0f), Theme::Faint,
                RightText);
}

void DrawIdentityHeader(const KitFaces& Faces, ImVec2 Minimum, float Width, ImU32 IconColour, const char* Name,
                        const char* Sub)
{
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    KitPillSurface(Ink, Minimum, ImVec2(Minimum.x + Width, Minimum.y + 56.0f), Theme::Surface, Theme::LineSoft,
                   12.0f);
    const ImVec2 DiscCentre(Minimum.x + 28.0f, Minimum.y + 28.0f);
    Ink->AddCircleFilled(DiscCentre, 14.0f, KitInkWithAlpha(IconColour, 0.16f));
    Ink->AddCircle(DiscCentre, 14.0f, KitInkWithAlpha(IconColour, 0.45f));
    Ink->AddCircleFilled(DiscCentre, 4.5f, IconColour);
    KitDrawText(Ink, Faces.Medium, 14.5f, ImVec2(Minimum.x + 54.0f, Minimum.y + 10.0f), Theme::Text, Name);
    KitDrawText(Ink, Faces.Regular, 11.5f, ImVec2(Minimum.x + 54.0f, Minimum.y + 32.0f), Theme::Muted, Sub);
}

TreeRowInteraction DrawTreeRow(const KitFaces& Faces, const char* Id, ImVec2 Minimum, float Width, float Indent,
                               bool HasChildren, bool Collapsed, ImU32 DotColour, const char* Name,
                               const char* Meta, bool Visible, bool Selected)
{
    ImDrawList*        Ink     = ImGui::GetWindowDrawList();
    const float        Height  = 30.0f;   // [px]
    const float        CentreY = Minimum.y + Height * 0.5f;
    const ImVec2       Maximum(Minimum.x + Width, Minimum.y + Height);
    TreeRowInteraction Result;

    ImGui::PushID(Id);
    bool BodyHovered = false;
    Result.Clicked   = KitTarget("##Row", Minimum, ImVec2(Width, Height), BodyHovered);

    if (Selected)
    {
        Ink->AddRectFilled(Minimum, Maximum, Theme::MintDim, 8.0f);
        Ink->AddRectFilled(ImVec2(Minimum.x, Minimum.y + 5.0f), ImVec2(Minimum.x + 2.5f, Maximum.y - 5.0f),
                           Theme::Mint, 1.2f);
    }
    else if (BodyHovered)
    {
        Ink->AddRectFilled(Minimum, Maximum, Theme::RowHover, 8.0f);
    }

    float CursorX = Minimum.x + 10.0f + Indent;

    // Fold chevron.
    if (HasChildren)
    {
        bool ChevronHovered = false;
        Result.ChevronClicked =
            KitTarget("##Fold", ImVec2(CursorX - 3.0f, CentreY - 9.0f), ImVec2(18.0f, 18.0f), ChevronHovered,
                      Collapsed ? "Expand" : "Collapse");
        const ImU32  ChevronColour = ChevronHovered ? Theme::Text : Theme::Faint;
        const ImVec2 C(CursorX + 6.0f, CentreY);
        if (Collapsed)
        {
            Ink->AddTriangleFilled(ImVec2(C.x - 3.0f, C.y - 5.0f), ImVec2(C.x - 3.0f, C.y + 5.0f),
                                   ImVec2(C.x + 4.0f, C.y), ChevronColour);
        }
        else
        {
            Ink->AddTriangleFilled(ImVec2(C.x - 5.0f, C.y - 3.0f), ImVec2(C.x + 5.0f, C.y - 3.0f),
                                   ImVec2(C.x, C.y + 4.0f), ChevronColour);
        }
    }
    CursorX += 18.0f;

    // Classification dot + name.
    Ink->AddCircleFilled(ImVec2(CursorX + 4.0f, CentreY), 3.5f,
                         Visible ? DotColour : KitInkWithAlpha(DotColour, 0.35f));
    CursorX += 16.0f;
    const ImU32 NameColour = !Visible ? Theme::Faint : (Selected ? Theme::Text : Theme::Muted);
    KitDrawText(Ink, Selected ? Faces.Medium : Faces.Regular, 13.0f, ImVec2(CursorX, CentreY - 6.5f), NameColour,
                Name);

    // Visibility eye (right) and meta before it.
    const float EyeX = Maximum.x - 8.0f - 24.0f;
    bool        EyeHovered = false;
    Result.EyeClicked       = KitTarget("##Eye", ImVec2(EyeX, CentreY - 12.0f), ImVec2(24.0f, 24.0f), EyeHovered,
                                  Visible ? "Hide in viewport" : "Show in viewport");
    const ImU32  EyeColour = EyeHovered ? Theme::Text : (Visible ? Theme::Muted : Theme::Faint);
    const ImVec2 EyeCentre(EyeX + 12.0f, CentreY);
    Ink->AddCircle(EyeCentre, 6.5f, EyeColour, 24, 1.3f);
    Ink->AddCircleFilled(EyeCentre, 2.2f, Visible ? EyeColour : KitInkWithAlpha(EyeColour, 0.0f));
    if (!Visible)
    {
        Ink->AddLine(ImVec2(EyeCentre.x - 7.0f, EyeCentre.y + 6.0f), ImVec2(EyeCentre.x + 7.0f, EyeCentre.y - 6.0f),
                     EyeColour, 1.4f);
    }

    const float MetaW = KitTextWidth(Faces.Regular, 11.0f, Meta);
    KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(EyeX - 10.0f - MetaW, CentreY - 5.5f), Theme::Faint, Meta);

    ImGui::PopID();
    return Result;
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
