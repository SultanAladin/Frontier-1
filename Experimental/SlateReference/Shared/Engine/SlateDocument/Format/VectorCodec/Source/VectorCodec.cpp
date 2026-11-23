//============================================================================================================================================
//                                                             VECTORCODEC.CPP
//============================================================================================================================================
// 🧩 `10` §1 — vector streams translated into `52`'s accepted subset, with every refusal named and positioned.

#include "SlateDocument/Format/VectorCodec/Api/VectorCodec.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Slate
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REFUSED SET
//------------------------------------------------------------------------------------------------------------------------

// 🔴 `00` §5.2's rejected set, named element by element. Each is rejected rather than approximated: a vector
//    source that silently loses content is worse than one that refuses it, because the artist attributes the
//    loss to their own file and goes looking for a mistake they did not make.
struct RejectedElement
{
    const char*    Spelling = "";                              // [-] - the element as the source spells it
    RefusalReason  Reason   = RefusalReason::ContentUnsupported; // [-] - what the refusal reports
    const char*    Detail   = "";                              // [-] - static text, never allocated
};

const RejectedElement RejectedElements[] =
{
    { "filter",        RefusalReason::ContentUnsupported, "effect operations are outside the accepted subset — `00` §5.2" },
    { "clipPath",      RefusalReason::ContentUnsupported, "clipping is outside the accepted subset — `00` §5.2"           },
    { "mask",          RefusalReason::ContentUnsupported, "masking is outside the accepted subset — `00` §5.2"            },
    { "script",        RefusalReason::ContentUnsupported, "script is outside the accepted subset — `00` §5.2"             },
    { "animate",       RefusalReason::ContentUnsupported, "animation is outside the accepted subset — `00` §5.2"          },
    { "animateMotion", RefusalReason::ContentUnsupported, "animation is outside the accepted subset — `00` §5.2"          },
    { "animateTransform", RefusalReason::ContentUnsupported, "animation is outside the accepted subset — `00` §5.2"       },
    { "image",         RefusalReason::ContentUnsupported, "embedded raster content is outside the accepted subset — `00` §5.2" },
    { "text",          RefusalReason::ContentUnsupported, "text is resolved through `TypefaceCodec`, never as an outline here" },
    { "use",           RefusalReason::ContentUnsupported, "an instanced reference is not resolved by a translation"       },
};

// 📝 A stroked element is named too. `52` §2 converts strokes at intake, and intake is above this line — so a
//    stroke reaching here is geometry the artist will not see unless they are told about it.
const char* const StrokedDetail = "a stroke is converted at intake, not translated — `52` §2";

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SCANNING
//------------------------------------------------------------------------------------------------------------------------

bool Whitespace(char Carried)
{
    return Carried == ' ' || Carried == '\t' || Carried == '\r' || Carried == '\n' || Carried == ',';
}

/// 🧩 Advances past every separator, so a run of commands may be spaced however the source spaced it.
std::size_t SkipSeparators(const std::string& Reading, std::size_t Index)
{
    while (Index < Reading.size() && Whitespace(Reading[Index])) { ++Index; }

    return Index;
}

/// 🧩 Reads one real from the path data, reporting whether one was there to read.
/// note  📝 Read here rather than through the standard conversions because a path's numbers run together
///        without separators — "1.5.5" is two numbers — and a conversion that consumed as much as it could
///        would take both. The scan below stops at the second decimal point, which is what the grammar means.
bool ReadCoordinate(const std::string& Reading, std::size_t& Index, double& Produced)
{
    Index = SkipSeparators(Reading, Index);

    const std::size_t Beginning = Index;

    if (Index < Reading.size() && (Reading[Index] == '+' || Reading[Index] == '-')) { ++Index; }

    bool PointSeen = false;
    bool DigitSeen = false;

    while (Index < Reading.size())
    {
        const char Carried = Reading[Index];

        if (Carried >= '0' && Carried <= '9')
        {
            DigitSeen = true;
            ++Index;
        }
        else if (Carried == '.' && !PointSeen)
        {
            PointSeen = true;
            ++Index;
        }
        else if ((Carried == 'e' || Carried == 'E') && DigitSeen)
        {
            ++Index;

            if (Index < Reading.size() && (Reading[Index] == '+' || Reading[Index] == '-')) { ++Index; }
        }
        else
        {
            break;
        }
    }

    if (!DigitSeen)
    {
        Index = Beginning;
        return false;
    }

    Produced = std::strtod(Reading.substr(Beginning, Index - Beginning).c_str(), nullptr);

    return true;
}

