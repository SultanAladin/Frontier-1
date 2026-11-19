/*====================================================================================================================================
                                                       STEPREADING.CPP
====================================================================================================================================*/
// 🧩 STEP reader: ISO 10303-21 scan → instances → kernel carriers in model units → one blueprint per solid → AssembleBody and
//    AuditBody in a scratch partition → the same blueprints assembled into the caller's partition when every body is sound

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Exchange/StepExchange.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Precision/PrecisionSpecification.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL — PART 21 SCAN
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr double FullCircle = 6.283185307179586476925;                                       // [rad] - 2π

// Unit to rounding → as read; otherwise normalised (the writer's rule, so a file read and written again is unchanged).
Vector3 KeepUnit(Vector3 Value)
{
    return std::fabs(Dot(Value, Value) - 1.0) <= 4.0e-16 ? Value : Normalize(Value);
}

enum class StepCategory : uint8_t
{
    Omitted,                                                                                 // $
    Derived,                                                                                 // *
    Number,                                                                                  // 12, -1.5E-3
    Text,                                                                                    // 'string'
    Enumeration,                                                                             // .T.
    Reference,                                                                               // #12
    List,                                                                                    // ( … )
    Typed                                                                                    // LENGTH_MEASURE(1.)
};

struct StepParameter
{
    StepCategory                   Kind      = StepCategory::Omitted;                                // [-]
    double                     Number    = 0.0;                                              // [-] - Number
    int                        Reference = 0;                                                // [-] - Reference
    std::string                Text      = {};                                               // [-] - Text, Enumeration, Typed name
    std::vector<StepParameter> List      = {};                                               // [-] - List items, Typed arguments
};

// One partial entity: a simple instance has one, a complex instance ( A(…) B(…) … ) several.
struct StepPartial
{
    std::string                Name       = {};                                              // [-] - Upper-case entity name
    std::vector<StepParameter> Parameters = {};                                              // [-]
};

struct StepInstance
{
    std::vector<StepPartial> Partials = {};                                                  // [-]
};

struct StepFile
{
    std::unordered_map<int, StepInstance> Instances = {};                                    // [-] - By #n
    std::string                           Refusal   = {};                                    // [-]
};

class PartScanner
{
public:
    explicit PartScanner(const std::string& Text) : Text(Text) {}

    StepFile Scan()
    {
        StepFile Out;
        if (Text.find("ISO-10303-21") == std::string::npos) { Out.Refusal = "not an ISO 10303-21 exchange structure (no ISO-10303-21 header)"; return Out; }
        size_t Search = 0;
        bool   Found  = false;
        while (true)
        {
            const size_t Section = FindKeyword("DATA", Search);
            if (Section == std::string::npos) break;
            Found = true;
            At    = Section + 4;
            SkipBlank();
            if (Peek() == '(')                                                                // AP242 named data sections
            {
                std::vector<StepParameter> Ignored;
                if (!ScanList(Ignored)) { Out.Refusal = Failure("malformed DATA section header"); return Out; }
                SkipBlank();
            }
            if (Peek() != ';') { Out.Refusal = Failure("expected ';' after DATA"); return Out; }
            ++At;
            while (true)
            {
                SkipBlank();
                if (At >= Text.size()) { Out.Refusal = "the DATA section is not closed by ENDSEC"; return Out; }
                if (Text.compare(At, 6, "ENDSEC") == 0) { At += 6; break; }
                int          Number = 0;
                StepInstance Instance;
                if (!ScanInstance(Number, Instance)) { Out.Refusal = Failure("malformed instance"); return Out; }
                if (!Out.Instances.emplace(Number, std::move(Instance)).second) { Out.Refusal = "instance #" + std::to_string(Number) + " is defined twice"; return Out; }
            }
            Search = At;
        }
        if (!Found) Out.Refusal = "no DATA section";
        return Out;
    }

private:
    // DATA as a keyword: at a line or statement start, outside strings (the header is free of instances).
    size_t FindKeyword(std::string_view Keyword, size_t From) const
    {
        for (size_t Position = Text.find(Keyword, From); Position != std::string::npos; Position = Text.find(Keyword, Position + 1))
        {
            const bool Before = Position == 0 || Text[Position - 1] == '\n' || Text[Position - 1] == '\r' || Text[Position - 1] == ';' || Text[Position - 1] == ' ';
            size_t     After  = Position + Keyword.size();
            while (After < Text.size() && (Text[After] == ' ' || Text[After] == '\r' || Text[After] == '\n' || Text[After] == '\t')) ++After;
            if (Before && After < Text.size() && (Text[After] == ';' || Text[After] == '(')) return Position;
        }
        return std::string::npos;
    }

    std::string Failure(const std::string& What) const
    {
        const size_t Line = static_cast<size_t>(std::count(Text.begin(), Text.begin() + static_cast<std::ptrdiff_t>(std::min(At, Text.size())), '\n')) + 1;
        return What + " at line " + std::to_string(Line);
    }

    char Peek() const { return At < Text.size() ? Text[At] : '\0'; }

    void SkipBlank()
    {
        while (At < Text.size())
        {
            const char Letter = Text[At];
            if (Letter == ' ' || Letter == '\n' || Letter == '\r' || Letter == '\t') { ++At; continue; }
            if (Letter == '/' && At + 1 < Text.size() && Text[At + 1] == '*')
            {
                const size_t Close = Text.find("*/", At + 2);
                At = Close == std::string::npos ? Text.size() : Close + 2;
                continue;
            }
            break;
        }
    }

    static bool KeywordLetter(char Letter) { return (Letter >= 'A' && Letter <= 'Z') || (Letter >= 'a' && Letter <= 'z') || (Letter >= '0' && Letter <= '9') || Letter == '_' || Letter == '!'; }

    bool ScanKeyword(std::string& Out)
    {
        SkipBlank();
        const size_t Begin = At;
        while (At < Text.size() && KeywordLetter(Text[At])) ++At;
        if (At == Begin) return false;
        Out = Text.substr(Begin, At - Begin);
        for (char& Letter : Out)
            if (Letter >= 'a' && Letter <= 'z') Letter = static_cast<char>(Letter - 'a' + 'A');
        return true;
    }

    bool ScanInstance(int& Number, StepInstance& Out)
    {
        if (Peek() != '#') return false;
        ++At;
        const size_t Begin = At;
        while (At < Text.size() && Text[At] >= '0' && Text[At] <= '9') ++At;
        if (At == Begin) return false;
        Number = std::atoi(Text.c_str() + Begin);
        SkipBlank();
        if (Peek() != '=') return false;
        ++At;
        SkipBlank();
        if (Peek() == '(')
        {
            ++At;
            while (true)
            {
                SkipBlank();
                if (Peek() == ')') { ++At; break; }
                StepPartial Partial;
                if (!ScanKeyword(Partial.Name)) return false;
                SkipBlank();
                if (!ScanList(Partial.Parameters)) return false;
                Out.Partials.push_back(std::move(Partial));
            }
        }
        else
        {
            StepPartial Partial;
            if (!ScanKeyword(Partial.Name)) return false;
            SkipBlank();
            if (!ScanList(Partial.Parameters)) return false;
            Out.Partials.push_back(std::move(Partial));
        }
        SkipBlank();
        if (Peek() != ';') return false;
        ++At;
        return !Out.Partials.empty();
    }

    bool ScanList(std::vector<StepParameter>& Out)
    {
        SkipBlank();
        if (Peek() != '(') return false;
        ++At;
        SkipBlank();
        if (Peek() == ')') { ++At; return true; }
        while (true)
        {
            StepParameter Parameter;
            if (!ScanParameter(Parameter)) return false;
            Out.push_back(std::move(Parameter));
            SkipBlank();
            if (Peek() == ',') { ++At; continue; }
            if (Peek() == ')') { ++At; return true; }
            return false;
        }
    }

    bool ScanParameter(StepParameter& Out)
    {
        SkipBlank();
        const char Letter = Peek();
        if (Letter == '$') { ++At; Out.Kind = StepCategory::Omitted; return true; }
        if (Letter == '*') { ++At; Out.Kind = StepCategory::Derived; return true; }
        if (Letter == '#')
        {
            ++At;
            const size_t Begin = At;
            while (At < Text.size() && Text[At] >= '0' && Text[At] <= '9') ++At;
            if (At == Begin) return false;
            Out.Kind      = StepCategory::Reference;
            Out.Reference = std::atoi(Text.c_str() + Begin);
            return true;
        }
        if (Letter == '\'')
        {
            ++At;
            Out.Kind = StepCategory::Text;
            while (At < Text.size())
            {
                if (Text[At] == '\'')
                {
                    if (At + 1 < Text.size() && Text[At + 1] == '\'') { Out.Text += '\''; At += 2; continue; }
                    ++At;
                    return true;
                }
                Out.Text += Text[At++];
            }
            return false;
        }
        if (Letter == '"')                                                                     // Binary: kept as text
        {
            const size_t Close = Text.find('"', At + 1);
            if (Close == std::string::npos) return false;
            Out.Kind = StepCategory::Text;
            Out.Text = Text.substr(At + 1, Close - At - 1);
            At       = Close + 1;
            return true;
        }
        if (Letter == '.')
        {
            const size_t Close = Text.find('.', At + 1);
            if (Close == std::string::npos) return false;
            Out.Kind = StepCategory::Enumeration;
            Out.Text = Text.substr(At + 1, Close - At - 1);
            At       = Close + 1;
            return true;
        }
        if (Letter == '(')
        {
            Out.Kind = StepCategory::List;
            return ScanList(Out.List);
        }
        if (Letter == '-' || Letter == '+' || (Letter >= '0' && Letter <= '9'))
        {
            char* End  = nullptr;
            Out.Kind   = StepCategory::Number;
            Out.Number = std::strtod(Text.c_str() + At, &End);
            if (End == Text.c_str() + At) return false;
            At = static_cast<size_t>(End - Text.c_str());
            return true;
        }
        if (KeywordLetter(Letter))
        {
            Out.Kind = StepCategory::Typed;
            if (!ScanKeyword(Out.Text)) return false;
            return ScanList(Out.List);
        }
        return false;
    }

