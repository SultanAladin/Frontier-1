/*====================================================================================================================================
                                                         JSONWRITER.H
====================================================================================================================================*/
// 🧩 Minimal streaming JSON writer shared by the inspection encoders

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLASSES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Minimal streaming JSON writer: the caller opens and closes scopes; commas are inserted automatically.
class JsonWriter
{
public:
    void OpenObject(const char* Key = nullptr)  { Prefix(Key); Text += '{'; FirstInScope.push_back(true); }
    void CloseObject()                          { Text += '}'; FirstInScope.pop_back(); }
    void OpenArray(const char* Key = nullptr)   { Prefix(Key); Text += '['; FirstInScope.push_back(true); }
    void CloseArray()                           { Text += ']'; FirstInScope.pop_back(); }

    void String(const char* Key, const std::string& Content)
    {
        Prefix(Key);
        Text += '"';
        for (char Character : Content)
        {
            if (Character == '"' || Character == '\\') { Text += '\\'; Text += Character; }
            else if (Character == '\n') Text += "\\n";
            else if (static_cast<unsigned char>(Character) < 0x20) Text += ' ';
            else Text += Character;
        }
        Text += '"';
    }

    void Number(const char* Key, double Magnitude)
    {
        Prefix(Key);
        if (!std::isfinite(Magnitude)) { Text += "null"; return; }
        char Buffer[32];
        std::snprintf(Buffer, sizeof(Buffer), "%.10g", Magnitude);
        Text += Buffer;
    }

    void Integer(const char* Key, long Count)     { Prefix(Key); Text += std::to_string(Count); }
    void Boolean(const char* Key, bool Condition) { Prefix(Key); Text += Condition ? "true" : "false"; }

    void Point(const char* Key, Vector3 Position)
    {
        OpenArray(Key);
        Number(nullptr, Position.X);
        Number(nullptr, Position.Y);
        Number(nullptr, Position.Z);
        CloseArray();
    }

    [[nodiscard]] const std::string& Result() const noexcept { return Text; }

private:
    void Prefix(const char* Key)
    {
        if (!FirstInScope.empty())
        {
            if (!FirstInScope.back()) Text += ',';
            FirstInScope.back() = false;
        }
        if (Key != nullptr) { Text += '"'; Text += Key; Text += "\":"; }
    }

    std::string       Text;
    std::vector<bool> FirstInScope;
};

} // namespace Frontier::GeometrySpace::Parametric
