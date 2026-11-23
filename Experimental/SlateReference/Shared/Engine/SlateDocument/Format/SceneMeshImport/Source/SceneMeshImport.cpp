//============================================================================================================================================
//                                                       SCENEMESHIMPORT.CPP
//============================================================================================================================================

#include "SlateDocument/Format/SceneMeshImport/Api/SceneMeshImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace Slate
{

namespace
{
    constexpr double UnitlessToMetres = 0.001;

    std::string Lower(std::string Text)
    {
        for (char& Character : Text)
            Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        return Text;
    }

    std::string StemOf(const std::string& Path)
    {
        return std::filesystem::path(Path).stem().string();
    }

    Deliver<std::vector<std::uint8_t>> ReadBytes(const std::string& Path)
    {
        std::ifstream Input(Path, std::ios::binary);
        if (!Input)
            return Deliver<std::vector<std::uint8_t>>::Refuse({ RefusalReason::ContentUnsupported, "the mesh file could not be opened" });
        Input.seekg(0, std::ios::end);
        const std::streamoff Count = Input.tellg();
        if (Count <= 0)
            return Deliver<std::vector<std::uint8_t>>::Refuse({ RefusalReason::ContentUnsupported, "the mesh file is empty" });
        Input.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(Count));
        Input.read(reinterpret_cast<char*>(Bytes.data()), Count);
        return Deliver<std::vector<std::uint8_t>>::Result(std::move(Bytes));
    }

    void AddMaterialSlot(ImportedSceneMesh& Imported, const std::string& Slot)
    {
        if (Slot.empty())
            return;
        if (std::find(Imported.MaterialSlots.begin(), Imported.MaterialSlots.end(), Slot) == Imported.MaterialSlots.end())
            Imported.MaterialSlots.push_back(Slot);
    }

    void AddVertex(CodexSceneMesh& Mesh, double X, double Y, double Z, double Scale)
    {
        Mesh.Positions.push_back(X * Scale);
        Mesh.Positions.push_back(Y * Scale);
        Mesh.Positions.push_back(Z * Scale);
    }

    void AddTriangle(CodexSceneMesh& Mesh, std::uint32_t A, std::uint32_t B, std::uint32_t C)
    {
        Mesh.Indices.push_back(A);
        Mesh.Indices.push_back(B);
        Mesh.Indices.push_back(C);
    }

    void AddFaceFan(CodexSceneMesh& Mesh, const std::vector<std::uint32_t>& Face)
    {
        if (Face.size() < 3u)
            return;
        for (std::size_t Index = 1u; Index + 1u < Face.size(); ++Index)
            AddTriangle(Mesh, Face[0], Face[Index], Face[Index + 1u]);
    }

    void FinaliseImported(ImportedSceneMesh& Imported, const std::string& Path)
    {
        const std::string Stem = StemOf(Path);
        Imported.Mesh.Naming = Stem + "_mesh";
        Imported.Entry.Subject = CodexSceneSubject::Geometry;
        Imported.Entry.Naming = Stem.empty() ? "Imported Mesh" : Stem;
        Imported.Entry.GeometryReference = Imported.Mesh.Naming;
        if (Imported.MaterialSlots.empty())
            Imported.MaterialSlots.push_back("ImportedDefault");
        Imported.Entry.MaterialReference = Imported.MaterialSlots.size() == 1u
            ? Imported.MaterialSlots.front()
            : std::to_string(Imported.MaterialSlots.size()) + " material slots";
        Imported.MaterialRecords.clear();
        for (const std::string& Slot : Imported.MaterialSlots)
            Imported.MaterialRecords.push_back(DefaultWorkspaceMaterialRecord(Slot));
        Imported.Entry.Scale[0] = Imported.Entry.Scale[1] = Imported.Entry.Scale[2] = 1.0;
    }

    bool MeshStanding(const CodexSceneMesh& Mesh)
    {
        return Mesh.Positions.size() >= 9u && Mesh.Positions.size() % 3u == 0u && Mesh.Indices.size() >= 3u;
    }

    Deliver<ImportedSceneMesh> FinishOrRefuse(ImportedSceneMesh Imported, const std::string& Path)
    {
        if (!MeshStanding(Imported.Mesh))
            return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the imported mesh did not contain triangles" });
        FinaliseImported(Imported, Path);
        return Deliver<ImportedSceneMesh>::Result(std::move(Imported));
    }

    std::int32_t ObjIndex(const std::string& Token, std::uint32_t VertexCount)
    {
        const std::size_t Slash = Token.find('/');
        const std::string Head = Slash == std::string::npos ? Token : Token.substr(0u, Slash);
        const int Raw = std::atoi(Head.c_str());
        if (Raw > 0)
            return Raw - 1;
        if (Raw < 0)
            return static_cast<std::int32_t>(VertexCount) + Raw;
        return -1;
    }

    Deliver<ImportedSceneMesh> ImportObj(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        ImportedSceneMesh Imported;
        Imported.Format = SceneMeshFormat::WavefrontObj;
        std::string Text(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
        std::istringstream Lines(Text);
        std::string Line;
        std::string ActiveMaterial;
        while (std::getline(Lines, Line))
        {
            const std::size_t Comment = Line.find('#');
            if (Comment != std::string::npos)
                Line.erase(Comment);
            std::istringstream Tokens(Line);
            std::string Directive;
            Tokens >> Directive;
            if (Directive == "v")
            {
                double X = 0.0, Y = 0.0, Z = 0.0;
                Tokens >> X >> Y >> Z;
                AddVertex(Imported.Mesh, X, Y, Z, UnitlessToMetres);
            }
            else if (Directive == "usemtl")
            {
                Tokens >> ActiveMaterial;
                AddMaterialSlot(Imported, ActiveMaterial);
            }
            else if (Directive == "f")
            {
                std::vector<std::uint32_t> Face;
                std::string Corner;
                while (Tokens >> Corner)
                {
                    const std::int32_t Index = ObjIndex(Corner, static_cast<std::uint32_t>(Imported.Mesh.Positions.size() / 3u));
                    if (Index >= 0)
                        Face.push_back(static_cast<std::uint32_t>(Index));
                }
                AddFaceFan(Imported.Mesh, Face);
            }
        }
        return FinishOrRefuse(std::move(Imported), Path);
    }

    Deliver<ImportedSceneMesh> ImportAsciiStl(const std::string& Text, const std::string& Path)
    {
        ImportedSceneMesh Imported;
        Imported.Format = SceneMeshFormat::Stl;
        std::istringstream Lines(Text);
        std::string Token;
        std::vector<std::uint32_t> Face;
        while (Lines >> Token)
        {
            if (Token == "solid")
            {
                std::string Name;
                Lines >> Name;
                AddMaterialSlot(Imported, Name);
            }
            else if (Token == "vertex")
            {
                double X = 0.0, Y = 0.0, Z = 0.0;
                Lines >> X >> Y >> Z;
                const std::uint32_t Vertex = static_cast<std::uint32_t>(Imported.Mesh.Positions.size() / 3u);
                AddVertex(Imported.Mesh, X, Y, Z, UnitlessToMetres);
                Face.push_back(Vertex);
                if (Face.size() == 3u)
                {
                    AddTriangle(Imported.Mesh, Face[0], Face[1], Face[2]);
                    Face.clear();
                }
            }
        }
        return FinishOrRefuse(std::move(Imported), Path);
    }

    float ReadFloat32(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
    {
        float Value = 0.0f;
        std::memcpy(&Value, Bytes.data() + Offset, sizeof(float));
        return Value;
    }

    Deliver<ImportedSceneMesh> ImportBinaryStl(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        if (Bytes.size() < 84u)
            return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the binary STL header is incomplete" });
        std::uint32_t TriangleCount = 0u;
        std::memcpy(&TriangleCount, Bytes.data() + 80u, sizeof(std::uint32_t));
        if (84ull + static_cast<unsigned long long>(TriangleCount) * 50ull > Bytes.size())
            return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the binary STL triangle run is incomplete" });
        ImportedSceneMesh Imported;
        Imported.Format = SceneMeshFormat::Stl;
        for (std::uint32_t Triangle = 0u; Triangle < TriangleCount; ++Triangle)
        {
            const std::size_t Base = 84u + static_cast<std::size_t>(Triangle) * 50u + 12u;
            std::uint32_t Face[3] = {};
            for (std::uint32_t Corner = 0u; Corner < 3u; ++Corner)
            {
                Face[Corner] = static_cast<std::uint32_t>(Imported.Mesh.Positions.size() / 3u);
                AddVertex(Imported.Mesh, ReadFloat32(Bytes, Base + Corner * 12u + 0u),
                                          ReadFloat32(Bytes, Base + Corner * 12u + 4u),
                                          ReadFloat32(Bytes, Base + Corner * 12u + 8u), UnitlessToMetres);
            }
            AddTriangle(Imported.Mesh, Face[0], Face[1], Face[2]);
        }
        return FinishOrRefuse(std::move(Imported), Path);
    }

    Deliver<ImportedSceneMesh> ImportStl(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        const bool LooksAscii = Bytes.size() >= 5u && std::memcmp(Bytes.data(), "solid", 5u) == 0;
        if (LooksAscii)
        {
            std::string Text(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
            Deliver<ImportedSceneMesh> Ascii = ImportAsciiStl(Text, Path);
            if (Ascii.Resolved)
                return Ascii;
        }
        return ImportBinaryStl(Bytes, Path);
    }

    Deliver<ImportedSceneMesh> ImportAsciiPly(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        ImportedSceneMesh Imported;
        Imported.Format = SceneMeshFormat::Ply;
        std::string Text(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
        std::istringstream Lines(Text);
        std::string Line;
        std::uint32_t VertexCount = 0u;
        std::uint32_t FaceCount = 0u;
        bool Header = true;
        while (Header && std::getline(Lines, Line))
        {
            std::istringstream Tokens(Line);
            std::string A, B;
            Tokens >> A >> B;
            if (A == "format" && B != "ascii")
                return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "only ASCII PLY is supported by the MVP importer" });
            if (A == "element" && B == "vertex") Tokens >> VertexCount;
            if (A == "element" && B == "face") Tokens >> FaceCount;
            if (A == "end_header") Header = false;
        }
        for (std::uint32_t Index = 0u; Index < VertexCount && std::getline(Lines, Line); ++Index)
        {
            std::istringstream Tokens(Line);
            double X = 0.0, Y = 0.0, Z = 0.0;
            Tokens >> X >> Y >> Z;
            AddVertex(Imported.Mesh, X, Y, Z, UnitlessToMetres);
        }
        for (std::uint32_t Index = 0u; Index < FaceCount && std::getline(Lines, Line); ++Index)
        {
            std::istringstream Tokens(Line);
            std::uint32_t Count = 0u;
            Tokens >> Count;
            std::vector<std::uint32_t> Face;
            for (std::uint32_t Corner = 0u; Corner < Count; ++Corner)
            {
                std::uint32_t Vertex = 0u;
                Tokens >> Vertex;
                Face.push_back(Vertex);
            }
            AddFaceFan(Imported.Mesh, Face);
        }
        return FinishOrRefuse(std::move(Imported), Path);
    }

    std::vector<double> ParseNumberArrayAfter(const std::string& Text, const std::string& Marker)
    {
        std::vector<double> Values;
        const std::size_t MarkerAt = Text.find(Marker);
        if (MarkerAt == std::string::npos)
            return Values;
        const std::size_t Open = Text.find('[', MarkerAt);
        const std::size_t Close = Text.find(']', Open);
        if (Open == std::string::npos || Close == std::string::npos)
            return Values;
        std::string Run = Text.substr(Open + 1u, Close - Open - 1u);
        for (char& Character : Run)
            if (Character == ',') Character = ' ';
        std::istringstream Tokens(Run);
        double Value = 0.0;
        while (Tokens >> Value)
            Values.push_back(Value);
        return Values;
    }

    std::vector<std::int32_t> ParseIntegerArrayAfter(const std::string& Text, const std::string& Marker)
    {
        std::vector<std::int32_t> Values;
        const std::vector<double> RealValues = ParseNumberArrayAfter(Text, Marker);
        Values.reserve(RealValues.size());
        for (double Value : RealValues)
            Values.push_back(static_cast<std::int32_t>(Value));
        return Values;
    }

    Deliver<ImportedSceneMesh> ImportAsciiFbx(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        ImportedSceneMesh Imported;
        Imported.Format = SceneMeshFormat::FbxAscii;
        std::string Text(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
        const std::vector<double> Vertices = ParseNumberArrayAfter(Text, "Vertices:");
        const std::vector<std::int32_t> Polygon = ParseIntegerArrayAfter(Text, "PolygonVertexIndex:");
        for (std::size_t Index = 0u; Index + 2u < Vertices.size(); Index += 3u)
            AddVertex(Imported.Mesh, Vertices[Index], Vertices[Index + 1u], Vertices[Index + 2u], UnitlessToMetres);
        std::vector<std::uint32_t> Face;
        for (std::int32_t Raw : Polygon)
        {
            const bool End = Raw < 0;
            const std::uint32_t Vertex = static_cast<std::uint32_t>(End ? -Raw - 1 : Raw);
            Face.push_back(Vertex);
            if (End)
            {
                AddFaceFan(Imported.Mesh, Face);
                Face.clear();
            }
        }
        AddMaterialSlot(Imported, "FBXMaterialSlot0");
        return FinishOrRefuse(std::move(Imported), Path);
    }

    Deliver<ImportedSceneMesh> ImportSimpleGltfJson(const std::string& Json, const std::vector<std::uint8_t>& Binary, const std::string& Path, SceneMeshFormat Format)
    {
        ImportedSceneMesh Imported;
        Imported.Format = Format;

        // MVP route 1: a tiny authoring/test JSON with explicit arrays. This is useful for generated tests and
        // keeps the importer deterministic even when no full JSON library is linked into the document unit.
        std::vector<double> Positions = ParseNumberArrayAfter(Json, "\"positions\"");
        std::vector<std::int32_t> Indices = ParseIntegerArrayAfter(Json, "\"indices\"");
        if (!Positions.empty())
        {
            for (std::size_t Index = 0u; Index + 2u < Positions.size(); Index += 3u)
                AddVertex(Imported.Mesh, Positions[Index], Positions[Index + 1u], Positions[Index + 2u], Format == SceneMeshFormat::GltfBinary || Format == SceneMeshFormat::GltfText ? 1.0 : UnitlessToMetres);
            if (!Indices.empty())
                for (std::size_t Index = 0u; Index + 2u < Indices.size(); Index += 3u)
                    AddTriangle(Imported.Mesh, static_cast<std::uint32_t>(Indices[Index]), static_cast<std::uint32_t>(Indices[Index + 1u]), static_cast<std::uint32_t>(Indices[Index + 2u]));
            else
                for (std::uint32_t Index = 0u; Index + 2u < Imported.Mesh.Positions.size() / 3u; Index += 3u)
                    AddTriangle(Imported.Mesh, Index, Index + 1u, Index + 2u);
        }

        // MVP route 2: GLB files generated by tools often carry a single binary float32 POSITION accessor and
        // optional uint16/uint32 indices. The parsing below is deliberately conservative; unsupported layouts
        // refuse rather than guessing.
        if (!MeshStanding(Imported.Mesh) && !Binary.empty() && Json.find("POSITION") != std::string::npos)
        {
            const std::size_t FloatCount = Binary.size() / sizeof(float);
            if (FloatCount >= 9u)
            {
                const float* Floats = reinterpret_cast<const float*>(Binary.data());
                const std::uint32_t VertexCount = static_cast<std::uint32_t>(FloatCount / 3u);
                const std::uint32_t Used = std::min<std::uint32_t>(VertexCount, 65535u);
                for (std::uint32_t Vertex = 0u; Vertex < Used; ++Vertex)
                    AddVertex(Imported.Mesh, Floats[Vertex * 3u + 0u], Floats[Vertex * 3u + 1u], Floats[Vertex * 3u + 2u], 1.0);
                for (std::uint32_t Vertex = 0u; Vertex + 2u < Used; Vertex += 3u)
                    AddTriangle(Imported.Mesh, Vertex, Vertex + 1u, Vertex + 2u);
            }
        }

        const std::size_t Materials = Json.find("materials");
        if (Materials != std::string::npos)
            AddMaterialSlot(Imported, "glTFMaterialSlot0");
        return FinishOrRefuse(std::move(Imported), Path);
    }

    Deliver<ImportedSceneMesh> ImportGltfText(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        return ImportSimpleGltfJson(std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size()), {}, Path, SceneMeshFormat::GltfText);
    }

    std::uint32_t ReadU32(const std::vector<std::uint8_t>& Bytes, std::size_t Offset)
    {
        std::uint32_t Value = 0u;
        if (Offset + sizeof(std::uint32_t) <= Bytes.size())
            std::memcpy(&Value, Bytes.data() + Offset, sizeof(std::uint32_t));
        return Value;
    }

    Deliver<ImportedSceneMesh> ImportGlb(const std::vector<std::uint8_t>& Bytes, const std::string& Path)
    {
        if (Bytes.size() < 20u || ReadU32(Bytes, 0u) != 0x46546C67u)
            return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the GLB header is not recognised" });
        std::string Json;
        std::vector<std::uint8_t> Binary;
        std::size_t Offset = 12u;
        while (Offset + 8u <= Bytes.size())
        {
            const std::uint32_t Length = ReadU32(Bytes, Offset);
            const std::uint32_t Subject = ReadU32(Bytes, Offset + 4u);
            Offset += 8u;
            if (Offset + Length > Bytes.size())
                break;
            if (Subject == 0x4E4F534Au)
                Json.assign(reinterpret_cast<const char*>(Bytes.data() + Offset), Length);
            else if (Subject == 0x004E4942u)
                Binary.assign(Bytes.begin() + static_cast<std::ptrdiff_t>(Offset), Bytes.begin() + static_cast<std::ptrdiff_t>(Offset + Length));
            Offset += Length;
        }
        if (Json.empty())
            return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the GLB JSON chunk is absent" });
        return ImportSimpleGltfJson(Json, Binary, Path, SceneMeshFormat::GltfBinary);
    }
}

SceneMeshFormat ClassifySceneMeshFormat(const std::string& Path)
{
    const std::string Extension = Lower(std::filesystem::path(Path).extension().string());
    if (Extension == ".obj") return SceneMeshFormat::WavefrontObj;
    if (Extension == ".gltf") return SceneMeshFormat::GltfText;
    if (Extension == ".glb") return SceneMeshFormat::GltfBinary;
    if (Extension == ".fbx") return SceneMeshFormat::FbxAscii;
    if (Extension == ".stl") return SceneMeshFormat::Stl;
    if (Extension == ".ply") return SceneMeshFormat::Ply;
    return SceneMeshFormat::Unsupported;
}

bool SceneMeshFormatSupported(const std::string& Path)
{
    return ClassifySceneMeshFormat(Path) != SceneMeshFormat::Unsupported;
}

Deliver<ImportedSceneMesh> ImportSceneMeshFile(const std::string& Path)
{
    const SceneMeshFormat Format = ClassifySceneMeshFormat(Path);
    if (Format == SceneMeshFormat::Unsupported)
        return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the mesh import format is unsupported" });
    const Deliver<std::vector<std::uint8_t>> Bytes = ReadBytes(Path);
    if (!Bytes.Resolved)
        return Deliver<ImportedSceneMesh>::Refuse(Bytes.Error);

    switch (Format)
    {
        case SceneMeshFormat::WavefrontObj: return ImportObj(Bytes.Resolve(), Path);
        case SceneMeshFormat::GltfText:     return ImportGltfText(Bytes.Resolve(), Path);
        case SceneMeshFormat::GltfBinary:   return ImportGlb(Bytes.Resolve(), Path);
        case SceneMeshFormat::FbxAscii:     return ImportAsciiFbx(Bytes.Resolve(), Path);
        case SceneMeshFormat::Stl:          return ImportStl(Bytes.Resolve(), Path);
        case SceneMeshFormat::Ply:          return ImportAsciiPly(Bytes.Resolve(), Path);
        case SceneMeshFormat::Unsupported:  break;
    }
    return Deliver<ImportedSceneMesh>::Refuse({ RefusalReason::ContentUnsupported, "the mesh import format is unsupported" });
}

} // namespace Slate
