/*====================================================================================================================================
                                                      DRAWINGEXPORT.CPP
====================================================================================================================================*/
// 🧩 Drawing sheet export: SVG (millimetre viewBox, one group per layer) and DXF R12 (AC1009 ASCII: layers, line types, entities)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingExport.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <tuple>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int LayerCount = 10;

    struct LayerStyle
    {
        double      Width     = 0.25;                                                        // [mm] - Line width (ISO 128-2)
        const char* Dashes    = "";                                                          // [mm] - SVG dash pattern
        const char* LineType  = "CONTINUOUS";                                                // [-]  - DXF line type
        int         Colour    = 7;                                                           // [-]  - DXF colour index
    };

    [[nodiscard]] LayerStyle StyleOf(SheetLayer Layer)
    {
        switch (Layer)
        {
        case SheetLayer::Visible:        return { 0.5, "", "CONTINUOUS", 7 };
        case SheetLayer::Hidden:         return { 0.25, "3 1.5", "HIDDEN", 5 };
        case SheetLayer::Tangent:        return { 0.18, "", "CONTINUOUS", 8 };
        case SheetLayer::SectionOutline: return { 0.5, "", "CONTINUOUS", 7 };
        case SheetLayer::Hatch:          return { 0.18, "", "CONTINUOUS", 8 };
        case SheetLayer::Dimension:      return { 0.25, "", "CONTINUOUS", 3 };
        case SheetLayer::Centre:         return { 0.18, "8 1.5 1 1.5", "CENTER", 1 };
        case SheetLayer::CuttingPlane:   return { 0.35, "8 1.5 1 1.5", "CENTER", 1 };
        case SheetLayer::Border:         return { 0.7, "", "CONTINUOUS", 7 };
        case SheetLayer::Title:          return { 0.35, "", "CONTINUOUS", 7 };
        }
        return {};
    }

    void Append(std::string& Out, const char* Pattern, double First, double Second)
    {
        char Text[96];
        std::snprintf(Text, sizeof(Text), Pattern, First, Second);
        Out += Text;
    }

    [[nodiscard]] std::string EscapeXml(const std::string& Text)
    {
        std::string Out;
        for (const char Character : Text)
        {
            if (Character == '&') Out += "&amp;";
            else if (Character == '<') Out += "&lt;";
            else if (Character == '>') Out += "&gt;";
            else if (Character == '"') Out += "&quot;";
            else Out += Character;
        }
        return Out;
    }

    // DXF R12 is 8-bit: the diameter, degree and plus-minus signs become their control codes.
    [[nodiscard]] std::string EncodeDxfText(const std::string& Text)
    {
        std::string Out;
        for (size_t Index = 0; Index < Text.size(); ++Index)
        {
            if (Text.compare(Index, 3, "⌀") == 0) { Out += "%%c"; Index += 2; }
            else if (Text.compare(Index, 2, "°") == 0) { Out += "%%d"; Index += 1; }
            else if (Text.compare(Index, 2, "±") == 0) { Out += "%%p"; Index += 1; }
            else Out += Text[Index];
        }
        return Out;
    }

    void Group(std::string& Out, int Code, const std::string& Value)
    {
        char Text[16];
        std::snprintf(Text, sizeof(Text), "%3d\n", Code);
        Out += Text;
        Out += Value;
        Out += '\n';
    }

    void Group(std::string& Out, int Code, double Value)
    {
        char Text[64];
        std::snprintf(Text, sizeof(Text), "%.15g", Value);
        Group(Out, Code, std::string(Text));
    }

    void Group(std::string& Out, int Code, int Value) { Group(Out, Code, std::to_string(Value)); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string WriteDrawingSvg(const DrawingSheet& Sheet)
{
    std::string Out;
    Out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    Append(Out, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.6fmm\" height=\"%.6fmm\"", Sheet.Width, Sheet.Height);
    Append(Out, " viewBox=\"0 0 %.6f %.6f\">\n", Sheet.Width, Sheet.Height);
    Append(Out, "<rect x=\"0\" y=\"0\" width=\"%.6f\" height=\"%.6f\" fill=\"#ffffff\"/>\n", Sheet.Width, Sheet.Height);
    const auto Point = [&](Vector2 At) { char Text[64]; std::snprintf(Text, sizeof(Text), "%.6f,%.6f", At.X, Sheet.Height - At.Y); return std::string(Text); };
    for (int Index = 0; Index < LayerCount; ++Index)
    {
        const SheetLayer Layer = static_cast<SheetLayer>(Index);
        const LayerStyle Style = StyleOf(Layer);
        Out += std::string("<g id=\"") + SheetLayerLabel(Layer) + "\" stroke=\"#000000\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
        Append(Out, " stroke-width=\"%.2f\"", Style.Width, 0.0);
        if (*Style.Dashes) Out += std::string(" stroke-dasharray=\"") + Style.Dashes + "\"";
        Out += ">\n";
        for (const SheetPolyline& Line : Sheet.Lines)
        {
            if (Line.Layer != Layer || Line.Points.size() < 2) continue;
            Out += "<polyline points=\"";
            for (size_t Corner = 0; Corner < Line.Points.size(); ++Corner)
            {
                if (Corner) Out += ' ';
                Out += Point(Line.Points[Corner]);
            }
            Out += "\"/>\n";
        }
        for (const SheetSolid& Solid : Sheet.Solids)
        {
            if (Solid.Layer != Layer) continue;
            Out += "<polygon fill=\"#000000\" stroke=\"none\" points=\"";
            for (size_t Corner = 0; Corner < 3; ++Corner)
            {
                if (Corner) Out += ' ';
                Out += Point(Solid.Corners[Corner]);
            }
            Out += "\"/>\n";
        }
        for (const SheetText& Text : Sheet.Texts)
        {
            if (Text.Layer != Layer) continue;
            const char* Anchor = Text.Anchor == SheetTextAnchor::Start ? "start" : Text.Anchor == SheetTextAnchor::Middle ? "middle" : "end";
            Out += "<text fill=\"#000000\" stroke=\"none\" font-family=\"DejaVu Sans Mono, Consolas, monospace\"";
            Append(Out, " x=\"%.6f\" y=\"%.6f\"", Text.Position.X, Sheet.Height - Text.Position.Y);
            Append(Out, " font-size=\"%.6f\"", Text.Height / 0.72, 0.0);                     // Cap height ≈ 0.72 em
            Out += std::string(" text-anchor=\"") + Anchor + "\"";
            if (Text.Angle != 0.0)
            {
                char Rotation[128];
                std::snprintf(Rotation, sizeof(Rotation), " transform=\"rotate(%.6f %.6f %.6f)\"", -Text.Angle * 180.0 / std::numbers::pi, Text.Position.X, Sheet.Height - Text.Position.Y);
                Out += Rotation;
            }
            Out += '>';
            Out += EscapeXml(Text.Content);
            Out += "</text>\n";
        }
        Out += "</g>\n";
    }
    Out += "</svg>\n";
    return Out;
}

std::string WriteDrawingDxf(const DrawingSheet& Sheet)
{
    std::string Out;
    Group(Out, 0, std::string("SECTION"));
    Group(Out, 2, std::string("HEADER"));
    Group(Out, 9, std::string("$ACADVER"));
    Group(Out, 1, std::string("AC1009"));
    for (const auto& [Name, X, Y] : { std::tuple{ "$EXTMIN", 0.0, 0.0 }, std::tuple{ "$EXTMAX", Sheet.Width, Sheet.Height }, std::tuple{ "$LIMMIN", 0.0, 0.0 }, std::tuple{ "$LIMMAX", Sheet.Width, Sheet.Height } })
    {
        Group(Out, 9, std::string(Name));
        Group(Out, 10, X);
        Group(Out, 20, Y);
    }
    Group(Out, 0, std::string("ENDSEC"));

    Group(Out, 0, std::string("SECTION"));
    Group(Out, 2, std::string("TABLES"));
    Group(Out, 0, std::string("TABLE"));
    Group(Out, 2, std::string("LTYPE"));
    Group(Out, 70, 3);
    struct Pattern { const char* Name; const char* Description; std::vector<double> Dashes; };
    for (const Pattern& Type : { Pattern{ "CONTINUOUS", "Solid line", {} }, Pattern{ "HIDDEN", "__ __ __", { 3.0, -1.5 } }, Pattern{ "CENTER", "____ _ ____", { 8.0, -1.5, 1.0, -1.5 } } })
    {
        Group(Out, 0, std::string("LTYPE"));
        Group(Out, 2, std::string(Type.Name));
        Group(Out, 70, 0);
        Group(Out, 3, std::string(Type.Description));
        Group(Out, 72, 65);
        Group(Out, 73, static_cast<int>(Type.Dashes.size()));
        double Total = 0.0;
        for (const double Dash : Type.Dashes) Total += std::fabs(Dash);
        Group(Out, 40, Total);
        for (const double Dash : Type.Dashes) Group(Out, 49, Dash);
    }
    Group(Out, 0, std::string("ENDTAB"));
    Group(Out, 0, std::string("TABLE"));
    Group(Out, 2, std::string("LAYER"));
    Group(Out, 70, LayerCount);
    for (int Index = 0; Index < LayerCount; ++Index)
    {
        const SheetLayer Layer = static_cast<SheetLayer>(Index);
        Group(Out, 0, std::string("LAYER"));
        Group(Out, 2, std::string(SheetLayerLabel(Layer)));
        Group(Out, 70, 0);
        Group(Out, 62, StyleOf(Layer).Colour);
        Group(Out, 6, std::string(StyleOf(Layer).LineType));
    }
    Group(Out, 0, std::string("ENDTAB"));
    Group(Out, 0, std::string("TABLE"));
    Group(Out, 2, std::string("STYLE"));
    Group(Out, 70, 1);
    Group(Out, 0, std::string("STYLE"));
    Group(Out, 2, std::string("STANDARD"));
    Group(Out, 70, 0);
    Group(Out, 40, 0.0);
    Group(Out, 41, 0.6);
    Group(Out, 50, 0.0);
    Group(Out, 71, 0);
    Group(Out, 42, 3.5);
    Group(Out, 3, std::string("txt"));
    Group(Out, 4, std::string(""));
    Group(Out, 0, std::string("ENDTAB"));
    Group(Out, 0, std::string("ENDSEC"));

    Group(Out, 0, std::string("SECTION"));
    Group(Out, 2, std::string("ENTITIES"));
    for (const SheetPolyline& Line : Sheet.Lines)
    {
        if (Line.Points.size() < 2) continue;
        const std::string Layer = SheetLayerLabel(Line.Layer);
        if (Line.Points.size() == 2)
        {
            Group(Out, 0, std::string("LINE"));
            Group(Out, 8, Layer);
            Group(Out, 10, Line.Points[0].X); Group(Out, 20, Line.Points[0].Y); Group(Out, 30, 0.0);
            Group(Out, 11, Line.Points[1].X); Group(Out, 21, Line.Points[1].Y); Group(Out, 31, 0.0);
            continue;
        }
        Group(Out, 0, std::string("POLYLINE"));
        Group(Out, 8, Layer);
        Group(Out, 66, 1);
        Group(Out, 10, 0.0); Group(Out, 20, 0.0); Group(Out, 30, 0.0);
        Group(Out, 70, 0);
        for (const Vector2& Point : Line.Points)
        {
            Group(Out, 0, std::string("VERTEX"));
            Group(Out, 8, Layer);
            Group(Out, 10, Point.X); Group(Out, 20, Point.Y); Group(Out, 30, 0.0);
        }
        Group(Out, 0, std::string("SEQEND"));
        Group(Out, 8, Layer);
    }
    for (const SheetSolid& Solid : Sheet.Solids)
    {
        Group(Out, 0, std::string("SOLID"));
        Group(Out, 8, std::string(SheetLayerLabel(Solid.Layer)));
        for (int Corner = 0; Corner < 4; ++Corner)
        {
            const Vector2 Point = Solid.Corners[static_cast<size_t>(std::min(Corner, 2))];
            Group(Out, 10 + Corner, Point.X); Group(Out, 20 + Corner, Point.Y); Group(Out, 30 + Corner, 0.0);
        }
    }
    for (const SheetText& Text : Sheet.Texts)
    {
        Group(Out, 0, std::string("TEXT"));
        Group(Out, 8, std::string(SheetLayerLabel(Text.Layer)));
        Group(Out, 10, Text.Position.X); Group(Out, 20, Text.Position.Y); Group(Out, 30, 0.0);
        Group(Out, 40, Text.Height);
        Group(Out, 1, EncodeDxfText(Text.Content));
        Group(Out, 50, Text.Angle * 180.0 / std::numbers::pi);
        Group(Out, 7, std::string("STANDARD"));
        const int Justify = Text.Anchor == SheetTextAnchor::Start ? 0 : Text.Anchor == SheetTextAnchor::Middle ? 1 : 2;
        Group(Out, 72, Justify);
        if (Justify != 0) { Group(Out, 11, Text.Position.X); Group(Out, 21, Text.Position.Y); Group(Out, 31, 0.0); }
    }
    Group(Out, 0, std::string("ENDSEC"));
    Group(Out, 0, std::string("EOF"));
    return Out;
}

std::string ExportDrawingFile(const DrawingSheet& Sheet, const std::string& Path)
{
    if (!Sheet.Refusal.empty()) return "Export: the sheet was refused (" + Sheet.Refusal + ")";
    const auto Ends = [&](const char* Suffix) { const std::string Tail = Suffix; return Path.size() >= Tail.size() && Path.compare(Path.size() - Tail.size(), Tail.size(), Tail) == 0; };
    std::string Content;
    if (Ends(".svg") || Ends(".SVG")) Content = WriteDrawingSvg(Sheet);
    else if (Ends(".dxf") || Ends(".DXF")) Content = WriteDrawingDxf(Sheet);
    else return "Export: unknown drawing format (use .svg or .dxf)";
    std::ofstream File(ResolveUtf8Path(Path), std::ios::binary);
    if (!File) return "Export: cannot write " + Path;
    File << Content;
    return File.good() ? std::string() : "Export: write failed for " + Path;
}

} // namespace Frontier::GeometrySpace::Parametric
