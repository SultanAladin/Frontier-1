// 🧩 Runtime token-to-record registry, present in every profile: construction inserts, destruction removes.

#include "Engine/Scene/RecordStore.h"

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Scene/RecordEntry.h"

namespace Frontier
{

namespace
{

// Registration refusals are editor-machinery diagnostics: they ride the Interface log channel.
uint32_t InterfaceLogToken()
{
    const LogCategory* Category = LogCategories().QueryByName("Interface");
    return Category != nullptr ? Category->Token : 0;
}

} // namespace

RecordStore& RecordStore::Instance()
{
    static RecordStore Store;
    return Store;
}

bool RecordStore::Insert(RecordEntry& Entry)
{
    const RecordToken Token = Entry.Token();
    if (!Token.IsValid())
    {
        WriteLog(InterfaceLogToken(), LogSeverity::Error, LogVerbosity::Log,
                 "RecordStore refused a record carrying an invalid token", "RecordStore.cpp");
        return false;
    }

    std::lock_guard<std::mutex> Guard(Mutex);
    if (Entries.find(Token.Value) != Entries.end())
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Error, LogVerbosity::Log, "RecordStore.cpp",
                       "RecordStore refused duplicate token %llu", static_cast<unsigned long long>(Token.Value));
        return false;
    }
    Entries.emplace(Token.Value, &Entry);
    return true;
}

void RecordStore::Remove(RecordToken Token)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    if (Entries.erase(Token.Value) == 0)
    {
        WriteLogFormat(InterfaceLogToken(), LogSeverity::Warning, LogVerbosity::Log, "RecordStore.cpp",
                       "RecordStore removed unknown token %llu (already gone)",
                       static_cast<unsigned long long>(Token.Value));
    }
}

RecordEntry* RecordStore::Find(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto                  It = Entries.find(Token.Value);
    return It != Entries.end() ? It->second : nullptr;
}

bool RecordStore::Authenticate(RecordToken Token, const RecordEntry& Entry) const
{
    const RecordEntry* Found = Find(Token);
    return Found == &Entry && Entry.Token() == Token;
}

size_t RecordStore::QueryCount() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Entries.size();
}

} // namespace Frontier
