//============================================================================================================================================
//                                                           FRACTURESEQUENCE.JS
//============================================================================================================================================
// 🧩 Breakable props at run time: impact energy against a material threshold, fracture, fragment bodies, settling, reassembly.

// 📝 Every breakable prop is built at bring-up as three things: an intact occupant (rastered, in the distance field,
//    carded, a collider for the car), a composite facet set holding room for its fragments, and Capacity fragment
//    occupants that sit inactive until it breaks. Breaking is therefore bookkeeping plus one vertex upload:
//
//      ① each frame, before the car steps, its hull spheres are tested against the intact props (a frame ahead, so a
//         fast car cannot tunnel); the impact energy is ½·μ·vₙ² with μ the reduced mass of car and prop;
//      ② below the material threshold nothing happens here — the prop is a collider and the car bounces off it;
//      ③ above it the fragments are taken from the baked pattern, or (Dynamic) cut on the spot at the true impact
//         point with the impact's share of energy as the budget — a faster hit buys more, smaller pieces;
//      ④ the fragments' triangles are written into the composite set (each vertex naming its fragment occupant), the
//         intact occupant leaves the distance field, the card relighting lists and the colliders, and the car pays
//         the momentum of an inelastic exchange with the prop's mass;
//      ⑤ every fragment becomes a convex rigid body in the shared RigidBodyWorld — against the course, the walls,
//         every other fragment and cone, and the car both ways (a heavy slab can be driven over) — and sleeps once
//         settled;
//      ⑥ every fragment large enough for the scene distance field to resolve gets a distance volume of its own in
//         the fragment pool, computed on the device the frame it is born, and joins the field like any occupant —
//         so the debris occludes, shadows and bounces light in the GI the frame the prop breaks;
//      ⑦ the debris stays where it lies. The prop is rebuilt only on request (Reassemble, the arena reset) or, if
//         Respawn is switched on, after the lifetime — sinking away first, and never under a parked car.
//
//    Fragments draw in ONE call per prop (the composite set) and cast shadows. They own no surface cards — a traced
//    ray that lands on one shades it from its packed albedo, the sun and the sky (CardSampling's fallback).

import { OccupantRole, OccupantStride, VertexStride } from "./FacetStructure.js";
import { FractureMaterials, ConstructSolid, FractureSolid, ResolveFragments, FragmentTriangleBudget } from "./FractureStructure.js";
import { HullSpheres } from "./VehicleSolver.js";
import { VehicleGeometry } from "./VehicleSpecification.js";
import { AddVector, SubtractVector, ScaleVector, DotVector, CrossVector, LengthVector, NormalizeVector, MultiplyAddVector,
         RotateVector, ConjugateQuaternion, MultiplyQuaternion, ConstructAxisQuaternion, IntegrateQuaternion,
         ConstructRigidTransform } from "./TransformProjection.js";

const SinkSeconds    = 1.6;         // [s]   debris sinks into the course before the prop reassembles (Respawn only)
const SinkSpeed      = 0.35;        // [m/s]

// 📝 Global behaviour the editor exposes (per-prop recipes live on each prop).
export const FractureSettingsDefaults = Object.freeze({
    Enabled:     true,   // [-]  props break; off, they are only colliders
    Lifetime:    25.0,   // [s]  with Respawn, debris stays this long before the prop reassembles
    Respawn:     false,  // [-]  off: the debris stays until the prop is reassembled by hand or the arena is reset
    FieldRadius: 0.75,   // [×]  fragments at least this many finest-cascade voxels in radius join the distance field
    Separation:  1.0,    // [×]  how violently fragments leave the impact
    ImpactShare: 0.35,   // [-]  share of the impact energy a Dynamic fracture may spend on new surface
    DebrisBounce: 1.0,   // [×]  scale on the material's restitution
    DebrisFriction: 1.0  // [×]  scale on the material's friction
});

export const FractureModes = Object.freeze({ Dynamic: "dynamic", Baked: "baked" });

