//============================================================================================================================================
//                                                          RECORDINGSURFACE.CPP
//============================================================================================================================================
// 🧩 The second and last translation unit that names ImGui.

#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     VENDOR CONVERSION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::uint32_t ConfineLimit  = 16u;   // [-] - nesting depth a scroll extent may reach
constexpr float         EmphaticOffset  = 0.34f; // [px] - the second recording's displacement

ImU32 Vendor(ThemeToken Colour)
{
    return IM_COL32(Colour.Red, Colour.Green, Colour.Blue, Colour.Opacity);
}

ImDrawFlags VendorCorners(std::uint32_t Corners)
{
    ImDrawFlags Declared = ImDrawFlags_None;

    if ((Corners & CornerLeadingUpper)  != 0u) Declared |= ImDrawFlags_RoundCornersTopLeft;
    if ((Corners & CornerTrailingUpper) != 0u) Declared |= ImDrawFlags_RoundCornersTopRight;
    if ((Corners & CornerTrailingLower) != 0u) Declared |= ImDrawFlags_RoundCornersBottomRight;
    if ((Corners & CornerLeadingLower)  != 0u) Declared |= ImDrawFlags_RoundCornersBottomLeft;

    // 📝 The vendor treats an all-absent mask as "decide for me" rather than as "square", so an explicit
    //    none is spelled here. A card that rounded itself because its mask was empty would be a corner
    //    radius nobody wrote and nobody could find.
    if (Declared == ImDrawFlags_None)
        Declared = ImDrawFlags_RoundCornersNone;

    return Declared;
}

ImDrawList* Commands(void* Slot)
{
    return static_cast<ImDrawList*>(Slot);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE ADOPTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> RecordingSurface::Adopt(ShellLayer Layer)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no interface context is current" });

    // 📝 A shell list rather than a window's. The shell covers the whole drawable extent and owns no
    //    window, so a window's list would clip the drawers to a region the source does not have.
    // 🔴 The FOREGROUND list for `Above`. Every ImGui window — including a docked workspace filling the
    //    whole body — records between the two, so drawers laid into the background were textured over by
    //    the first workspace that docked full-width.
    CommandSlot = (Layer == ShellLayer::Above)
                ? static_cast<void*>(ImGui::GetForegroundDrawList())
                : static_cast<void*>(ImGui::GetBackgroundDrawList());

    if (CommandSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no command list is open" });

    const ImGuiIO& Sampled = ImGui::GetIO();

    SampledPointer.PositionX   = Sampled.MousePos.x;
    SampledPointer.PositionY  = Sampled.MousePos.y;
    SampledPointer.TravelX     = Sampled.MouseDelta.x;
    SampledPointer.TravelY    = Sampled.MouseDelta.y;
    SampledPointer.WheelY     = Sampled.MouseWheel;
    SampledPointer.ContactHeld          = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    SampledPointer.ContactPressed       = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    SampledPointer.ContactDoublePressed = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    SampledPointer.ContactReleased      = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    SampledPointer.SecondaryHeld        = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    SampledPointer.SecondaryPressed     = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    SampledPointer.SecondaryReleased    = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    SampledPointer.HeldDuration         = SampledPointer.ContactHeld
                                        ? static_cast<double>(Sampled.MouseDownDuration[0]) * 1000.0
                                        : 0.0;

    SampledText = {};

    for (int Index = 0; Index < Sampled.InputQueueCharacters.Size; ++Index)
    {
        const ImWchar Typed = Sampled.InputQueueCharacters[Index];

        if (Typed < 0x20 || Typed > 0x7E ||
            SampledText.IntakeCount + 1u >= TextInputCondition::IntakeLimit)
            continue;

        SampledText.Intake[SampledText.IntakeCount++] = static_cast<char>(Typed);
    }

    SampledText.Intake[SampledText.IntakeCount] = '\0';
    SampledText.AcceptPressed    = ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                                || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    SampledText.CancelPressed    = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    SampledText.BackspacePressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, true);
    SampledText.DeletePressed    = ImGui::IsKeyPressed(ImGuiKey_Delete, true);
    SampledText.HomePressed      = ImGui::IsKeyPressed(ImGuiKey_Home, true);
    SampledText.EndPressed       = ImGui::IsKeyPressed(ImGuiKey_End, true);
    SampledText.LeftPressed      = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
    SampledText.RightPressed     = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);

    SampledDisplay.Width  = Sampled.DisplaySize.x;
    SampledDisplay.Height = Sampled.DisplaySize.y;
    SampledDisplay.Elapsed      = static_cast<double>(Sampled.DeltaTime) * 1000.0;
    SampledDisplay.DisplayScale = static_cast<double>(Sampled.DisplayFramebufferScale.x > 0.0f
                                                    ? Sampled.DisplayFramebufferScale.x
                                                    : 1.0f);

    // 🔴 Cleared last, so a refusal above leaves the surface unadopted rather than half-open.
    ConfineDepth = 0u;

    return Deliver<bool>::Result(true);
}

