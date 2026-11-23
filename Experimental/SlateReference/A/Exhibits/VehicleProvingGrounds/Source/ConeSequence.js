//============================================================================================================================================
//                                                             CONESEQUENCE.JS
//============================================================================================================================================
// 🧩 The training arena's cones: rigid bodies asleep on their marks until the car arrives, rezzed back into place when a drill restarts.

// 📝 A cone at rest is a sleeping body in the RigidBodyWorld and a static occupant everywhere else — in the distance
//    field, carded, relit round-robin — so a hundred and thirty of them cost nothing. When something with momentum
//    reaches one it wakes: from then until it settles its occupant follows the body every frame (Moving: relit every
//    frame, its bricks rewritten as it moves). A cone that has left its mark is Displaced; the drills count those.
//
//    Resetting a cone is a small derez/rez: it bursts into cubes where it lies, vanishes, and its cubes assemble it
//    again on its mark.

import { ConeConstants, ResolveConeHullPoints } from "./CourseStructure.js";
import { ConstructHullPlanes, ConstructContactSamples } from "./RigidBodyWorld.js";
import { OccupantStride } from "./FacetStructure.js";
import { ConstructRigidTransform, RotateVector, LengthVector } from "./TransformProjection.js";

const RezSeconds = 0.9;   // [s] a reset cone is hidden this long while its cubes fly in

export class ConeSequence
{
    constructor(Host)
    {
        this.Host  = Host;
        this.Scene = Host.Scene;
        this.Clock = 0.0;
        const Block = this.Scene.ConeBlock;
        const Lift = ConeConstants.CentreHeight;
        // 📝 One hull for every cone, about its centre of mass.
        const Points = ResolveConeHullPoints().map(([X, Y, Z]) => [X, Y, Z - Lift]);
        const Planes = ConstructHullPlanes(Points);
        const Samples = ConstructContactSamples(Points, Planes, 1.0e-4, false, false);
        this.Hull = { Points, Planes, Samples };
        this.Metrics = { Displaced: 0, Moving: 0, Hits: 0 };
        this.Cones = Block.Placements.map((Placement, Index) =>
        {
            const Half = 0.5 * Placement.Yaw;
            const Orientation = [0.0, 0.0, Math.sin(Half), Math.cos(Half)];
            const Rest = { Position: [Placement.Position[0], Placement.Position[1], Lift], Orientation };
            const Cone = { Index, Occupant: Block.First + Index, Group: Placement.Group, Name: Placement.Name, Rest, Body: null,
                           Displaced: false, Settling: false, HiddenUntil: -1.0 };
            Cone.Body = Host.Bodies.AddBody({
                Kind: "cone", Owner: Cone, Position: Rest.Position, Orientation, Mass: ConeConstants.Mass, Inertia: [0.09, 0.09, 0.058],
                Points, Planes, Samples, Restitution: 0.35, Friction: 0.75, Sleeping: true
            });
            return Cone;
        });
    }

    // 📝 The occupant (base-centred) transform of a cone body (centre-of-mass-centred).
    ResolveTransform(Body)
    {
        const Down = RotateVector(Body.Orientation, [0.0, 0.0, -ConeConstants.CentreHeight]);
        return ConstructRigidTransform([Body.Position[0] + Down[0], Body.Position[1] + Down[1], Body.Position[2] + Down[2]], Body.Orientation);
    }

    IsDisplaced(Cone)
    {
        const Body = Cone.Body;
        const Offset = Math.hypot(Body.Position[0] - Cone.Rest.Position[0], Body.Position[1] - Cone.Rest.Position[1]);
        const Upright = 1.0 - 2.0 * (Body.Orientation[0] ** 2 + Body.Orientation[1] ** 2);
        return Offset > 0.06 || Upright < 0.985;
    }