    const std::string& Text;
    size_t             At = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL — KNOT VECTORS
//------------------------------------------------------------------------------------------------------------------------

using Homogeneous = std::array<double, 4>;                                                   // (w·x, w·y, w·z, w)

int Multiplicity(const std::vector<double>& Knots, double Value)
{
    return static_cast<int>(std::count(Knots.begin(), Knots.end(), Value));
}

// 📝 Boehm insertion of one knot (Piegl & Tiller A5.1 with r = 1) into every strip of homogeneous poles sharing the vector.
void InsertKnot(int Degree, std::vector<double>& Knots, std::vector<std::vector<Homogeneous>>& Strips, double Value)
{
    int Span = Degree;
    while (Span + 1 < static_cast<int>(Knots.size()) - Degree - 1 && Knots[Span + 1] <= Value) ++Span;
    const int Repeats = Multiplicity(Knots, Value);
    for (std::vector<Homogeneous>& Poles : Strips)
    {
        std::vector<Homogeneous> Out(Poles.size() + 1);
        for (int Index = 0; Index <= Span - Degree; ++Index) Out[Index] = Poles[Index];
        for (int Index = Span - Degree + 1; Index <= Span - Repeats; ++Index)
        {
            const double Alpha = (Value - Knots[Index]) / (Knots[Index + Degree] - Knots[Index]);
            for (int Component = 0; Component < 4; ++Component) Out[Index][Component] = Alpha * Poles[Index][Component] + (1.0 - Alpha) * Poles[Index - 1][Component];
        }
        for (int Index = std::max(Span - Repeats + 1, Span - Degree + 1); Index < static_cast<int>(Out.size()); ++Index) Out[Index] = Poles[Index - 1];
        Poles = std::move(Out);
    }
    Knots.insert(Knots.begin() + Span + 1, Value);
}

// 💡 An unclamped vector (uniform, periodic exports) describes the curve on [u_p, u_{m−p}]: insert u_p up to multiplicity p,
//    then the pole before that run is C(u_p) and everything earlier is dropped — the same curve, clamped. The end is the
//    mirror image (vector negated and reversed).
void ClampStart(int Degree, std::vector<double>& Knots, std::vector<std::vector<Homogeneous>>& Strips)
{
    const double Value = Knots[Degree];
    if (Multiplicity(Knots, Value) >= Degree + 1 && Knots.front() == Value) return;
    while (Multiplicity(Knots, Value) < Degree) InsertKnot(Degree, Knots, Strips, Value);
    const int First = static_cast<int>(std::find(Knots.begin(), Knots.end(), Value) - Knots.begin());
    const int Drop  = First - 1;
    if (Drop > 0)
    {
        Knots.erase(Knots.begin(), Knots.begin() + Drop);
        for (std::vector<Homogeneous>& Poles : Strips) Poles.erase(Poles.begin(), Poles.begin() + Drop);
    }
    Knots.front() = Value;
}

void Mirror(std::vector<double>& Knots, std::vector<std::vector<Homogeneous>>& Strips)
{
    std::reverse(Knots.begin(), Knots.end());
    for (double& Knot : Knots) Knot = -Knot;
    for (std::vector<Homogeneous>& Poles : Strips) std::reverse(Poles.begin(), Poles.end());
}

void ClampKnots(int Degree, std::vector<double>& Knots, std::vector<std::vector<Homogeneous>>& Strips)
{
    ClampStart(Degree, Knots, Strips);
    Mirror(Knots, Strips);
    ClampStart(Degree, Knots, Strips);
    Mirror(Knots, Strips);
}

enum class KnotRule : uint8_t
{
    Explicit,                                                                                // B_SPLINE_…_WITH_KNOTS
    Bezier,                                                                                  // Pieces of degree p, interior multiplicity p
    QuasiUniform,                                                                            // Clamped, unit interior spacing
    Uniform                                                                                  // Unclamped, unit spacing
};

std::vector<double> ImpliedKnots(KnotRule Rule, int Degree, int Count)
{
    std::vector<double> Out;
    if (Rule == KnotRule::Bezier)
    {
        const int Pieces = std::max(1, (Count - 1) / std::max(1, Degree));
        for (int Repeat = 0; Repeat <= Degree; ++Repeat) Out.push_back(0.0);
        for (int Piece = 1; Piece < Pieces; ++Piece)
            for (int Repeat = 0; Repeat < Degree; ++Repeat) Out.push_back(Piece);
        for (int Repeat = 0; Repeat <= Degree; ++Repeat) Out.push_back(Pieces);
    }
    else if (Rule == KnotRule::QuasiUniform)
    {
        for (int Repeat = 0; Repeat <= Degree; ++Repeat) Out.push_back(0.0);
        for (int Interior = 1; Interior < Count - Degree; ++Interior) Out.push_back(Interior);
        for (int Repeat = 0; Repeat <= Degree; ++Repeat) Out.push_back(Count - Degree);
    }
    else
        for (int Index = 0; Index < Count + Degree + 1; ++Index) Out.push_back(Index - Degree);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL — INTERPRETATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Units of one representation context: millimetres per file length unit, radians per file angle unit.
struct TransferUnits
{
    double LengthMillimetres = 1.0;                                                          // [mm]
    double AngleRadians      = 1.0;                                                          // [rad]
    bool   DeclaredCondition = false;                                                        // [-] - A GLOBAL_UNIT_ASSIGNED_CONTEXT was found
};

// Per-body conversion state: blueprint plus the instance → index maps that make shared vertices and edges shared.
struct BodyConversion
{
    BodyBlueprint                     Blueprint      = {};
    std::map<int, int>                Vertices       = {};                                   // [-] - VERTEX_POINT → blueprint vertex
    std::map<int, int>                Edges          = {};                                   // [-] - EDGE_CURVE → blueprint edge (−1: degenerate)
    std::map<int, bool>               EdgeAlong      = {};                                   // [-] - EDGE_CURVE same_sense
    std::vector<std::vector<int>>     EdgeFaces      = {};                                   // [-] - Blueprint faces using each edge
    int                               Voids          = 0;                                    // [-]
};

class StepInterpreter
{
public:
    StepInterpreter(const StepFile& File, StepImport& Out) : File(File), Out(Out) {}

    std::string Refusal = {};                                                                // [-] - First failure

    // ---- instance access
    const StepInstance* Find(int Reference)
    {
        const auto Found = File.Instances.find(Reference);
        if (Found == File.Instances.end()) { Fail("reference to the missing instance #" + std::to_string(Reference)); return nullptr; }
        return &Found->second;
    }

    const StepPartial* Partial(int Reference, std::string_view Name)
    {
        const auto Found = File.Instances.find(Reference);
        if (Found == File.Instances.end()) return nullptr;
        for (const StepPartial& Candidate : Found->second.Partials)
            if (Candidate.Name == Name) return &Candidate;
        return nullptr;
    }

    // The entity name of a simple instance; for a complex one the first partial that is not a generic supertype.
    std::string Kind(int Reference)
    {
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return {};
        if (Instance->Partials.size() == 1) return Instance->Partials.front().Name;
        std::string Names;
        for (const StepPartial& Candidate : Instance->Partials) Names += (Names.empty() ? "" : "+") + Candidate.Name;
        return Names;
    }

    const std::vector<StepParameter>* Parameters(int Reference, std::string_view Name, size_t Least)
    {
        const StepPartial* Found = Partial(Reference, Name);
        if (!Found) { Fail("#" + std::to_string(Reference) + " is " + Kind(Reference) + ", expected " + std::string(Name)); return nullptr; }
        if (Found->Parameters.size() < Least) { Fail("#" + std::to_string(Reference) + " " + std::string(Name) + " has too few attributes"); return nullptr; }
        return &Found->Parameters;
    }

    bool Fail(const std::string& What)
    {
        if (Refusal.empty()) Refusal = What;
        return false;
    }

    // ---- attribute access
    double Real(const StepParameter& Parameter)
    {
        if (Parameter.Kind == StepCategory::Number) return Parameter.Number;
        if (Parameter.Kind == StepCategory::Typed && !Parameter.List.empty() && Parameter.List.front().Kind == StepCategory::Number) return Parameter.List.front().Number;
        Fail("a REAL attribute is missing or not a number");
        return 0.0;
    }

    int Ref(const StepParameter& Parameter)
    {
        if (Parameter.Kind == StepCategory::Reference) return Parameter.Reference;
        Fail("an entity reference attribute is missing");
        return 0;
    }

    bool Flag(const StepParameter& Parameter)
    {
        if (Parameter.Kind == StepCategory::Enumeration && (Parameter.Text == "T" || Parameter.Text == "TRUE")) return true;
        if (Parameter.Kind == StepCategory::Enumeration && (Parameter.Text == "F" || Parameter.Text == "FALSE")) return false;
        Fail("a BOOLEAN attribute is not .T. or .F.");
        return true;
    }

    const std::vector<StepParameter>& List(const StepParameter& Parameter)
    {
        static const std::vector<StepParameter> Empty;
        if (Parameter.Kind == StepCategory::List) return Parameter.List;
        Fail("a list attribute is missing");
        return Empty;
    }

    // ---- units
    double LengthUnitMillimetres(int Unit, int Depth = 0)
    {
        if (Depth > 8) { Fail("a unit definition loops"); return 1.0; }
        if (const StepPartial* Si = Partial(Unit, "SI_UNIT"))
        {
            if (Si->Parameters.size() < 2 || Si->Parameters[1].Text != "METRE") { Fail("the length unit is not based on the metre"); return 1.0; }
            return 1000.0 * Prefix(Si->Parameters[0]);
        }
        if (const StepPartial* Conversion = Partial(Unit, "CONVERSION_BASED_UNIT"))
        {
            if (Conversion->Parameters.size() < 2) { Fail("a malformed CONVERSION_BASED_UNIT"); return 1.0; }
            const std::vector<StepParameter>& With = Last(Ref(Conversion->Parameters[1]));
            if (With.size() < 2) { Fail("a malformed LENGTH_MEASURE_WITH_UNIT"); return 1.0; }
            return Real(With[0]) * LengthUnitMillimetres(Ref(With[1]), Depth + 1);
        }
        Fail("unsupported length unit #" + std::to_string(Unit) + " (" + Kind(Unit) + ")");
        return 1.0;
    }

    double AngleUnitRadians(int Unit, int Depth = 0)
    {
        if (Depth > 8) { Fail("a unit definition loops"); return 1.0; }
        if (const StepPartial* Si = Partial(Unit, "SI_UNIT"))
        {
            if (Si->Parameters.size() < 2 || Si->Parameters[1].Text != "RADIAN") { Fail("the plane angle unit is not based on the radian"); return 1.0; }
            return Prefix(Si->Parameters[0]);
        }
        if (const StepPartial* Conversion = Partial(Unit, "CONVERSION_BASED_UNIT"))
        {
            if (Conversion->Parameters.size() < 2) { Fail("a malformed CONVERSION_BASED_UNIT"); return 1.0; }
            const std::vector<StepParameter>& With = Last(Ref(Conversion->Parameters[1]));
            if (With.size() < 2) { Fail("a malformed PLANE_ANGLE_MEASURE_WITH_UNIT"); return 1.0; }
            return Real(With[0]) * AngleUnitRadians(Ref(With[1]), Depth + 1);
        }
        Fail("unsupported plane angle unit #" + std::to_string(Unit));
        return 1.0;
    }

    const std::vector<StepParameter>& Last(int Reference)
    {
        static const std::vector<StepParameter> Empty;
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return Empty;
        for (const StepPartial& Candidate : Instance->Partials)
            if (Candidate.Parameters.size() >= 2) return Candidate.Parameters;
        return Empty;
    }

    double Prefix(const StepParameter& Parameter)
    {
        if (Parameter.Kind != StepCategory::Enumeration) return 1.0;
        static const std::map<std::string, double> Prefixes = { { "EXA", 1e18 },   { "PETA", 1e15 }, { "TERA", 1e12 }, { "GIGA", 1e9 },   { "MEGA", 1e6 },
                                                                { "KILO", 1e3 },   { "HECTO", 1e2 }, { "DECA", 1e1 },  { "DECI", 1e-1 },  { "CENTI", 1e-2 },
                                                                { "MILLI", 1e-3 }, { "MICRO", 1e-6 }, { "NANO", 1e-9 }, { "PICO", 1e-12 }, { "FEMTO", 1e-15 },
                                                                { "ATTO", 1e-18 } };
        const auto Found = Prefixes.find(Parameter.Text);
        return Found == Prefixes.end() ? 1.0 : Found->second;
    }

    TransferUnits UnitsOf(int Context)
    {
        TransferUnits Units;
        const StepPartial* Assigned = Partial(Context, "GLOBAL_UNIT_ASSIGNED_CONTEXT");
        if (!Assigned || Assigned->Parameters.empty()) return Units;
        Units.DeclaredCondition = true;
        for (const StepParameter& Unit : List(Assigned->Parameters[0]))
        {
            const int Reference = Ref(Unit);
            if (Partial(Reference, "LENGTH_UNIT")) Units.LengthMillimetres = LengthUnitMillimetres(Reference);
            else if (Partial(Reference, "PLANE_ANGLE_UNIT")) Units.AngleRadians = AngleUnitRadians(Reference);
        }
        return Units;
    }

    // ---- geometry (model units)
    Vector3 Point(int Reference)
    {
        const std::vector<StepParameter>* Attributes = Parameters(Reference, "CARTESIAN_POINT", 2);
        if (!Attributes) return {};
        const std::vector<StepParameter>& Coordinates = List((*Attributes)[1]);
        Vector3 Value{};
        if (Coordinates.size() >= 1) Value.X = Real(Coordinates[0]);
        if (Coordinates.size() >= 2) Value.Y = Real(Coordinates[1]);
        if (Coordinates.size() >= 3) Value.Z = Real(Coordinates[2]);
        return Value * Scale;
    }

    Vector3 Direction(int Reference)
    {
        const std::vector<StepParameter>* Attributes = Parameters(Reference, "DIRECTION", 2);
        if (!Attributes) return { 0.0, 0.0, 1.0 };
        const std::vector<StepParameter>& Ratios = List((*Attributes)[1]);
        Vector3 Value{};
        if (Ratios.size() >= 1) Value.X = Real(Ratios[0]);
        if (Ratios.size() >= 2) Value.Y = Real(Ratios[1]);
        if (Ratios.size() >= 3) Value.Z = Real(Ratios[2]);
        if (!(Length(Value) > 0.0)) { Fail("DIRECTION #" + std::to_string(Reference) + " has zero length"); return { 0.0, 0.0, 1.0 }; }
        return KeepUnit(Value);
    }

    // AXIS2_PLACEMENT_3D: axis defaults to +Z, reference to +X (+Z when the axis is along X), made orthogonal.
    OrthonormalBasis Placement(int Reference)
    {
        const std::vector<StepParameter>* Attributes = Parameters(Reference, "AXIS2_PLACEMENT_3D", 2);
        if (!Attributes) return {};
        const Vector3 Origin = Point(Ref((*Attributes)[1]));
        const Vector3 Axis   = Attributes->size() > 2 && (*Attributes)[2].Kind == StepCategory::Reference ? Direction((*Attributes)[2].Reference) : Vector3{ 0.0, 0.0, 1.0 };
        Vector3 Reference3   = Attributes->size() > 3 && (*Attributes)[3].Kind == StepCategory::Reference ? Direction((*Attributes)[3].Reference) : Vector3{ 1.0, 0.0, 0.0 };
        if (Length(Cross(Axis, Reference3)) < 1.0e-12) Reference3 = std::fabs(Axis.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 0.0, 1.0 };
        // Already orthogonal to rounding: kept as read (re-orthogonalising would move the last bit, see KeepUnit).
        if (std::fabs(Dot(Axis, Reference3)) > 1.0e-15) Reference3 = KeepUnit(Reference3 - Axis * Dot(Reference3, Axis));
        return { Origin, Reference3, Cross(Axis, Reference3), Axis };
    }

    // B-spline curve in any of its Part 21 forms (simple with knots, implied-knot subtypes, complex rational).
    std::optional<NurbsCurve> SplineCurve(int Reference)
    {
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return std::nullopt;
        int                  Degree = 0;
        const std::vector<StepParameter>* Poles = nullptr;
        const std::vector<StepParameter>* Multiplicities = nullptr;
        const std::vector<StepParameter>* KnotValues = nullptr;
        const std::vector<StepParameter>* WeightValues = nullptr;
        KnotRule Rule = KnotRule::Explicit;
        if (Instance->Partials.size() == 1)
        {
            const StepPartial& Simple = Instance->Partials.front();
            if (Simple.Parameters.size() < 3) { Fail("a malformed " + Simple.Name); return std::nullopt; }
            Degree = static_cast<int>(Real(Simple.Parameters[1]));
            Poles  = &List(Simple.Parameters[2]);
            if (Simple.Name == "B_SPLINE_CURVE_WITH_KNOTS")
            {
                if (Simple.Parameters.size() < 8) { Fail("a malformed B_SPLINE_CURVE_WITH_KNOTS"); return std::nullopt; }
                Multiplicities = &List(Simple.Parameters[6]);
                KnotValues     = &List(Simple.Parameters[7]);
            }
            else if (Simple.Name == "BEZIER_CURVE") Rule = KnotRule::Bezier;
            else if (Simple.Name == "QUASI_UNIFORM_CURVE") Rule = KnotRule::QuasiUniform;
            else if (Simple.Name == "UNIFORM_CURVE") Rule = KnotRule::Uniform;
            else { Fail("unsupported curve " + Simple.Name); return std::nullopt; }
        }
        else
        {
            const StepPartial* Spline = Partial(Reference, "B_SPLINE_CURVE");
            if (!Spline || Spline->Parameters.size() < 2) { Fail("unsupported complex curve " + Kind(Reference)); return std::nullopt; }
            Degree = static_cast<int>(Real(Spline->Parameters[0]));
            Poles  = &List(Spline->Parameters[1]);
            if (const StepPartial* Knots = Partial(Reference, "B_SPLINE_CURVE_WITH_KNOTS"); Knots && Knots->Parameters.size() >= 2)
            {
                Multiplicities = &List(Knots->Parameters[0]);
                KnotValues     = &List(Knots->Parameters[1]);
            }
            else if (Partial(Reference, "BEZIER_CURVE")) Rule = KnotRule::Bezier;
            else if (Partial(Reference, "QUASI_UNIFORM_CURVE")) Rule = KnotRule::QuasiUniform;
            else if (Partial(Reference, "UNIFORM_CURVE")) Rule = KnotRule::Uniform;
            else { Fail("a complex B-spline curve without knots #" + std::to_string(Reference)); return std::nullopt; }
            if (const StepPartial* Rational = Partial(Reference, "RATIONAL_B_SPLINE_CURVE"); Rational && !Rational->Parameters.empty()) WeightValues = &List(Rational->Parameters[0]);
        }
        if (!Refusal.empty()) return std::nullopt;
        const int Count = static_cast<int>(Poles->size());
        std::vector<double> Knots;
        if (Rule == KnotRule::Explicit)
        {
            if (Multiplicities->size() != KnotValues->size()) { Fail("B-spline curve #" + std::to_string(Reference) + ": multiplicities and knots differ in number"); return std::nullopt; }
            for (size_t Index = 0; Index < KnotValues->size(); ++Index)
                for (int Repeat = 0; Repeat < static_cast<int>(Real((*Multiplicities)[Index])); ++Repeat) Knots.push_back(Real((*KnotValues)[Index]));
        }
        else Knots = ImpliedKnots(Rule, Degree, Count);
        if (Degree < 1 || Degree > NurbsDegreeLimit || static_cast<int>(Knots.size()) != Count + Degree + 1 || (WeightValues && static_cast<int>(WeightValues->size()) != Count))
        {
            Fail("B-spline curve #" + std::to_string(Reference) + ": degree, poles, knots and weights do not agree");
            return std::nullopt;
        }
        std::vector<std::vector<Homogeneous>> Strips(1);
        for (int Index = 0; Index < Count; ++Index)
        {
            const Vector3 Position = Point(Ref((*Poles)[Index]));
            const double  Weight   = WeightValues ? Real((*WeightValues)[Index]) : 1.0;
            Strips[0].push_back({ Position.X * Weight, Position.Y * Weight, Position.Z * Weight, Weight });
        }
        if (!Refusal.empty()) return std::nullopt;
        ClampKnots(Degree, Knots, Strips);
        std::vector<Vector3> Positions;
        std::vector<double>  Weights;
        for (const Homogeneous& Pole : Strips[0])
        {
            if (!(Pole[3] > 0.0)) { Fail("B-spline curve #" + std::to_string(Reference) + " has a non-positive weight"); return std::nullopt; }
            Positions.push_back(Vector3{ Pole[0], Pole[1], Pole[2] } * (1.0 / Pole[3]));
            Weights.push_back(Pole[3]);
        }
        std::optional<NurbsCurve> Curve = ConstructNurbsCurve(Degree, std::move(Knots), std::move(Positions), std::move(Weights));
        if (!Curve) Fail("B-spline curve #" + std::to_string(Reference) + " is not a valid clamped B-spline");
        return Curve;
    }

    std::optional<CurveGeometry> Curve(int Reference, int Depth = 0)
    {
        if (Depth > 8) { Fail("curve references loop"); return std::nullopt; }
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return std::nullopt;
        const std::string Name = Instance->Partials.size() == 1 ? Instance->Partials.front().Name : std::string();
        if (Name == "LINE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, "LINE", 3);
            if (!Attributes) return std::nullopt;
            const std::vector<StepParameter>* Vector = Parameters(Ref((*Attributes)[2]), "VECTOR", 3);
            if (!Vector) return std::nullopt;
            return StraightLine{ Point(Ref((*Attributes)[1])), Direction(Ref((*Vector)[1])) };
        }
        if (Name == "CIRCLE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, "CIRCLE", 3);
            if (!Attributes) return std::nullopt;
            const double Radius = Real((*Attributes)[2]) * Scale;
            if (!(Radius > 0.0)) { Fail("CIRCLE #" + std::to_string(Reference) + " has no positive radius"); return std::nullopt; }
            return CircularArc{ Placement(Ref((*Attributes)[1])), Radius };
        }
        if (Name == "ELLIPSE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, "ELLIPSE", 4);
            if (!Attributes) return std::nullopt;
            OrthonormalBasis Frame = Placement(Ref((*Attributes)[1]));
            double First = Real((*Attributes)[2]) * Scale, Second = Real((*Attributes)[3]) * Scale;
            if (!(First > 0.0) || !(Second > 0.0)) { Fail("ELLIPSE #" + std::to_string(Reference) + " has a non-positive semi-axis"); return std::nullopt; }
            if (First < Second)                                                              // Major axis along X: rotate the frame a quarter turn
            {
                Frame = { Frame.Origin, Frame.YAxis, -Frame.XAxis, Frame.ZAxis };
                std::swap(First, Second);
            }
            return EllipticalArc{ Frame, First, Second };
        }
        if (Name == "SURFACE_CURVE" || Name == "SEAM_CURVE" || Name == "INTERSECTION_CURVE" || Name == "BOUNDED_SURFACE_CURVE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
            if (!Attributes) return std::nullopt;
            return Curve(Ref((*Attributes)[1]), Depth + 1);
        }
        if (Name == "TRIMMED_CURVE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
            if (!Attributes) return std::nullopt;
            return Curve(Ref((*Attributes)[1]), Depth + 1);
        }
        if (Name == "POLYLINE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
            if (!Attributes) return std::nullopt;
            std::vector<Vector3> Positions;
            for (const StepParameter& Vertex : List((*Attributes)[1])) Positions.push_back(Point(Ref(Vertex)));
            if (Positions.size() < 2) { Fail("POLYLINE #" + std::to_string(Reference) + " has fewer than two points"); return std::nullopt; }
            std::vector<double> Knots = { 0.0 };
            for (size_t Index = 0; Index < Positions.size(); ++Index) Knots.push_back(static_cast<double>(Index));
            Knots.push_back(static_cast<double>(Positions.size() - 1));
            std::optional<NurbsCurve> Spline = ConstructNurbsCurve(1, std::move(Knots), Positions, std::vector<double>(Positions.size(), 1.0));
            if (!Spline) { Fail("POLYLINE #" + std::to_string(Reference) + " could not be converted"); return std::nullopt; }
            return *Spline;
        }
        if (Name == "B_SPLINE_CURVE_WITH_KNOTS" || Name == "BEZIER_CURVE" || Name == "QUASI_UNIFORM_CURVE" || Name == "UNIFORM_CURVE" || Partial(Reference, "B_SPLINE_CURVE"))
        {
            std::optional<NurbsCurve> Spline = SplineCurve(Reference);
            if (!Spline) return std::nullopt;
            return *Spline;
        }
        Fail("unsupported edge curve " + Kind(Reference) + " (#" + std::to_string(Reference) + "); supported: LINE, CIRCLE, ELLIPSE, B-spline, POLYLINE, SURFACE_CURVE, SEAM_CURVE, TRIMMED_CURVE");
        return std::nullopt;
    }

    std::optional<NurbsSurface> SplineSurface(int Reference)
    {
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return std::nullopt;
        int DegreeU = 0, DegreeV = 0;
        const std::vector<StepParameter>* Rows = nullptr;
        const std::vector<StepParameter>* MultiplicitiesU = nullptr;
        const std::vector<StepParameter>* MultiplicitiesV = nullptr;
        const std::vector<StepParameter>* KnotsU = nullptr;
        const std::vector<StepParameter>* KnotsV = nullptr;
        const std::vector<StepParameter>* WeightRows = nullptr;
        KnotRule Rule = KnotRule::Explicit;
        if (Instance->Partials.size() == 1)
        {
            const StepPartial& Simple = Instance->Partials.front();
            if (Simple.Parameters.size() < 4) { Fail("a malformed " + Simple.Name); return std::nullopt; }
            DegreeU = static_cast<int>(Real(Simple.Parameters[1]));
            DegreeV = static_cast<int>(Real(Simple.Parameters[2]));
            Rows    = &List(Simple.Parameters[3]);
            if (Simple.Name == "B_SPLINE_SURFACE_WITH_KNOTS")
            {
                if (Simple.Parameters.size() < 12) { Fail("a malformed B_SPLINE_SURFACE_WITH_KNOTS"); return std::nullopt; }
                MultiplicitiesU = &List(Simple.Parameters[8]);
                MultiplicitiesV = &List(Simple.Parameters[9]);
                KnotsU          = &List(Simple.Parameters[10]);
                KnotsV          = &List(Simple.Parameters[11]);
            }
            else if (Simple.Name == "BEZIER_SURFACE") Rule = KnotRule::Bezier;
            else if (Simple.Name == "QUASI_UNIFORM_SURFACE") Rule = KnotRule::QuasiUniform;
            else if (Simple.Name == "UNIFORM_SURFACE") Rule = KnotRule::Uniform;
            else { Fail("unsupported surface " + Simple.Name); return std::nullopt; }
        }
        else
        {
            const StepPartial* Spline = Partial(Reference, "B_SPLINE_SURFACE");
            if (!Spline || Spline->Parameters.size() < 3) { Fail("unsupported complex surface " + Kind(Reference)); return std::nullopt; }
            DegreeU = static_cast<int>(Real(Spline->Parameters[0]));
            DegreeV = static_cast<int>(Real(Spline->Parameters[1]));
            Rows    = &List(Spline->Parameters[2]);
            if (const StepPartial* Knots = Partial(Reference, "B_SPLINE_SURFACE_WITH_KNOTS"); Knots && Knots->Parameters.size() >= 4)
            {
                MultiplicitiesU = &List(Knots->Parameters[0]);
                MultiplicitiesV = &List(Knots->Parameters[1]);
                KnotsU          = &List(Knots->Parameters[2]);
                KnotsV          = &List(Knots->Parameters[3]);
            }
            else if (Partial(Reference, "BEZIER_SURFACE")) Rule = KnotRule::Bezier;
            else if (Partial(Reference, "QUASI_UNIFORM_SURFACE")) Rule = KnotRule::QuasiUniform;
            else if (Partial(Reference, "UNIFORM_SURFACE")) Rule = KnotRule::Uniform;
            else { Fail("a complex B-spline surface without knots #" + std::to_string(Reference)); return std::nullopt; }
            if (const StepPartial* Rational = Partial(Reference, "RATIONAL_B_SPLINE_SURFACE"); Rational && !Rational->Parameters.empty()) WeightRows = &List(Rational->Parameters[0]);
        }
        if (!Refusal.empty()) return std::nullopt;
        const int CountU = static_cast<int>(Rows->size());
        const int CountV = CountU ? static_cast<int>(List((*Rows)[0]).size()) : 0;
        const auto Expand = [&](const std::vector<StepParameter>* Multiplicities, const std::vector<StepParameter>* Values, int Degree, int Count)
        {
            std::vector<double> Knots;
            if (Rule != KnotRule::Explicit) return ImpliedKnots(Rule, Degree, Count);
            if (Multiplicities->size() != Values->size()) { Fail("B-spline surface #" + std::to_string(Reference) + ": multiplicities and knots differ in number"); return Knots; }
            for (size_t Index = 0; Index < Values->size(); ++Index)
                for (int Repeat = 0; Repeat < static_cast<int>(Real((*Multiplicities)[Index])); ++Repeat) Knots.push_back(Real((*Values)[Index]));
            return Knots;
        };
        std::vector<double> ExpandedU = Expand(MultiplicitiesU, KnotsU, DegreeU, CountU);
        std::vector<double> ExpandedV = Expand(MultiplicitiesV, KnotsV, DegreeV, CountV);
        if (DegreeU < 1 || DegreeV < 1 || DegreeU > NurbsDegreeLimit || DegreeV > NurbsDegreeLimit || static_cast<int>(ExpandedU.size()) != CountU + DegreeU + 1
            || static_cast<int>(ExpandedV.size()) != CountV + DegreeV + 1 || (WeightRows && static_cast<int>(WeightRows->size()) != CountU))
        {
            Fail("B-spline surface #" + std::to_string(Reference) + ": degrees, poles, knots and weights do not agree");
            return std::nullopt;
        }
        // Grid [u][v] of homogeneous poles; clamping along 𝑢 works on the columns, along 𝑣 on the rows.
        std::vector<std::vector<Homogeneous>> Grid(CountU);
        for (int Row = 0; Row < CountU; ++Row)
        {
            const std::vector<StepParameter>& Line = List((*Rows)[Row]);
            const std::vector<StepParameter>* Weights = WeightRows ? &List((*WeightRows)[Row]) : nullptr;
            if (static_cast<int>(Line.size()) != CountV || (Weights && static_cast<int>(Weights->size()) != CountV)) { Fail("B-spline surface #" + std::to_string(Reference) + " has ragged pole rows"); return std::nullopt; }
            for (int Column = 0; Column < CountV; ++Column)
            {
                const Vector3 Position = Point(Ref(Line[Column]));
                const double  Weight   = Weights ? Real((*Weights)[Column]) : 1.0;
                Grid[Row].push_back({ Position.X * Weight, Position.Y * Weight, Position.Z * Weight, Weight });
            }
        }
        if (!Refusal.empty()) return std::nullopt;
        ClampKnots(DegreeV, ExpandedV, Grid);
        std::vector<std::vector<Homogeneous>> Columns(Grid.front().size());
        for (size_t Column = 0; Column < Columns.size(); ++Column)
            for (int Row = 0; Row < CountU; ++Row) Columns[Column].push_back(Grid[Row][Column]);
        ClampKnots(DegreeU, ExpandedU, Columns);
        const int FinalU = static_cast<int>(Columns.front().size()), FinalV = static_cast<int>(Columns.size());
        std::vector<Vector3> Positions;
        std::vector<double>  Weights;
        for (int Row = 0; Row < FinalU; ++Row)
            for (int Column = 0; Column < FinalV; ++Column)
            {
                const Homogeneous& Pole = Columns[Column][Row];
                if (!(Pole[3] > 0.0)) { Fail("B-spline surface #" + std::to_string(Reference) + " has a non-positive weight"); return std::nullopt; }
                Positions.push_back(Vector3{ Pole[0], Pole[1], Pole[2] } * (1.0 / Pole[3]));
                Weights.push_back(Pole[3]);
            }
        std::optional<NurbsSurface> Surface = ConstructNurbsSurface(DegreeU, DegreeV, FinalU, FinalV, std::move(ExpandedU), std::move(ExpandedV), std::move(Positions), std::move(Weights));
        if (!Surface) Fail("B-spline surface #" + std::to_string(Reference) + " is not a valid clamped B-spline");
        return Surface;
    }

    // 📝 Swept curve of a revolution or extrusion as an exact NURBS: conics over a full turn, lines over the span the face
    //    needs (Samples, the face's boundary points, projected on the line with a generous margin), B-splines as they are.
    std::optional<NurbsCurveDefinition> SweptProfile(int Reference, const std::vector<Vector3>& Samples)
    {
        const std::optional<CurveGeometry> Swept = Curve(Reference);
        if (!Swept) return std::nullopt;
        if (const auto* Spline = std::get_if<NurbsCurve>(&*Swept)) return *Spline->Definition;
        if (const auto* Line = std::get_if<StraightLine>(&*Swept))
        {
            double Low = 1e300, High = -1e300;
            for (const Vector3& Sample : Samples)
            {
                const double Along = Dot(Sample - Line->Origin, Line->Direction);
                Low  = std::min(Low, Along);
                High = std::max(High, Along);
            }
            if (Samples.empty()) { Low = -1.0; High = 1.0; }
            const double Margin = 0.5 * (High - Low) + 1.0e-3;
            return *ConvertLineToNurbs(*Line, Low - Margin, High + Margin).Definition;
        }
        std::optional<NurbsCurve> Converted = ConvertCurveToNurbs(*Swept, 0.0, FullCircle);
        if (!Converted) { Fail("a swept curve could not be converted to NURBS"); return std::nullopt; }
        return *Converted->Definition;
    }

    std::optional<SurfaceGeometry> Surface(int Reference, const std::vector<Vector3>& Samples, int Depth = 0)
    {
        if (Depth > 8) { Fail("surface references loop"); return std::nullopt; }
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return std::nullopt;
        const std::string Name = Instance->Partials.size() == 1 ? Instance->Partials.front().Name : std::string();
        if (Name == "PLANE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
            if (!Attributes) return std::nullopt;
            return PlaneSurface{ Placement(Ref((*Attributes)[1])) };
        }
        if (Name == "CYLINDRICAL_SURFACE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 3);
            if (!Attributes) return std::nullopt;
            return CylinderSurface{ Placement(Ref((*Attributes)[1])), Real((*Attributes)[2]) * Scale };
        }
        if (Name == "CONICAL_SURFACE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 4);
            if (!Attributes) return std::nullopt;
            const double Angle = Real((*Attributes)[3]) * AngleRadians;
            if (!(std::fabs(Angle) < 0.25 * FullCircle)) { Fail("CONICAL_SURFACE #" + std::to_string(Reference) + " has a semi-angle outside (−90°, 90°)"); return std::nullopt; }
            return ConeSurface{ Placement(Ref((*Attributes)[1])), Real((*Attributes)[2]) * Scale, std::tan(Angle) };
        }
        if (Name == "SPHERICAL_SURFACE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 3);
            if (!Attributes) return std::nullopt;
            return SphereSurface{ Placement(Ref((*Attributes)[1])), Real((*Attributes)[2]) * Scale };
        }
        if (Name == "TOROIDAL_SURFACE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 4);
            if (!Attributes) return std::nullopt;
            const double Major = Real((*Attributes)[2]) * Scale, Minor = Real((*Attributes)[3]) * Scale;
            if (!(Major > Minor) || !(Minor > 0.0)) { Fail("TOROIDAL_SURFACE #" + std::to_string(Reference) + " is degenerate (major radius not above the minor)"); return std::nullopt; }
            return TorusSurface{ Placement(Ref((*Attributes)[1])), Major, Minor };
        }
        if (Name == "RECTANGULAR_TRIMMED_SURFACE" || Name == "CURVE_BOUNDED_SURFACE")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
            if (!Attributes) return std::nullopt;
            return Surface(Ref((*Attributes)[1]), Samples, Depth + 1);
        }
        if (Name == "SURFACE_OF_REVOLUTION")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 3);
            if (!Attributes) return std::nullopt;
            const std::vector<StepParameter>* Axis = Parameters(Ref((*Attributes)[2]), "AXIS1_PLACEMENT", 2);
            if (!Axis) return std::nullopt;
            const Vector3 Origin    = Point(Ref((*Axis)[1]));
            const Vector3 Direction3 = Axis->size() > 2 && (*Axis)[2].Kind == StepCategory::Reference ? Direction((*Axis)[2].Reference) : Vector3{ 0.0, 0.0, 1.0 };
            const std::optional<NurbsCurveDefinition> Profile = SweptProfile(Ref((*Attributes)[1]), Samples);
            if (!Profile) return std::nullopt;
            const Vector3 Seed = std::fabs(Direction3.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            ++Out.ConvertedSurfaces;
            return ConstructNurbsRevolution(*Profile, ConstructFrame(Origin, Direction3, Seed), 0.0, FullCircle);
        }
        if (Name == "SURFACE_OF_LINEAR_EXTRUSION")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 3);
            if (!Attributes) return std::nullopt;
            const std::vector<StepParameter>* Vector = Parameters(Ref((*Attributes)[2]), "VECTOR", 3);
            if (!Vector) return std::nullopt;
            const Vector3 Along = Direction(Ref((*Vector)[1]));
            const std::optional<NurbsCurveDefinition> Profile = SweptProfile(Ref((*Attributes)[1]), Samples);
            if (!Profile) return std::nullopt;
            // S(𝑢, 𝑣) = C(𝑢) + 𝑣·d: the 𝑣-span covers every boundary point (pole hull bounds C along d), with a margin.
            double PoleLow = 1e300, PoleHigh = -1e300, SampleLow = 1e300, SampleHigh = -1e300;
            for (const Vector3& Pole : Profile->Poles) { PoleLow = std::min(PoleLow, Dot(Pole, Along)); PoleHigh = std::max(PoleHigh, Dot(Pole, Along)); }
            for (const Vector3& Sample : Samples) { SampleLow = std::min(SampleLow, Dot(Sample, Along)); SampleHigh = std::max(SampleHigh, Dot(Sample, Along)); }
            if (Samples.empty()) { Fail("SURFACE_OF_LINEAR_EXTRUSION #" + std::to_string(Reference) + " bounds no edge"); return std::nullopt; }
            const double Margin = 0.05 * (SampleHigh - SampleLow + PoleHigh - PoleLow) + 1.0e-3;
            const double Low = SampleLow - PoleHigh - Margin, High = SampleHigh - PoleLow + Margin;
            const int    Count = static_cast<int>(Profile->Poles.size());
            std::vector<Vector3> Poles;
            std::vector<double>  Weights;
            for (int Index = 0; Index < Count; ++Index)
            {
                Poles.push_back(Profile->Poles[Index] + Along * Low);
                Poles.push_back(Profile->Poles[Index] + Along * High);
                Weights.push_back(Profile->Weights[Index]);
                Weights.push_back(Profile->Weights[Index]);
            }
            std::optional<NurbsSurface> Extrusion = ConstructNurbsSurface(Profile->Degree, 1, Count, 2, Profile->Knots, { Low, Low, High, High }, std::move(Poles), std::move(Weights));
            if (!Extrusion) { Fail("SURFACE_OF_LINEAR_EXTRUSION #" + std::to_string(Reference) + " could not be converted"); return std::nullopt; }
            ++Out.ConvertedSurfaces;
            return *Extrusion;
        }
        if (Name == "B_SPLINE_SURFACE_WITH_KNOTS" || Name == "BEZIER_SURFACE" || Name == "QUASI_UNIFORM_SURFACE" || Name == "UNIFORM_SURFACE" || Partial(Reference, "B_SPLINE_SURFACE"))
        {
            std::optional<NurbsSurface> Spline = SplineSurface(Reference);
            if (!Spline) return std::nullopt;
            return *Spline;
        }
        Fail("unsupported face surface " + Kind(Reference) + " (#" + std::to_string(Reference)
             + "); supported: PLANE, CYLINDRICAL / CONICAL / SPHERICAL / TOROIDAL_SURFACE, B-spline, SURFACE_OF_REVOLUTION, SURFACE_OF_LINEAR_EXTRUSION, RECTANGULAR_TRIMMED_SURFACE");
        return std::nullopt;
    }

    // ---- topology
    int Vertex(BodyConversion& Body, int Reference)
    {
        const auto Found = Body.Vertices.find(Reference);
        if (Found != Body.Vertices.end()) return Found->second;
        const std::vector<StepParameter>* Attributes = Parameters(Reference, "VERTEX_POINT", 2);
        if (!Attributes) return -1;
        const int Index = static_cast<int>(Body.Blueprint.Vertices.size());
        Body.Blueprint.Vertices.push_back({ Point(Ref((*Attributes)[1])) });
        Body.Vertices[Reference] = Index;
        return Index;
    }

    // 📝 EDGE_CURVE → blueprint edge running with increasing τ: Start is v1 when same_sense, v2 otherwise; the interval
    //    comes from projecting the vertices (periodic carriers: the end folded above the start, a closed edge spans one
    //    period). Zero-length edges (OpenCascade writes them at poles and apexes) become −1 and are left out of loops.
    int Edge(BodyConversion& Body, int Reference)
    {
        const auto Found = Body.Edges.find(Reference);
        if (Found != Body.Edges.end()) return Found->second;
        const std::vector<StepParameter>* Attributes = Parameters(Reference, "EDGE_CURVE", 5);
        if (!Attributes) return -1;
        const int  First  = Vertex(Body, Ref((*Attributes)[1]));
        const int  Second = Vertex(Body, Ref((*Attributes)[2]));
        const bool Along  = Flag((*Attributes)[4]);
        std::optional<CurveGeometry> Carrier = Curve(Ref((*Attributes)[3]));
        if (!Carrier || First < 0 || Second < 0) return -1;
        BlueprintEdge Made;
        Made.Curve = *Carrier;
        Made.Start = Along ? First : Second;
        Made.End   = Along ? Second : First;
        const Vector3 StartPosition = Body.Blueprint.Vertices[Made.Start].Position;
        const Vector3 EndPosition   = Body.Blueprint.Vertices[Made.End].Position;
        double Period = 0.0, DomainLow = 0.0, DomainHigh = 0.0;
        bool   Bounded = false;
        if (std::holds_alternative<CircularArc>(Made.Curve) || std::holds_alternative<EllipticalArc>(Made.Curve)) Period = FullCircle;
        if (const auto* Spline = std::get_if<NurbsCurve>(&Made.Curve))
        {
            const NurbsDomain Domain = MeasureNurbsCurveDomain(*Spline->Definition);
            DomainLow  = Domain.Low;
            DomainHigh = Domain.High;
            Period     = Spline->Definition->Period;
            Bounded    = true;
        }
        const bool Closed = Made.Start == Made.End;
        if (Period > 0.0)
        {
            Made.Low  = ProjectOntoCurve(Made.Curve, StartPosition, DomainLow);
            Made.High = ProjectOntoCurve(Made.Curve, EndPosition, Made.Low);
            if (Closed || Made.High - Made.Low < 1.0e-12 * Period) Made.High = Made.Low + Period;
        }
        else if (Bounded && Closed)
        {
            Made.Low  = DomainLow;
            Made.High = DomainHigh;
        }
        else
        {
            Made.Low  = ProjectOntoCurve(Made.Curve, StartPosition, DomainLow);
            Made.High = ProjectOntoCurve(Made.Curve, EndPosition, DomainLow);
            // A closed open-domain spline whose start and end vertices are distinct but sit on the joint.
            if (Bounded && Made.High <= Made.Low)
            {
                const double Span = DomainHigh - DomainLow;
                if (Made.Low - DomainLow < 1.0e-9 * Span && DomainHigh - Made.High > 0.5 * Span) {}
                else if (DomainHigh - Made.Low < 1.0e-9 * Span) Made.Low = DomainLow;
                else if (Made.High - DomainLow < 1.0e-9 * Span) Made.High = DomainHigh;
            }
        }
        // Degenerate: the whole interval stays at the vertex.
        double Reach = 0.0;
        for (int Step = 1; Step < 8; ++Step) Reach = std::max(Reach, Length(EvaluateCurve(Made.Curve, Made.Low + (Made.High - Made.Low) * Step / 8.0).Position - StartPosition));
        if (Closed && Reach < 1.0e-7 * std::max(1.0, Length(StartPosition)) + SessionPrecision)
        {
            ++Out.DegenerateEdges;
            Body.Edges[Reference] = -1;
            return -1;
        }
        if (!(Made.High > Made.Low)) { Fail("EDGE_CURVE #" + std::to_string(Reference) + ": its vertices do not bound a positive interval of its curve"); return -1; }
        const int Index = static_cast<int>(Body.Blueprint.Edges.size());
        Body.Blueprint.Edges.push_back(Made);
        Body.EdgeFaces.emplace_back();
        Body.Edges[Reference]     = Index;
        Body.EdgeAlong[Reference] = Along;
        return Index;
    }

    // 💡 A sphere written with no edge at all (OpenCascade: one face bounded by a VERTEX_LOOP at a pole) gets the kernel's
    //    layout: a seam meridian from the south pole to the north pole through +X, used twice by the face.
    bool SynthesizeSeam(BodyConversion& Body, BlueprintFace& Face, const std::vector<int>& LoopVertices)
    {
        const auto* Sphere = std::get_if<SphereSurface>(&Face.Surface);
        if (!Sphere) return Fail("a face bounded by no edge on a " + std::string(SurfaceCategoryLabel(Face.Surface)) + " carrier (only full spheres are completed with a seam)");
        const OrthonormalBasis& Frame = Sphere->Frame;
        const Vector3 South = Frame.Origin - Frame.ZAxis * Sphere->Radius, North = Frame.Origin + Frame.ZAxis * Sphere->Radius;
        int SouthIndex = -1, NorthIndex = -1;
        for (const int Candidate : LoopVertices)
        {
            const Vector3 Position = Body.Blueprint.Vertices[Candidate].Position;
            if (Length(Position - South) < 1.0e-6 * Sphere->Radius) SouthIndex = Candidate;
            else if (Length(Position - North) < 1.0e-6 * Sphere->Radius) NorthIndex = Candidate;
            else return Fail("a sphere face bounded by a vertex loop away from its poles");
        }
        if (SouthIndex < 0) { SouthIndex = static_cast<int>(Body.Blueprint.Vertices.size()); Body.Blueprint.Vertices.push_back({ South }); }
        if (NorthIndex < 0) { NorthIndex = static_cast<int>(Body.Blueprint.Vertices.size()); Body.Blueprint.Vertices.push_back({ North }); }
        BlueprintEdge Seam;
        Seam.Curve = CircularArc{ OrthonormalBasis{ Frame.Origin, -Frame.ZAxis, Frame.XAxis, Cross(-Frame.ZAxis, Frame.XAxis) }, Sphere->Radius };
        Seam.Low   = 0.0;
        Seam.High  = 0.5 * FullCircle;
        Seam.Start = SouthIndex;
        Seam.End   = NorthIndex;
        const int Index = static_cast<int>(Body.Blueprint.Edges.size());
        Body.Blueprint.Edges.push_back(Seam);
        Body.EdgeFaces.emplace_back();
        Face.Loops.push_back({ BlueprintUse{ Index, Orientation::Forward, ParameterLine{} }, BlueprintUse{ Index, Orientation::Reversed, ParameterLine{} } });
        ++Out.SynthesizedSeams;
        return true;
    }

    // ADVANCED_FACE / FACE_SURFACE (name, bounds, surface, same_sense); Flip for faces of a shell used reversed.
    bool Face(BodyConversion& Body, int Reference, bool Flip)
    {
        const StepInstance* Instance = Find(Reference);
        if (!Instance) return false;
        const std::string Name = Instance->Partials.front().Name;
        if (Name != "ADVANCED_FACE" && Name != "FACE_SURFACE") return Fail("unsupported face " + Kind(Reference) + " (#" + std::to_string(Reference) + ")");
        const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 4);
        if (!Attributes) return false;
        BlueprintFace Made;
        Made.Sense = Flag((*Attributes)[3]) != Flip ? Orientation::Forward : Orientation::Reversed;
        std::vector<int>     LoopVertices;
        std::vector<Vector3> Samples;
        for (const StepParameter& BoundParameter : List((*Attributes)[1]))
        {
            const int   Bound     = Ref(BoundParameter);
            const std::string BoundName = Kind(Bound);
            if (BoundName != "FACE_BOUND" && BoundName != "FACE_OUTER_BOUND") return Fail("unsupported face bound " + BoundName + " (#" + std::to_string(Bound) + ")");
            const std::vector<StepParameter>* BoundAttributes = Parameters(Bound, BoundName, 3);
            if (!BoundAttributes) return false;
            const bool Reverse = Flag((*BoundAttributes)[2]) == Flip;
            const int  Loop    = Ref((*BoundAttributes)[1]);
            const std::string LoopName = Kind(Loop);
            if (LoopName == "VERTEX_LOOP")
            {
                const std::vector<StepParameter>* LoopAttributes = Parameters(Loop, LoopName, 2);
                if (!LoopAttributes) return false;
                LoopVertices.push_back(Vertex(Body, Ref((*LoopAttributes)[1])));
                continue;
            }
            if (LoopName != "EDGE_LOOP") return Fail("unsupported loop " + LoopName + " (#" + std::to_string(Loop) + ")");
            const std::vector<StepParameter>* LoopAttributes = Parameters(Loop, LoopName, 2);
            if (!LoopAttributes) return false;
            std::vector<BlueprintUse> Uses;
            for (const StepParameter& UseParameter : List((*LoopAttributes)[1]))
            {
                const std::vector<StepParameter>* UseAttributes = Parameters(Ref(UseParameter), "ORIENTED_EDGE", 5);
                if (!UseAttributes) return false;
                const int StepEdge = Ref((*UseAttributes)[3]);
                const int Index    = Edge(Body, StepEdge);
                if (!Refusal.empty()) return false;
                if (Index < 0) continue;                                                     // Degenerate pole / apex edge
                const bool Forward = Flag((*UseAttributes)[4]) == Body.EdgeAlong[StepEdge];
                Uses.push_back({ Index, Forward ? Orientation::Forward : Orientation::Reversed, ParameterLine{} });
            }
            if (Uses.empty()) continue;
            if (Reverse)
            {
                std::reverse(Uses.begin(), Uses.end());
                for (BlueprintUse& Use : Uses) Use.Sense = Use.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
            }
            for (const BlueprintUse& Use : Uses)
            {
                const BlueprintEdge& Used = Body.Blueprint.Edges[Use.Edge];
                for (int Step = 0; Step < 8; ++Step) Samples.push_back(EvaluateCurve(Used.Curve, Used.Low + (Used.High - Used.Low) * Step / 8.0).Position);
            }
            Made.Loops.push_back(std::move(Uses));
        }
        if (!Refusal.empty()) return false;
        std::optional<SurfaceGeometry> Carrier = Surface(Ref((*Attributes)[2]), Samples);
        if (!Carrier) return false;
        Made.Surface = std::move(*Carrier);
        if (Made.Loops.empty() && !SynthesizeSeam(Body, Made, LoopVertices)) return false;
        const int FaceIndex = static_cast<int>(Body.Blueprint.Faces.size());
        for (const auto& Loop : Made.Loops)
            for (const BlueprintUse& Use : Loop) Body.EdgeFaces[Use.Edge].push_back(FaceIndex);
        Body.Blueprint.Faces.push_back(std::move(Made));
        return true;
    }

    // CLOSED_SHELL / OPEN_SHELL, or ORIENTED_CLOSED_SHELL(name, *, shell, orientation) around one.
    bool ShellFaces(BodyConversion& Body, int Reference, bool Flip)
    {
        const std::string Name = Kind(Reference);
        if (Name == "ORIENTED_CLOSED_SHELL" || Name == "ORIENTED_OPEN_SHELL")
        {
            const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 4);
            if (!Attributes) return false;
            return ShellFaces(Body, Ref((*Attributes)[2]), Flip != !Flag((*Attributes)[3]));
        }
        if (Name != "CLOSED_SHELL" && Name != "OPEN_SHELL") return Fail("unsupported shell " + Name + " (#" + std::to_string(Reference) + ")");
        const std::vector<StepParameter>* Attributes = Parameters(Reference, Name, 2);
        if (!Attributes) return false;
        for (const StepParameter& Face3 : List((*Attributes)[1]))
            if (!Face(Body, Ref(Face3), Flip)) return false;
        return true;
    }

    // 💡 Edge-use parity: an edge shared by faces A and B must be traversed oppositely, so A and B need the same loop
    //    direction when their uses already oppose and different ones when they agree. Breadth-first 2-colouring per
    //    connected set of faces; a contradiction leaves everything as read (assembly then names the edge). The smaller
    //    colour class of each component has its loops reversed (order and every sense).
    void RepairLoopOrientation(BodyConversion& Body)
    {
        BodyBlueprint& Blueprint = Body.Blueprint;
        std::vector<std::vector<std::pair<int, int>>> Uses(Blueprint.Edges.size());          // Edge → (face, ±1)
        for (size_t FaceIndex = 0; FaceIndex < Blueprint.Faces.size(); ++FaceIndex)
            for (const auto& Loop : Blueprint.Faces[FaceIndex].Loops)
                for (const BlueprintUse& Use : Loop) Uses[Use.Edge].push_back({ static_cast<int>(FaceIndex), Use.Sense == Orientation::Forward ? 1 : -1 });
        std::vector<std::vector<std::pair<int, int>>> Links(Blueprint.Faces.size());          // Face → (face, parity)
        bool Unbalanced = false;
        for (const auto& EdgeUses : Uses)
        {
            if (EdgeUses.size() != 2 || EdgeUses[0].first == EdgeUses[1].first) continue;
            const int Parity = EdgeUses[0].second == EdgeUses[1].second ? 1 : 0;
            Unbalanced = Unbalanced || Parity == 1;
            Links[EdgeUses[0].first].push_back({ EdgeUses[1].first, Parity });
            Links[EdgeUses[1].first].push_back({ EdgeUses[0].first, Parity });
        }
        if (!Unbalanced) return;
        std::vector<int> Colour(Blueprint.Faces.size(), -1);
        std::vector<int> Turned;
        for (size_t Seed = 0; Seed < Blueprint.Faces.size(); ++Seed)
        {
            if (Colour[Seed] >= 0) continue;
            std::vector<int> Component = { static_cast<int>(Seed) }, Queue = { static_cast<int>(Seed) };
            Colour[Seed] = 0;
            for (size_t Head = 0; Head < Queue.size(); ++Head)
                for (const auto& [Next, Parity] : Links[Queue[Head]])
                {
                    const int Wanted = Colour[Queue[Head]] ^ Parity;
                    if (Colour[Next] < 0)
                    {
                        Colour[Next] = Wanted;
                        Queue.push_back(Next);
                        Component.push_back(Next);
                    }
                    else if (Colour[Next] != Wanted) return;                                 // Contradiction: not a loop-direction slip
                }
            const long Ones = std::count_if(Component.begin(), Component.end(), [&](int Index) { return Colour[Index] == 1; });
            const int  Minority = 2 * Ones <= static_cast<long>(Component.size()) ? 1 : 0;
            for (const int Index : Component)
                if (Colour[Index] == Minority) Turned.push_back(Index);
        }
        for (const int Index : Turned)
        {
            for (auto& Loop : Blueprint.Faces[Index].Loops)
            {
                std::reverse(Loop.begin(), Loop.end());
                for (BlueprintUse& Use : Loop) Use.Sense = Use.Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward;
            }
            ++Out.ReorientedFaces;
        }
    }

    // 📝 Local precision where the file's geometry misses its own topology (another kernel's tolerances): an edge is a tube
    //    as wide as twice its worst distance to either face carrier, a vertex a sphere covering its edges' ends and tubes.
    void AssignPrecisions(BodyConversion& Body)
    {
        BodyBlueprint& Blueprint = Body.Blueprint;
        const double   Threshold = 0.5 * SessionPrecision;
        std::vector<double> VertexGap(Blueprint.Vertices.size(), 0.0);
        for (size_t Index = 0; Index < Blueprint.Edges.size(); ++Index)
        {
            BlueprintEdge& Made = Blueprint.Edges[Index];
            double Gap = 0.0;
            for (const int FaceIndex : Body.EdgeFaces[Index])
            {
                const SurfaceGeometry& Carrier = Blueprint.Faces[FaceIndex].Surface;
                // Boundary candidates are actual points on the carrier, not a
                // tolerance exemption. Near-singular macro charts can fool the
                // unconstrained inversion into reporting millimetre "gaps" on
                // exact iso rims. Retain the smallest measured spatial residual.
                std::vector<ParameterLine> BoundaryCandidates;
                if (const auto* Spline = std::get_if<NurbsSurface>(&Carrier); Spline && Spline->Definition && Made.High > Made.Low)
                {
                    const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline->Definition, true);
                    const NurbsDomain V = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
                    for (int Side = 0; Side < 4; ++Side)
                    {
                        const bool FixedU = Side < 2;
                        const double Fixed = FixedU ? (Side == 0 ? U.Low : U.High) : (Side == 2 ? V.Low : V.High);
                        const NurbsCurve Iso = ExtractNurbsIsoCurve(*Spline, FixedU, Fixed);
                        const NurbsDomain Domain = MeasureNurbsCurveDomain(*Iso.Definition);
                        const double Low = ProjectOntoNurbsCurve(*Iso.Definition, EvaluateCurve(Made.Curve, Made.Low).Position, Domain.Low);
                        const double High = ProjectOntoNurbsCurve(*Iso.Definition, EvaluateCurve(Made.Curve, Made.High).Position, Domain.Low);
                        const Vector2 A = FixedU ? Vector2{ Fixed, Low } : Vector2{ Low, Fixed };
                        const Vector2 B = FixedU ? Vector2{ Fixed, High } : Vector2{ High, Fixed };
                        const Vector2 Direction = (B - A) * (1.0 / (Made.High - Made.Low));
                        BoundaryCandidates.push_back({ A - Direction * Made.Low, Direction });
                    }
                }
                Vector2 Reference = {};
                for (int Step = 0; Step <= 16; ++Step)
                {
                    const Vector3 Position = EvaluateCurve(Made.Curve, Made.Low + (Made.High - Made.Low) * Step / 16.0).Position;
                    Reference = ProjectOntoSurface(Carrier, Position, Reference);
                    double Residual = Length(EvaluateSurface(Carrier, Reference).Position - Position);
                    const double T = Made.Low + (Made.High - Made.Low) * Step / 16.0;
                    for (const ParameterLine& Candidate : BoundaryCandidates)
                    {
                        const Vector2 UV = Candidate.Origin + Candidate.Direction * T;
                        const double Measured = Length(EvaluateSurface(Carrier, UV).Position - Position);
                        if (Measured < Residual) { Residual = Measured; Reference = UV; }
                    }
                    Gap = std::max(Gap, Residual);
                }
            }
            const double StartGap = Length(EvaluateCurve(Made.Curve, Made.Low).Position - Blueprint.Vertices[Made.Start].Position);
            const double EndGap   = Length(EvaluateCurve(Made.Curve, Made.High).Position - Blueprint.Vertices[Made.End].Position);
            Out.LargestGap = std::max({ Out.LargestGap, Gap, StartGap, EndGap });
            if (Gap > Threshold)
            {
                Made.Precision = 2.0 * Gap;
                ++Out.TolerantEdges;
            }
            const double Tube = std::isnan(Made.Precision) ? 0.0 : Made.Precision;
            VertexGap[Made.Start] = std::max({ VertexGap[Made.Start], StartGap > Threshold ? 2.0 * StartGap : 0.0, Tube });
            VertexGap[Made.End]   = std::max({ VertexGap[Made.End], EndGap > Threshold ? 2.0 * EndGap : 0.0, Tube });
        }
        for (size_t Index = 0; Index < Blueprint.Vertices.size(); ++Index)
            if (VertexGap[Index] > 0.0)
            {
                Blueprint.Vertices[Index].Precision = VertexGap[Index];
                ++Out.TolerantVertices;
            }
    }

    // ---- representation context of every item, so each solid is read in its own units
    std::map<int, int> ItemContexts()
    {
        std::map<int, int> Contexts;
        for (const auto& [Number, Instance] : File.Instances)
        {
            if (Instance.Partials.size() != 1) continue;
            const StepPartial& Simple = Instance.Partials.front();
            const size_t       Suffix = Simple.Name.size() >= 14 ? Simple.Name.size() - 14 : std::string::npos;
            if (Suffix == std::string::npos || Simple.Name.compare(Suffix, 14, "REPRESENTATION") != 0 || Simple.Parameters.size() < 3) continue;
            if (Simple.Parameters[1].Kind != StepCategory::List || Simple.Parameters[2].Kind != StepCategory::Reference) continue;
            for (const StepParameter& Member : Simple.Parameters[1].List)
                if (Member.Kind == StepCategory::Reference && !Contexts.count(Member.Reference)) Contexts[Member.Reference] = Simple.Parameters[2].Reference;
        }
        return Contexts;
    }

    double Scale        = 1.0;                                                               // [-]   - Model units per file length unit
    double AngleRadians = 1.0;                                                               // [rad] - Per file angle unit

