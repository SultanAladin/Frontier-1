//============================================================================================================================================
//                                                             SPARKSEQUENCE.JS
//============================================================================================================================================
// 🧩 Sparks: hot metal ground off where a rim or the body slides on the road or a wall — thrown, cooling, bouncing, drawn as streaks.

// 📝 The vehicle solver reports each frame's scrapes (VehicleSolver.RecordScrape): the strongest per contact — a hull
//    patch dragged along the asphalt, a rim pinched through a flat or bare once the rubber has gone — with the contact
//    point, the sliding velocity of the metal over the surface, the surface normal and a strength. Each scrape throws
//    irregular packets of sparks in proportion, with class-dependent temperatures:
//
//    | Source | Where a spark leaves                          | With what velocity                                    |
//    |--------|-----------------------------------------------|-------------------------------------------------------|
//    | hull   | the scraping point                            | 30–75 % of the slide, fanned, kicked off the surface  |
//    | rim    | the rim's edge a little (α ≤ 0.47 rad) behind | that point's own: Slip + S(1 − cos α)·F + |S| sin α·N |
//    |        | the patch, where the metal leaves the road    | (S = Spin·r) — up off the trailing edge, so the trail |
//    |        |                                               | streams back from a rolling wheel                     |
//
//    In flight a spark falls, feels air drag, cools radiatively (its glow follows the blackbody temperature) and skips
//    off the road, losing most of its speed, until it is too cool to see.
//
//    A few hundred particles: they are integrated here on the CPU and drawn after the temporal antialiasing as thin
//    additive streaks (2.2 mm across, with bounded subpixel widths) from where the camera saw them a frame ago to where it sees
//    them now (SparkRaster.wgsl), depth-tested against the scene — thin, bright and fast-moving, they would otherwise
//    be averaged away by the history.

import { SweepSpark, BounceSpark } from "./SparkCollision.js";

const SparkCapacity   = 2048;
const InstanceFloats  = 12;            // head xyz + width, tail xyz + pixel-width factor, radiance rgb + shutter fraction
const Gravity         = 9.81;          // [m/s²]
const AirDrag         = 1.6;           // [1/s]
const CoolingSeconds  = 0.42;          // [s]  e-folding of a spark's excess temperature
const VisibleKelvin   = 950.0;         // [K]  below this a spark is gone
const SparksPerStrength = 700.0;       // [1/s] per unit scrape strength
const SparkWidth      = 0.0022;        // [m]  streak width at Size 1 — a glowing chip a fraction of a millimetre across, blurred

export const SparkSettingsDefaults = Object.freeze({ Enabled: true, Rate: 1.0, Brightness: 1.0, Size: 1.0, Variation: 1.0 });

