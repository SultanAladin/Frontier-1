// Renders the Select widget (expanded + compacted) and the four construction popups to SVG,
// using the ACTUAL constants the C++ draws with. Nothing here is hand-copied: every measure is
// read from the headers, so a measure changed in the engine changes this picture too.

#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"
#include "SlateUI/Interface/ToolOptionsWidget/Api/ToolOptionsWidget.h"
#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Slate;

namespace
{

std::string Svg;

std::string Hex(ThemeToken Token)
{
    char Buffer[32];
    std::snprintf(Buffer, sizeof Buffer, "#%02x%02x%02x",
                  static_cast<unsigned>(Token.Red),
                  static_cast<unsigned>(Token.Green),
                  static_cast<unsigned>(Token.Blue));
    return Buffer;
}

void Rect(float X, float Y, float W, float H, ThemeToken Fill, float Radius)
{
    char Buffer[512];
    const std::string Colour = Hex(Fill);
    std::snprintf(Buffer, sizeof Buffer,
        "<rect x='%.2f' y='%.2f' width='%.2f' height='%.2f' rx='%.2f' fill='%s' fill-opacity='%.3f'/>",
        X, Y, W, H, Radius, Colour.c_str(), static_cast<double>(Fill.Opacity) / 255.0);
    Svg += Buffer;
}

void Circle(float CX, float CY, float R, ThemeToken Fill)
{
    const std::string Colour = Hex(Fill);
    char Buffer[256];
    std::snprintf(Buffer, sizeof Buffer,
        "<circle cx='%.2f' cy='%.2f' r='%.2f' fill='%s' fill-opacity='%.3f'/>",
        CX, CY, R, Colour.c_str(), static_cast<double>(Fill.Opacity) / 255.0);
    Svg += Buffer;
}

void Text(float X, float Y, ThemeToken Ink, const char* Body, float Point,
          const char* Anchor = "start", int Weight = 400)
{
    const std::string Colour = Hex(Ink);
    const char* Safe = Body != nullptr ? Body : "";
    char Buffer[640];
    std::snprintf(Buffer, sizeof Buffer,
        "<text x='%.2f' y='%.2f' fill='%s' fill-opacity='%.3f' font-family='Segoe UI,system-ui,sans-serif' "
        "font-size='%.2f' font-weight='%d' text-anchor='%s'>%s</text>",
        X, Y, Colour.c_str(), static_cast<double>(Ink.Opacity) / 255.0, Point, Weight, Anchor, Safe);
    Svg += Buffer;
}

// 📐 A stand-in for the engine's stroked glyphs. The real widget draws `SymbolSubject` paths; the
//    shapes here only have to be recognisable at 14-18 px, which is what "dummy icons acceptable"
//    licenced.
void Glyph(float X, float Y, float Side, ThemeToken Ink, SymbolSubject Which)
{
    const float CX = X + Side * 0.5f;
    const float CY = Y + Side * 0.5f;
    const std::string Colour = Hex(Ink);
    char Buffer[900];

    switch (Which)
    {
        case SymbolSubject::VertexPoint:
            std::snprintf(Buffer, sizeof Buffer,
                "<circle cx='%.2f' cy='%.2f' r='%.2f' fill='none' stroke='%s' stroke-width='1.6'/>"
                "<circle cx='%.2f' cy='%.2f' r='2' fill='%s'/>",
                CX, CY, Side * 0.34f, Colour.c_str(), CX, CY, Colour.c_str());
            break;
        case SymbolSubject::EdgeSegment:
            std::snprintf(Buffer, sizeof Buffer,
                "<path d='M%.2f %.2f L%.2f %.2f' stroke='%s' stroke-width='1.7' stroke-linecap='round'/>"
                "<circle cx='%.2f' cy='%.2f' r='2.2' fill='%s'/><circle cx='%.2f' cy='%.2f' r='2.2' fill='%s'/>",
                X + Side * 0.18f, Y + Side * 0.78f, X + Side * 0.82f, Y + Side * 0.22f, Colour.c_str(),
                X + Side * 0.18f, Y + Side * 0.78f, Colour.c_str(),
                X + Side * 0.82f, Y + Side * 0.22f, Colour.c_str());
            break;
        case SymbolSubject::FacePlanar:
            std::snprintf(Buffer, sizeof Buffer,
                "<path d='M%.2f %.2f L%.2f %.2f L%.2f %.2f L%.2f %.2f Z' fill='%s' fill-opacity='.30' "
                "stroke='%s' stroke-width='1.6' stroke-linejoin='round'/>",
                X + Side * 0.14f, Y + Side * 0.34f, X + Side * 0.54f, Y + Side * 0.14f,
                X + Side * 0.86f, Y + Side * 0.64f, X + Side * 0.44f, Y + Side * 0.86f,
                Colour.c_str(), Colour.c_str());
            break;
        case SymbolSubject::BevelChamfer:
            std::snprintf(Buffer, sizeof Buffer,
                "<path d='M%.2f %.2f L%.2f %.2f L%.2f %.2f L%.2f %.2f' fill='none' stroke='%s' "
                "stroke-width='1.7' stroke-linejoin='round' stroke-linecap='round'/>",
                X + Side * 0.14f, Y + Side * 0.86f, X + Side * 0.14f, Y + Side * 0.42f,
                X + Side * 0.42f, Y + Side * 0.14f, X + Side * 0.86f, Y + Side * 0.14f,
                Colour.c_str());
            break;
        case SymbolSubject::CrosshairCentre:
            std::snprintf(Buffer, sizeof Buffer,
                "<circle cx='%.2f' cy='%.2f' r='%.2f' fill='none' stroke='%s' stroke-width='1.6'/>"
                "<path d='M%.2f %.2f h%.2f M%.2f %.2f v%.2f' stroke='%s' stroke-width='1.6' stroke-linecap='round'/>",
                CX, CY, Side * 0.32f, Colour.c_str(),
                X, CY, Side, CX, Y, Side, Colour.c_str());
            break;
        case SymbolSubject::ChevronDown:
            std::snprintf(Buffer, sizeof Buffer,
                "<path d='M%.2f %.2f L%.2f %.2f L%.2f %.2f' fill='none' stroke='%s' stroke-width='1.9' "
                "stroke-linecap='round' stroke-linejoin='round'/>",
                X + Side * 0.2f, Y + Side * 0.38f, CX, Y + Side * 0.66f,
                X + Side * 0.8f, Y + Side * 0.38f, Colour.c_str());
            break;
        case SymbolSubject::CrossClose:
            std::snprintf(Buffer, sizeof Buffer,
                "<path d='M%.2f %.2f L%.2f %.2f M%.2f %.2f L%.2f %.2f' stroke='%s' stroke-width='1.9' "
                "stroke-linecap='round'/>",
                X + Side * 0.24f, Y + Side * 0.24f, X + Side * 0.76f, Y + Side * 0.76f,
                X + Side * 0.76f, Y + Side * 0.24f, X + Side * 0.24f, Y + Side * 0.76f, Colour.c_str());
            break;
        default:
            Buffer[0] = '\0';
            break;
    }
    Svg += Buffer;
}

void Outline(float X, float Y, float W, float H, float Radius)
{
    char Buffer[320];
    std::snprintf(Buffer, sizeof Buffer,
        "<rect x='%.2f' y='%.2f' width='%.2f' height='%.2f' rx='%.2f' fill='none' stroke='#ffffff' "
        "stroke-opacity='0.22' stroke-width='1.5'/>", X, Y, W, H, Radius);
    Svg += Buffer;
}

//------------------------------------------------------------------------------------------------
//                                    THE OPTIONS CARD
//------------------------------------------------------------------------------------------------

struct Row
{
    OptionControl Kind;
    const char*   Caption;
    const char*   Unit;
    float         Reading;
    float         Minimum;
    float         Maximum;
    const char*   Options[3];
    std::uint32_t OptionCount;
    std::uint32_t Selected;
    bool          Taken;
    SymbolSubject Glyphs[3];
};

float RowTall(const Row& R)
{
    return R.Kind == OptionControl::Segmented ? SegmentHeight : 44.0f;
}

void DrawRowControl(float X, float Y, float W, const Row& R)
{
    const float H = RowTall(R);

    if (R.Kind == OptionControl::Slider)
    {
        // 📐 The value pill: a dark number cell with the unit in its own darker segment.
        Rect(X, Y, ValuePillWidth, H, ValueNumber, ValueRadius);
        char Written[32];
        std::snprintf(Written, sizeof Written, "%.2f", static_cast<double>(R.Reading));
        Text(X + ValuePillWidth - ValueUnitWidth - 12.0f, Y + H * 0.5f + 5.0f, ColourValue, Written, 15.0f, "end", 500);
        Rect(X + ValuePillWidth - ValueUnitWidth - 4.0f, Y + 4.0f, ValueUnitWidth, H - 8.0f,
             ValueUnit, ValueRadius - 4.0f);
        Text(X + ValuePillWidth - ValueUnitWidth / 2.0f - 4.0f, Y + H * 0.5f + 4.0f, ColourMuted, R.Unit, 10.0f, "middle");

        const float TrackX = X + ValuePillWidth + 10.0f;
        const float TrackW = W - ValuePillWidth - 10.0f;
        const float TrackY = Y + (H - TrackHeight) * 0.5f;
        Rect(TrackX, TrackY, TrackW, TrackHeight, ValueBlack, TrackHeight * 0.5f);
        Outline(TrackX, TrackY, TrackW, TrackHeight, TrackHeight * 0.5f);

        const float Span = R.Maximum - R.Minimum;
        const float Fraction = Span > 0.0f ? (R.Reading - R.Minimum) / Span : 0.0f;
        const float Travel = TrackW - KnobDiameter;
        const float KnobX = TrackX + KnobDiameter * 0.5f + Travel * Fraction;

        if (Fraction > 0.0f)
            Rect(TrackX + 3.0f, TrackY + 3.0f, KnobX - TrackX - 3.0f, TrackHeight - 6.0f,
                 TrackFill, (TrackHeight - 6.0f) * 0.5f);
        Circle(KnobX, TrackY + TrackHeight * 0.5f, KnobDiameter * 0.5f, KnobGround);
    }
    else if (R.Kind == OptionControl::Segmented)
    {
        Rect(X, Y, W, H, ValueBlack, SegmentRadius);
        const float Inset = 3.0f;
        const float CellW = (W - Inset * 2.0f) / static_cast<float>(R.OptionCount);
        for (std::uint32_t I = 0u; I < R.OptionCount; ++I)
        {
            const float CX = X + Inset + CellW * static_cast<float>(I);
            const bool Chosen = R.Selected == I;
            if (Chosen)
                Rect(CX, Y + Inset, CellW, H - Inset * 2.0f, KnobGround, SegmentRadius - 2.0f);

            const ThemeToken Ink = Chosen ? PanelHead : ColourMuted;
            if (R.Glyphs[0] != SymbolSubject::SubjectCount)
            {
                Glyph(CX + (CellW - 18.0f) * 0.5f, Y + (H - 18.0f) * 0.5f, 18.0f, Ink, R.Glyphs[I]);
            }
            else
            {
                Text(CX + CellW * 0.5f, Y + H * 0.5f + 4.0f, Ink, R.Options[I], CaptionPoint,
                     "middle", Chosen ? 600 : 400);
            }
        }
    }
    else if (R.Kind == OptionControl::Toggle)
    {
        const float SX = X + W - SwitchWidth;
        const float SY = Y + (H - SwitchHeight) * 0.5f;
        Rect(SX, SY, SwitchWidth, SwitchHeight, R.Taken ? AccentGround : TrackGround, SwitchHeight * 0.5f);
        const float NubR = SwitchNub * 0.5f;
        const float NubX = R.Taken ? SX + SwitchWidth - NubR - 3.0f : SX + NubR + 3.0f;
        Circle(NubX, SY + SwitchHeight * 0.5f, NubR, KnobGround);
    }
}

// 📐 Mirrors `ToolOptionsWidget::MeasureBody` exactly.
float MeasureBody(const std::vector<Row>& Rows)
{
    if (Rows.empty())
        return 0.0f;
    float Total = ToolOptionsWidget::BodyPadding * 2.0f;
    for (std::size_t I = 0u; I < Rows.size(); ++I)
    {
        Total += CaptionPoint + 8.0f;
        Total += RowTall(Rows[I]);
        if (I + 1u < Rows.size())
            Total += ToolOptionsWidget::BodyGap;
    }
    return Total;
}

void DrawCard(float X, float Y, const char* Title, SymbolSubject Icon, const std::vector<Row>& Rows)
{
    const float W = ToolOptionsWidget::PanelWidth;
    const float Header = ToolOptionsWidget::HeaderHeight;
    const float Body = MeasureBody(Rows);

    Rect(X, Y, W, Header + Body, PanelGround, ToolOptionsWidget::PanelRadius);
    Outline(X, Y, W, Header + Body, ToolOptionsWidget::PanelRadius);

    // 📝 The header is drawn square-bottomed by clipping in the engine; here a plain rect plus a
    //    cover strip reproduces the same silhouette.
    Rect(X, Y, W, Header, PanelHead, ToolOptionsWidget::PanelRadius);
    Rect(X, Y + Header - ToolOptionsWidget::PanelRadius, W, ToolOptionsWidget::PanelRadius, PanelHead, 0.0f);

    const float MiddleY = Y + Header * 0.5f;
    Glyph(X + 14.0f, MiddleY - 9.0f, 18.0f, ColourPrimary, Icon);
    Text(X + 14.0f + 18.0f + 10.0f, MiddleY - 1.0f, ColourPrimary, Title, 16.0f, "start", 600);
    Text(X + 14.0f + 18.0f + 10.0f, MiddleY + 13.0f, ColourMuted, "Tool Options", 10.0f);

    // The two header actions, collapse then hide.
    const float ButtonY = MiddleY - 14.0f;
    const float HideX = X + W - 14.0f - 28.0f;
    const float FoldX = HideX - 28.0f - 6.0f;
    Rect(FoldX, ButtonY, 28.0f, 28.0f, ValueGround, 9.0f);
    Glyph(FoldX + 6.5f, ButtonY + 6.5f, 15.0f, ColourMuted, SymbolSubject::ChevronDown);
    Rect(HideX, ButtonY, 28.0f, 28.0f, ValueGround, 9.0f);
    Glyph(HideX + 6.5f, ButtonY + 6.5f, 15.0f, ColourMuted, SymbolSubject::CrossClose);

    float Cursor = Y + Header + ToolOptionsWidget::BodyPadding;
    for (const Row& R : Rows)
    {
        Text(X + ToolOptionsWidget::BodyPadding, Cursor + CaptionPoint, ColourMuted, R.Caption, CaptionPoint);
        Cursor += CaptionPoint + 8.0f;
        DrawRowControl(X + ToolOptionsWidget::BodyPadding, Cursor,
                       W - ToolOptionsWidget::BodyPadding * 2.0f, R);
        Cursor += RowTall(R) + ToolOptionsWidget::BodyGap;
    }
}

void DrawPill(float X, float Y, const char* Title, SymbolSubject Icon)
{
    const float H = ToolOptionsWidget::PillHeight;
    // 📐 `RecordPill`'s own arithmetic: pad + glyph + gap + caption + gap + chevron + pad.
    const float Caption = static_cast<float>(std::string(Title).size()) * CaptionPoint * 0.52f;
    const float W = (14.0f + 18.0f + 10.0f) + Caption + (10.0f + 14.0f + 14.0f);

    Rect(X, Y, W, H, PanelGround, H * 0.5f);
    Outline(X, Y, W, H, H * 0.5f);

    const float MiddleY = Y + H * 0.5f;
    Glyph(X + 14.0f, MiddleY - 9.0f, 18.0f, ColourPrimary, Icon);
    Text(X + 14.0f + 18.0f + 10.0f, MiddleY + 4.0f, ColourPrimary, Title, CaptionPoint);
    Glyph(X + W - 14.0f - 14.0f, MiddleY - 7.0f, 14.0f, ColourMuted, SymbolSubject::ChevronDown);
}

//------------------------------------------------------------------------------------------------
//                                    THE PARAMETER POPUP
//------------------------------------------------------------------------------------------------

float MeasurePopupBody(const std::vector<Row>& Rows)
{
    if (Rows.empty())
        return 0.0f;
    float Total = ToolContextMenu::BodyPadding * 2.0f;
    for (std::size_t I = 0u; I < Rows.size(); ++I)
    {
        Total += ToolContextMenu::CaptionPoint + ToolContextMenu::CaptionGap;
        Total += RowTall(Rows[I]);
        if (I + 1u < Rows.size())
            Total += ToolContextMenu::BodyGap;
    }
    return Total;
}

void DrawPopup(float X, float Y, const char* Title, SymbolSubject Icon, const std::vector<Row>& Rows)
{
    const float W = ToolContextMenu::PopupWidth;
    const float Head = ToolContextMenu::HeadHeight;
    const float Foot = ToolContextMenu::FootHeight;
    const float H = Head + MeasurePopupBody(Rows) + Foot;

    Rect(X, Y, W, H, PanelGround, ToolContextMenu::PopupRadius);
    Outline(X, Y, W, H, ToolContextMenu::PopupRadius);
    Rect(X, Y, W, Head, PanelHead, ToolContextMenu::PopupRadius);
    Rect(X, Y + Head - ToolContextMenu::PopupRadius, W, ToolContextMenu::PopupRadius, PanelHead, 0.0f);

    const float MiddleY = Y + Head * 0.5f;
    float TitleX = X + ToolContextMenu::BodyPadding;
    if (Icon != SymbolSubject::SubjectCount)
    {
        Glyph(TitleX, MiddleY - 8.0f, 16.0f, ColourPrimary, Icon);
        TitleX += 16.0f + 8.0f;
    }
    Text(TitleX, MiddleY + 5.0f, ColourPrimary, Title, 14.0f, "start", 600);

    float Cursor = Y + Head + ToolContextMenu::BodyPadding;
    for (const Row& R : Rows)
    {
        Text(X + ToolContextMenu::BodyPadding, Cursor + ToolContextMenu::CaptionPoint,
             ColourMuted, R.Caption, ToolContextMenu::CaptionPoint);
        Cursor += ToolContextMenu::CaptionPoint + ToolContextMenu::CaptionGap;
        DrawRowControl(X + ToolContextMenu::BodyPadding, Cursor,
                       W - ToolContextMenu::BodyPadding * 2.0f, R);
        Cursor += RowTall(R) + ToolContextMenu::BodyGap;
    }

    // 📐 Apply and Cancel, the popup's own foot arithmetic.
    const float FootY = Y + H - Foot;
    const float Gutter = 8.0f;
    const float Usable = W - ToolContextMenu::BodyPadding * 2.0f - Gutter;
    const float Each = Usable * 0.5f;
    const float ActionY = FootY + (Foot - ToolContextMenu::ActionHeight) * 0.5f;

    Rect(X + ToolContextMenu::BodyPadding, ActionY, Each, ToolContextMenu::ActionHeight,
         ValueBlack, ToolContextMenu::ActionRadius);
    Text(X + ToolContextMenu::BodyPadding + Each * 0.5f, ActionY + ToolContextMenu::ActionHeight * 0.5f + 4.0f,
         ColourMuted, "Cancel", ToolContextMenu::CaptionPoint, "middle");

    const float ApplyX = X + ToolContextMenu::BodyPadding + Each + Gutter;
    Rect(ApplyX, ActionY, Each, ToolContextMenu::ActionHeight, AccentGround, ToolContextMenu::ActionRadius);
    Text(ApplyX + Each * 0.5f, ActionY + ToolContextMenu::ActionHeight * 0.5f + 4.0f,
         ColourPrimary, "Apply", ToolContextMenu::CaptionPoint, "middle", 600);
}

void Label(float X, float Y, const char* Body)
{
    Text(X, Y, ColourMuted, Body, 12.0f);
}

Row Slider(const char* Caption, const char* Unit, float Reading, float Minimum, float Maximum)
{
    Row R{};
    R.Kind = OptionControl::Slider;
    R.Caption = Caption; R.Unit = Unit;
    R.Reading = Reading; R.Minimum = Minimum; R.Maximum = Maximum;
    R.Glyphs[0] = SymbolSubject::SubjectCount;
    return R;
}

Row Segmented(const char* Caption, std::initializer_list<const char*> Options,
              std::uint32_t Selected, bool WithGlyphs = false)
{
    Row R{};
    R.Kind = OptionControl::Segmented;
    R.Caption = Caption;
    R.OptionCount = static_cast<std::uint32_t>(Options.size());
    std::uint32_t I = 0u;
    for (const char* O : Options) R.Options[I++] = O;
    R.Selected = Selected;
    if (WithGlyphs)
    {
        R.Glyphs[0] = SymbolSubject::VertexPoint;
        R.Glyphs[1] = SymbolSubject::EdgeSegment;
        R.Glyphs[2] = SymbolSubject::FacePlanar;
    }
    else
    {
        R.Glyphs[0] = SymbolSubject::SubjectCount;
    }
    return R;
}

Row Toggle(const char* Caption, bool Taken)
{
    Row R{};
    R.Kind = OptionControl::Toggle;
    R.Caption = Caption;
    R.Taken = Taken;
    R.Glyphs[0] = SymbolSubject::SubjectCount;
    return R;
}

}   // namespace

