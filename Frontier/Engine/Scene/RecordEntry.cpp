// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#include "Engine/Scene/RecordEntry.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

#include <utility>

namespace Frontier
{

RecordEntry::RecordEntry(RecordClassification Classification)
    : IssuedToken(TokenIssuer::Issue())
    , EntryClassification(Classification)
{
    RecordStore::Instance().Insert(*this);
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory::Instance().Register(*this);   // 📝 Outliner visibility only; Shipping contains no directory
#endif
}

#if defined(FRONTIER_EDITOR_PROFILE)
void RecordEntry::RegisterEditorDescription(RecordEditorDescription Description)
{
    SceneDirectory::Instance().ConfigureEditorDescription(IssuedToken, std::move(Description));
}
#endif

RecordEntry::~RecordEntry()
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory::Instance().Unregister(IssuedToken);
#endif
    RecordStore::Instance().Remove(IssuedToken);
}

} // namespace Frontier
