/*====================================================================================================================================
                                                       STLEXCHANGE.CPP
====================================================================================================================================*/
// 🧩 STL exchange: binary / ASCII writer over BodyTessellation, bit-exact welding reader, soundness measures

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Exchange/StlExchange.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr size_t StlHeaderBytes = 80;                                                        // [B] - Binary header
constexpr size_t StlFacetBytes  = 50;                                                        // [B] - Normal + 3 corners + attribute

void AppendFloat(std::string& Out, float Value)
{
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    for (int Shift = 0; Shift < 32; Shift += 8) Out.push_back(static_cast<char>((Bits >> Shift) & 0xFFu));
}

[[nodiscard]] float ReadFloat(const std::string& Bytes, size_t At)
{
    uint32_t Bits = 0;
    for (int Index = 0; Index < 4; ++Index) Bits |= static_cast<uint32_t>(static_cast<unsigned char>(Bytes[At + Index])) << (8 * Index);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

[[nodiscard]] uint32_t ReadCount(const std::string& Bytes, size_t At)
{
    uint32_t Count = 0;
    for (int Index = 0; Index < 4; ++Index) Count |= static_cast<uint32_t>(static_cast<unsigned char>(Bytes[At + Index])) << (8 * Index);
    return Count;
}

struct FloatCorner
{
    std::array<float, 3> Coordinates = {};                                                   // [mm] - As written
};

// Facet normal of float corners, in double then rounded (zero for a degenerate facet).
[[nodiscard]] std::array<float, 3> FacetNormal(const std::array<FloatCorner, 3>& Corners)
{
    const auto At = [&](int Corner) { return Vector3{ Corners[Corner].Coordinates[0], Corners[Corner].Coordinates[1], Corners[Corner].Coordinates[2] }; };
    const Vector3 Normal = Cross(At(1) - At(0), At(2) - At(0));
    const double  Size   = Length(Normal);
    if (!(Size > 0.0)) return { 0.0f, 0.0f, 0.0f };
    return { static_cast<float>(Normal.X / Size), static_cast<float>(Normal.Y / Size), static_cast<float>(Normal.Z / Size) };
}

void AppendAsciiNumber(std::string& Out, float Value)
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%.9g", static_cast<double>(Value));
    Out += Text;
}

// Welds a corner by its float bit pattern.
struct CornerWelder
{
    std::map<std::array<uint32_t, 3>, int> Known = {};
    StlReading*                            Reading = nullptr;

    int Weld(const std::array<float, 3>& Coordinates)
    {
        std::array<uint32_t, 3> Key{};
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Clean = Coordinates[Axis] == 0.0f ? 0.0f : Coordinates[Axis];       // −0 welds with +0
            std::memcpy(&Key[Axis], &Clean, sizeof(uint32_t));
        }
        const auto [Entry, Inserted] = Known.emplace(Key, static_cast<int>(Reading->Positions.size()));
        if (Inserted) Reading->Positions.push_back({ Coordinates[0], Coordinates[1], Coordinates[2] });
        return Entry->second;
    }
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