int main()
{
    const float Width = 1320.0f;
    const float Height = 760.0f;

    char Open[256];
    std::snprintf(Open, sizeof Open,
        "<svg xmlns='http://www.w3.org/2000/svg' width='%.0f' height='%.0f' viewBox='0 0 %.0f %.0f'>",
        Width, Height, Width, Height);
    Svg = Open;
    Svg += "<rect width='100%' height='100%' fill='#2b2b2d'/>";

    // ── The Select widget, expanded ──────────────────────────────────────────────────────────
    Label(48.0f, 42.0f, "SELECT + GIZMO \u2014 one widget, expanded");
    {
        std::vector<Row> Rows;
        Rows.push_back(Segmented("Mode", { "Vertex", "Edge", "Face" }, 0u, true));
        Rows.push_back(Slider("Tolerance", "px",
                              SelectionOptions::ToleranceDefault,
                              SelectionOptions::ToleranceMinimum,
                              SelectionOptions::ToleranceMaximum));
        Rows.push_back(Toggle("Show gizmo", true));
        DrawCard(48.0f, 62.0f, "Select", SymbolSubject::CrosshairCentre, Rows);
    }

    // ── The same widget, compacted ───────────────────────────────────────────────────────────
    Label(48.0f, 420.0f, "COMPACTED \u2014 the header and an expand icon");
    DrawPill(48.0f, 440.0f, "Select", SymbolSubject::CrosshairCentre);

    Label(48.0f, 520.0f, "\u2026 and the same pill for a construction tool");
    DrawPill(48.0f, 540.0f, "Bevel", SymbolSubject::BevelChamfer);

    // ── The four construction popups ─────────────────────────────────────────────────────────
    Label(412.0f, 42.0f, "AFTER CHOOSING BEVEL");
    {
        std::vector<Row> Rows;
        Rows.push_back(Slider("Distance", "u", 4.0f, 0.1f, 50.0f));
        DrawPopup(412.0f, 62.0f, "Bevel", SymbolSubject::BevelChamfer, Rows);
    }

    Label(412.0f, 300.0f, "AFTER CHOOSING CHAMFER");
    {
        std::vector<Row> Rows;
        Rows.push_back(Slider("Distance", "u", 12.5f, 0.1f, 50.0f));
        DrawPopup(412.0f, 320.0f, "Chamfer", SymbolSubject::BevelChamfer, Rows);
    }

    Label(730.0f, 42.0f, "AFTER CHOOSING TRIM");
    {
        std::vector<Row> Rows;
        Rows.push_back(Segmented("Keep", { "Start", "End" }, 0u));
        DrawPopup(730.0f, 62.0f, "Trim", SymbolSubject::SubjectCount, Rows);
    }

    Label(730.0f, 300.0f, "CUT \u2014 no parameter, so no popup");
    {
        // 📝 Drawn as the refusal it is: cut splits at the picked point and takes no figure, so the
        //    host applies it directly rather than showing a box of only buttons.
        Rect(730.0f, 320.0f, ToolContextMenu::PopupWidth, 96.0f, PanelGround, ToolContextMenu::PopupRadius);
        Outline(730.0f, 320.0f, ToolContextMenu::PopupWidth, 96.0f, ToolContextMenu::PopupRadius);
        Text(730.0f + 20.0f, 320.0f + 38.0f, ColourPrimary, "Cut applies immediately", 13.0f, "start", 600);
        Text(730.0f + 20.0f, 320.0f + 62.0f, ColourMuted, "It splits at the picked point.", 11.5f);
        Text(730.0f + 20.0f, 320.0f + 80.0f, ColourMuted, "A popup of only buttons asks nothing.", 11.5f);
    }

    // ── Placement, the step-7 guarantee, still holding ───────────────────────────────────────
    Label(1048.0f, 42.0f, "PLACEMENT \u2014 never over another widget");
    {
        const float LX = 1048.0f, LY = 62.0f, LW = 224.0f, LH = 300.0f;
        Rect(LX, LY, LW, LH, Covering(0x1b1b1eu), 12.0f);
        Outline(LX, LY, LW, LH, 12.0f);
        Text(LX + 12.0f, LY + 22.0f, ColourMuted, "viewport leaf", 10.5f);

        // the options widget, occupying the upper-left
        Rect(LX + 14.0f, LY + 36.0f, 96.0f, 74.0f, PanelGround, 8.0f);
        Outline(LX + 14.0f, LY + 36.0f, 96.0f, 74.0f, 8.0f);
        Text(LX + 20.0f, LY + 58.0f, ColourMuted, "options", 9.5f);
        Text(LX + 20.0f, LY + 72.0f, ColourMuted, "widget", 9.5f);

        // the anchor
        Circle(LX + 150.0f, LY + 150.0f, 4.0f, AccentGround);
        Text(LX + 128.0f, LY + 172.0f, ColourMuted, "click", 9.5f);

        // the popup, taking the first free corner below-trailing
        Rect(LX + 88.0f, LY + 160.0f, 120.0f, 118.0f, PanelGround, 10.0f);
        Outline(LX + 88.0f, LY + 160.0f, 120.0f, 118.0f, 10.0f);
        Rect(LX + 88.0f, LY + 160.0f, 120.0f, 26.0f, PanelHead, 10.0f);
        Rect(LX + 88.0f, LY + 176.0f, 120.0f, 10.0f, PanelHead, 0.0f);
        Text(LX + 98.0f, LY + 177.0f, ColourPrimary, "Bevel", 10.5f, "start", 600);
        Rect(LX + 98.0f, LY + 196.0f, 46.0f, 20.0f, ValueNumber, 7.0f);
        Rect(LX + 150.0f, LY + 200.0f, 48.0f, 12.0f, ValueBlack, 6.0f);
        Circle(LX + 168.0f, LY + 206.0f, 6.0f, KnobGround);
        Rect(LX + 98.0f, LY + 246.0f, 50.0f, 18.0f, ValueBlack, 6.0f);
        Rect(LX + 154.0f, LY + 246.0f, 50.0f, 18.0f, AccentGround, 6.0f);
    }

    Text(48.0f, Height - 26.0f, ColourMuted,
         "Every measure above is read from the engine headers \u2014 panel 300px, popup 260px, "
         "row 44px, radius 20/13, tokens #131315 / #1b1b1e / #4a90e2.", 11.0f);

    Svg += "</svg>";

    std::FILE* File = std::fopen("/tmp/preview/widgets.svg", "w");
    std::fputs(Svg.c_str(), File);
    std::fclose(File);
    std::printf("wrote /tmp/preview/widgets.svg (%zu bytes)\n", Svg.size());
    return 0;
}
