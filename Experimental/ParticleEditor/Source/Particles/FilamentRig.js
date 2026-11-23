// FILAMENTRIG.JS
// 🧩 The Filament renderer without any page around it: the ControlVehicle on a dark set, a backdrop dome, the glass
//    sphere, bloom, an orbit camera and one particle layer per look. The showroom (Filament.js) shows one layer at a
//    time; the particle editor (Experimental/ParticleEditor) stacks several, each with its own count, speed, trail, size, colours and lens.
// 📝 Every layer owns its uniforms and its instanced geometry (the per-particle attributes are shared), so layers
//    tune independently and cost one draw call each. Timelines (Digital twin, Vehicle transition, Charging) run per
//    layer on the layer's own cycle; the clock driving the motion is shared. Pointer orbit and wheel zoom bind to the
//    canvas. Raise once: RaiseRig(Canvas, { Capture }) → Rig.

import { THREE, AssembleVehicle, DefaultFinish, PanelEnvironment, Family } from '../Vehicle/VehicleFacets.js';
import { ScatterSurface, SilhouetteProfile, Seeded } from './SurfaceScatter.js';
import { Prelude, Projection, Fragment, Looks, Palettes } from './FilamentLooks.js';
import { BloomChain } from './BloomChain.js';
import { SweepJag } from './FilamentFibres.js';

export const MaxCount = 160000;
export const CarModes = ['Auto', 'Paint', 'Ghost', 'Glass', 'Hidden'];
export { Looks, Palettes, THREE };
const Radian = Math.PI / 180;
const Span = (Time, From, To) => Math.min(1, Math.max(0, (Time - From) / (To - From)));