Deliver<bool> RecordingSurface::SwitchLayer(ShellLayer Layer)
{
    // 🔴 Refuses rather than adopting. A layer change on an unadopted surface would otherwise open a tick
    //    nothing had asked for, and the caller would record into a list no seal is going to assemble.
    if (CommandSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no tick stands adopted" });

    if (ImGui::GetCurrentContext() == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no interface context is current" });

    // 📝 The destination list and nothing else. The pointer, the display condition, the confine depth and
    //    the tick ordinal all belong to the adoption and are left exactly as the tick found them.
    // 🔴 A RESTORE RETURNS TO THE WORKSPACE WINDOW, when one stands. Every caller of this pair raises one
    //    small thing above the windows and then asks to be put back; inside a panel, "back" is that panel's
    //    own list. Answering with the background list is what made the SECOND viewport leaf lose its sky:
    //    the first leaf's orientation widget switched Above and restored to the BACKGROUND, so the next
    //    leaf's sky geometry — and every leaf recorded after it — was laid behind the workspace window
    //    instead of inside it, and the leaf's own ground plus its "3D VIEWPORT RENDER TARGET" placeholder
    //    were then drawn over the top of it. The grid never moved, because the overlay pass is recorded
    //    between the two interface bands rather than through this surface, which is exactly the reported
    //    shape of the defect: grid yes, sky no, placeholder showing.
    if (Layer == ShellLayer::Above)
        CommandSlot = static_cast<void*>(ImGui::GetForegroundDrawList());
    else if (WindowSlot != nullptr)
        CommandSlot = WindowSlot;
    else
        CommandSlot = static_cast<void*>(ImGui::GetBackgroundDrawList());

    if (CommandSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no command list is open" });

    return Deliver<bool>::Result(true);
}

Deliver<bool> RecordingSurface::SwitchToWindow()
{
    if (CommandSlot == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no tick stands adopted" });

    if (ImGui::GetCurrentContext() == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no interface context is current" });

    // 📝 Remembered as well as adopted, so a later restore knows there is a window to return to.
    CommandSlot = static_cast<void*>(ImGui::GetWindowDrawList());
    WindowSlot  = CommandSlot;
    return Deliver<bool>::Result(true);
}

void RecordingSurface::LeaveWindow()
{
    // 📝 The list belongs to the vendor and ends with the window; only this surface's claim on it is
    //    dropped. The standing slot is left alone — whoever leaves the window states its next destination.
    WindowSlot = nullptr;
}

void RecordingSurface::Retire()
{
    // 📝 The command list belongs to the vendor and is assembled by the seal; only this surface's claim on
    //    it is dropped. Clearing the slot is what makes every later recording refuse — each of them tests
    //    the slot and nothing else, which is the whole of the mechanism.
    CommandSlot  = nullptr;
    WindowSlot   = nullptr;
    ConfineDepth = 0u;
}

bool RecordingSurface::Recording() const
{
    return CommandSlot != nullptr;
}

void RecordingSurface::Reset()
{
    // 📝 The clip stack belongs to the vendor's own list and is released by the tick that owned it. Only
    //    this surface's reckoning of the depth is dropped here.
    CommandSlot    = nullptr;
    WindowSlot     = nullptr;
    SampledPointer = {};
    SampledText    = {};
    SampledDisplay = {};
    ConfineDepth   = 0u;
}

const PointerCondition& RecordingSurface::Pointer() const
{
    return SampledPointer;
}

const TextInputCondition& RecordingSurface::TextInput() const
{
    return SampledText;
}

const DisplayCondition& RecordingSurface::Display() const
{
    return SampledDisplay;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GROUNDS AND EDGES
//------------------------------------------------------------------------------------------------------------------------

void RecordingSurface::Ground(const PlaneExtent& Extent, ThemeToken Colour, float Radius, std::uint32_t Corners)
{
    if (CommandSlot == nullptr || Colour.Opacity == 0u)
        return;

    Commands(CommandSlot)->AddRectFilled(ImVec2(Extent.MinimumX,  Extent.MinimumY),
                                         ImVec2(Extent.MaximumX,   Extent.MaximumY),
                                         Vendor(Colour), Radius * CornerScale, VendorCorners(Corners));
}

void RecordingSurface::Edge(const PlaneExtent& Extent, ThemeToken Colour, float Weight,
                            float Radius, std::uint32_t Corners)
{
    if (CommandSlot == nullptr || Colour.Opacity == 0u)
        return;

    // 📝 🔴 Inset by half the weight. The vendor centres a stroke on the extent it is given; the source's
    //    border is inside the box, because the whole sheet declares `box-sizing: border-box`. Stroking on the
    //    centre line makes every card a pixel wider than its neighbour's gap, and the lattice drifts.
    const float Inset = Weight * 0.5f;

    Commands(CommandSlot)->AddRect(ImVec2(Extent.MinimumX  + Inset, Extent.MinimumY + Inset),
                                   ImVec2(Extent.MaximumX   - Inset, Extent.MaximumY  - Inset),
                                   Vendor(Colour), Radius * CornerScale, VendorCorners(Corners), Weight);
}

void RecordingSurface::Scrim(const PlaneExtent& Extent, ThemeToken UpperColour, ThemeToken LowerColour,
                             ScrimAxis Axis)
{
    if (CommandSlot == nullptr)
        return;

    const ImU32 Upper = Vendor(UpperColour);
    const ImU32 Lower = Vendor(LowerColour);

    // 📝 The vendor takes its four colours in winding order from the leading upper corner. An Y ramp
    //    therefore repeats each colour across the pair that shares an coordinate, and an X ramp across the
    //    pair that shares an abscissa — the same call with two corners exchanged.
    const ImU32 LeadingUpper  = Upper;
    const ImU32 TrailingUpper = (Axis == ScrimAxis::X) ? Lower : Upper;
    const ImU32 TrailingLower = Lower;
    const ImU32 LeadingLower  = (Axis == ScrimAxis::X) ? Upper : Lower;

    Commands(CommandSlot)->AddRectFilledMultiColor(ImVec2(Extent.MinimumX, Extent.MinimumY),
                                                   ImVec2(Extent.MaximumX,  Extent.MaximumY),
                                                   LeadingUpper, TrailingUpper, TrailingLower, LeadingLower);
}

void RecordingSurface::MaskCorners(const PlaneExtent& Extent, ThemeToken OutsideColour, float Radius)
{
    if (CommandSlot == nullptr || OutsideColour.Opacity == 0u || Radius <= 0.0f)
        return;

    const float HeldRadius = std::fmin(Radius, std::fmin(Extent.Width(), Extent.Height()) * 0.5f);
    constexpr std::uint32_t ArcSteps = 8u;
    constexpr float HalfTurn = 3.1415926536f;
    constexpr float QuarterTurn = HalfTurn * 0.5f;

    const ImVec2 Outer[4] = {
        { Extent.MinimumX, Extent.MinimumY }, { Extent.MaximumX, Extent.MinimumY },
        { Extent.MaximumX, Extent.MaximumY },   { Extent.MinimumX, Extent.MaximumY }
    };
    const ImVec2 Centre[4] = {
        { Extent.MinimumX + HeldRadius, Extent.MinimumY + HeldRadius },
        { Extent.MaximumX - HeldRadius,  Extent.MinimumY + HeldRadius },
        { Extent.MaximumX - HeldRadius,  Extent.MaximumY - HeldRadius },
        { Extent.MinimumX + HeldRadius, Extent.MaximumY - HeldRadius }
    };
    const float Start[4] = { -QuarterTurn, -QuarterTurn, 0.0f, QuarterTurn };
    const float Travel[4] = { -QuarterTurn, QuarterTurn, QuarterTurn, QuarterTurn };
    ImDrawList* Target = Commands(CommandSlot);
    const ImU32 CoveringColour = Vendor(OutsideColour);

    for (std::uint32_t CornerIndex = 0u; CornerIndex < 4u; ++CornerIndex)
    {
        for (std::uint32_t StepIndex = 0u; StepIndex < ArcSteps; ++StepIndex)
        {
            const float FirstFraction = static_cast<float>(StepIndex) / static_cast<float>(ArcSteps);
            const float SecondFraction = static_cast<float>(StepIndex + 1u) / static_cast<float>(ArcSteps);
            const float FirstAngle = Start[CornerIndex] + Travel[CornerIndex] * FirstFraction;
            const float SecondAngle = Start[CornerIndex] + Travel[CornerIndex] * SecondFraction;
            const ImVec2 First = { Centre[CornerIndex].x + std::cos(FirstAngle) * HeldRadius,
                                   Centre[CornerIndex].y + std::sin(FirstAngle) * HeldRadius };
            const ImVec2 Second = { Centre[CornerIndex].x + std::cos(SecondAngle) * HeldRadius,
                                    Centre[CornerIndex].y + std::sin(SecondAngle) * HeldRadius };
            Target->AddTriangleFilled(Outer[CornerIndex], First, Second, CoveringColour);
        }
    }
}

void RecordingSurface::Medallion(float CentreX, float CentreY, float Radius, ThemeToken Colour)
{
    if (CommandSlot == nullptr || Colour.Opacity == 0u || Radius <= 0.0f)
        return;

    Commands(CommandSlot)->AddCircleFilled(ImVec2(CentreX, CentreY), Radius, Vendor(Colour), 0);
}

void RecordingSurface::Tongue(const float* Corners, std::uint32_t CornerCount, ThemeToken Colour)
{
    if (CommandSlot == nullptr || Corners == nullptr || CornerCount < 3u || CornerCount > 8u)
        return;

    ImVec2 Outline[8];

    for (std::uint32_t CornerIndex = 0u; CornerIndex < CornerCount; ++CornerIndex)
    {
        Outline[CornerIndex] = ImVec2(Corners[CornerIndex * 2u], Corners[CornerIndex * 2u + 1u]);
    }

    Commands(CommandSlot)->AddConvexPolyFilled(Outline, static_cast<int>(CornerCount), Vendor(Colour));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SYMBOLS
//------------------------------------------------------------------------------------------------------------------------

void RecordingSurface::Stroke(SymbolSubject Subject, const PlaneExtent& SquareExtent, ThemeToken Colour,
                              float TurnRadians)
{
    if (CommandSlot == nullptr || Colour.Opacity == 0u)
        return;

    const SymbolFigure& Declared = Figure(Subject);

    if (Declared.Steps == nullptr || Declared.StepCount == 0u)
        return;

    const float Scale        = SquareExtent.Width() / DeclaredSquare;
    const float OriginX  = SquareExtent.MinimumX;
    const float OriginY = SquareExtent.MinimumY;
    const float Weight       = Declared.Weight * Scale;
    const float TurnCosine   = std::cos(TurnRadians);
    const float TurnSine     = std::sin(TurnRadians);
    const ImU32 Vendored     = Vendor(Colour);

    ImDrawList* Target      = Commands(CommandSlot);
    bool        OutlineOpen = false;

    const auto Place = [&](float X, float Y) -> ImVec2
    {
        const float CentredX  = X  - DeclaredSquare * 0.5f;
        const float CentredY = Y - DeclaredSquare * 0.5f;
        const float TurnedX   = CentredX * TurnCosine - CentredY * TurnSine;
        const float TurnedY  = CentredX * TurnSine   + CentredY * TurnCosine;
        return ImVec2(OriginX + (TurnedX + DeclaredSquare * 0.5f) * Scale,
                      OriginY + (TurnedY + DeclaredSquare * 0.5f) * Scale);
    };

    const auto Finish = [&](bool Closing)
    {
        if (!OutlineOpen)
            return;

        Target->PathStroke(Vendored, Closing ? ImDrawFlags_Closed : ImDrawFlags_None, Weight);
        OutlineOpen = false;
    };

    for (std::uint32_t StepIndex = 0u; StepIndex < Declared.StepCount; ++StepIndex)
    {
        const StrokeStep& Step = Declared.Steps[StepIndex];

        switch (Step.Command)
        {
            case StrokeCommand::Origin:
                Finish(false);
                Target->PathLineTo(Place(Step.X, Step.Y));
                OutlineOpen = true;
                break;

            case StrokeCommand::Segment:
                Target->PathLineTo(Place(Step.X, Step.Y));
                break;

            case StrokeCommand::Curve:
                Target->PathBezierCubicCurveTo(Place(Step.FirstX,  Step.FirstY),
                                               Place(Step.SecondX, Step.SecondY),
                                               Place(Step.X,       Step.Y), 0);
                break;

            case StrokeCommand::Close:
                Finish(true);
                break;

            case StrokeCommand::Disc:
                Finish(false);
                Target->AddCircle(Place(Step.X, Step.Y), Step.FirstX * Scale, Vendored, 0, Weight);
                break;

            case StrokeCommand::Enclosure:
                Finish(false);
                Target->AddRect(Place(Step.X, Step.Y),
                                Place(Step.FirstX, Step.FirstY),
                                Vendored, Step.SecondX * Scale, ImDrawFlags_RoundCornersAll, Weight);
                break;

            default:
                break;
        }
    }

    Finish(false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          TEXT
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The capitalisation and truncation staging extents. Fixed rather than allocated: `00`'s no-allocation
//    rule holds inside an interaction, and no caption or asset name in the source approaches this.
constexpr std::uint32_t StagingCapacity = 512u;

// 📝 One face decision, made once and shared by the four run recorders. An override (the typeface strip's
//    preview) wins; otherwise the loader resolves the requested weight and falls back to the regular face
//    when the family lacks it, exactly as `FontLoader::Face` already promises; with no loader at all the
//    vendor's own standing font is what an unadorned run always drew.
std::uint32_t TypographyRoleFor(float AuthoredSize)
{
    if (AuthoredSize <= -1.0f && AuthoredSize >= -8.0f)
        return static_cast<std::uint32_t>(-AuthoredSize - 1.0f);
    // Family specimens and large numeric readouts are intentionally literal; the ordinary interface
    // ladder occupies 8–32 px and maps onto the six generally inferred semantic roles.
    if (AuthoredSize > 32.0f) return 8u;
    if (AuthoredSize >= 22.0f) return 0u; // Title
    if (AuthoredSize >= 18.0f) return 1u; // Header
    if (AuthoredSize >= 15.0f) return 2u; // Subheader
    if (AuthoredSize >= 13.0f) return 3u; // Body
    if (AuthoredSize >= 11.0f) return 4u; // Label
    return 5u;                            // Caption
}

ImFont* ResolveRunFace(FontLoader* Fonts, ImFont* Override, FontWeight Weight)
{
    if (Override != nullptr)
        return Override;

    if (Fonts != nullptr)
    {
        ImFont* Role = Fonts->Face(Weight, FontSlant::Upright);
        if (Role != nullptr)
            return Role;
    }

    return const_cast<ImFont*>(ImGui::GetFont());
}

void Capitalise(const char* Text, char* Staging)
{
    std::uint32_t Index = 0u;

    while (Text[Index] != '\0' && Index + 1u < StagingCapacity)
    {
        const char Sampled = Text[Index];
        Staging[Index]   = (Sampled >= 'a' && Sampled <= 'z')
                           ? static_cast<char>(Sampled - ('a' - 'A'))
                           : Sampled;
        ++Index;
    }

    Staging[Index] = '\0';
}

}   // namespace

void RecordingSurface::ApplyTypographyScale(float Scale)
{
    TypographyScale = (Scale < 0.5f) ? 0.5f : ((Scale > 2.0f) ? 2.0f : Scale);
}

void RecordingSurface::ApplyTypographyRoles(const std::uint32_t Sizes[8],
                                            const std::uint32_t Weights[8])
{
    if (Sizes == nullptr || Weights == nullptr) return;
    const std::uint32_t Minimum[8] = {20u, 16u, 12u, 10u, 8u, 8u, 10u, 10u};
    const std::uint32_t Maximum[8] = {64u, 40u, 32u, 24u, 20u, 16u, 24u, 24u};
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
    {
        TypographySizes[Index] = static_cast<float>(std::clamp(Sizes[Index], Minimum[Index], Maximum[Index]));
        TypographyWeights[Index] = static_cast<FontWeight>(std::clamp(Weights[Index], 100u, 900u));
    }
}

float RecordingSurface::ResolveTypographySize(float AuthoredSize) const
{
    const std::uint32_t Role = TypographyRoleFor(AuthoredSize);
    const float Semantic = Role < 8u ? TypographySizes[Role] : AuthoredSize;
    return Semantic * TypographyScale;
}

void RecordingSurface::ApplyCornerScale(float Scale)
{
    CornerScale = (Scale < 0.5f) ? 0.5f : ((Scale > 1.5f) ? 1.5f : Scale);
}

void RecordingSurface::ApplyFontLoader(FontLoader& Loader)
{
    Fonts = &Loader;
    FontOverride = nullptr;
}

void RecordingSurface::ApplyFontPreview(ImFont* Preview)
{
    FontOverride = Preview;
}

void RecordingSurface::Image(const PlaneExtent& Extent, std::uintptr_t Identity,
                             float U0, float V0, float U1, float V1)
{
    if (CommandSlot == nullptr || Identity == 0u)
        return;

    // 📝 The vendor's texture reference is constructed from the opaque identity; the sampled rectangle
    //    is the caller's crop, and the clip stack confines the quad to whatever confine stands.
    Commands(CommandSlot)->AddImage(ImTextureRef(static_cast<ImTextureID>(Identity)),
                                    ImVec2(Extent.MinimumX, Extent.MinimumY),
                                    ImVec2(Extent.MaximumX, Extent.MaximumY),
                                    ImVec2(U0, V0), ImVec2(U1, V1),
                                    IM_COL32_WHITE);
}

void RecordingSurface::ImageGeometry(std::uintptr_t Identity,
                                  const float* Positions, const float* UVs, std::uint32_t VertexCount,
                                  const std::uint32_t* Indices, std::uint32_t IndexCount)
{
    if (CommandSlot == nullptr || Identity == 0u || Positions == nullptr || UVs == nullptr ||
        Indices == nullptr || VertexCount == 0u || IndexCount < 3u || IndexCount % 3u != 0u)
        return;

    // 📝 The vendor's own texture switch, exactly as `AddImage` performs it: `PushTexture` states the
    //    identity on the command header AND opens a new command when the standing one carries other
    //    prims, so the geometry never inherits the font atlas's identity and the atlas text never inherits
    //    the sky's. Writing the header directly would corrupt whichever command stood — the reported
    //    defect where the viewport read the atlas as sky.
    ImDrawList* Target = Commands(CommandSlot);

    // 🔴 The indices a command list carries are ABSOLUTE positions in the list's vertex buffer, not
    //    positions in the geometry: a geometry run that wrote 0-based indices would reference the top bar's text
    //    vertices and the viewport would read the atlas as sky. The base is the count of vertices the
    //    list already holds, and every delivered index is offset by it.
    const unsigned int Base = Target->_VtxCurrentIdx;

    Target->PushTexture(ImTextureRef(static_cast<ImTextureID>(Identity)));
    Target->PrimReserve(static_cast<int>(IndexCount), static_cast<int>(VertexCount));

    for (std::uint32_t Index = 0u; Index < VertexCount; ++Index)
    {
        Target->PrimWriteVtx(ImVec2(Positions[Index * 2u], Positions[Index * 2u + 1u]),
                             ImVec2(UVs[Index * 2u], UVs[Index * 2u + 1u]),
                             IM_COL32_WHITE);
    }

    for (std::uint32_t Index = 0u; Index < IndexCount; ++Index)
        Target->PrimWriteIdx(static_cast<ImDrawIdx>(Base + Indices[Index]));

    Target->PopTexture();
}

void RecordingSurface::Polyline(const float* PointsX, const float* PointsY, std::uint32_t Count,
                                ThemeToken Colour, float Weight)
{
    if (CommandSlot == nullptr || PointsX == nullptr || PointsY == nullptr || Count < 2u ||
        Colour.Opacity == 0u)
        return;

    // 📝 Bounded: a caller cannot allocate inside a tick, and a grid line needs far fewer than 64
    //    samples. The clamp is stated here so a caller that grows its sampling never overruns silently.
    if (Count > 64u)
        Count = 64u;

    ImVec2 Points[64];

    for (std::uint32_t Index = 0u; Index < Count; ++Index)
        Points[Index] = ImVec2(PointsX[Index], PointsY[Index]);

    Commands(CommandSlot)->AddPolyline(Points, static_cast<int>(Count), Vendor(Colour), Weight);
}

void RecordingSurface::TextRun(float X, float Y, ThemeToken Colour, const char* Text,
                               float PointSize, float Tracking, bool Emphatic, FontWeight Weight)
{
    if (CommandSlot == nullptr || Text == nullptr || Text[0] == '\0' || Colour.Opacity == 0u)
        return;

    const std::uint32_t Role = TypographyRoleFor(PointSize);
    if (Role < 8u)
    {
        PointSize = TypographySizes[Role];
        if (Weight == FontWeight::Regular)
            Weight = TypographyWeights[Role];
    }
    if (Weight != FontWeight::Regular)
        Emphatic = false;
    ImDrawList*   Target   = Commands(CommandSlot);
    ImFont*       Typeface = ResolveRunFace(Fonts, FontOverride, Weight);
    const ImU32   Vendored = Vendor(Colour);
    PointSize *= TypographyScale;
    const float   Added    = Tracking * PointSize;

    const auto Emit = [&](float StartX, float StartY)
    {
        if (Added == 0.0f)
        {
            Target->AddText(Typeface, PointSize, ImVec2(StartX, StartY), Vendored, Text);
            return;
        }

        // 📝 🔴 Tracking is applied per glyph because the vendor has no letter-spacing. The source declares
        //    0.2em on the LIBRARY caption and 0.05em on two more, and a run recorded without it is thirty per
        //    cent narrower than the source's — which moves everything to its right.
        // 🔴 The step is one CODEPOINT and not one byte. Stepping by bytes handed the vendor the halves of
        //    every multi-byte sequence separately, and each half rasterised as the replacement mark — so a
        //    tracked run carrying U+00B7 drew `??` where the same run drew correctly untracked. The defect
        //    was invisible in ASCII captions, which is every caption but the ones that separate readings.
        float       Pen      = StartX;
        const char* Sweeping = Text;

        while (*Sweeping != '\0')
        {
            const auto    Leading  = static_cast<unsigned char>(*Sweeping);
            std::uint32_t Occupied = 1u;

            if      ((Leading & 0xF8u) == 0xF0u) Occupied = 4u;
            else if ((Leading & 0xF0u) == 0xE0u) Occupied = 3u;
            else if ((Leading & 0xE0u) == 0xC0u) Occupied = 2u;

            // 📝 A truncated sequence at the run's end is stepped one byte at a time rather than read past
            //    its terminator, which is what a malformed run must never be allowed to do.
            for (std::uint32_t Walk = 1u; Walk < Occupied; ++Walk)
                if (Sweeping[Walk] == '\0')
                {
                    Occupied = Walk;
                    break;
                }

            const char* const Ending = Sweeping + Occupied;

            Target->AddText(Typeface, PointSize, ImVec2(Pen, StartY), Vendored, Sweeping, Ending);

            Pen += Typeface->CalcTextSizeA(PointSize, FLT_MAX, 0.0f, Sweeping, Ending).x + Added;
            Sweeping = Ending;
        }
    };

    Emit(X, Y);

    if (Emphatic)
        Emit(X + EmphaticOffset, Y);
}

void RecordingSurface::TextRunRole(float X, float Y, ThemeToken Colour, const char* Text,
                                   TypographyRole Role, float Tracking)
{
    const std::uint32_t Index = static_cast<std::uint32_t>(Role);
    TextRun(X, Y, Colour, Text, -1.0f - static_cast<float>(Index), Tracking, false,
            Index < 8u ? TypographyWeights[Index] : FontWeight::Regular);
}

float RecordingSurface::MeasureRunRole(const char* Text, TypographyRole Role, float Tracking) const
{
    const std::uint32_t Index = static_cast<std::uint32_t>(Role);
    return MeasureRun(Text, -1.0f - static_cast<float>(Index), Tracking,
                      Index < 8u ? TypographyWeights[Index] : FontWeight::Regular);
}

void RecordingSurface::TextRunCapitalised(float X, float Y, ThemeToken Colour, const char* Text,
                                          float PointSize, float Tracking, bool Emphatic, FontWeight Weight)
{
    if (Text == nullptr)
        return;

    char Staging[StagingCapacity];
    Capitalise(Text, Staging);

    TextRun(X, Y, Colour, Staging, PointSize, Tracking, Emphatic, Weight);
}

void RecordingSurface::TextRunTruncated(float X, float Y, float LimitX, ThemeToken Colour,
                                        const char* Text, float PointSize, bool Emphatic, FontWeight Weight)
{
    if (CommandSlot == nullptr || Text == nullptr || Text[0] == '\0')
        return;

    if (MeasureRun(Text, PointSize, 0.0f, Weight) <= LimitX)
    {
        TextRun(X, Y, Colour, Text, PointSize, 0.0f, Emphatic, Weight);
        return;
    }

    const std::uint32_t Role = TypographyRoleFor(PointSize);
    if (Role < 8u && Weight == FontWeight::Regular)
        Weight = TypographyWeights[Role];
    const float EffectivePointSize = ResolveTypographySize(PointSize);
    ImFont*       Typeface     = ResolveRunFace(Fonts, FontOverride, Weight);
    const float   EllipsisSpan = Typeface->CalcTextSizeA(EffectivePointSize, FLT_MAX, 0.0f, "...").x;
    const float   Admissible   = LimitX - EllipsisSpan;

    char          Staging[StagingCapacity];
    std::uint32_t Kept = 0u;
    float         Pen  = 0.0f;

    while (Text[Kept] != '\0' && Kept + 4u < StagingCapacity)
    {
        const char Glyph[2] = { Text[Kept], '\0' };
        const float Advance = Typeface->CalcTextSizeA(EffectivePointSize, FLT_MAX, 0.0f, Glyph, Glyph + 1).x;

        if (Pen + Advance > Admissible)
            break;

        Staging[Kept] = Text[Kept];
        Pen          += Advance;
        ++Kept;
    }

    Staging[Kept]      = '.';
    Staging[Kept + 1u] = '.';
    Staging[Kept + 2u] = '.';
    Staging[Kept + 3u] = '\0';

    TextRun(X, Y, Colour, Staging, PointSize, 0.0f, Emphatic, Weight);
}

float RecordingSurface::MeasureRun(const char* Text, float PointSize, float Tracking, FontWeight Weight) const
{
    if (Text == nullptr || Text[0] == '\0' || ImGui::GetCurrentContext() == nullptr)
        return 0.0f;

    const std::uint32_t Role = TypographyRoleFor(PointSize);
    if (Role < 8u)
    {
        PointSize = TypographySizes[Role];
        if (Weight == FontWeight::Regular)
            Weight = TypographyWeights[Role];
    }
    ImFont*       Typeface = ResolveRunFace(Fonts, FontOverride, Weight);
    PointSize *= TypographyScale;
    const float   Measured = Typeface->CalcTextSizeA(PointSize, FLT_MAX, 0.0f, Text).x;

    if (Tracking == 0.0f)
        return Measured;

    // 📝 One added advance per glyph, and the trailing one is subtracted back off: the source's letter-spacing
    //    lands after every glyph including the last, but the visible run ends at the last glyph's own edge.
    std::uint32_t GlyphCount = 0u;

    while (Text[GlyphCount] != '\0')
        ++GlyphCount;

    return Measured + Tracking * PointSize * static_cast<float>(GlyphCount) - Tracking * PointSize;
}

float RecordingSurface::LineHeight(float PointSize) const
{
    // 📐 The source's `leading-normal` is 1.5 for body text; the two captions declare `leading-none`, which the
    //    caller states by asking for the point size itself. 1.5 is the figure a run occupies when nothing
    //    overrides it, so it is what this reports.
    return ResolveTypographySize(PointSize) * 1.5f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLIPPING
//------------------------------------------------------------------------------------------------------------------------

void RecordingSurface::Confine(const PlaneExtent& Extent)
{
    if (CommandSlot == nullptr || ConfineDepth >= ConfineLimit)
        return;

    Commands(CommandSlot)->PushClipRect(ImVec2(Extent.MinimumX, Extent.MinimumY),
                                        ImVec2(Extent.MaximumX,  Extent.MaximumY), true);
    ++ConfineDepth;
}

void RecordingSurface::Release()
{
    if (CommandSlot == nullptr || ConfineDepth == 0u)
        return;

    Commands(CommandSlot)->PopClipRect();
    --ConfineDepth;
}

bool RecordingSurface::Excluded(const PlaneExtent& Extent) const
{
    if (CommandSlot == nullptr)
        return true;

    // 📝 🔴 The previous read took `_ClipRectStack.back()` on a vector the vendor is free to leave empty
    //    between ticks. `back()` on an empty ImVector reads one element before the allocation.
    const ImDrawList* Target = Commands(CommandSlot);

    if (Target->_ClipRectStack.Size == 0)
        return false;

    const ImVec2 Minimum = Target->GetClipRectMin();
    const ImVec2 Maximum  = Target->GetClipRectMax();

    return Extent.MaximumX  <= Minimum.x || Extent.MinimumX  >= Maximum.x
        || Extent.MaximumY <= Minimum.y || Extent.MinimumY >= Maximum.y;
}

}   // namespace Slate
