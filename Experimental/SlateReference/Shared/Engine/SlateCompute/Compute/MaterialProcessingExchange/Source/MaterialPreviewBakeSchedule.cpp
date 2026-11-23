#include "SlateCompute/Compute/MaterialProcessingExchange/Api/MaterialPreviewBakeSchedule.h"

namespace Slate
{

Deliver<MaterialPreviewBakeJob> MaterialPreviewBakeSchedule::Schedule(
    const MaterialPreviewTile& Tile, const MaterialProcessingSnapshot& Snapshot) const
{
    if (!Tile.Active || Tile.State != MaterialPreviewState::BakeOwed)
    {
        return Deliver<MaterialPreviewBakeJob>::Refuse(
            { RefusalReason::IdentityStale, "the material preview tile does not currently require a bake" });
    }

    if (Tile.Fingerprint != Snapshot.DirtyKey.Combined)
    {
        return Deliver<MaterialPreviewBakeJob>::Refuse(
            { RefusalReason::IdentityStale, "the preview tile and material snapshot name different revisions" });
    }

    if (!Snapshot.PhysicalSurfaceResolved || !Snapshot.PhysicalPacketResolved)
    {
        return Deliver<MaterialPreviewBakeJob>::Refuse(
            { RefusalReason::ContentUnsupported, "a preview bake requires a resolved physical material packet" });
    }

    MaterialPreviewBakeJob Job;
    Job.Tile = Tile;
    Job.Physical = Snapshot.PhysicalPacket;
    Job.Fingerprint = Snapshot.DirtyKey.Combined;
    return Deliver<MaterialPreviewBakeJob>::Result(Job);
}

} // namespace Slate