/// 🧩 Reads one flag — a single digit, which the arc grammar writes without a separator after it.
bool ReadFlag(const std::string& Reading, std::size_t& Index, bool& Produced)
{
    Index = SkipSeparators(Reading, Index);

    if (Index >= Reading.size()) { return false; }

    const char Carried = Reading[Index];

    if (Carried != '0' && Carried != '1') { return false; }

    Produced = Carried == '1';
    ++Index;

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE PATH RUN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Holds the state one run of path data is translated against — where it is, and what it last curved with.
struct PathReading
{
    PlanarPosition  Position       = {};      // [-] - the current position, in the source's own space
    PlanarPosition  Beginning      = {};      // [-] - where the current subpath started, for a close
    PlanarPosition  LastControl    = {};      // [-] - reflected by a smooth continuation
    bool            CubicPreceding = false;   // [-] - the preceding segment was a cubic
    bool            QuadraticPreceding = false; // [-] - the preceding segment was a quadratic
};

/// 🧩 Translates one `d` attribute into the closed and open paths it declares.
/// note  🔴 A subpath that never closes stays open. `52` §1: closing it silently moves which side of it the
///        interior is on, and the artist reads that as the fill having moved rather than the path having been
///        altered by something they cannot see.
void TranslatePathData(const std::string& PathData, FillRule Rule, std::vector<OutlinePath>& Appending)
{
    PathReading  Reading;
    OutlinePath  Constructing;
    bool         PathOccupied = false;
    std::size_t  Index      = 0u;
    char         Command      = '\0';

    const auto SealPath = [&]()
    {
        if (PathOccupied && !Constructing.Segments.empty())
        {
            Constructing.Rule = Rule;
            Appending.push_back(Constructing);
        }

        Constructing = OutlinePath{};
        PathOccupied = false;
    };

    while (Index < PathData.size())
    {
        Index = SkipSeparators(PathData, Index);

        if (Index >= PathData.size()) { break; }

        const char Carried = PathData[Index];

        if ((Carried >= 'A' && Carried <= 'Z') || (Carried >= 'a' && Carried <= 'z'))
        {
            Command = Carried;
            ++Index;
        }
        else if (Command == '\0')
        {
            break;
        }
        else if (Command == 'M')
        {
            Command = 'L';
        }
        else if (Command == 'm')
        {
            Command = 'l';
        }

        const bool  Relative = Command >= 'a' && Command <= 'z';
        const char  Absolute = Relative ? static_cast<char>(Command - 'a' + 'A') : Command;

        if (Absolute == 'Z')
        {
            if (PathOccupied && !Constructing.Segments.empty())
            {
                Constructing.ClosedRun = true;
                Constructing.Rule      = Rule;
                Appending.push_back(Constructing);
            }

            Constructing        = OutlinePath{};
            PathOccupied        = false;
            Reading.Position    = Reading.Beginning;
            Reading.CubicPreceding     = false;
            Reading.QuadraticPreceding = false;

            continue;
        }

        if (Absolute == 'M')
        {
            double XCoordinate  = 0.0;
            double YCoordinate = 0.0;

            if (!ReadCoordinate(PathData, Index, XCoordinate) || !ReadCoordinate(PathData, Index, YCoordinate))
            {
                break;
            }

            SealPath();

            Reading.Position.PositionX = Relative ? Reading.Position.PositionX + XCoordinate  : XCoordinate;
            Reading.Position.PositionY = Relative ? Reading.Position.PositionY + YCoordinate : YCoordinate;
            Reading.Beginning          = Reading.Position;

            Constructing        = OutlinePath{};
            Constructing.Origin = Reading.Position;
            PathOccupied        = true;

            Reading.CubicPreceding     = false;
            Reading.QuadraticPreceding = false;

            continue;
        }

        if (!PathOccupied)
        {
            // 📝 A run that curves before it moves has no origin to curve from. The source's own beginning is
            //    the origin in that case, which is what the grammar declares the current position to be.
            Constructing        = OutlinePath{};
            Constructing.Origin = Reading.Position;
            Reading.Beginning   = Reading.Position;
            PathOccupied        = true;
        }

        PathSegment  Placed;
        bool         SegmentRead = false;

        if (Absolute == 'L' || Absolute == 'H' || Absolute == 'V')
        {
            double XCoordinate  = Reading.Position.PositionX;
            double YCoordinate = Reading.Position.PositionY;

            if (Absolute == 'L')
            {
                double ReadX  = 0.0;
                double ReadY = 0.0;

                if (ReadCoordinate(PathData, Index, ReadX) && ReadCoordinate(PathData, Index, ReadY))
                {
                    XCoordinate  = Relative ? Reading.Position.PositionX + ReadX  : ReadX;
                    YCoordinate = Relative ? Reading.Position.PositionY + ReadY : ReadY;
                    SegmentRead    = true;
                }
            }
            else if (Absolute == 'H')
            {
                double ReadX = 0.0;

                if (ReadCoordinate(PathData, Index, ReadX))
                {
                    XCoordinate = Relative ? Reading.Position.PositionX + ReadX : ReadX;
                    SegmentRead   = true;
                }
            }
            else
            {
                double ReadY = 0.0;

                if (ReadCoordinate(PathData, Index, ReadY))
                {
                    YCoordinate = Relative ? Reading.Position.PositionY + ReadY : ReadY;
                    SegmentRead    = true;
                }
            }

            if (SegmentRead)
            {
                Placed.Subject  = SegmentSubject::Line;
                Placed.Terminus = { XCoordinate, YCoordinate };

                Reading.CubicPreceding     = false;
                Reading.QuadraticPreceding = false;
            }
        }
        else if (Absolute == 'C' || Absolute == 'S')
        {
            PlanarPosition  FirstControl  = Reading.Position;
            PlanarPosition  SecondControl = {};
            PlanarPosition  Terminus      = {};

            bool Occupied = true;

            if (Absolute == 'C')
            {
                double FirstX = 0.0, FirstY = 0.0;

                Occupied = ReadCoordinate(PathData, Index, FirstX) && ReadCoordinate(PathData, Index, FirstY);

                FirstControl = { Relative ? Reading.Position.PositionX + FirstX  : FirstX,
                                 Relative ? Reading.Position.PositionY + FirstY : FirstY };
            }
            else
            {
                // 📝 A smooth continuation reflects the preceding control through the current position. Where
                //    nothing cubic preceded it, the grammar declares the current position itself — so a smooth
                //    curve opening a run is a straight departure rather than an invented curvature.
                FirstControl = Reading.CubicPreceding
                             ? PlanarPosition{ 2.0 * Reading.Position.PositionX - Reading.LastControl.PositionX,
                                               2.0 * Reading.Position.PositionY - Reading.LastControl.PositionY }
                             : Reading.Position;
            }

            double SecondX = 0.0, SecondY = 0.0, TerminusX = 0.0, TerminusY = 0.0;

            Occupied = Occupied
                    && ReadCoordinate(PathData, Index, SecondX)   && ReadCoordinate(PathData, Index, SecondY)
                    && ReadCoordinate(PathData, Index, TerminusX) && ReadCoordinate(PathData, Index, TerminusY);

            if (Occupied)
            {
                SecondControl = { Relative ? Reading.Position.PositionX + SecondX  : SecondX,
                                  Relative ? Reading.Position.PositionY + SecondY : SecondY };
                Terminus      = { Relative ? Reading.Position.PositionX + TerminusX  : TerminusX,
                                  Relative ? Reading.Position.PositionY + TerminusY : TerminusY };

                Placed.Subject       = SegmentSubject::Cubic;
                Placed.FirstControl  = FirstControl;
                Placed.SecondControl = SecondControl;
                Placed.Terminus      = Terminus;

                Reading.LastControl        = SecondControl;
                Reading.CubicPreceding     = true;
                Reading.QuadraticPreceding = false;

                SegmentRead = true;
            }
        }
        else if (Absolute == 'Q' || Absolute == 'T')
        {
            PlanarPosition  Control  = Reading.Position;
            PlanarPosition  Terminus = {};

            bool Occupied = true;

            if (Absolute == 'Q')
            {
                double ControlX = 0.0, ControlY = 0.0;

                Occupied = ReadCoordinate(PathData, Index, ControlX) && ReadCoordinate(PathData, Index, ControlY);

                Control = { Relative ? Reading.Position.PositionX + ControlX  : ControlX,
                            Relative ? Reading.Position.PositionY + ControlY : ControlY };
            }
            else
            {
                Control = Reading.QuadraticPreceding
                        ? PlanarPosition{ 2.0 * Reading.Position.PositionX - Reading.LastControl.PositionX,
                                          2.0 * Reading.Position.PositionY - Reading.LastControl.PositionY }
                        : Reading.Position;
            }

            double TerminusX = 0.0, TerminusY = 0.0;

            Occupied = Occupied && ReadCoordinate(PathData, Index, TerminusX)
                                && ReadCoordinate(PathData, Index, TerminusY);

            if (Occupied)
            {
                Terminus = { Relative ? Reading.Position.PositionX + TerminusX  : TerminusX,
                             Relative ? Reading.Position.PositionY + TerminusY : TerminusY };

                Placed.Subject      = SegmentSubject::Quadratic;
                Placed.FirstControl = Control;
                Placed.Terminus     = Terminus;

                Reading.LastControl        = Control;
                Reading.CubicPreceding     = false;
                Reading.QuadraticPreceding = true;

                SegmentRead = true;
            }
        }
        else if (Absolute == 'A')
        {
            double RadiusX = 0.0, RadiusY = 0.0, Rotation = 0.0;
            double TerminusX = 0.0, TerminusY = 0.0;
            bool   LargeArc = false, Sweep = false;

            const bool Occupied = ReadCoordinate(PathData, Index, RadiusX)
                               && ReadCoordinate(PathData, Index, RadiusY)
                               && ReadCoordinate(PathData, Index, Rotation)
                               && ReadFlag(PathData, Index, LargeArc)
                               && ReadFlag(PathData, Index, Sweep)
                               && ReadCoordinate(PathData, Index, TerminusX)
                               && ReadCoordinate(PathData, Index, TerminusY);

            if (Occupied)
            {
                Placed.Subject         = SegmentSubject::Arc;
                Placed.RadiusX     = RadiusX;
                Placed.RadiusY    = RadiusY;
                Placed.Rotation        = Rotation;
                Placed.LargeArcEnabled = LargeArc;
                Placed.SweepEnabled    = Sweep;
                Placed.Terminus        = { Relative ? Reading.Position.PositionX + TerminusX  : TerminusX,
                                           Relative ? Reading.Position.PositionY + TerminusY : TerminusY };

                Reading.CubicPreceding     = false;
                Reading.QuadraticPreceding = false;

                SegmentRead = true;
            }
        }

        if (!SegmentRead)
        {
            break;
        }

        Constructing.Segments.push_back(Placed);
        Reading.Position = Placed.Terminus;
    }

    SealPath();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ELEMENTS AND ATTRIBUTES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Reads one attribute's value out of an element's own text, empty where the element declares none.
std::string AttributeValue(const std::string& Element, const char* Attribute)
{
    const std::string  Wanted   = std::string(Attribute) + "=";
    std::size_t        Index  = Element.find(Wanted);

    while (Index != std::string::npos)
    {
        // 📝 The preceding character must be a separator, so `fill` does not match inside `fill-rule`.
        const bool Delimited = Index == 0u || Whitespace(Element[Index - 1u]);

        if (Delimited)
        {
            std::size_t Beginning = Index + Wanted.size();

            if (Beginning < Element.size() && (Element[Beginning] == '"' || Element[Beginning] == '\''))
            {
                const char        Quoting = Element[Beginning];
                const std::size_t Ending  = Element.find(Quoting, Beginning + 1u);

                if (Ending != std::string::npos)
                {
                    return Element.substr(Beginning + 1u, Ending - Beginning - 1u);
                }
            }
        }

        Index = Element.find(Wanted, Index + 1u);
    }

    return std::string();
}

/// 🧩 Whether an element's opening tag names one spelling.
bool ElementNamed(const std::string& Element, const char* Spelling)
{
    const std::size_t Spanned = std::strlen(Spelling);

    if (Element.size() < Spanned) { return false; }

    if (Element.compare(0u, Spanned, Spelling) != 0) { return false; }

    if (Element.size() == Spanned) { return true; }

    const char Following = Element[Spanned];

    return Whitespace(Following) || Following == '/' || Following == '>';
}

/// 🧩 Translates one whole vector source, whichever route it arrived by.
Deliver<DecodedOutline> TranslateSource(const std::string& Source)
{
    DecodedOutline Produced;

    std::size_t Index = 0u;

    while (Index < Source.size())
    {
        const std::size_t Opening = Source.find('<', Index);

        if (Opening == std::string::npos) { break; }

        const std::size_t Closing = Source.find('>', Opening);

        if (Closing == std::string::npos) { break; }

        const std::string Element = Source.substr(Opening + 1u, Closing - Opening - 1u);

        Index = Closing + 1u;

        if (Element.empty() || Element[0] == '/' || Element[0] == '?' || Element[0] == '!') { continue; }

        // 🔴 `52` §2: a refusal names the construct **and the position in the source**. "Unsupported" with no
        //    position sends the artist to search a file they did not write.
        bool Rejected = false;

        for (const RejectedElement& Refusing : RejectedElements)
        {
            if (ElementNamed(Element, Refusing.Spelling))
            {
                RejectedConstruct Recording;
                Recording.Construct     = Refusing.Spelling;
                Recording.SourceIndex = static_cast<std::uint32_t>(Opening);
                Recording.Declining     = { Refusing.Reason, Refusing.Detail };

                Produced.Rejected.push_back(Recording);

                Rejected = true;
                break;
            }
        }

        if (Rejected || !ElementNamed(Element, "path")) { continue; }

        const std::string PathData = AttributeValue(Element, "d");

        if (PathData.empty()) { continue; }

        const std::string  DeclaredRule = AttributeValue(Element, "fill-rule");
        const FillRule     Rule         = DeclaredRule == "evenodd" ? FillRule::EvenOdd : FillRule::NonZero;

        const std::string  Stroked = AttributeValue(Element, "stroke");

        if (!Stroked.empty() && Stroked != "none")
        {
            RejectedConstruct Recording;
            Recording.Construct     = "stroke";
            Recording.SourceIndex = static_cast<std::uint32_t>(Opening);
            Recording.Declining     = { RefusalReason::ContentUnsupported, StrokedDetail };

            Produced.Rejected.push_back(Recording);
        }

        TranslatePathData(PathData, Rule, Produced.Declared.Paths);
    }

    if (Produced.Declared.Paths.empty())
    {
        return Deliver<DecodedOutline>::Refuse(
            { RefusalReason::ContentUnsupported, "the vector stream declares no path the accepted subset takes" });
    }

    // 🔴 `36` §1: no colour is declared here. A vector source's own fill is a presentation the artist may
    //    replace, and a colour without its space is a number three subsystems each interpret differently.
    Produced.Declared.ColourDeclared = false;

    return Deliver<DecodedOutline>::Result(Produced);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<DecodedOutline> Translate(const std::vector<std::uint8_t>& Stream, const std::string& OriginPath)
{
    if (Stream.empty())
    {
        return Deliver<DecodedOutline>::Refuse(
            { RefusalReason::ContentUnsupported, "a vector stream of no bytes carries no outline" });
    }

    const std::string Source(reinterpret_cast<const char*>(Stream.data()), Stream.size());

    Deliver<DecodedOutline> Produced = TranslateSource(Source);

    if (Produced.Resolved)
    {
        Produced.Delivered.Declared.OriginPath = OriginPath;
    }

    return Produced;
}

Deliver<DecodedOutline> TranslateText(const std::string& SourceText)
{
    if (SourceText.empty())
    {
        return Deliver<DecodedOutline>::Refuse(
            { RefusalReason::ContentUnsupported, "a supplied vector source of no text carries no outline" });
    }

    Deliver<DecodedOutline> Produced = TranslateSource(SourceText);

    // 🔴 `52` §1: the text is retained, because there is no file to re-read. A source whose only copy was a
    //    clipboard is unrecoverable after a reopen, and the artist reads that as the document having lost
    //    their work rather than as the source never having been storable in the first place.
    if (Produced.Resolved)
    {
        Produced.Delivered.Declared.SourceText = SourceText;
    }

    return Produced;
}

}   // namespace Slate