StlExport WriteStl(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const StlRequest& Request)
{
    const auto Start = std::chrono::steady_clock::now();
    StlExport Out;
    const auto Finish = [&]() -> StlExport
    {
        Out.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return Out;
    };
    if (Bodies.empty()) { Out.Refusal = "STL: no body to export"; return Finish(); }
    if (!(Request.ModelUnitMillimetres > 0.0) || !std::isfinite(Request.ModelUnitMillimetres)) { Out.Refusal = "STL: the model unit must be a positive length"; return Finish(); }

    std::vector<std::array<FloatCorner, 3>> Facets;
    for (const BodyToken Body : Bodies)
    {
        if (!Partition.Bodies.Authenticate(Body)) { Out.Refusal = "STL: a body is not in the partition"; return Finish(); }
        const TessellationOutcome Tessellation = TessellateBody(Partition, Body, Request.Tessellation);
        if (!Tessellation.Refusal.empty()) { Out.Refusal = "STL: tessellation refused (" + Tessellation.Refusal + ")"; return Finish(); }
        Out.Sag = std::max(Out.Sag, Tessellation.Sag);
        const PolygonComplex& Complex = Tessellation.Complex;
        std::vector<FloatCorner> Corners(Complex.Positions.size());
        for (size_t Index = 0; Index < Complex.Positions.size(); ++Index)
        {
            const Vector3 Point = Complex.Positions[Index] * Request.ModelUnitMillimetres;
            Corners[Index].Coordinates = { static_cast<float>(Point.X), static_cast<float>(Point.Y), static_cast<float>(Point.Z) };
        }
        for (const std::array<int, 3>& Triangle : Complex.Triangles) Facets.push_back({ Corners[Triangle[0]], Corners[Triangle[1]], Corners[Triangle[2]] });
        Out.Bodies += 1;
    }
    if (Facets.size() > 0xFFFFFFFFull) { Out.Refusal = "STL: more facets than a binary count can hold"; return Finish(); }
    Out.Triangles = static_cast<int>(Facets.size());

    if (Request.BinaryEnabled)
    {
        Out.Bytes.reserve(StlHeaderBytes + 4 + StlFacetBytes * Facets.size());
        std::string Header = "Frontier GeometryProcessing STL (mm) " + Request.Name;
        Header.resize(StlHeaderBytes, ' ');
        if (Header.compare(0, 5, "solid") == 0) Header[0] = 'S';                             // Never mistaken for ASCII
        Out.Bytes += Header;
        const uint32_t Count = static_cast<uint32_t>(Facets.size());
        for (int Shift = 0; Shift < 32; Shift += 8) Out.Bytes.push_back(static_cast<char>((Count >> Shift) & 0xFFu));
        for (const std::array<FloatCorner, 3>& Facet : Facets)
        {
            for (const float Component : FacetNormal(Facet)) AppendFloat(Out.Bytes, Component);
            for (const FloatCorner& Corner : Facet)
                for (const float Coordinate : Corner.Coordinates) AppendFloat(Out.Bytes, Coordinate);
            Out.Bytes.push_back('\0');
            Out.Bytes.push_back('\0');
        }
        return Finish();
    }
    std::string Name = Request.Name;
    std::replace_if(Name.begin(), Name.end(), [](char Character) { return Character == '\n' || Character == '\r'; }, ' ');
    Out.Bytes.reserve(64 + 260 * Facets.size());
    Out.Bytes += "solid " + Name + "\n";
    for (const std::array<FloatCorner, 3>& Facet : Facets)
    {
        Out.Bytes += "  facet normal";
        for (const float Component : FacetNormal(Facet)) { Out.Bytes += ' '; AppendAsciiNumber(Out.Bytes, Component); }
        Out.Bytes += "\n    outer loop\n";
        for (const FloatCorner& Corner : Facet)
        {
            Out.Bytes += "      vertex";
            for (const float Coordinate : Corner.Coordinates) { Out.Bytes += ' '; AppendAsciiNumber(Out.Bytes, Coordinate); }
            Out.Bytes += '\n';
        }
        Out.Bytes += "    endloop\n  endfacet\n";
    }
    Out.Bytes += "endsolid " + Name + "\n";
    return Finish();
}

StlReading ReadStl(const std::string& Bytes)
{
    StlReading Reading;
    CornerWelder Welder;
    Welder.Reading = &Reading;
    if (Bytes.size() >= StlHeaderBytes + 4)
    {
        const uint64_t Count = ReadCount(Bytes, StlHeaderBytes);
        if (StlHeaderBytes + 4 + StlFacetBytes * Count == Bytes.size())
        {
            Reading.BinaryCondition = true;
            Reading.Name = Bytes.substr(0, StlHeaderBytes);
            Reading.Name.erase(Reading.Name.find_last_not_of(std::string(" \0", 2)) + 1);
            for (uint64_t Facet = 0; Facet < Count; ++Facet)
            {
                const size_t At = StlHeaderBytes + 4 + StlFacetBytes * Facet;
                Reading.Normals.push_back({ ReadFloat(Bytes, At), ReadFloat(Bytes, At + 4), ReadFloat(Bytes, At + 8) });
                std::array<int, 3> Triangle{};
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const size_t Base = At + 12 + 12 * Corner;
                    Triangle[Corner] = Welder.Weld({ ReadFloat(Bytes, Base), ReadFloat(Bytes, Base + 4), ReadFloat(Bytes, Base + 8) });
                }
                Reading.Triangles.push_back(Triangle);
            }
            return Reading;
        }
    }
    std::istringstream Stream(Bytes);
    std::string Word;
    if (!(Stream >> Word) || Word != "solid") { Reading.Refusal = "STL: neither a binary layout (84 + 50 · count bytes) nor ASCII \"solid\""; return Reading; }
    std::getline(Stream, Reading.Name);
    Reading.Name.erase(0, Reading.Name.find_first_not_of(' '));
    while (!Reading.Name.empty() && (Reading.Name.back() == '\r' || Reading.Name.back() == ' ')) Reading.Name.pop_back();
    bool Ended = false;
    while (Stream >> Word)
    {
        if (Word == "endsolid") { Ended = true; break; }
        if (Word != "facet") { Reading.Refusal = "STL: expected \"facet\", found \"" + Word + "\""; return Reading; }
        std::string Keyword;
        Vector3 Normal{};
        if (!(Stream >> Keyword >> Normal.X >> Normal.Y >> Normal.Z) || Keyword != "normal") { Reading.Refusal = "STL: malformed facet normal"; return Reading; }
        std::string Outer, Loop;
        if (!(Stream >> Outer >> Loop) || Outer != "outer" || Loop != "loop") { Reading.Refusal = "STL: expected \"outer loop\""; return Reading; }
        std::array<int, 3> Triangle{};
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            std::string Vertex;
            std::array<float, 3> Coordinates{};
            if (!(Stream >> Vertex >> Coordinates[0] >> Coordinates[1] >> Coordinates[2]) || Vertex != "vertex") { Reading.Refusal = "STL: a facet needs three vertices"; return Reading; }
            Triangle[Corner] = Welder.Weld(Coordinates);
        }
        std::string EndLoop, EndFacet;
        if (!(Stream >> EndLoop >> EndFacet) || EndLoop != "endloop" || EndFacet != "endfacet") { Reading.Refusal = "STL: expected \"endloop endfacet\""; return Reading; }
        Reading.Normals.push_back(Normal);
        Reading.Triangles.push_back(Triangle);
    }
    if (!Ended) Reading.Refusal = "STL: missing \"endsolid\"";
    return Reading;
}

