// Browser Jolt Physics 1.1.0, single-threaded WebAssembly. No native build/CDN/COOP requirement.
// Jolt owns loose-body integration, sleeping and rigid contacts. Existing suspension/tyre/
// drivetrain model stays; a dynamic chassis proxy exchanges collision velocity changes with it.
import InitJolt from '../node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js';
import { RigidBodyWorld } from './RigidBodyWorld.js';
import { VehicleGeometry } from './VehicleSpecification.js';
import { KerbConstants, EmitRampFacets } from './CourseStructure.js';
const Static = 0, Loose = 1, Car = 2, Step = 1 / 120;
let ModulePromise;
const XYZ = V => [V.GetX(), V.GetY(), V.GetZ()];
const XYZW = Q => [...XYZ(Q), Q.GetW()];
const Same = (A,B) => A.every((V,I) => Math.abs(V-B[I]) < 1e-7);
export class JoltWorld extends RigidBodyWorld {
    static async Create() {
        const J = await (ModulePromise ||= InitJolt());
        return new JoltWorld(J);
    }
    constructor(J) {
        super(); this.J = J; this.Backend = 'Jolt Physics 1.1.0 · WebAssembly';
        const Settings = new J.JoltSettings();
        Settings.mMaxWorkerThreads = 0;
        Settings.mMaxBodies = 8192;
        const Filter = new J.ObjectLayerPairFilterTable(3);
        Filter.EnableCollision(Static, Loose); Filter.EnableCollision(Loose, Loose); Filter.EnableCollision(Loose, Car);
        const BP = new J.BroadPhaseLayerInterfaceTable(3, 2), Still = new J.BroadPhaseLayer(0), Moving = new J.BroadPhaseLayer(1);
        BP.MapObjectToBroadPhaseLayer(Static, Still); BP.MapObjectToBroadPhaseLayer(Loose, Moving); BP.MapObjectToBroadPhaseLayer(Car, Moving);
        Settings.mObjectLayerPairFilter = Filter; Settings.mBroadPhaseLayerInterface = BP;
        Settings.mObjectVsBroadPhaseLayerFilter = new J.ObjectVsBroadPhaseLayerFilterTable(BP, 2, Filter, 3);
        this.World = new J.JoltInterface(Settings); J.destroy(Settings); J.destroy(Still); J.destroy(Moving);
        this.System = this.World.GetPhysicsSystem(); this.Interface = this.System.GetBodyInterface();
        this.ContactCount = 0;
        this.Listener = new J.ContactListenerJS();
        this.Listener.OnContactValidate = () => J.ValidateResult_AcceptAllContactsForThisBodyPair;
        this.Listener.OnContactAdded = () => { this.ContactCount++; };
        this.Listener.OnContactPersisted = () => { this.ContactCount++; };
        this.Listener.OnContactRemoved = () => {};
        this.System.SetContactListener(this.Listener);
        this.V = new J.Vec3(0,0,0); this.W = new J.Vec3(0,0,0); this.P = new J.RVec3(0,0,0); this.Q = new J.Quat(0,0,0,1);
        this.V.Set(0,0,-9.81); this.System.SetGravity(this.V);
        this.StaticBodies = []; this.TerrainBodies = [];
        this.TerrainBodies.push(this.AddBox({ Minimum: [-500,-500,-2], Maximum: [500,500,0] }));
        for (const [X0,X1,Y0,Y1] of KerbConstants.Strips)
            this.TerrainBodies.push(this.AddBox({ Minimum: [X0,Y0,0], Maximum: [X1,Y1,KerbConstants.Height] }));
        const Ramp = EmitRampFacets().flatMap(F => F.Corners);
        this.TerrainBodies.push(this.CreateHull(Ramp, [0,0,0], [0,0,0,1], 0, Static));
        this.Metrics.Backend = this.Backend;
    }
    Pose(Position, Orientation) { this.P.Set(...Position); this.Q.Set(...Orientation); }
    CreateShapeBody(Shape, Position, Orientation, Mass, Layer, Sleeping = false) {
        const J = this.J; this.Pose(Position, Orientation);
        const S = new J.BodyCreationSettings(Shape, this.P, this.Q, Mass > 0 ? J.EMotionType_Dynamic : J.EMotionType_Static, Layer);
        if (Mass > 0) {
            S.mOverrideMassProperties = J.EOverrideMassProperties_CalculateInertia;
            S.mMassPropertiesOverride.mMass = Mass;
            S.mMotionQuality = J.EMotionQuality_LinearCast;
        }
        S.mLinearDamping = 0.02; S.mAngularDamping = 0.08;
        const Body = this.Interface.CreateBody(S); J.destroy(S);
        if (!Body) throw new Error('Jolt body capacity exhausted');
        this.Interface.AddBody(Body.GetID(), Sleeping || Mass === 0 ? J.EActivation_DontActivate : J.EActivation_Activate);
        return Body;
    }
    CreateHull(Points, Position, Orientation, Mass, Layer, Sleeping = false) {
        const J = this.J, Hull = new J.ConvexHullShapeSettings();
        Hull.mMaxConvexRadius = 0.005;
        for (const Point of Points) { this.V.Set(...Point); Hull.mPoints.push_back(this.V); }
        const Result = Hull.Create();
        if (Result.HasError()) { J.destroy(Result); J.destroy(Hull); throw new Error('Jolt could not construct convex hull'); }
        const Body = this.CreateShapeBody(Result.Get(), Position, Orientation, Mass, Layer, Sleeping);
        J.destroy(Result); J.destroy(Hull); return Body;
    }
    AddBox(Box) {
        const Size = Box.Maximum.map((Value, Axis) => Math.max(0.005, (Value - Box.Minimum[Axis]) / 2));
        this.V.Set(...Size);
        const Shape = new this.J.BoxShape(this.V, Math.min(0.005, ...Size) * 0.5, null);
        Shape.AddRef();
        const Body = this.CreateShapeBody(Shape, Box.Minimum.map((V,I) => (V + Box.Maximum[I]) / 2), [0,0,0,1], 0, Static);
        Shape.Release(); return Body;
    }
    DropBody(Body) {
        this.Interface.RemoveBody(Body.GetID()); this.Interface.DestroyBody(Body.GetID());
    }
    AssignObstacles(Boxes) {
        super.AssignObstacles(Boxes);
        for (const Body of this.StaticBodies) this.DropBody(Body);
        this.StaticBodies = Boxes.map(Box => this.AddBox(Box));
    }
    AddBody(Spec) {
        const Body = super.AddBody(Spec);
        try { Body.Jolt = this.CreateHull(Spec.Points, Spec.Position, Spec.Orientation, Spec.Mass, Loose, Spec.Sleeping); }
        catch (Error) { super.RemoveBody(Body); throw Error; }
        Body.Jolt.SetFriction(Body.Friction); Body.Jolt.SetRestitution(Body.Restitution);
        this.SetVelocity(Body.Jolt, Body.Linear, Body.Angular); return Body;
    }
    SetVelocity(Body, Linear, Angular) {
        this.V.Set(...Linear); this.W.Set(...Angular);
        this.Interface.SetLinearAndAngularVelocity(Body.GetID(), this.V, this.W);
    }
    RemoveBody(Body) { if (Body.Jolt) { this.DropBody(Body.Jolt); Body.Jolt = null; } super.RemoveBody(Body); }
    PlaceBody(Body, Position, Orientation, Sleeping = true) {
        super.PlaceBody(Body, Position, Orientation, Sleeping);
        if (!Body.Jolt) return;
        this.Pose(Position, Orientation);
        this.Interface.SetPositionAndRotation(Body.Jolt.GetID(), this.P, this.Q, this.J.EActivation_DontActivate);
        this.SetVelocity(Body.Jolt, [0,0,0], [0,0,0]);
        if (Sleeping) this.Interface.DeactivateBody(Body.Jolt.GetID()); else this.Interface.ActivateBody(Body.Jolt.GetID());
    }
    Wake(Body) { super.Wake(Body); if (Body.Jolt) this.Interface.ActivateBody(Body.Jolt.GetID()); }
    Advance(Seconds, Vehicle = null) {
        const Started = performance.now(), J = this.J;
        this.Accumulator = Math.min(this.Accumulator + Math.max(0, Seconds), Step * 12);
        const Steps = Math.floor((this.Accumulator + 1e-10) / Step); this.Accumulator -= Steps * Step;
        this.ApplySupportLoads(Vehicle);
        for (const Body of this.Bodies) {
            Body.PriorSleeping = Body.Sleeping; Body.Moved = false;
            // Owners can sink retired debris or reset bodies; mirror those explicit edits into Jolt.
            if (!Same(Body.Position, XYZ(Body.Jolt.GetPosition())) || !Same(Body.Orientation, XYZW(Body.Jolt.GetRotation()))) {
                this.Pose(Body.Position, Body.Orientation);
                this.Interface.SetPositionAndRotation(Body.Jolt.GetID(), this.P, this.Q, J.EActivation_DontActivate);
            }
            if (!Same(Body.Linear, XYZ(Body.Jolt.GetLinearVelocity())) || !Same(Body.Angular, XYZ(Body.Jolt.GetAngularVelocity()))) {
                this.SetVelocity(Body.Jolt, Body.Linear, Body.Angular);
                if (Math.hypot(...Body.Linear, ...Body.Angular) > 0.01) this.Interface.ActivateBody(Body.Jolt.GetID());
            }
            Body.Jolt.SetFriction(Body.Friction * this.Settings.Friction);
            Body.Jolt.SetRestitution(Math.min(1, Body.Restitution * this.Settings.Bounce));
        }
        const ActiveCar = Vehicle && !Vehicle.Hidden && this.Settings.Vehicle;
        if (ActiveCar && !this.CarBody) {
            const Points = Vehicle.HullPoints || [[-3,-1,-0.1],[-3,1,-0.1],[3,-1,-0.1],[3,1,-0.1],[-2,-1,1],[-2,1,1],[2,-1,1],[2,1,1]];
            this.CarBody = this.CreateHull(Points, Vehicle.Position, Vehicle.Orientation, VehicleGeometry.Mass, Car);
            this.Interface.SetGravityFactor(this.CarBody.GetID(), 0);
            this.CarBody.GetMotionProperties().SetLinearDamping(0);
            this.CarBody.GetMotionProperties().SetAngularDamping(0);
        }
        let Before;
        if (this.CarBody) {
            this.CarBody.SetIsSensor(!ActiveCar);
            if (ActiveCar) {
                this.Pose(Vehicle.Position, Vehicle.Orientation);
                this.Interface.SetPositionAndRotation(this.CarBody.GetID(), this.P, this.Q, J.EActivation_Activate);
                this.SetVelocity(this.CarBody, Vehicle.LinearVelocity, Vehicle.AngularVelocity);
                Before = { Linear: Vehicle.LinearVelocity.slice(), Angular: Vehicle.AngularVelocity.slice() };
            }
        }
        this.ContactCount = 0;
        if (Steps > 0) this.World.Step(Steps * Step, Steps);
        if (Before && Steps > 0) {
            // Apply only collision-induced velocity changes. Do not double-integrate chassis travel/gravity.
            const Linear = XYZ(this.CarBody.GetLinearVelocity()), Angular = XYZ(this.CarBody.GetAngularVelocity());
            Vehicle.LinearVelocity = Vehicle.LinearVelocity.map((V,I) => V + Linear[I] - Before.Linear[I]);
            Vehicle.AngularVelocity = Vehicle.AngularVelocity.map((V,I) => V + Angular[I] - Before.Angular[I]);
        }
        let Awake = 0;
        for (const Body of this.Bodies) {
            const Position = XYZ(Body.Jolt.GetPosition()), Orientation = XYZW(Body.Jolt.GetRotation());
            Body.Moved = !Same(Position, Body.Position) || !Same(Orientation, Body.Orientation);
            Body.Position = Position; Body.Orientation = Orientation;
            Body.Linear = XYZ(Body.Jolt.GetLinearVelocity()); Body.Angular = XYZ(Body.Jolt.GetAngularVelocity());
            Body.Sleeping = !Body.Jolt.IsActive();
            if (Body.PriorSleeping && !Body.Sleeping) Body.Woke = true;
            if (!Body.Sleeping) Awake++;
            this.RefreshBody(Body); // same world planes used by tyre support and spark sweeps
        }
        this.RefreshSupport(Vehicle);
        this.Metrics = { Backend: this.Backend, Bodies: this.Bodies.length, Awake, Contacts: this.ContactCount,
            Milliseconds: performance.now() - Started };
    }
    Dispose() {
        for (const Body of [...this.Bodies]) this.RemoveBody(Body);
        for (const Body of [...this.StaticBodies, ...this.TerrainBodies, ...(this.CarBody ? [this.CarBody] : [])]) this.DropBody(Body);
        this.StaticBodies = []; this.TerrainBodies = []; this.CarBody = null;
        for (const Object of [this.V,this.W,this.P,this.Q,this.World,this.Listener]) this.J.destroy(Object);
    }
}
