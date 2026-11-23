/*==========================================================================================================================================
                                                               SCENE RECORD
                                     ------------------------------------------------------------------
             The concrete empty scene object: what the editor spawns when no richer record type applies yet.
==========================================================================================================================================*/

// 🧩 The concrete empty scene object: what the editor spawns when no richer record type applies yet.

#pragma once

#include "RecordEntry.h"

namespace Frontier
{

// 📝 A leaf RecordEntry with no payload of its own — the Outliner's "empty object". Richer scene objects
//    (vehicle, geometry, lights…) derive from RecordEntry directly as their phases land; until then, editor
//    spawn requests and scene placeholders are SceneRecords named through the SceneDirectory.
class SceneRecord final : public RecordEntry
{
  public:
    explicit SceneRecord(RecordClassification Classification) : RecordEntry(Classification) {}
};

} // namespace Frontier
