/*==========================================================================================================================================
                                                             PROJECT DESCRIPTOR
                                     ------------------------------------------------------------------
                        TOML decoding via toml++ (the one sanctioned engine format); fingerprints accept integer or
                                                            quoted-hex spelling.
==========================================================================================================================================*/

// 🧩 TOML decoding via toml++ (the one sanctioned engine format); fingerprints accept integer or quoted-hex spelling.

#include "ProjectDescriptor.h"

#include "ProjectContent.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <toml++/toml.hpp>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::string ReadString(const toml::table& Table, const char* Key, const std::string& Fallback)
{
    if (const auto Value = Table[Key].value<std::string>())
    {
        return *Value;
    }
    return Fallback;
}

uint32_t ReadUnsigned(const toml::table& Table, const char* Key, uint32_t Fallback)
{
    if (const auto Value = Table[Key].value<int64_t>())
    {
        return *Value < 0 ? Fallback : static_cast<uint32_t>(*Value);
    }
    return Fallback;
}

uint64_t ReadFingerprint(const toml::table& Table, const char* Key)
{
    const toml::node_view Node = Table[Key];
    if (const auto Integer = Node.value<int64_t>())
    {
        return static_cast<uint64_t>(*Integer);   // [bits] - Hex literals keep their bit pattern through int64
    }
    if (const auto Text = Node.value<std::string>())
    {
        return std::strtoull(Text->c_str(), nullptr, 0);
    }
    return 0;
}

// 📝 TOML integers are signed 64-bit, so a fingerprint literal with the top bit set — the committed
//    0xb6725f24d0869173 in every repository descriptor — is rejected by conforming parsers. The text is
//    read first and that one bare-hex value quoted in memory (underscores stripped); the file on disk is
//    never modified, and ReadFingerprint's string branch recovers the exact bits via strtoull.
void QuoteBareHexFingerprint(std::string& Text)
{
    const std::string Key   = "InterfaceFingerprint";
    size_t            At    = Text.find(Key);
    if (At == std::string::npos)
    {
        return;
    }
    At = Text.find('=', At + Key.size());
    if (At == std::string::npos)
    {
        return;
    }
    size_t Begin = At + 1;
    while (Begin < Text.size() && std::isspace(static_cast<unsigned char>(Text[Begin])) != 0)
    {
        ++Begin;
    }
    if (Begin + 1 >= Text.size() || Text[Begin] != '0' || (Text[Begin + 1] != 'x' && Text[Begin + 1] != 'X'))
    {
        return;   // Quoted or decimal spelling: the parser handles it directly
    }
    size_t End = Begin + 2;
    while (End < Text.size() &&
           (std::isxdigit(static_cast<unsigned char>(Text[End])) != 0 || Text[End] == '_'))
    {
        ++End;
    }
    std::string Hex = "0x";
    for (size_t Index = Begin + 2; Index < End; ++Index)
    {
        if (Text[Index] != '_')
        {
            Hex.push_back(Text[Index]);
        }
    }
    Text.replace(Begin, End - Begin, "\"" + Hex + "\"");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool DecodeProjectDescriptor(const std::filesystem::path& Path, ProjectDescriptor& Out, std::string& Diagnostic)
{
    Out        = ProjectDescriptor{};
    Diagnostic.clear();

    std::ifstream Input(Path, std::ios::binary);
    if (!Input.is_open())
    {
        Diagnostic = "Descriptor not found: " + EncodeUtf8Path(Path);
        return false;
    }
    std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    QuoteBareHexFingerprint(Text);

    toml::table Root;
    try
    {
        Root = toml::parse(Text, EncodeUtf8Path(Path));
    }
    catch (const toml::parse_error& Error)
    {
        Diagnostic = Error.description();
        return false;
    }

    const toml::table* Project = Root["Project"].as_table();
    if (Project == nullptr)
    {
        Diagnostic = "Descriptor has no [Project] table";
        return false;
    }

    Out.Name                  = ReadString(*Project, "ProjectName", "");
    Out.ContentLocation       = ReadString(*Project, "ContentLocation", "Content");
    Out.OpeningScene          = ReadString(*Project, "OpeningScene", "");
    Out.CodeImage             = ReadString(*Project, "CodeImage", "");
    Out.ProjectFormatNumber   = ReadUnsigned(*Project, "ProjectFormatNumber", 0);
    Out.CodeInterchangeNumber = ReadUnsigned(*Project, "CodeInterchangeNumber", 0);
    Out.InterfaceFingerprint  = ReadFingerprint(*Project, "InterfaceFingerprint");

    if (Out.Name.empty())
    {
        Diagnostic = "Descriptor [Project] table has no ProjectName";
        return false;
    }

    if (const toml::array* Categories = Root["LogCategories"].as_array())
    {
        for (const toml::node& Node : *Categories)
        {
            const toml::table* Entry = Node.as_table();
            if (Entry == nullptr)
            {
                continue;
            }
            ProjectLogCategory Category;
            Category.Name   = ReadString(*Entry, "Name", "");
            Category.Colour = ReadString(*Entry, "Colour", "");
            if (!Category.Name.empty())
            {
                Out.LogCategories.push_back(Category);
            }
        }
    }

    std::error_code       Error;
    Out.DescriptorPath   = std::filesystem::absolute(Path, Error);
    Out.ProjectDirectory = Out.DescriptorPath.parent_path();
    return true;
}

} // namespace Frontier