    // 📝 After the rigid-body world steps: woken cones count as hits, moved cones follow their bodies, and the range of
    //    records that changed is uploaded once.
    Advance(Seconds)
    {
        this.Clock += Seconds;
        const Scene = this.Scene;
        let First = Infinity, Last = -1, ListsChanged = false, Displaced = 0, Moving = 0;
        for (const Cone of this.Cones)
        {
            const Body = Cone.Body;
            const Occupant = Scene.Occupants[Cone.Occupant];
            if (Cone.HiddenUntil >= 0.0 && this.Clock >= Cone.HiddenUntil)
            {
                Cone.HiddenUntil = -1.0;
                Occupant.Inactive = false;
                ListsChanged = true;
                this.Host.Distance.InvalidateBounds([Scene.QueryWorldBounds(Cone.Occupant)]);
                First = Math.min(First, Cone.Occupant);
                Last = Math.max(Last, Cone.Occupant);
            }
            if (Body.Woke)
            {
                Body.Woke = false;
                this.Metrics.Hits++;
                if (this.OnHit)
                    this.OnHit(Cone);
            }
            const IsMoving = !Body.Sleeping;
            if (Occupant.Moving !== IsMoving)
            {
                Occupant.Moving = IsMoving;
                ListsChanged = true;
            }
            if (IsMoving)
                Moving++;
            if (Body.Moved)
            {
                Scene.AssignOccupantTransform(Cone.Occupant, this.ResolveTransform(Body));
                Cone.Displaced = this.IsDisplaced(Cone);
                Cone.Settling = true;
                First = Math.min(First, Cone.Occupant);
                Last = Math.max(Last, Cone.Occupant);
            }
            else if (Cone.Settling)
            {
                // 📝 One more upload once it stops, so its previous transform (motion vectors) catches up.
                Scene.RetainOccupantTransform(Cone.Occupant);
                Cone.Settling = false;
                First = Math.min(First, Cone.Occupant);
                Last = Math.max(Last, Cone.Occupant);
            }
            if (Cone.Displaced)
                Displaced++;
        }
        if (Last >= First)
        {
            const Scalars = Scene.EncodeOccupantRange(this.Host.Volumes, First, Last - First + 1);
            this.Host.Exchange.Device.queue.writeBuffer(this.Host.OccupantRecords, First * OccupantStride, Scalars);
        }
        if (ListsChanged)
        {
            Scene.ConstructDrawRuns();
            this.Host.Cards.RebuildTileLists(Scene);
        }
        this.Metrics.Displaced = Displaced;
        this.Metrics.Moving = Moving;
    }

    QueryGroup(Group)
    {
        return this.Cones.filter((Cone) => Cone.Group === Group);
    }

    // 📝 Every displaced cone of the groups given (all when null) goes back to its mark: it bursts into cubes where it
    //    lies (Rez), vanishes, and its cubes assemble it on the mark. Returns how many were reset.
    ResetCones(Groups = null, Rez = true, Only = null)
    {
        const Scene = this.Scene;
        const Effects = this.Host.Effects;
        const Box = { Centre: [0.0, 0.0, 0.5 * ConeConstants.TipTop], Extent: [ConeConstants.BaseHalf, ConeConstants.BaseHalf, 0.5 * ConeConstants.TipTop] };
        let Count = 0, First = Infinity, Last = -1;
        for (const Cone of this.Cones)
        {
            if (Only ? Cone !== Only : (Groups && !Groups.includes(Cone.Group)))
                continue;
            if (!Only && !Cone.Displaced && Cone.Body.Sleeping && !this.IsDisplaced(Cone))
                continue;
            const Occupant = Scene.Occupants[Cone.Occupant];
            const Before = Scene.QueryWorldBounds(Cone.Occupant);
            if (Rez && Effects)
                Effects.DerezBox(this.ResolveTransform(Cone.Body), Box.Centre, Box.Extent, Cone.Body.Linear);
            this.Host.Bodies.PlaceBody(Cone.Body, Cone.Rest.Position, Cone.Rest.Orientation, true);
            const Transform = this.ResolveTransform(Cone.Body);
            Occupant.Transform = Transform;
            Occupant.PreviousTransform = Transform.slice();
            Occupant.UnionTransform = Transform.slice();
            Occupant.Moving = false;
            Cone.Displaced = false;
            Cone.Settling = false;
            if (Rez && Effects)
            {
                Effects.RezBox(Transform, Box.Centre, Box.Extent);
                Cone.HiddenUntil = this.Clock + RezSeconds;
                Occupant.Inactive = true;
            }
            this.Host.Distance.InvalidateBounds([Before, Scene.QueryWorldBounds(Cone.Occupant)]);
            First = Math.min(First, Cone.Occupant);
            Last = Math.max(Last, Cone.Occupant);
            Count++;
        }
        if (Count > 0)
        {
            const Scalars = Scene.EncodeOccupantRange(this.Host.Volumes, First, Last - First + 1);
            this.Host.Exchange.Device.queue.writeBuffer(this.Host.OccupantRecords, First * OccupantStride, Scalars);
            Scene.ConstructDrawRuns();
            this.Host.Cards.RebuildTileLists(Scene);
        }
        return Count;
    }

    // 📝 One cone bursts and reassembles on its mark, displaced or not (the VFX page's test).
    BurstCone(Cone)
    {
        return this.ResetCones(null, true, Cone);
    }

    // 📝 Displaced cones of a group (a drill's penalty count).
    CountDisplaced(Groups)
    {
        return this.Cones.filter((Cone) => Groups.includes(Cone.Group) && (Cone.Displaced || this.IsDisplaced(Cone))).length;
    }

    QueryReadout()
    {
        return [`cones: ${this.Cones.length} · displaced ${this.Metrics.Displaced} · moving ${this.Metrics.Moving} · hits ${this.Metrics.Hits}`,
                `hull: ${this.Hull.Points.length} points, ${this.Hull.Planes.length} planes, ${this.Hull.Samples.length} contact samples · ${ConeConstants.Mass} kg`];
    }
}

export { LengthVector };