export class SparkSequence
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.Settings = { ...SparkSettingsDefaults };
        this.Particles = [];
        this.Instances = new Float32Array(SparkCapacity * InstanceFloats);
        this.InstanceRecords = Exchange.ConstructRecords(SparkCapacity * InstanceFloats * 4, GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, "Spark instances");
        this.Metrics = { Live: 0, Emitted: 0, Scrapes: 0 };
        this.Seed = 0x5EED;
        this.Live = 0;
        this.Clock = 0;
        this.EmissionPulses = new Map();
    }

    async ConstructPrograms(FrameRecords)
    {
        this.Layout = this.Exchange.ConstructDescriptorLayout(["Uniform"], GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, "Spark raster");
        this.Group = this.Exchange.ConstructDescriptorGroup(this.Layout, [FrameRecords]);
        this.Program = await this.Shaders.ConstructRasterProgram("SparkRaster.wgsl", {
            VertexEntry: "SparkVertex", FragmentEntry: "SparkFragment",
            VertexLayouts: [{ arrayStride: InstanceFloats * 4, stepMode: "instance", attributes: [
                { shaderLocation: 0, offset: 0,  format: "float32x4" },
                { shaderLocation: 1, offset: 16, format: "float32x4" },
                { shaderLocation: 2, offset: 32, format: "float32x4" }] }],
            Primitive: { topology: "triangle-list", cullMode: "none" },
            DepthStencil: { format: "depth32float", depthWriteEnabled: false, depthCompare: "greater" },
            Targets: [{ format: "rgba16float", blend: { color: { srcFactor: "one", dstFactor: "one", operation: "add" },
                                                        alpha: { srcFactor: "zero", dstFactor: "one", operation: "add" } } }]
        }, [this.Layout]);
    }

    Random()
    {
        this.Seed = (Math.imul(this.Seed, 1664525) + 1013904223) >>> 0;
        return this.Seed / 4294967296;
    }

    // 📝 Scrapes: [{ Point, Velocity (the metal's slide over the surface), Normal, Strength, Wheel,
    //    Rim: { Forward, SpinSpeed, Radius } for a rim, null for the hull }].
    Emit(Scrapes, Seconds)
    {
        if (!this.Settings.Enabled || !(Seconds > 0.0))
            return;
        this.Clock += Math.min(Seconds, 0.1);
        for (const [Key,Pulse] of this.EmissionPulses) if(this.Clock-Pulse.Last>0.5) this.EmissionPulses.delete(Key);
        const Variation = Math.max(0,Math.min(1,this.Settings.Variation ?? 1));
        const Mix = (Neutral,Varied) => Neutral+(Varied-Neutral)*Variation;
        this.Metrics.Scrapes = Scrapes.length;
        for (const Scrape of Scrapes)
        {
            // Correlated 40–130 ms contact packets, not an identical stream every frame.
            const Key = Scrape.Rim && Number.isInteger(Scrape.Wheel) ? `wheel:${Scrape.Wheel}` : Scrape.Point.map(P=>Math.floor(P*0.5)).join(',');
            let Pulse=this.EmissionPulses.get(Key);
            if(!Pulse || this.Clock>=Pulse.Next) {
                Pulse={Next:this.Clock+0.04+this.Random()*0.09, Last:this.Clock,
                    Power:this.Random()<0.65 ? 0.25+this.Random()*0.70 : 1.2+this.Random()*1.4};
                if(this.EmissionPulses.size>=128) this.EmissionPulses.delete(this.EmissionPulses.keys().next().value);
                this.EmissionPulses.set(Key,Pulse);
            }
            Pulse.Last=this.Clock;
            const Expected = SparksPerStrength * this.Settings.Rate * Scrape.Strength * Seconds * Mix(1,Pulse.Power);
            let Count = Math.floor(Expected) + (this.Random() < Expected - Math.floor(Expected) ? 1 : 0);
            Count = Math.min(Count, 96);
            const Normal = Scrape.Normal || [0.0, 0.0, 1.0];
            const Slide = Scrape.Velocity;
            const SlideSpeed = Math.hypot(Slide[0], Slide[1], Slide[2]);
            for (let Spark = 0; Spark < Count && this.Particles.length < SparkCapacity; ++Spark)
            {
                // Mostly fine fast filings, some medium streaks, a few heavier orange chips.
                const Roll=this.Random(), Kind=Roll<0.72?0:Roll<0.94?1:2;
                const Size=Mix(1,Kind===0?0.25+0.5*this.Random():Kind===1?0.7+0.6*this.Random():1.3+0.7*this.Random());
                const SpeedShare=Mix(1,Kind===2?0.35+0.40*this.Random():0.65+0.55*this.Random());
                const Spread = [this.Random() * 2.0 - 1.0, this.Random() * 2.0 - 1.0, this.Random() * 2.0 - 1.0];
                let Origin, Velocity;
                if (Scrape.Rim)
                {
                    // 📐 Detached α behind the patch in the sense the rim turns (most right behind it): from the patch at
                    //    R((1 − cos α)·N − σ·sin α·F), moving at Slip + S(1 − cos α)·F + |S|·sin α·N, σ = sign S; it keeps
                    //    75–100 % of that, scattered in a narrow cone and bounced a little off the road.
                    const { Forward, SpinSpeed, Radius } = Scrape.Rim;
                    const Sense = SpinSpeed < 0.0 ? -1.0 : 1.0;
                    const Detach = 0.02 + 0.45 * this.Random() * this.Random();
                    const Cosine = Math.cos(Detach), Sine = Math.sin(Detach);
                    Origin = [0, 1, 2].map((Axis) => Scrape.Point[Axis] + Radius * ((1.0 - Cosine) * Normal[Axis] - Sense * Sine * Forward[Axis]));
                    const Metal = [0, 1, 2].map((Axis) => Slide[Axis] + SpinSpeed * (1.0 - Cosine) * Forward[Axis] + Math.abs(SpinSpeed) * Sine * Normal[Axis]);
                    const Speed = Math.hypot(Metal[0], Metal[1], Metal[2]);
                    const Share = 0.75 + 0.25 * this.Random();
                    const Scatter = 0.12 * Speed + 0.4;
                    const Bounce = 0.3 + 1.2 * this.Random();
                    Velocity = [0, 1, 2].map((Axis) => Metal[Axis] * Share + Spread[Axis] * Scatter + Normal[Axis] * Bounce);
                }
                else
                {
                    // 📝 Along the slide at 30–75 % of its speed, fanned ±35° and kicked 1–4 m/s off the surface.
                    const Share = 0.3 + 0.45 * this.Random();
                    const Fan = 0.6 * Math.max(2.0, SlideSpeed * 0.35);
                    const Kick = 1.0 + 3.0 * this.Random();
                    const Jitter = 0.04;
                    Origin = [Scrape.Point[0] + (this.Random() - 0.5) * Jitter, Scrape.Point[1] + (this.Random() - 0.5) * Jitter,
                              Scrape.Point[2] + 0.01 + this.Random() * 0.02];
                    Velocity = [0, 1, 2].map((Axis) => Slide[Axis] * Share + Spread[Axis] * Fan + Normal[Axis] * Kick);
                }
                Velocity = Velocity.map(V=>V*SpeedShare);
                // New sparks integrate only their birth-frame remainder, with the same swept collision as older sparks.
                const Born = this.Random() * Math.min(Seconds, 0.1);
                const Position = Origin.slice();
                this.Particles.push({
                    Position,
                    Prior: Origin,
                    Velocity,
                    Kind,
                    Kelvin: Mix(2450,Kind===2?1800+800*this.Random():2100+900*this.Random()),
                    Drag: Mix(AirDrag,Kind===0?2.0+2.5*this.Random():0.65+1.2*this.Random()),
                    Cooling: Mix(1,Kind===0?0.55+0.35*this.Random():Kind===1?0.85+0.4*this.Random():1.2+0.5*this.Random()),
                    LifeSeconds: Mix(2.5,Kind===0?0.20+0.4*this.Random():Kind===1?0.45+0.75*this.Random():0.9+this.Random()),
                    BounceScale: Mix(1,0.70+0.30*this.Random()),
                    Energy: Mix(1,0.35+0.85*this.Random()),
                    Flicker: Mix(0,0.04+0.12*this.Random()),
                    Frequency: 10+18*this.Random(), Phase: this.Random()*Math.PI*2,
                    PixelWidth: Mix(1,Kind===0?0.4+0.35*this.Random():Kind===1?0.7+0.25*this.Random():0.9+0.1*this.Random()),
                    Shutter: Mix(1,Kind===0?0.18+0.45*this.Random():0.55+0.45*this.Random()),
                    Age: 0.0,
                    BornSeconds: Born,
                    Impacts: 0,
                    Size: Size * this.Settings.Size
                });
                this.Metrics.Emitted++;
            }
        }
    }

    // 📝 Ballistic with linear drag, radiative cooling toward ambient, skips off the course.
    Advance(Seconds, Boxes = [], Bodies = [])
    {
        if (!(Seconds > 0.0))
            return;
        if (!this.Settings.Enabled) { this.Clear(); return; }
        // Broadphase once per frame, not once per particle/substep. Refresh from live world lists.
        if (this.Particles.length) {
            const Minimum = [Infinity,Infinity,Infinity], Maximum = [-Infinity,-Infinity,-Infinity];
            for (const Spark of this.Particles) for (let Axis = 0; Axis < 3; ++Axis) {
                const Travel = Math.abs(Spark.Velocity[Axis]) * Math.min(Seconds, 0.1) + 0.2;
                Minimum[Axis] = Math.min(Minimum[Axis], Spark.Position[Axis] - Travel);
                Maximum[Axis] = Math.max(Maximum[Axis], Spark.Position[Axis] + Travel);
            }
            Boxes = Boxes.filter(Box => Minimum.every((Low, Axis) => Maximum[Axis] >= Box.Minimum[Axis] && Low <= Box.Maximum[Axis]));
            Bodies = Bodies.filter(Body => Minimum.every((Low, Axis) => Maximum[Axis] >= Body.Position[Axis] - Body.Radius && Low <= Body.Position[Axis] + Body.Radius));
        }
        const Survivors = [];
        for (const Spark of this.Particles)
        {
            Spark.Prior = Spark.Position.slice();
            const Duration = Spark.BornSeconds ?? Math.min(Seconds, 0.1);
            delete Spark.BornSeconds;
            const Steps = Math.max(1, Math.ceil(Duration * 120)), Dt = Duration / Steps;
            for (let Step = 0; Step < Steps; ++Step) {
                Spark.Age += Dt;
                const Damping = Math.exp(-(Spark.Drag ?? AirDrag) * Dt);
                Spark.Velocity = Spark.Velocity.map((Value, Axis) => Value * Damping - (Axis === 2 ? Gravity * Dt : 0));
                const From = Spark.Position;
                const To = From.map((Value, Axis) => Value + Spark.Velocity[Axis] * Dt);
                const Hit = SweepSpark(From, To, Boxes, Bodies);
                if (Hit) {
                    Spark.Position = From.map((Value, Axis) => Value + (To[Axis] - Value) * Hit.Time + Hit.Normal[Axis] * Hit.Push);
                    Spark.Velocity = BounceSpark(Spark.Velocity, Hit.Normal).map(V=>V*(Spark.BounceScale ?? 1));
                    Spark.Impacts = (Spark.Impacts || 0) + 1;
                    Spark.Kelvin = 293 + (Spark.Kelvin - 293) * 0.92;
                    // Do not draw a streak through the wall/ground after a bounce.
                    Spark.Prior = Spark.Position.slice();
                } else Spark.Position = To;
                Spark.Kelvin = 293 + (Spark.Kelvin - 293) * Math.exp(-Dt / (CoolingSeconds * (Spark.Cooling ?? 1) * Math.max(0.5, Spark.Size)));
            }
            if (Spark.Kelvin > VisibleKelvin && Spark.Age < (Spark.LifeSeconds ?? 2.5) && (Spark.Impacts < 8 || Math.hypot(...Spark.Velocity) > 0.6))
                Survivors.push(Spark);
        }
        this.Particles = Survivors;
        this.Live = Survivors.length;
        this.Metrics.Live = this.Live;
        if (this.Live === 0)
            return;
        // 📝 Instances: the streak from where the spark was a frame ago (the raster projects it with last frame's camera,
        //    so the streak is the motion the camera saw) to where it is.
        const Brightness = this.Settings.Brightness;
        for (let Index = 0; Index < this.Live; ++Index)
        {
            const Spark = this.Particles[Index];
            const Offset = Index * InstanceFloats;
            const Tail = Spark.Prior || Spark.Position;
            const Radiance = ResolveSparkRadiance(Spark.Kelvin).map((Channel) => Channel * Brightness * ResolveSparkGain(Spark));
            this.Instances.set([...Spark.Position, SparkWidth * Spark.Size, ...Tail, Spark.PixelWidth ?? 1, ...Radiance, Spark.Shutter ?? 1], Offset);
        }
        this.Exchange.Device.queue.writeBuffer(this.InstanceRecords, 0, this.Instances, 0, this.Live * InstanceFloats);
    }

    // 📝 Additively over the post radiance, depth-tested against the opaque surface (read only).
    Record(Encoder, TargetView, DepthView)
    {
        if (!this.Program || this.Live === 0)
            return;
        const Pass = Encoder.beginRenderPass({
            label: "Sparks",
            colorAttachments: [{ view: TargetView, loadOp: "load", storeOp: "store" }],
            depthStencilAttachment: { view: DepthView, depthReadOnly: true }
        });
        Pass.setPipeline(this.Program);
        Pass.setBindGroup(0, this.Group);
        Pass.setVertexBuffer(0, this.InstanceRecords);
        Pass.draw(6, this.Live, 0, 0);
        Pass.end();
    }

    Clear()
    {
        this.Particles = [];
        this.EmissionPulses.clear();
        this.Clock = 0;
        this.Live = 0;
        this.Metrics.Live = 0;
    }

    QueryReadout()
    {
        return [`sparks live: ${this.Metrics.Live} · emitted ${this.Metrics.Emitted} · scrapes this frame ${this.Metrics.Scrapes}`];
    }
}