private:
    const StepFile& File;
    StepImport&     Out;
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

StepImport ReadStep(ModelPartition& Partition, const std::string& Bytes, const StepImportRequest& Request)
{
    const auto Start = std::chrono::steady_clock::now();
    StepImport Out;
    const auto Finish = [&](const std::string& Refusal) -> StepImport
    {
        Out.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        if (!Refusal.empty())
        {
            Out.Refusal = Request.Label + ": " + Refusal;
            Out.Bodies.clear();
        }
        return Out;
    };
    if (!(Request.ModelUnitMillimetres > 0.0) || !std::isfinite(Request.ModelUnitMillimetres)) return Finish("the model unit must be a positive length");

    const StepFile File = PartScanner(Bytes).Scan();
    if (!File.Refusal.empty()) return Finish(File.Refusal);
    Out.Instances = static_cast<int>(File.Instances.size());

    StepInterpreter Interpreter(File, Out);
    const std::map<int, int> Contexts = Interpreter.ItemContexts();

    // ---- every solid (and surface model), in instance order
    std::vector<int> Roots;
    bool AssemblyCondition = false;
    for (const auto& [Number, Instance] : File.Instances)
    {
        const std::string& Name = Instance.Partials.front().Name;
        if (Instance.Partials.size() == 1 && (Name == "MANIFOLD_SOLID_BREP" || Name == "BREP_WITH_VOIDS" || Name == "SHELL_BASED_SURFACE_MODEL")) Roots.push_back(Number);
        if (Name == "NEXT_ASSEMBLY_USAGE_OCCURRENCE" || Name == "MAPPED_ITEM") AssemblyCondition = true;
        if (Name == "FACETED_BREP") return Finish("FACETED_BREP (#" + std::to_string(Number) + ") is not supported");
    }
    std::sort(Roots.begin(), Roots.end());
    if (Roots.empty()) return Finish("the file holds no MANIFOLD_SOLID_BREP, BREP_WITH_VOIDS or SHELL_BASED_SURFACE_MODEL");
    if (AssemblyCondition) Out.Notes.push_back("assembly structure present: every solid is read in its own representation's coordinates (placements are not applied)");

    std::vector<BodyBlueprint> Blueprints;
    bool UnitNoted = false;
    for (const int Root : Roots)
    {
        TransferUnits Units;
        if (const auto Found = Contexts.find(Root); Found != Contexts.end()) Units = Interpreter.UnitsOf(Found->second);
        if (!Units.DeclaredCondition)
        {
            for (const auto& [Number, Instance] : File.Instances)                             // No context link: the first unit context
                for (const StepPartial& Candidate : Instance.Partials)
                    if (Candidate.Name == "GLOBAL_UNIT_ASSIGNED_CONTEXT" && !Units.DeclaredCondition) Units = Interpreter.UnitsOf(Number);
            if (!Units.DeclaredCondition && !UnitNoted) Out.Notes.push_back("no unit context: millimetres assumed");
            UnitNoted = true;
        }
        if (!Interpreter.Refusal.empty()) return Finish(Interpreter.Refusal);
        Out.FileUnitMillimetres  = Units.LengthMillimetres;
        Interpreter.Scale        = Units.LengthMillimetres / Request.ModelUnitMillimetres;
        Interpreter.AngleRadians = Units.AngleRadians;

        BodyConversion Body;
        const std::string Name = Interpreter.Kind(Root);
        const std::vector<StepParameter>* Attributes = Interpreter.Parameters(Root, Name, 2);
        if (!Attributes) return Finish(Interpreter.Refusal);
        if (Name == "SHELL_BASED_SURFACE_MODEL")
        {
            Body.Blueprint.Category = BodyCategory::Sheet;
            for (const StepParameter& Shell3 : Interpreter.List((*Attributes)[1]))
                if (!Interpreter.ShellFaces(Body, Interpreter.Ref(Shell3), false)) return Finish(Interpreter.Refusal);
            ++Out.Sheets;
        }
        else
        {
            if (!Interpreter.ShellFaces(Body, Interpreter.Ref((*Attributes)[1]), false)) return Finish(Interpreter.Refusal);
            if (Name == "BREP_WITH_VOIDS")
            {
                if (Attributes->size() < 3) return Finish("BREP_WITH_VOIDS #" + std::to_string(Root) + " has no void list");
                for (const StepParameter& Void : Interpreter.List((*Attributes)[2]))
                {
                    if (!Interpreter.ShellFaces(Body, Interpreter.Ref(Void), false)) return Finish(Interpreter.Refusal);
                    ++Out.Voids;
                }
            }
            ++Out.Solids;
        }
        if (!Interpreter.Refusal.empty()) return Finish(Interpreter.Refusal);
        Interpreter.RepairLoopOrientation(Body);
        for (BlueprintFace& Face : Body.Blueprint.Faces) FitFaceSurfaceCurves(Body.Blueprint, Face);
        Interpreter.AssignPrecisions(Body);
        Out.Faces += static_cast<int>(Body.Blueprint.Faces.size());
        Blueprints.push_back(std::move(Body.Blueprint));
    }

    // ---- assemble and audit every body away from the caller first, so a refusal leaves the partition untouched
    {
        ModelPartition Scratch;
        for (size_t Index = 0; Index < Blueprints.size(); ++Index)
        {
            const ArrangementOutcome Assembly = AssembleBody(Scratch, Blueprints[Index], Request.Label);
            if (!Assembly.Refusal.empty())
            {
                const std::string Prefix = Request.Label + ": ";                             // AssembleBody labels its refusal too
                const std::string Cause  = Assembly.Refusal.rfind(Prefix, 0) == 0 ? Assembly.Refusal.substr(Prefix.size()) : Assembly.Refusal;
                return Finish("solid " + std::to_string(Index + 1) + " (#" + std::to_string(Roots[Index]) + "): " + Cause);
            }
            Blueprints[Index].ShellVolumes = Assembly.ShellVolumes;                          // The caller's assembly reuses them
            const AuditReport Audit = AuditBody(Scratch, Assembly.Body);
            if (!Audit.SoundCondition())
            {
                const AuditFinding& Finding = Audit.Findings.front();
                return Finish("solid " + std::to_string(Index + 1) + " (#" + std::to_string(Roots[Index]) + ") fails audit " + Finding.Code + " on " + Finding.Subject + ": " + Finding.Detail + " ("
                              + std::to_string(Audit.Findings.size()) + " findings)");
            }
        }
    }
    for (const BodyBlueprint& Blueprint : Blueprints)
    {
        const ArrangementOutcome Assembly = AssembleBody(Partition, Blueprint, Request.Label);
        Out.Bodies.push_back(Assembly.Body);
        const BodyInventory Inventory = Partition.CollectInventory(Assembly.Body);
        Out.Edges    += static_cast<int>(Inventory.Edges.size());
        Out.Vertices += static_cast<int>(Inventory.Vertices.size());
    }
    return Finish({});
}

static StepImport ImportStepFileUntraced(ModelPartition& Partition, const std::string& Path, const StepImportRequest& Request)
{
    std::ifstream Stream(ResolveUtf8Path(Path), std::ios::binary);
    if (!Stream)
    {
        StepImport Out;
        Out.Refusal = Request.Label + ": cannot open " + Path;
        return Out;
    }
    std::ostringstream Content;
    Content << Stream.rdbuf();
    return ReadStep(Partition, Content.str(), Request);
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

StepImport ImportStepFile(ModelPartition& Partition, const std::string& Path, const StepImportRequest& Request)
{
    FRONTIER_TRACE_SCOPE("Exchange", "ImportStepFile · " + Path);
    StepImport Outcome = ImportStepFileUntraced(Partition, Path, Request);
    FRONTIER_TRACE("Exchange", Outcome.Refusal.empty() ? std::to_string(Outcome.Bodies.size()) + " body(ies) · " + std::to_string(Outcome.Faces) + " face(s)"
                                                       : "refused: " + Outcome.Refusal);
    return Outcome;
}


} // namespace Frontier::GeometrySpace::Parametric