StlSoundness MeasureStlSoundness(const StlReading& Reading)
{
    StlSoundness Out;
    std::map<std::pair<int, int>, std::array<int, 2>> Uses;                                  // (low, high) → uses low→high, high→low
    std::vector<int> Parent(Reading.Positions.size());
    std::iota(Parent.begin(), Parent.end(), 0);
    const auto Root = [&](int Vertex) { while (Parent[Vertex] != Vertex) Vertex = Parent[Vertex] = Parent[Parent[Vertex]]; return Vertex; };
    std::vector<char> Used(Reading.Positions.size(), 0);
    for (const std::array<int, 3>& Triangle : Reading.Triangles)
    {
        const Vector3 A = Reading.Positions[Triangle[0]], B = Reading.Positions[Triangle[1]], C = Reading.Positions[Triangle[2]];
        const double  Twice = Length(Cross(B - A, C - A));
        if (Triangle[0] == Triangle[1] || Triangle[1] == Triangle[2] || Triangle[2] == Triangle[0] || !(Twice > 0.0)) Out.DegenerateTriangles += 1;
        Out.Area   += 0.5 * Twice;
        Out.Volume += Dot(A, Cross(B, C)) / 6.0;
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const int From = Triangle[Corner], To = Triangle[(Corner + 1) % 3];
            Used[From] = 1;
            if (From == To) continue;
            std::array<int, 2>& Count = Uses[{ std::min(From, To), std::max(From, To) }];
            Count[From < To ? 0 : 1] += 1;
            Parent[Root(From)] = Root(To);
        }
    }
    for (const auto& [Side, Count] : Uses)
    {
        const int Total = Count[0] + Count[1];
        if (Total == 1) Out.OpenSides += 1;
        else if (Total > 2) Out.NonManifoldSides += 1;
        else if (Count[0] != 1) Out.MisorientedSides += 1;
    }
    int Vertices = 0;
    for (size_t Vertex = 0; Vertex < Used.size(); ++Vertex)
    {
        if (!Used[Vertex]) continue;
        Vertices += 1;
        if (Root(static_cast<int>(Vertex)) == static_cast<int>(Vertex)) Out.Components += 1;
    }
    Out.Sides               = static_cast<int>(Uses.size());
    Out.EulerCharacteristic = Vertices - Out.Sides + static_cast<int>(Reading.Triangles.size());
    Out.WatertightCondition = !Reading.Triangles.empty() && Out.OpenSides == 0 && Out.NonManifoldSides == 0 && Out.MisorientedSides == 0;
    return Out;
}

std::string ExportStlFile(const StlExport& Export, const std::string& Path)
{
    if (!Export.Refusal.empty()) return Export.Refusal;
    std::ofstream Stream(ResolveUtf8Path(Path), std::ios::binary | std::ios::trunc);
    if (!Stream) return "STL: cannot open " + Path + " for writing";
    Stream.write(Export.Bytes.data(), static_cast<std::streamsize>(Export.Bytes.size()));
    return Stream.good() ? std::string() : "STL: writing " + Path + " failed";
}

} // namespace Frontier::GeometrySpace::Parametric
