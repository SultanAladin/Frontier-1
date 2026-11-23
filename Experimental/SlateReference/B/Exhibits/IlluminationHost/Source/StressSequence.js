//============================================================================================================================================
//                                                             STRESSSEQUENCE.JS
//============================================================================================================================================
// 🧩 The stress test at run time: enables a prefix of the pre-built pool, optionally sets part of it moving, keeps every stage informed.

// 📝 Everything a stress object needs — facet set, distance volume, captured cards, occupant record — was built at
//    bring-up with the pool, so enabling objects is bookkeeping, not construction: the union pass walks one more span
//    of occupants, the draw runs grow, the cards join the relighting lists, the vehicle gains colliders, and the bricks
//    under the objects are rewritten once. Moving objects take the same path the vehicle does every frame: a dynamic
//    role, a fresh transform, rewritten bricks and every-frame card relighting — the honest worst case for the cache.

import { OccupantRole, OccupantStride } from "./FacetStructure.js";
import { ConstructRigidTransform } from "./TransformProjection.js";

export class StressSequence
{
    // 📝 Host supplies Scene, Volumes, Cards, Distance, Vehicle, Exchange and OccupantRecords.
    constructor(Host)
    {
        this.Host   = Host;
        this.Pool   = Host.Scene.StressPool;
        this.Motion = 0;
        this.Moving = [];
        this.Clock  = 0.0;
    }

    QueryActive()
    {
        return this.Pool.Active;
    }

    QueryCapacity()
    {
        return this.Pool.Capacity;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 ENABLING
    //--------------------------------------------------------------------------------------------------------------------

    Add(Count)
    {
        this.Assign(this.Pool.Active + Count);
    }

    Assign(Count)
    {
        const Scene = this.Host.Scene;
        const Old = this.Pool.Active;
        const New = Math.max(0, Math.min(this.Pool.Capacity, Math.round(Count)));
        if (New === Old)
            return;
        const Bounds = [];
        const Enabled = [];
        for (let Slot = Math.min(Old, New); Slot < Math.max(Old, New); ++Slot)
        {
            const Index = this.Pool.First + Slot;
            const Occupant = Scene.Occupants[Index];
            if (New < Old)
            {
                // 📝 Its current (possibly moving) bounds are rewritten without it once it leaves the union span.
                Bounds.push(Scene.QueryWorldBounds(Index));
                Occupant.Roles &= ~OccupantRole.Dynamic;
                this.RestOccupant(Occupant);
                Occupant.Inactive = true;
            }
            else
            {
                Occupant.Inactive = false;
                this.RestOccupant(Occupant);
                Bounds.push(Scene.QueryWorldBounds(Index));
                Enabled.push(Index);
            }
        }
        this.Pool.Active = New;
        this.ApplyMotion();
        this.Host.Distance.InvalidateBounds(Bounds);
        this.Host.Cards.WarmOccupants(Scene, Enabled);
        this.UploadRecords(0, Math.max(Old, New));
    }

    AssignMotion(Motion)
    {
        this.Motion = Motion;
        this.ApplyMotion();
        this.UploadRecords(0, this.Pool.Active);
    }

    RestOccupant(Occupant)
    {
        Occupant.Transform = Occupant.RestTransform;
        Occupant.PreviousTransform = Occupant.RestTransform;
        Occupant.UnionTransform = Occupant.RestTransform.slice();
    }

    // 📝 Decides which enabled slots move — none, every tenth, or all — and rebuilds what depends on that: roles,
    //    draw runs, the card relighting lists and the vehicle's colliders (moving objects do not collide).
    ApplyMotion()
    {
        const Scene = this.Host.Scene;
        const Bounds = [];
        this.Moving = [];
        for (let Slot = 0; Slot < this.Pool.Active; ++Slot)
        {
            const Index = this.Pool.First + Slot;
            const Occupant = Scene.Occupants[Index];
            const Moves = this.Motion === 2 || (this.Motion === 1 && Slot % 10 === 3);
            const WasMoving = (Occupant.Roles & OccupantRole.Dynamic) !== 0;
            if (Moves)
            {
                Occupant.Roles |= OccupantRole.Dynamic;
                this.Moving.push(Index);
            }
            else if (WasMoving)
            {
                Bounds.push(Scene.QueryWorldBounds(Index));
                Occupant.Roles &= ~OccupantRole.Dynamic;
                this.RestOccupant(Occupant);
                Bounds.push(Scene.QueryWorldBounds(Index));
            }
        }
        Scene.ConstructDrawRuns();
        this.Host.Cards.RebuildTileLists(Scene);
        const Obstacles = Scene.QueryObstacleBounds();
        this.Host.Vehicle.AssignObstacles(Obstacles);
        if (this.Host.Bodies)
            this.Host.Bodies.AssignObstacles(Obstacles);
        this.Host.Distance.InvalidateBounds(Bounds);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   MOTION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Moving objects bob, orbit a small circle and turn, each on its own phase; the previous transform is kept by
    //    AssignOccupantTransform for motion vectors and reservoir carriage, as for the vehicle.
    Advance(Seconds)
    {
        if (this.Moving.length === 0)
            return;
        this.Clock += Seconds;
        const Scene = this.Host.Scene;
        const Time = this.Clock;
        for (const Index of this.Moving)
        {
            const Occupant = Scene.Occupants[Index];
            const Placement = this.Pool.Placements[Occupant.StressSlot];
            const Lift = 0.35 + 0.9 * (0.5 + 0.5 * Math.sin(1.3 * Time + Placement.Phase));
            const Turn = Placement.Yaw + 0.7 * Time * (Placement.Phase > Math.PI ? 1.0 : -1.0);
            const Orbit = 0.8 * Time + Placement.Phase;
            const Position = [Placement.Position[0] + 0.6 * Math.cos(Orbit), Placement.Position[1] + 0.6 * Math.sin(Orbit),
                              Placement.Position[2] + Lift];
            Scene.AssignOccupantTransform(Index, ConstructRigidTransform(Position, [0.0, 0.0, Math.sin(0.5 * Turn), Math.cos(0.5 * Turn)]));
        }
        if (this.Motion === 2)
        {
            this.UploadRecords(0, this.Pool.Active);
            return;
        }
        for (const Index of this.Moving)
            this.UploadRecords(Index - this.Pool.First, 1);
    }

    UploadRecords(FirstSlot, Count)
    {
        if (Count <= 0)
            return;
        const First = this.Pool.First + FirstSlot;
        const Scalars = this.Host.Scene.EncodeOccupantRange(this.Host.Volumes, First, Count);
        this.Host.Exchange.Device.queue.writeBuffer(this.Host.OccupantRecords, First * OccupantStride, Scalars);
    }

    QueryReadout()
    {
        const Scene = this.Host.Scene;
        return [`objects: ${this.Pool.Active} / ${this.Pool.Capacity} · moving: ${this.Moving.length}`,
                `draws: ${Scene.DrawRuns.length} surface · ${Scene.ShadowRuns.length} × 3 shadow · union span ${Scene.QueryUnionCount()}`,
                `colliders near the car: ${(this.Host.Vehicle.NearObstacles || []).length} of ${this.Host.Vehicle.Obstacles.length}`];
    }
}