export class FractureSequence
{
    // 📝 Host supplies Scene, Volumes, Distance, Cards, Vehicle, Exchange, OccupantRecords and VertexRecords.
    constructor(Host)
    {
        this.Host     = Host;
        this.Scene    = Host.Scene;
        this.Settings = { ...FractureSettingsDefaults };
        this.Clock    = 0.0;
        this.Accumulator = 0.0;
        this.Metrics  = { Breaks: 0, Reassemblies: 0, LastMilliseconds: 0.0, ActiveFragments: 0, Sleeping: 0 };
        this.Props = this.Scene.Breakables.map((Breakable, Index) =>
        {
            const Material = FractureMaterials[Breakable.Material] || FractureMaterials.concrete;
            const Solid = ConstructSolid(Breakable.Shape);
            const Volume = MeasureSolidVolume(Breakable.Shape);
            return {
                Index,
                Definition: Breakable,
                Solid,
                Volume,
                Mass: Volume * Material.Density * (Breakable.DensityScale || 1.0),
                Recipe: {
                    Material:    Breakable.Material,
                    Mode:        Breakable.Mode || FractureModes.Dynamic,
                    Energy:      Breakable.Energy || 6000,
                    Seed:        Breakable.Seed || (7331 + Index * 101),
                    Ceiling:     Math.min(Breakable.Capacity, Breakable.Ceiling || Breakable.Capacity),
                    MinimumSize: Breakable.MinimumSize || 0.05,
                    Threshold:   Breakable.Threshold || Material.Threshold,
                    Impact:      Breakable.Impact ? Breakable.Impact.slice() : [0.0, 0.0, 0.0],
                    Enabled:     true
                },
                State: "intact",          // intact | broken | sinking
                Pieces: [],
                BrokenAt: 0.0,
                SinkAt: 0.0,
                Baked: null,
                Receipt: null,
                Stale: true
            };
        });
        for (const Prop of this.Props)
            this.BakeProp(Prop);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   BAKING
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The stored pattern a Baked prop breaks into (and a Dynamic one previews): the recipe's energy at its authored
    //    impact point. Cheap enough to redo whenever a recipe changes.
    BakeProp(Prop)
    {
        const Result = FractureSolid(Prop.Solid, { ...Prop.Recipe, Impact: Prop.Recipe.Impact });
        Prop.Baked = { Fragments: ResolveFragments(Result, Prop.Recipe.Material, Prop.Definition.DensityScale || 1.0), Result };
        Prop.Receipt = this.DescribeResult(Result, Prop.Baked.Fragments);
        Prop.Stale = false;
        return Prop.Receipt;
    }

    DescribeResult(Result, Fragments)
    {
        return { Fragments: Fragments.length, Triangles: Result.Triangles, Rejected: Result.Rejected, VolumeError: Result.VolumeError,
                 Quality: Result.Quality, Milliseconds: Result.Milliseconds, Spent: Result.Spent, Budget: Result.Budget,
                 Closed: Result.VolumeError < 1.0e-7 };
    }

    // 📝 Recipe edits from the Fracture editor: the change applies to the next break; Baked props re-bake now.
    AssignRecipe(Prop, Changes)
    {
        Object.assign(Prop.Recipe, Changes);
        Prop.Recipe.Ceiling = Math.max(2, Math.min(Prop.Definition.Capacity, Math.round(Prop.Recipe.Ceiling)));
        if ("Material" in Changes)
        {
            // 📝 A new material changes the threshold, the mass and the whole prop's skin, not only its fragments'.
            const Material = FractureMaterials[Prop.Recipe.Material];
            Prop.Recipe.Threshold = Material.Threshold;
            Prop.Mass = Prop.Volume * Material.Density * (Prop.Definition.DensityScale || 1.0);
            this.Scene.Occupants[Prop.Definition.IntactOccupant].MaterialOverride = Material.Skin;
            this.UploadRange(Prop.Definition.IntactOccupant, 1);
        }
        Prop.Stale = true;
        if (Prop.Recipe.Mode === FractureModes.Baked || "Material" in Changes)
            this.BakeProp(Prop);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   IMPACTS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Before the vehicle steps: any intact prop the hull will touch within the next frame and that receives at least
    //    its threshold energy breaks now, so the car drives through instead of bouncing off a prop already gone.
    ResolveImpacts(Vehicle, Seconds)
    {
        this.DynamicThisFrame = 0;
        if (!this.Settings.Enabled)
            return;
        const Lookahead = Math.max(Seconds, 1.0 / 60.0) * 1.5;
        const CarMass = VehicleGeometry.Mass;
        for (const Prop of this.Props)
        {
            if (Prop.State !== "intact" || !Prop.Recipe.Enabled)
                continue;
            const Placement = Prop.Definition;
            const Centre = Placement.Centre;
            if (LengthVector(SubtractVector(Vehicle.Position, Centre)) > Placement.Radius + 6.0)
                continue;
            let Best = null;
            for (const [X, Y, Z, Radius] of HullSpheres)
            {
                const Lever = RotateVector(Vehicle.Orientation, [X, Y, Z]);
                const Sphere = AddVector(Vehicle.Position, Lever);
                const Velocity = AddVector(Vehicle.LinearVelocity, CrossVector(Vehicle.AngularVelocity, Lever));
                for (const Ahead of [0.0, Lookahead])
                {
                    const Probe = MultiplyAddVector(Sphere, Velocity, Ahead);
                    const Contact = ResolveSolidContact(Placement, Probe, Radius);
                    if (!Contact)
                        continue;
                    const Approach = -DotVector(Velocity, Contact.Normal);
                    if (Approach > 0.0 && (!Best || Approach > Best.Approach))
                        Best = { Approach, Normal: Contact.Normal, Point: Contact.Point, Velocity };
                }
            }
            if (!Best)
                continue;
            const Reduced = CarMass * Prop.Mass / (CarMass + Prop.Mass);
            const Energy = 0.5 * Reduced * Best.Approach * Best.Approach;
            if (Energy >= Prop.Recipe.Threshold)
                this.BreakProp(Prop, { ...Best, Energy, Vehicle });
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  BREAKING
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Impact: { Point, Normal (out of the prop), Velocity (striker at the contact), Energy, Approach, Vehicle? }.
    BreakProp(Prop, Impact)
    {
        const Started = performance.now();
        const Placement = Prop.Definition;
        let Fragments, Receipt;
        // 📝 One on-the-spot fracture per frame bounds the hitch to about 10 ms: a second prop struck in the same frame
        //    (three crates abreast) takes its baked pattern instead.
        if (Prop.Recipe.Mode === FractureModes.Dynamic && !(this.DynamicThisFrame > 0))
        {
            this.DynamicThisFrame = (this.DynamicThisFrame || 0) + 1;
            const Local = TransformToLocal(Placement, Impact.Point);
            const Budget = Math.max(400.0, Math.min(60000.0, Impact.Energy * this.Settings.ImpactShare));
            const Result = FractureSolid(Prop.Solid, { ...Prop.Recipe, Energy: Budget, Impact: Local });
            Fragments = ResolveFragments(Result, Prop.Recipe.Material, Placement.DensityScale || 1.0);
            Receipt = this.DescribeResult(Result, Fragments);
        }
        else
        {
            if (!Prop.Baked || Prop.Stale)
                this.BakeProp(Prop);
            Fragments = Prop.Baked.Fragments;
            Receipt = Prop.Receipt;
        }
        const Written = this.WriteFragments(Prop, Fragments);
        Prop.Receipt = { ...Receipt, Fragments: Written.length };
        this.ActivateFragments(Prop, Written, Impact);
        this.RetireIntact(Prop);
        Prop.State = "broken";
        Prop.BrokenAt = this.Clock;
        this.Metrics.Breaks++;
        this.Metrics.LastMilliseconds = performance.now() - Started;
        if (this.OnBreak)
            this.OnBreak(Prop);
        // 📐 The car's share of an inelastic exchange along the contact normal: Δv = vₙ · m / (M + m), minus the part
        //    the fracture itself absorbed (the material's toughness keeps a little of the hit).
        if (Impact.Vehicle)
        {
            const Vehicle = Impact.Vehicle;
            const Loss = Impact.Approach * Prop.Mass / (VehicleGeometry.Mass + Prop.Mass) * 0.9;
            Vehicle.LinearVelocity = MultiplyAddVector(Vehicle.LinearVelocity, Impact.Normal, Loss);
        }
        this.Refresh();
        return Prop.Receipt;
    }

    // 📝 Triangles of every fragment into the prop's composite facet set, relative to each fragment's centroid; a
    //    fragment that would overflow the reserved room is left out (the prop's capacity is sized so it never is).
    WriteFragments(Prop, Fragments)
    {
        const Scene = this.Scene;
        const Set = Scene.FacetSets[Prop.Definition.CompositeSet];
        const Capacity = Set.VertexCapacity;
        const Limit = Math.min(Fragments.length, Prop.Definition.Capacity);
        const Bytes = new ArrayBuffer(Capacity * VertexStride);
        const Scalars = new Float32Array(Bytes);
        const Integers = new Uint32Array(Bytes);
        const Written = [];
        let Cursor = 0;
        for (let Index = 0; Index < Limit; ++Index)
        {
            const Fragment = Fragments[Index];
            if (Cursor + Fragment.Triangles.length * 3 > Capacity)
                break;
            for (const Triangle of Fragment.Triangles)
            {
                const [A, B, C] = Triangle.Corners;
                const Normal = NormalizeVector(CrossVector(SubtractVector(B, A), SubtractVector(C, A)));
                for (const Corner of [A, B, C])
                {
                    const Offset = Cursor * 8;
                    Scalars[Offset + 0] = Corner[0];
                    Scalars[Offset + 1] = Corner[1];
                    Scalars[Offset + 2] = Corner[2];
                    Integers[Offset + 3] = Triangle.Family;
                    Scalars[Offset + 4] = Normal[0];
                    Scalars[Offset + 5] = Normal[1];
                    Scalars[Offset + 6] = Normal[2];
                    Integers[Offset + 7] = Written.length;
                    ++Cursor;
                }
            }
            Written.push(Fragment);
        }
        Set.VertexCount = Cursor;
        this.Host.Exchange.Device.queue.writeBuffer(this.Host.VertexRecords, Set.FirstVertex * VertexStride, Bytes, 0, Math.max(4, Cursor * VertexStride));
        // 📝 Live fragments join the triangle hierarchy, so triangle reflections see the debris as the distance field does.
        if (this.Host.Triangles)
            this.Host.Triangles.RegisterPropFragments(Prop.Index, Prop.Definition.PieceFirst, Prop.Definition.Capacity, Written);
        return Written;
    }

    ActivateFragments(Prop, Fragments, Impact)
    {
        const Scene = this.Scene;
        const Placement = Prop.Definition;
        const Material = FractureMaterials[Prop.Recipe.Material] || FractureMaterials.concrete;
        const Random = ConstructStream(Prop.Recipe.Seed ^ (this.Metrics.Breaks * 7919));
        const Striker = Impact.Velocity || [0.0, 0.0, 0.0];
        const Exchange = VehicleGeometry.Mass / (VehicleGeometry.Mass + Prop.Mass);
        const Excess = Math.max(0.0, (Impact.Energy || 0.0) - Prop.Recipe.Threshold);
        const Burst = Math.min(9.0, Math.sqrt(2.0 * (0.15 * Excess + 40.0) / Math.max(Prop.Mass, 1.0))) * this.Settings.Separation;
        const Reach = Math.max(0.4, 0.8 * Placement.Radius);
        const Albedo = this.ResolveFallbackAlbedo(Material);
        const FieldRadius = this.Settings.FieldRadius * (this.Host.Distance ? this.Host.Distance.Clips[0].VoxelExtent : 0.125);
        Prop.Pieces = Fragments.map((Fragment, Slot) =>
        {
            const Position = TransformFromLocal(Placement, Fragment.Centroid);
            const Offset = SubtractVector(Position, Impact.Point || Placement.Centre);
            const Proximity = Math.exp(-LengthVector(Offset) / Reach);
            const Outward = NormalizeVector(AddVector(Offset, [0.0, 0.0, 0.25]));
            let Linear = ScaleVector(Striker, Exchange * (0.45 + 0.8 * Proximity));
            Linear = MultiplyAddVector(Linear, Outward, Burst * Proximity * (0.6 + 0.6 * Random()));
            Linear[2] += (0.4 + 1.8 * Proximity * Random()) * this.Settings.Separation;
            const Spin = (1.5 + 7.0 * Proximity) * Math.min(3.0, 0.35 / Math.max(0.08, Fragment.Radius));
            const Axis = NormalizeVector([Random() - 0.5, Random() - 0.5, Random() - 0.5]);
            const Piece = { Occupant: Placement.PieceFirst + Slot, Fragment, Volume: null, Body: null };
            Piece.Body = this.Host.Bodies.AddBody({
                Kind: "debris", Owner: Piece, Position, Orientation: Placement.Orientation.slice(), Linear, Angular: ScaleVector(Axis, Spin),
                Mass: Fragment.Mass, Inertia: Fragment.Inertia, Points: Fragment.Points, Planes: Fragment.Planes,
                Restitution: Material.Restitution, Friction: Material.Friction
            });
            const Occupant = Scene.Occupants[Piece.Occupant];
            Occupant.LocalMinimum = Fragment.Minimum;
            Occupant.LocalMaximum = Fragment.Maximum;
            Occupant.FallbackAlbedo = Albedo;
            if (Fragment.Radius >= FieldRadius)
                this.AdmitToField(Piece);
            return Piece;
        });
        const Records = Scene.Occupants;
        for (let Slot = 0; Slot < Placement.Capacity; ++Slot)
        {
            const Occupant = Records[Placement.PieceFirst + Slot];
            const Piece = Prop.Pieces[Slot];
            Occupant.Inactive = !Piece;
            if (!Piece)
                continue;
            const Transform = ConstructRigidTransform(Piece.Body.Position, Piece.Body.Orientation);
            Occupant.Transform = Transform;
            Occupant.PreviousTransform = Transform;
            Occupant.UnionTransform = Transform.slice();
        }
        // 📝 The anchor (first fragment occupant) carries the composite draw; it must be live whenever any fragment is.
        Records[Placement.PieceFirst].Inactive = Prop.Pieces.length === 0;
        for (const Piece of Prop.Pieces)
            if (Piece.Volume)
                this.Host.Distance.InvalidateBounds([Scene.QueryWorldBounds(Piece.Occupant)]);
        this.UploadPieces(Prop);
    }

    // 📐 The albedo a traced ray reads off a card-less fragment: the mean of the material's skin and interior, packed
    //    as linear RGBA8 into the occupant's FirstCard word.
    ResolveFallbackAlbedo(Material)
    {
        const Specification = this.Host.Materials || [];
        const Skin = (Specification[Material.Skin] || { Albedo: [0.5, 0.5, 0.5] }).Albedo;
        const Interior = (Specification[Material.Interior] || { Albedo: Skin }).Albedo;
        const Channel = (Axis) => Math.max(0, Math.min(255, Math.round(127.5 * (Skin[Axis] + Interior[Axis]))));
        return (Channel(0) | (Channel(1) << 8) | (Channel(2) << 16) | (255 << 24)) >>> 0;
    }

    // 📝 A fragment joins the scene distance field: a pool volume (computed on the device before this frame's union)
    //    and the Distance role; the caller invalidates its bounds once it has a transform.
    AdmitToField(Piece)
    {
        const Volumes = this.Host.Volumes;
        if (!Volumes || !Volumes.AssignFragment)
            return;
        const Volume = Volumes.AssignFragment(Piece, Piece.Fragment, (Evicted) => this.DismissFromField(Evicted));
        if (!Volume)
            return;
        Piece.Volume = Volume;
        const Occupant = this.Scene.Occupants[Piece.Occupant];
        Occupant.Volume = Volume;
        Occupant.Roles |= OccupantRole.Distance;
        this.Metrics.InField = (this.Metrics.InField || 0) + 1;
    }

    // 📝 Out of the field again (pool eviction, sinking, reassembly): its bricks are rewritten without it.
    DismissFromField(Piece)
    {
        const Occupant = this.Scene.Occupants[Piece.Occupant];
        if (!(Occupant.Roles & OccupantRole.Distance))
            return;
        if (!Occupant.Inactive)
            this.Host.Distance.InvalidateBounds([this.Scene.QueryWorldBounds(Piece.Occupant)]);
        Occupant.Roles &= ~OccupantRole.Distance;
        Occupant.Volume = null;
        if (Piece.Volume)
            this.Host.Volumes.ReleaseFragment(Piece.Volume);
        Piece.Volume = null;
        this.Metrics.InField = Math.max(0, (this.Metrics.InField || 1) - 1);
        this.UploadRange(Piece.Occupant, 1);
    }

    RetireIntact(Prop)
    {
        const Scene = this.Scene;
        const Index = Prop.Definition.IntactOccupant;
        const Bounds = Scene.QueryWorldBounds(Index);
        Scene.Occupants[Index].Inactive = true;
        this.Host.Distance.InvalidateBounds([Bounds]);
        this.UploadRange(Index, 1);
    }

    // 📝 Back to whole: fragments off, the intact occupant back in the field, the cards and the colliders.
    ReassembleProp(Prop)
    {
        const Scene = this.Scene;
        const Placement = Prop.Definition;
        for (const Piece of Prop.Pieces)
        {
            if (Piece.Volume)
                this.DismissFromField(Piece);
            else if (!Scene.Occupants[Piece.Occupant].Inactive)
                this.Host.Distance.InvalidateBounds([Scene.QueryWorldBounds(Piece.Occupant)]);
            if (Piece.Body)
                this.Host.Bodies.RemoveBody(Piece.Body);
        }
        for (let Slot = 0; Slot < Placement.Capacity; ++Slot)
        {
            const Occupant = Scene.Occupants[Placement.PieceFirst + Slot];
            Occupant.Inactive = true;
            Occupant.Roles &= ~OccupantRole.Distance;
            Occupant.Volume = null;
            Occupant.LocalMinimum = null;
            Occupant.LocalMaximum = null;
        }
        Prop.Pieces = [];
        if (this.Host.Triangles)
            this.Host.Triangles.ClearPropFragments(Placement.PieceFirst, Placement.Capacity);
        const Index = Placement.IntactOccupant;
        Scene.Occupants[Index].Inactive = false;
        this.Host.Distance.InvalidateBounds([Scene.QueryWorldBounds(Index)]);
        this.Host.Cards.WarmOccupants(Scene, [Index]);
        this.UploadRange(Index, 1);
        Prop.State = "intact";
        this.Metrics.Reassemblies++;
        this.Refresh();
    }

    ReassembleAll()
    {
        for (const Prop of this.Props)
            if (Prop.State !== "intact")
                this.ReassembleProp(Prop);
    }

    // 📝 A test break from the editor: a 12 m/s hit square on the side the car approaches from.
    BreakFromEditor(Prop)
    {
        if (Prop.State !== "intact")
            this.ReassembleProp(Prop);
        const Placement = Prop.Definition;
        const Facing = NormalizeVector(Placement.Approach);
        const Point = MultiplyAddVector(Placement.Centre, Facing, 0.5 * Placement.Depth);
        const Speed = 12.0;
        const Reduced = VehicleGeometry.Mass * Prop.Mass / (VehicleGeometry.Mass + Prop.Mass);
        return this.BreakProp(Prop, { Point, Normal: Facing, Velocity: ScaleVector(Facing, -Speed), Approach: Speed,
                                      Energy: Math.max(Prop.Recipe.Threshold * 1.5, 0.5 * Reduced * Speed * Speed) });
    }

    // 📝 Draw runs, card lists and colliders follow the occupants' activity.
    Refresh()
    {
        const Scene = this.Scene;
        Scene.ConstructDrawRuns();
        this.Host.Cards.RebuildTileLists(Scene);
        const Obstacles = Scene.QueryObstacleBounds();
        this.Host.Vehicle.AssignObstacles(Obstacles);
        if (this.Host.Bodies)
            this.Host.Bodies.AssignObstacles(Obstacles);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   DEBRIS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 After the rigid-body world steps: each moved fragment's occupant transform, then one record upload per prop
    //    whose debris moved. With Respawn on, debris older than the lifetime sinks away and the prop is rebuilt.
    Advance(Seconds, Vehicle)
    {
        this.Clock += Seconds;
        let Active = 0, Sleeping = 0;
        const Live = this.Props.filter((Prop) => Prop.State !== "intact");
        if (Live.length === 0)
        {
            this.Metrics.ActiveFragments = 0;
            this.Metrics.Sleeping = 0;
            return;
        }
        for (const Prop of Live)
        {
            if (Prop.State === "broken" && this.Settings.Respawn && this.Clock - Prop.BrokenAt > this.Settings.Lifetime)
            {
                Prop.State = "sinking";
                Prop.SinkAt = this.Clock;
                for (const Piece of Prop.Pieces)
                {
                    if (Piece.Volume)
                        this.DismissFromField(Piece);
                    this.Host.Bodies.RemoveBody(Piece.Body);
                }
            }
            let Moved = false;
            if (Prop.State === "sinking")
            {
                for (const Piece of Prop.Pieces)
                {
                    Piece.Body.Position[2] -= SinkSpeed * Seconds;
                    Piece.Body.Moved = true;
                }
                if (this.Clock - Prop.SinkAt > SinkSeconds && !this.VehicleOccupies(Prop, Vehicle))
                {
                    this.ReassembleProp(Prop);
                    continue;
                }
            }
            for (const Piece of Prop.Pieces)
            {
                Active++;
                if (Piece.Body.Sleeping)
                    Sleeping++;
                if (!Piece.Body.Moved)
                {
                    this.Scene.RetainOccupantTransform(Piece.Occupant);
                    continue;
                }
                Moved = true;
                this.Scene.AssignOccupantTransform(Piece.Occupant, ConstructRigidTransform(Piece.Body.Position, Piece.Body.Orientation));
            }
            // 📝 One more upload after the last motion, so the previous transform (motion vectors) settles too.
            if (Moved || Prop.Settling)
                this.UploadPieces(Prop);
            Prop.Settling = Moved;
        }
        this.Metrics.ActiveFragments = Active;
        this.Metrics.Sleeping = Sleeping;
    }

    VehicleOccupies(Prop, Vehicle)
    {
        const Placement = Prop.Definition;
        return LengthVector(SubtractVector(Vehicle.Position, Placement.Centre)) < Placement.Radius + 3.2;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   UPLOADS
    //--------------------------------------------------------------------------------------------------------------------

    UploadPieces(Prop)
    {
        const Count = Math.max(1, Prop.Pieces.length);
        this.UploadRange(Prop.Definition.PieceFirst, Count);
    }

    UploadRange(First, Count)
    {
        const Scalars = this.Scene.EncodeOccupantRange(this.Host.Volumes, First, Count);
        this.Host.Exchange.Device.queue.writeBuffer(this.Host.OccupantRecords, First * OccupantStride, Scalars);
    }

    QueryReadout()
    {
        const Intact = this.Props.filter((Prop) => Prop.State === "intact").length;
        return [`props: ${this.Props.length} · intact ${Intact} · broken ${this.Props.length - Intact}`,
                `fragments live: ${this.Metrics.ActiveFragments} (${this.Metrics.Sleeping} asleep) · in the field ${this.Metrics.InField || 0} · breaks ${this.Metrics.Breaks}`,
                `last fracture: ${this.Metrics.LastMilliseconds.toFixed(1)} ms · reassembled ${this.Metrics.Reassemblies}`];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

function MeasureSolidVolume(Shape)
{
    if (Shape.Kind === "prism")
    {
        const Sides = Shape.Sides || 12;
        return 0.5 * Sides * Shape.Radius * Shape.Radius * Math.sin(2.0 * Math.PI / Sides) * Shape.Height;
    }
    return Shape.Extent[0] * Shape.Extent[1] * Shape.Extent[2];
}

// 📝 Placement frame: the solid's centre at Placement.Centre, turned by Placement.Orientation.
function TransformToLocal(Placement, Point)
{
    return RotateVector(ConjugateQuaternion(Placement.Orientation), SubtractVector(Point, Placement.Centre));
}

function TransformFromLocal(Placement, Point)
{
    return AddVector(Placement.Centre, RotateVector(Placement.Orientation, Point));
}

// 📝 The nearest point of the prop's solid (box or prism, in its own frame) to a sphere; null when they do not touch.
function ResolveSolidContact(Placement, Centre, Radius)
{
    const Local = TransformToLocal(Placement, Centre);
    const Shape = Placement.Shape;
    let Closest;
    if (Shape.Kind === "prism")
    {
        const Radial = Math.hypot(Local[0], Local[1]);
        const Scale = Radial > Shape.Radius ? Shape.Radius / Radial : 1.0;
        Closest = [Local[0] * Scale, Local[1] * Scale, Math.max(-0.5 * Shape.Height, Math.min(0.5 * Shape.Height, Local[2]))];
    }
    else
    {
        Closest = [0, 1, 2].map((Axis) => Math.max(-0.5 * Shape.Extent[Axis], Math.min(0.5 * Shape.Extent[Axis], Local[Axis])));
    }
    const Offset = SubtractVector(Local, Closest);
    const Distance = LengthVector(Offset);
    if (Distance >= Radius)
        return null;
    const LocalNormal = Distance > 1.0e-6 ? ScaleVector(Offset, 1.0 / Distance) : NormalizeVector(Local);
    return { Normal: RotateVector(Placement.Orientation, LocalNormal), Point: TransformFromLocal(Placement, Closest) };
}

function ConstructStream(Seed)
{
    let Accumulator = (Seed >>> 0) || 1;
    return () =>
    {
        Accumulator = (Math.imul(1664525, Accumulator) + 1013904223) >>> 0;
        return Accumulator / 4294967296;
    };
}
