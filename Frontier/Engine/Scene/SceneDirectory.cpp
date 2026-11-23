// 🧩 Editor-machinery Outliner model: names and nesting for every registered record, Editor/Development only.

#include "Engine/Scene/SceneDirectory.h"

#include "Engine/Diagnostics/BuildConfiguration.h"

// 📝 The directory is editor machinery: in Shipping this translation unit is intentionally empty (and Shipping
//    builds do not link it at all), so a Shipping binary contains no directory (plan §5/§6).
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Scene/RecordEntry.h"

#include <algorithm>
#include <utility>

namespace Frontier
{

namespace
{

uint32_t InterfaceLogToken()
{
    const LogCategory* Category = LogCategories().QueryByName("Interface");
    return Category != nullptr ? Category->Token : 0;
}

} // namespace

SceneDirectory& SceneDirectory::Instance()
{
    static SceneDirectory Directory;
    return Directory;
}

void SceneDirectory::Register(const RecordEntry& Entry)
{
    const RecordToken Token = Entry.Token();
    std::lock_guard<std::mutex> Guard(Mutex);
    if (!Token.IsValid() || Entries.find(Token.Value) != Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory refused registration for token %llu (invalid or duplicate)",
                       static_cast<unsigned long long>(Token.Value));
        return;
    }
    DirectoryEntry& Slot    = Entries[Token.Value];
    Slot.Classification     = Entry.Classification();
    RegistrationOrder.push_back(Token);
}

void SceneDirectory::Unregister(RecordToken Token)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    if (Entries.erase(Token.Value) == 0)
    {
        return;   // Never registered (or already gone): destruction stays silent and safe.
    }
    RegistrationOrder.erase(std::remove(RegistrationOrder.begin(), RegistrationOrder.end(), Token),
                            RegistrationOrder.end());
    for (auto& [Value, Slot] : Entries)
    {
        (void)Value;
        if (Slot.Parent == Token)
        {
            Slot.Parent = RecordToken{};   // Re-rooted: no dangling up-links survive a parent.
        }
    }
}


#if defined(FRONTIER_EDITOR_PROFILE)
void SceneDirectory::ConfigureEditorDescription(RecordToken Token, RecordEditorDescription Description)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return;   // Registration precedes description construction; an unknown token owns nothing.
    }
    It->second.EditorDescription    = std::move(Description);
    It->second.HasEditorDescription = true;
}

const RecordEditorDescription* SceneDirectory::QueryEditorDescription(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end() || !It->second.HasEditorDescription)
    {
        return nullptr;
    }
    return &It->second.EditorDescription;
}
#endif

bool SceneDirectory::ConfigureName(RecordToken Token, std::string Name)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory cannot name unknown token %llu",
                       static_cast<unsigned long long>(Token.Value));
        return false;
    }
    It->second.Name = std::move(Name);
    return true;
}

bool SceneDirectory::ConfigureParent(RecordToken Child, RecordToken Parent)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  ChildIt = Entries.find(Child.Value);
    if (ChildIt == Entries.end() || Child == Parent ||
        (Parent.IsValid() && Entries.find(Parent.Value) == Entries.end()))
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                       "SceneDirectory refused parenting %llu under %llu (unknown or self)",
                       static_cast<unsigned long long>(Child.Value), static_cast<unsigned long long>(Parent.Value));
        return false;
    }
    // Walk the proposed parent's ancestry: meeting Child would close a cycle.
    for (RecordToken Cursor = Parent; Cursor.IsValid();)
    {
        if (Cursor == Child)
        {
            WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "SceneDirectory.cpp",
                           "SceneDirectory refused parenting %llu under %llu (cycle)",
                           static_cast<unsigned long long>(Child.Value), static_cast<unsigned long long>(Parent.Value));
            return false;
        }
        const auto CursorIt = Entries.find(Cursor.Value);
        Cursor              = CursorIt != Entries.end() ? CursorIt->second.Parent : RecordToken{};
    }
    ChildIt->second.Parent = Parent;
    return true;
}

bool SceneDirectory::ConfigureVisible(RecordToken Token, bool Visible)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return false;
    }
    It->second.Visible = Visible;
    return true;
}

bool SceneDirectory::QueryVisible(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() && It->second.Visible;
}

bool SceneDirectory::QueryRegistered(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.find(Token.Value) != Entries.end();
}

std::string SceneDirectory::QueryName(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    if (It == Entries.end())
    {
        return {};
    }
    if (!It->second.Name.empty())
    {
        return It->second.Name;
    }
    return std::string(RecordClassificationName(It->second.Classification)) + " " + std::to_string(Token.Value);
}

RecordToken SceneDirectory::QueryParent(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second.Parent : RecordToken{};
}

RecordClassification SceneDirectory::QueryClassification(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second.Classification : RecordClassification::Unclassified;
}

uint32_t SceneDirectory::QueryChildCount(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    uint32_t                    Count = 0;
    for (const auto& [Value, Slot] : Entries)
    {
        (void)Value;
        if (Slot.Parent == Token)
        {
            ++Count;
        }
    }
    return Count;
}

size_t SceneDirectory::QueryCount() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.size();
}

void SceneDirectory::CopyRegistrationOrder(std::vector<RecordToken>& Out) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    Out = RegistrationOrder;
}

} // namespace Frontier

#endif // Editor/Development profiles