// 📐 Blackbody colour (Helland's fit, linear) × Stefan–Boltzmann brightness referred to 1500 K, in the scene's radiance
//    units (sunlit white ≈ 5): a fresh spark is a few hundred times brighter than the road it flies over.
export function ResolveSparkRadiance(Kelvin)
{
    const Hecto = Math.max(1000.0, Math.min(6600.0, Kelvin)) / 100.0;
    const Green = Math.min(1.0, Math.max(0.0, (99.4708025861 * Math.log(Hecto) - 161.1195681661) / 255.0));
    const Blue = Hecto <= 19.0 ? 0.0 : Math.min(1.0, Math.max(0.0, (138.5177312231 * Math.log(Math.max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0));
    const Ratio = Kelvin / 1500.0;
    const Brightness = 8.0 * Ratio * Ratio * Ratio * Ratio * Math.max(0, Math.min(1, (Kelvin - VisibleKelvin) / 500));
    return [1.0, Math.pow(Green, 2.2), Math.pow(Blue, 2.2)].map((Channel) => Channel * Brightness);
}

// Stable per-particle modulation; never re-roll brightness in the render loop.
// Shared with the pooled GI light proxies so visual and lighting energy agree.
export function ResolveSparkGain(Spark) {
    const Life = Spark.LifeSeconds ?? 2.5;
    const Fade = Math.max(0,Math.min(1,(Life-(Spark.Age ?? 0))/0.10));
    const Ripple = 1-(Spark.Flicker ?? 0)*(0.5+0.5*Math.sin((Spark.Age ?? 0)*(Spark.Frequency ?? 0)*Math.PI*2+(Spark.Phase ?? 0)));
    return (Spark.Energy ?? 1)*Fade*Ripple;
}