export async function RaiseRig(Canvas, { Capture = false } = {})
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                              RENDERER AND SET
    //--------------------------------------------------------------------------------------------------------------------

    const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: false, powerPreference: 'high-performance', preserveDrawingBuffer: Capture });
    Renderer.setPixelRatio(Capture ? 1 : Math.min(window.devicePixelRatio, 1.5));
    Renderer.toneMapping = THREE.NoToneMapping;
    Renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    Renderer.setClearColor(0x010203, 1);
    const Glow = new BloomChain(Renderer);

    const Scene = new THREE.Scene();
    const Camera = new THREE.PerspectiveCamera(36, 1, 0.1, 400);
    Scene.environment = PanelEnvironment(Renderer, [
        { Colour: '#dfe9ff', Intensity: 1.3, Position: [0, 9, 0], Size: [14, 4], Facing: [0, 0, 0] },
        { Colour: '#6fb6ff', Intensity: 0.9, Position: [-10, 3, 6], Size: [6, 3] },
        { Colour: '#ffb07a', Intensity: 0.6, Position: [10, 2.5, -6], Size: [6, 2] },
    ], '#020305');
    Scene.add(new THREE.HemisphereLight('#9fb8ff', '#000000', 0.5));
    const KeyLight = new THREE.DirectionalLight('#ffffff', 1.6);
    KeyLight.position.set(4, 9, 5);
    const RimLight = new THREE.DirectionalLight('#8fd8ff', 1.4);
    RimLight.position.set(-6, 3, -7);
    Scene.add(KeyLight, RimLight);

    // Backdrop: a gradient dome (black for the car looks, indigo, deep green or teal for the reference looks).
    const DomeTop = new THREE.Color('#010203'), DomeBottom = new THREE.Color('#010203');
    const Dome = new THREE.Mesh(new THREE.SphereGeometry(200, 32, 16), new THREE.ShaderMaterial({
        side: THREE.BackSide, depthWrite: false,
        uniforms: { uTop: { value: DomeTop }, uBottom: { value: DomeBottom } },
        vertexShader: 'varying vec3 vDirection; void main() { vDirection = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
        fragmentShader: 'uniform vec3 uTop, uBottom; varying vec3 vDirection; void main() { gl_FragColor = vec4(mix(uBottom, uTop, smoothstep(-0.35, 0.55, vDirection.y)), 1.0); }',
    }));
    Dome.renderOrder = -10;
    Scene.add(Dome);

    // Glass sphere for the Orbits look: fresnel rim, one sharp highlight and faint feathered fibres inside. It writes
    // depth, so the threads passing behind it disappear.
    const Globe = new THREE.Mesh(new THREE.SphereGeometry(1.32, 96, 64), new THREE.ShaderMaterial({
        uniforms: { uFade: { value: 0 }, uRim: { value: new THREE.Color() }, uInner: { value: new THREE.Color() } },
        vertexShader: 'varying vec3 vNormal, vView, vSpot; void main() { vSpot = position; vec4 P = modelViewMatrix * vec4(position, 1.0); vView = -P.xyz; vNormal = normalMatrix * normal; gl_Position = projectionMatrix * P; }',
        fragmentShader: `uniform float uFade; uniform vec3 uRim, uInner; varying vec3 vNormal, vView, vSpot;
            void main()
            {
                vec3 N = normalize(vNormal), V = normalize(vView), D = normalize(vSpot);
                float Facing = clamp(dot(N, V), 0.0, 1.0), Fresnel = pow(1.0 - Facing, 3.0);
                float Spec = pow(max(dot(N, normalize(normalize(vec3(-0.4, 0.7, 0.6)) + V)), 0.0), 140.0);
                float Wisp = 0.5 + 0.5 * sin(D.y * 7.0 + D.z * 5.0) * sin(D.x * 6.0 - D.y * 4.0);
                float Feather = pow(0.5 + 0.5 * sin(atan(D.y - 0.15, D.x) * 46.0 + D.z * 9.0 + Wisp * 3.0), 18.0) * smoothstep(0.2, 0.95, Facing) * Wisp * 0.12;
                vec3 Colour = uInner * (0.035 + 0.06 * Facing + Feather) + uRim * Fresnel * 1.5 + vec3(Spec * 3.0);
                gl_FragColor = vec4(Colour * uFade, 1.0);
            }`,
    }));
    Globe.position.set(0, 1.6, 0);
    Globe.visible = false;
    Scene.add(Globe);

    // Set-wide settings. Car 'Auto' and Backdrop null follow the lead layer's look.
    const Settings = { Glow: 1, Cut: 1.2, Exposure: 1, Car: 'Auto', Backdrop: null, Orbit: false, Paused: false, Lead: 0,
                       PaintFrom: '#3a4049', PaintTo: '#b8d032' };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    CAR
    //--------------------------------------------------------------------------------------------------------------------

    const Vehicle = await AssembleVehicle({ Finish: DefaultFinish({ 6: { color: '#3a4049', metalness: 0.6, roughness: 0.3 }, 13: { emissiveIntensity: 1.1 }, 14: { emissiveIntensity: 1.2 } }) });
    Scene.add(Vehicle.Root);
    const GhostCoat = new THREE.MeshPhysicalMaterial({ color: '#020304', metalness: 1, roughness: 0.16, clearcoat: 1, transparent: true, opacity: 1 });
    const GlassCoat = new THREE.MeshBasicMaterial({ color: '#0b1a26', transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
    const Edges = new THREE.Group();
    const EdgeLine = new THREE.LineBasicMaterial({ color: '#7ff3ff', transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.3 });
    Vehicle.Root.updateMatrixWorld(true);
    Vehicle.Root.traverse((Mesh) =>
    {
        if (!Mesh.isMesh) return;
        Mesh.userData.Painted = Mesh.material;
        Mesh.castShadow = false; Mesh.receiveShadow = false;
        const Lines = new THREE.LineSegments(new THREE.EdgesGeometry(Mesh.geometry, 28), EdgeLine);
        Lines.matrixAutoUpdate = false;
        Lines.matrix.copy(Mesh.matrixWorld);
        Edges.add(Lines);
    });
    Scene.add(Edges);

    // Vehicle transition: every paint material is patched so the same jagged edge the particles use (SweepJag) clips
    // the mesh (reveal / dissolve), swaps the body coat from one colour to the other (restyle) and glows where it crosses.
    // uSweepMode 0 hidden · 1 visible behind the edge · 2 recolour behind the edge · 3 visible ahead of it · 4 untouched.
    const Sweep = {
        uSweepFront: { value: 99 }, uSweepMode: { value: 4 }, uSweepClock: { value: 0 },
        uSweepFrom: { value: new THREE.Color() }, uSweepTo: { value: new THREE.Color() }, uSweepSeam: { value: new THREE.Color() },
    };
    const SweepHeader = `uniform float uSweepFront, uSweepMode, uSweepClock, uSweepCoat; uniform vec3 uSweepFrom, uSweepTo, uSweepSeam; varying vec3 vSweepSpot;\n${SweepJag}\n`;
    for (const [Key, Material] of Object.entries(Vehicle.Finish))
    {
        const Coat = +Key === Family.BodyCoat ? 1 : 0;
        Material.onBeforeCompile = (Shader) =>
        {
            Object.assign(Shader.uniforms, Sweep, { uSweepCoat: { value: Coat } });
            Shader.vertexShader = 'varying vec3 vSweepSpot;\n' + Shader.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n    vSweepSpot = (modelMatrix * vec4(transformed, 1.0)).xyz;');
            Shader.fragmentShader = SweepHeader + Shader.fragmentShader
                .replace('#include <clipping_planes_fragment>', `#include <clipping_planes_fragment>
    float SweepGap = vSweepSpot.x + SweepJag(vSweepSpot, uSweepClock) - uSweepFront;
    bool SweepDone = SweepGap > 0.0;
    if (uSweepMode < 0.5 || (uSweepMode < 1.5 && !SweepDone) || (uSweepMode > 2.5 && uSweepMode < 3.5 && SweepDone)) discard;`)
                .replace('#include <color_fragment>', `#include <color_fragment>
    if (uSweepCoat > 0.5 && uSweepMode < 3.5) diffuseColor.rgb = (uSweepMode > 2.5 || (uSweepMode > 1.5 && SweepDone)) ? uSweepTo : uSweepFrom;`)
                .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
    if (uSweepMode > 0.5 && uSweepMode < 3.5) totalEmissiveRadiance += uSweepSeam * exp(-abs(SweepGap) * 26.0) * 5.0;`);
        };
        Material.customProgramCacheKey = () => `Sweep${Coat}`;
        Material.needsUpdate = true;
    }
    const BodyCoat = Vehicle.Finish[Family.BodyCoat];

    let ShownCar = '';
    function DressCar(Mode)
    {
        if (Mode === ShownCar) return;
        ShownCar = Mode;
        Vehicle.Root.visible = Mode !== 'Hidden';
        Edges.visible = Mode === 'Glass' || Mode === 'Ghost';
        EdgeLine.opacity = Mode === 'Glass' ? 0.32 : 0.08;
        Vehicle.Root.traverse((Mesh) =>
        {
            if (!Mesh.isMesh) return;
            Mesh.material = Mode === 'Paint' ? Mesh.userData.Painted : (Mode === 'Glass' ? GlassCoat : GhostCoat);
            Mesh.renderOrder = Mode === 'Glass' ? 2 : 0;
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  LAYERS
    //--------------------------------------------------------------------------------------------------------------------

    const Scatter = ScatterSurface(Vehicle, MaxCount, 11);
    const Silhouette = SilhouetteProfile(Scatter);
    const Random = Seeded(23), Seeds = new Float32Array(MaxCount * 4);
    for (let Slot = 0; Slot < Seeds.length; Slot++) Seeds[Slot] = Random();
    const Attributes = {
        position: new THREE.Float32BufferAttribute(new Float32Array(12), 3),
        aCorner: new THREE.Float32BufferAttribute([-1, -1, 1, -1, 1, 1, -1, 1], 2),
        aSeed: new THREE.InstancedBufferAttribute(Seeds, 4),
        aAnchor: new THREE.InstancedBufferAttribute(Scatter.Positions, 3),
        aFacing: new THREE.InstancedBufferAttribute(Scatter.Normals, 3),
        aFamily: new THREE.InstancedBufferAttribute(Scatter.Families, 1),
    };
    const Corners = new THREE.Uint16BufferAttribute([0, 1, 2, 0, 2, 3], 1);

    // Uniforms every layer reads from the same objects.
    const Common = {
        uClock: { value: 0 }, uFocus: { value: 10 }, uViewport: { value: new THREE.Vector2(1, 1) },
        uProfile: { value: Silhouette.Texture }, uProfileFrom: { value: Silhouette.From }, uProfileSpan: { value: Silhouette.Span },
        uCarMin: { value: Scatter.Min }, uCarMax: { value: Scatter.Max },
    };

    const Layers = Looks.map((Look, Slot) =>
    {
        const Geometry = new THREE.InstancedBufferGeometry();
        Geometry.setIndex(Corners);
        for (const [Name, Attribute] of Object.entries(Attributes)) Geometry.setAttribute(Name, Attribute);
        Geometry.instanceCount = 0;
        const Uniforms = {
            ...Common,
            uSpeed: { value: 1 }, uTail: { value: 0.07 }, uSize: { value: 1 }, uTurbulence: { value: 0.5 }, uFade: { value: 0 },
            uSide: { value: 200 }, uVariant: { value: 0 }, uCount: { value: 60000 }, uAperture: { value: 0 },
            uFront: { value: 99 }, uFrontSpeed: { value: 1 }, uSweepMode: { value: 0 },
            uMorph: { value: 0 }, uMorphRate: { value: 0 }, uRelease: { value: 0 }, uReleaseRate: { value: 0 }, uScan: { value: -1 }, uCharge: { value: 0 },
            uTone0: { value: new THREE.Color() }, uTone1: { value: new THREE.Color() }, uTone2: { value: new THREE.Color() }, uTone3: { value: new THREE.Color() },
        };
        const Material = new THREE.ShaderMaterial({
            vertexShader: Prelude + Look.Track + Projection, fragmentShader: Fragment, uniforms: Uniforms,
            transparent: true, depthWrite: false, depthTest: true, blending: THREE.AdditiveBlending,
        });
        const Mesh = new THREE.Mesh(Geometry, Material);
        Mesh.frustumCulled = false;
        Mesh.renderOrder = 3 + Slot * 0.01;
        Mesh.visible = false;
        Scene.add(Mesh);
        const Layer = { Look, Slot, Mesh, Material, Geometry, Uniforms, Enabled: false, Fade: 0, Cycle: 0, Caption: '', Ghost: 1,
                        Params: { Count: 60000, Speed: 1, Tail: 0.07, Size: 1, Turbulence: 0.5, Aperture: 0, Variant: 0, Gain: 1,
                                  Palette: 'Ice', Tones: [...Palettes.Ice] } };
        ApplyDefaults(Layer);
        return Layer;
    });

    // Back to the look's own recipe: count, speed, trail, size, turbulence, lens, variant, palette.
    function ApplyDefaults(Layer)
    {
        const Defaults = Layer.Look.Defaults;
        Object.assign(Layer.Params, { Count: Defaults.Count, Speed: Defaults.Speed, Tail: Defaults.Tail, Size: Defaults.Size,
                                      Turbulence: Defaults.Turbulence, Aperture: Defaults.Aperture ?? 0, Variant: 0, Gain: 1 });
        AssignPalette(Layer, Defaults.Palette);
        Layer.Cycle = 0;
    }
    function AssignPalette(Layer, Name)
    {
        Layer.Params.Palette = Name;
        Layer.Params.Tones = [...Palettes[Name]];
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  CAMERA
    //--------------------------------------------------------------------------------------------------------------------

    const Orbit = { Yaw: 0, Pitch: 10, Distance: 10, Target: new THREE.Vector3() };
    const Goal = { Yaw: 0, Pitch: 10, Distance: 10, Target: new THREE.Vector3() };
    function AimAt(View, Snap)
    {
        Goal.Yaw = Orbit.Yaw + ((((View.Yaw - Orbit.Yaw) % 360) + 540) % 360 - 180);
        Goal.Pitch = View.Pitch; Goal.Distance = View.Distance; Goal.Target.fromArray(View.Target);
        if (Snap) { Orbit.Yaw = Goal.Yaw; Orbit.Pitch = Goal.Pitch; Orbit.Distance = Goal.Distance; Orbit.Target.copy(Goal.Target); }
    }
    function PlaceCamera(Seconds)
    {
        if (Settings.Orbit) Goal.Yaw += Seconds * 6;
        const Ease = 1 - Math.exp(-Seconds * 3.2);
        Orbit.Yaw += (Goal.Yaw - Orbit.Yaw) * Ease;
        Orbit.Pitch += (Goal.Pitch - Orbit.Pitch) * Ease;
        Orbit.Distance += (Goal.Distance - Orbit.Distance) * Ease;
        Orbit.Target.lerp(Goal.Target, Ease);
        const Yaw = Orbit.Yaw * Radian, Pitch = Orbit.Pitch * Radian;
        Camera.position.set(Math.cos(Pitch) * Math.cos(Yaw), Math.sin(Pitch), Math.cos(Pitch) * Math.sin(Yaw)).multiplyScalar(Orbit.Distance).add(Orbit.Target);
        Camera.lookAt(Orbit.Target);
    }
    let Drag = null;
    Canvas.addEventListener('pointerdown', (Event) => { Drag = { X: Event.clientX, Y: Event.clientY }; Canvas.setPointerCapture(Event.pointerId); Canvas.classList.add('Dragging'); });
    Canvas.addEventListener('pointermove', (Event) =>
    {
        if (!Drag) return;
        Goal.Yaw += (Event.clientX - Drag.X) * 0.3;
        Goal.Pitch = Math.min(80, Math.max(-2, Goal.Pitch + (Event.clientY - Drag.Y) * 0.25));
        Drag = { X: Event.clientX, Y: Event.clientY };
    });
    const EndDrag = () => { Drag = null; Canvas.classList.remove('Dragging'); };
    Canvas.addEventListener('pointerup', EndDrag);
    Canvas.addEventListener('pointercancel', EndDrag);
    Canvas.addEventListener('wheel', (Event) => { Event.preventDefault(); Goal.Distance = Math.min(40, Math.max(4, Goal.Distance * Math.exp(Event.deltaY * 0.001))); }, { passive: false });

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 TIMELINES
    //--------------------------------------------------------------------------------------------------------------------

    // Digital twin: 0–4.5 s assemble, hold, 6–8.5 s scan, hold, 10–13.5 s blow away, 0.5 s dark. Charging: 0 → 100 % in
    // 20 s. Vehicle transition: 17.5 s of dust · reveal · hold · restyle · hold · dissolve · dark, each sweep 4 s.
    let Clock = 0;
    function RunTimeline(Layer, Seconds)
    {
        const Key = Layer.Look.Key, U = Layer.Uniforms, Rate = Layer.Params.Speed;
        Layer.Cycle += Seconds * Rate;
        let CycleFade = 1, Caption = '';
        Layer.Ghost = 1;
        if (Key === 'Assemble')
        {
            const Time = Layer.Cycle % 14;
            U.uMorph.value = Span(Time, 0, 4.5);
            U.uMorphRate.value = Time < 4.5 ? Rate / 4.5 : 0;
            U.uScan.value = Time > 6 && Time < 8.5 ? Span(Time, 6, 8.5) : -1;
            U.uRelease.value = Span(Time, 10, 13.5);
            U.uReleaseRate.value = Time > 10 && Time < 13.5 ? Rate / 3.5 : 0;
            CycleFade = Math.min(Span(Time, 0, 0.6), 1 - Span(Time, 12.6, 13.5));
            Layer.Ghost = Math.min(1, Math.max(0, (U.uMorph.value - 0.8) / 0.2)) * (1 - Math.min(1, U.uRelease.value * 2.5));
            Caption = Time < 4.5 ? 'Assembling' : Time > 6 && Time < 8.5 ? 'Scanning' : Time > 10 ? 'Releasing' : 'Ready';
        }
        if (Key === 'Transition')
        {
            const Time = Layer.Cycle % 17.5, Start = Scatter.Max.x + 0.7, End = Scatter.Min.x - 0.7;
            const Stages = [[0, 1.2, 0, 'Dust twin'], [1.2, 5.2, 1, 'Materialising'], [5.2, 7, 1, 'Graphite'], [7, 11, 2, 'Restyling'], [11, 12.8, 2, 'Lime'], [12.8, 16.8, 3, 'Dematerialising'], [16.8, 17.5, 3, '']];
            const [From, To, Mode, Label] = Stages.find(([, Finish]) => Time < Finish) ?? Stages[Stages.length - 1];
            const Moving = Math.abs(To - From - 4) < 1e-6;
            U.uFront.value = Mode === 0 ? Start : Moving ? Start + (End - Start) * Span(Time, From, From + 4) : End;
            U.uFrontSpeed.value = (Start - End) / 4 * Rate;
            U.uSweepMode.value = Mode;
            CycleFade = Math.min(Span(Time, 0, 0.6), 1 - Span(Time, 16.8, 17.5));
            Caption = Label;
        }
        if (Key === 'Charge')
        {
            const Level = (Layer.Cycle % 20) / 20;
            U.uCharge.value = Level;
            const Power = Level < 0.55 ? 250 : Math.round(250 - (Level - 0.55) / 0.45 * 180);
            Caption = `Charging <b>${Math.round(Level * 100)} %</b> · ${Power} kW · +${Math.round(Level * 520)} km`;
        }
        Layer.Caption = Caption;
        return CycleFade;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   LOOP
    //--------------------------------------------------------------------------------------------------------------------

    function Resize(Width, Height)
    {
        const Ratio = Renderer.getPixelRatio();
        Renderer.setSize(Width, Height, false);
        Camera.aspect = Width / Math.max(1, Height);
        Camera.updateProjectionMatrix();
        Glow.Resize(Math.round(Width * Ratio), Math.round(Height * Ratio));
        Common.uViewport.value.set(Math.round(Width * Ratio), Math.round(Height * Ratio));
    }

    const Scratch = new THREE.Color();
    const LeadLayer = () => Layers[Settings.Lead] ?? Layers[0];
    function Advance(Seconds)
    {
        const Step = Settings.Paused ? 0 : Seconds;
        Clock += Step;
        Common.uClock.value = Clock;
        Common.uFocus.value = Orbit.Distance;
        const Lead = LeadLayer();
        Sweep.uSweepMode.value = 4;
        for (const Layer of Layers)
        {
            const Live = Layer.Enabled || Layer.Fade > 0;
            const CycleFade = Live ? RunTimeline(Layer, Step) : 1;
            const Target = Layer.Enabled ? 1 : 0;
            Layer.Fade += (Target - Layer.Fade) * Math.min(1, Seconds * 5);
            if (Target === 0 && Layer.Fade < 0.01) Layer.Fade = 0;
            Layer.Mesh.visible = Layer.Fade > 0;
            if (!Layer.Mesh.visible) continue;
            const P = Layer.Params, U = Layer.Uniforms;
            U.uFade.value = Layer.Fade * (Target ? CycleFade : 1) * P.Gain;
            U.uSpeed.value = P.Speed; U.uTail.value = P.Tail; U.uSize.value = P.Size; U.uTurbulence.value = P.Turbulence;
            U.uVariant.value = P.Variant; U.uAperture.value = P.Aperture;
            const Side = Math.floor(Math.sqrt(P.Count));
            U.uSide.value = Side;
            Layer.Geometry.instanceCount = Layer.Look.Key === 'Clones' ? Side * Side : Math.min(MaxCount, P.Count);
            U.uCount.value = Layer.Geometry.instanceCount;
            P.Tones.forEach((Hex, Slot) => U[`uTone${Slot}`].value.set(Hex).convertSRGBToLinear());
            if (Layer.Look.Key === 'Transition' && Layer.Enabled)
            {
                Sweep.uSweepFront.value = U.uFront.value; Sweep.uSweepMode.value = U.uSweepMode.value; Sweep.uSweepClock.value = Clock;
                Sweep.uSweepSeam.value.set(P.Tones[1]).convertSRGBToLinear();
            }
        }
        Sweep.uSweepFrom.value.set(Settings.PaintFrom).convertSRGBToLinear();
        Sweep.uSweepTo.value.set(Settings.PaintTo).convertSRGBToLinear();
        BodyCoat?.color?.set(Settings.PaintFrom);

        const Backdrop = Settings.Backdrop ?? Lead.Look.Backdrop ?? ['#010203', '#010203'], Blend = Math.min(1, Seconds * 4);
        DomeTop.lerp(Scratch.set(Backdrop[0]), Blend);                 // hex used as linear on purpose: a visible tint after ACES
        DomeBottom.lerp(Scratch.set(Backdrop[1]), Blend);

        const Orbiting = Layers.find((Layer) => Layer.Look.Sphere);
        Globe.material.uniforms.uFade.value = Orbiting.Fade;
        Globe.visible = Orbiting.Fade > 0.01;
        Globe.material.uniforms.uRim.value.set(Orbiting.Params.Tones[1]).convertSRGBToLinear();
        Globe.material.uniforms.uInner.value.set(Orbiting.Params.Tones[2]).convertSRGBToLinear();
        EdgeLine.color.set(Lead.Params.Tones[1]);

        DressCar(Settings.Car === 'Auto' ? Lead.Look.Car : Settings.Car);
        GhostCoat.opacity = Settings.Car === 'Auto' && Lead.Look.Key === 'Assemble' && Lead.Enabled ? Lead.Ghost : 1;
        GhostCoat.visible = GhostCoat.opacity > 0.01;
        if (ShownCar === 'Ghost') EdgeLine.opacity = 0.08 * GhostCoat.opacity;
        Glow.Strength = Settings.Glow; Glow.Cut = Settings.Cut; Glow.Exposure = Settings.Exposure;
        PlaceCamera(Seconds);
        Glow.Render(Scene, Camera, Clock);
    }

    // Jump straight to a moment: timelines run in 1/30 s steps so cycles land where they would live; fades settle.
    function Seek(Seconds)
    {
        Clock = 0;
        for (const Layer of Layers) { Layer.Cycle = 0; Layer.Fade = Layer.Enabled ? 1 : 0; }
        const Backdrop = Settings.Backdrop ?? LeadLayer().Look.Backdrop ?? ['#010203', '#010203'];
        DomeTop.set(Backdrop[0]); DomeBottom.set(Backdrop[1]);
        for (let Time = 0; Time < Seconds; Time += 1 / 30)
        {
            Clock += 1 / 30;
            for (const Layer of Layers) if (Layer.Enabled) RunTimeline(Layer, 1 / 30);
        }
        const Paused = Settings.Paused;
        Settings.Paused = true;
        PlaceCamera(10);
        Advance(0);
        Settings.Paused = Paused;
    }

    return {
        Renderer, Scene, Camera, Glow, Vehicle, Layers, Settings, Orbit, Goal, Scatter,
        ApplyDefaults, AssignPalette, AimAt, Resize, Advance, Seek,
        get Clock() { return Clock; },
        set Clock(Seconds) { Clock = Seconds; },
        get Lead() { return LeadLayer(); },
        get ShownCar() { return ShownCar; },
        Particles: () => Layers.reduce((Sum, Layer) => Sum + (Layer.Mesh.visible ? Layer.Geometry.instanceCount : 0), 0),
    };
}
