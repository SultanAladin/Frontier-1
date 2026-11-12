/*==========================================================================================================================================
                                                       FORCEFIELD3DVIEWPORT.JS
==========================================================================================================================================*/
// 🧩 Three.js 3D Field Stage: machined Wind Generator & Forcefield emitters, finite influence envelopes, non-cluttered
//    3D vector needles, smooth RK2 spatial flow lines, advected test motes, and interactive raycast placement.

import * as THREE from '../Vendor/three.esm.min.js';
import { OrbitControls } from '../Vendor/orbit-controls.esm.min.js';
import { ForceFieldCatalogue as F, Radian } from './ForceFieldCatalogue.js';

const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
const CONTRIB_HEX = { Flow: '#8fd3c4', Accelerate: '#e8c27a', Damp: '#b9a3e8' };

function CreateFloorGridTexture()
{
    const Canvas = document.createElement('canvas');
    Canvas.width = 1024;
    Canvas.height = 1024;
    const Ctx = Canvas.getContext('2d');
    Ctx.fillStyle = '#090b0f';
    Ctx.fillRect(0, 0, 1024, 1024);
    Ctx.strokeStyle = 'rgba(255,255,255,0.035)';
    Ctx.lineWidth = 1;
    for (let I = 0; I <= 16; ++I)
    {
        const P = (I / 16) * 1024;
        Ctx.beginPath(); Ctx.moveTo(P, 0); Ctx.lineTo(P, 1024); Ctx.stroke();
        Ctx.beginPath(); Ctx.moveTo(0, P); Ctx.lineTo(1024, P); Ctx.stroke();
    }
    const C = 512;
    for (const R of [100, 220, 340, 460])
    {
        Ctx.strokeStyle = 'rgba(143, 211, 196, 0.12)';
        Ctx.beginPath();
        Ctx.arc(C, C, R, 0, Math.PI * 2);
        Ctx.stroke();
    }
    const Tex = new THREE.CanvasTexture(Canvas);
    Tex.colorSpace = THREE.SRGBColorSpace;
    return Tex;
}

function CreateRingPoints(Radius, Segs = 64)
{
    const Pts = [];
    for (let I = 0; I <= Segs; ++I)
    {
        const A = (I / Segs) * Math.PI * 2;
        Pts.push(new THREE.Vector3(Math.cos(A) * Radius, 0, Math.sin(A) * Radius));
    }
    return new THREE.BufferGeometry().setFromPoints(Pts);
}

export class ForceField3DViewport
{
    constructor(MountEl, Callbacks = {})
    {
        this.MountEl = MountEl;
        this.Callbacks = Callbacks;
        this.SceneState = null;
        this.SelectedToken = null;
        this.TimeSeconds = 0;
        this.ScaleFactor = 1 / 12; // 12 world metres = 1 three.js unit so 1.4 km spans ~116 units smoothly
        this.PlacementMode = false;
        this.PlacementType = 'WindGenerator';
        this.Rotors = [];
        this.Swirls = [];
        this.Disposed = false;
        this._LastFrame = performance.now();

        this.Canvas = document.createElement('canvas');
        this.Canvas.className = 'stage3d-canvas';
        MountEl.replaceChildren(this.Canvas);

        this.Scene = new THREE.Scene();
        this.Scene.background = new THREE.Color('#06080c');
        this.Scene.fog = new THREE.FogExp2('#06080c', 0.0042);

        this.Camera = new THREE.PerspectiveCamera(44, 16 / 9, 0.2, 600);
        this.Camera.position.set(42, 34, 58);

        try
        {
            this.Renderer = new THREE.WebGLRenderer({ canvas: this.Canvas, antialias: true, powerPreference: 'high-performance' });
        }
        catch
        {
            try { this.Renderer = new THREE.WebGLRenderer({ canvas: this.Canvas, antialias: true }); }
            catch { this.Renderer = null; }
        }

        if (this.Renderer)
        {
            this.Renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
            this.Renderer.toneMapping = THREE.ACESFilmicToneMapping;
            this.Renderer.toneMappingExposure = 1.15;

            this.Controls = new OrbitControls(this.Camera, this.Canvas);
            this.Controls.enableDamping = true;
            this.Controls.dampingFactor = 0.08;
            this.Controls.maxPolarAngle = Math.PI * 0.48;
            this.Controls.target.set(0, 1, 0);

            this._BuildChamber();
            this.SourceGroup = new THREE.Group();
            this.VectorGroup = new THREE.Group();
            this.FlowLineGroup = new THREE.Group();
            this.HeatmapCanvas = document.createElement('canvas');
            this.HeatmapCanvas.width = 128;
            this.HeatmapCanvas.height = 128;
            this.HeatmapTexture = new THREE.CanvasTexture(this.HeatmapCanvas);
            this.HeatmapTexture.colorSpace = THREE.SRGBColorSpace;
            this.HeatmapTexture.minFilter = THREE.LinearFilter;
            this.HeatmapTexture.magFilter = THREE.LinearFilter;
            this.HeatmapMesh = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshBasicMaterial({ map: this.HeatmapTexture, transparent: true, opacity: 0.88, depthWrite: false, side: THREE.DoubleSide }));
            this.HeatmapMesh.rotation.x = -Math.PI / 2;
            this.HeatmapMesh.visible = false;
            this.PlacementGhost = this._BuildGhost();
            this.PlacementGhost.visible = false;
            this.MoteSystem = this._BuildMotes();
            this.Scene.add(this.SourceGroup, this.VectorGroup, this.FlowLineGroup, this.HeatmapMesh, this.PlacementGhost, this.MoteSystem);
            this._BindPointer();
        }

        if (typeof ResizeObserver !== 'undefined' && MountEl)
        {
            this._ResizeObserver = new ResizeObserver(() => this.Resize());
            this._ResizeObserver.observe(MountEl);
        }

        this.Resize();
        this._Animate();
    }

    _BuildChamber()
    {
        const Amb = new THREE.AmbientLight('#8fb0c9', 0.45);
        const Key = new THREE.DirectionalLight('#f4f7fb', 1.15);
        Key.position.set(35, 65, 42);
        const Rim = new THREE.DirectionalLight('#6098b8', 0.42);
        Rim.position.set(-45, 25, -40);
        this.Scene.add(Amb, Key, Rim);

        const Floor = new THREE.Mesh(
            new THREE.PlaneGeometry(180, 180),
            new THREE.MeshStandardMaterial({ map: CreateFloorGridTexture(), roughness: 0.72, metalness: 0.14 })
        );
        Floor.rotation.x = -Math.PI / 2;
        Floor.position.y = -0.02;
        this.Scene.add(Floor);
    }

    _BuildGhost()
    {
        const G = new THREE.Group();
        const Mat = new THREE.LineBasicMaterial({ color: '#d1f773', transparent: true, opacity: 0.85 });
        const Ring = new THREE.Line(CreateRingPoints(1, 64), Mat);
        const Pole = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, 0, -1)]), Mat);
        const Dot = new THREE.Mesh(new THREE.SphereGeometry(0.45, 16, 12), new THREE.MeshBasicMaterial({ color: '#d1f773' }));
        G.add(Ring, Pole, Dot);
        G._Ring = Ring;
        return G;
    }

    _BuildMotes()
    {
        const Count = 420;
        const Pos = new Float32Array(Count * 3);
        const Col = new Float32Array(Count * 3);
        this.MotePosWorld = new Float32Array(Count * 3);
        this.MoteVelWorld = new Float32Array(Count * 3);
        this.MoteAge = new Float32Array(Count);
        for (let I = 0; I < Count; ++I) this._RespawnMote(I, true);
        const Geo = new THREE.BufferGeometry();
        Geo.setAttribute('position', new THREE.BufferAttribute(Pos, 3));
        Geo.setAttribute('color', new THREE.BufferAttribute(Col, 3));
        const Mat = new THREE.PointsMaterial({ size: 0.55, vertexColors: true, transparent: true, opacity: 0.85, blending: THREE.AdditiveBlending, depthWrite: false });
        const Pts = new THREE.Points(Geo, Mat);
        Pts.frustumCulled = false;
        return Pts;
    }

    _RespawnMote(Idx, Initial = false)
    {
        const Sources = this.SceneState?.Sources?.filter((S) => S.Enabled) || [];
        let WX = (Math.random() - 0.5) * 700, WY = 4 + Math.random() * 60, WZ = (Math.random() - 0.5) * 700;
        if (Sources.length)
        {
            const S = Sources[Idx % Sources.length];
            const R = Math.min(420, S.Radius || 160) * Math.sqrt(Math.random());
            const A = Math.random() * Math.PI * 2;
            WX = S.Centre[0] + Math.cos(A) * R;
            WZ = S.Centre[2] + Math.sin(A) * R;
            WY = Math.max(2, (S.Centre[1] || 8) + (Math.random() - 0.3) * 38);
        }
        this.MotePosWorld[Idx * 3] = WX;
        this.MotePosWorld[Idx * 3 + 1] = WY;
        this.MotePosWorld[Idx * 3 + 2] = WZ;
        this.MoteVelWorld[Idx * 3] = 0;
        this.MoteVelWorld[Idx * 3 + 1] = 0;
        this.MoteVelWorld[Idx * 3 + 2] = 0;
        this.MoteAge[Idx] = Initial ? Math.random() * 5 : 0;
    }

    _RaycastFloor(ClientX, ClientY)
    {
        const Rect = this.Canvas.getBoundingClientRect();
        const Ndc = new THREE.Vector2(((ClientX - Rect.left) / Rect.width) * 2 - 1, -((ClientY - Rect.top) / Rect.height) * 2 + 1);
        const Raycaster = new THREE.Raycaster();
        Raycaster.setFromCamera(Ndc, this.Camera);
        const SliceY = (this.SceneState?.Visualization?.SliceHeight || 10) * this.ScaleFactor;
        const Plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -SliceY);
        const Hit = new THREE.Vector3();
        if (!Raycaster.ray.intersectPlane(Plane, Hit)) return null;
        return [Hit.x / this.ScaleFactor, Hit.z / this.ScaleFactor];
    }

    _BindPointer()
    {
        const Ray = new THREE.Raycaster();
        let DownPos = null;
        this.Canvas.addEventListener('pointerdown', (E) => { DownPos = [E.clientX, E.clientY]; });
        this.Canvas.addEventListener('pointermove', (E) =>
        {
            if (!this.PlacementMode) { this.PlacementGhost.visible = false; return; }
            const W = this._RaycastFloor(E.clientX, E.clientY);
            if (!W) { this.PlacementGhost.visible = false; return; }
            const Type = F.TypeById[this.PlacementType] || F.TypeById.WindGenerator;
            const R = (Type.Defaults.Radius || 180) * this.ScaleFactor;
            const Y = (this.SceneState?.Visualization?.SliceHeight || 10) * this.ScaleFactor;
            this.PlacementGhost.visible = true;
            this.PlacementGhost.position.set(W[0] * this.ScaleFactor, Y, W[1] * this.ScaleFactor);
            this.PlacementGhost._Ring.scale.set(R, 1, R);
            this.Callbacks.OnPlacementHover?.(W[0], W[1], null);
        });
        this.Canvas.addEventListener('pointerup', (E) =>
        {
            if (!DownPos || Math.hypot(E.clientX - DownPos[0], E.clientY - DownPos[1]) > 5) return;
            if (this.PlacementMode && E.button === 0)
            {
                const W = this._RaycastFloor(E.clientX, E.clientY);
                if (W) this.Callbacks.OnPlace?.(W[0], W[1], null, null);
                return;
            }
            const Rect = this.Canvas.getBoundingClientRect();
            const Ndc = new THREE.Vector2(((E.clientX - Rect.left) / Rect.width) * 2 - 1, -((E.clientY - Rect.top) / Rect.height) * 2 + 1);
            Ray.setFromCamera(Ndc, this.Camera);
            const Hits = Ray.intersectObjects(this.SourceGroup.children, true);
            for (const H of Hits)
            {
                let Obj = H.object;
                while (Obj && !Obj.userData?.token) Obj = Obj.parent;
                if (Obj?.userData?.token) { this.Callbacks.OnSourcePick?.(Obj.userData.token); break; }
            }
        });
    }

    _ClearGroup(G)
    {
        while (G.children.length)
        {
            const C = G.children[0];
            G.remove(C);
            C.traverse?.((O) => { O.geometry?.dispose?.(); O.material?.dispose?.(); });
        }
    }

    SyncScene(SceneState, SelectedToken, TimeSeconds, VizOpts = {})
    {
        this.SceneState = SceneState;
        this.SelectedToken = SelectedToken;
        this.TimeSeconds = TimeSeconds;
        this.PlacementMode = Boolean(VizOpts.PlacementMode);
        this.PlacementType = VizOpts.PlacementType || 'WindGenerator';
        if (!this.Renderer) return;

        // Auto-scale so the visible view span maps comfortably into ~100 three.js units
        const Span = Math.max(200, Math.min(20000, VizOpts.Span || SceneState.Visualization?.Span || 1400));
        this.ScaleFactor = 85 / Span;
        const S = this.ScaleFactor;

        this._ClearGroup(this.SourceGroup);
        this._ClearGroup(this.VectorGroup);
        this._ClearGroup(this.FlowLineGroup);
        this.Rotors = [];
        this.Swirls = [];

        const Prepared = F.Prepare(SceneState, TimeSeconds);
        const SliceY = (SceneState.Visualization?.SliceHeight || 10) * S;
        this._SyncHeatmap(SceneState, Prepared, Span, S, SliceY);

        for (const Entry of Prepared.Entries)
        {
            const Src = Entry.Source;
            const Type = Entry.Type;
            const Selected = Src.Token === SelectedToken;
            const ColourHex = F.ResolveSourceColour(Src);
            const Col = new THREE.Color(ColourHex);
            const Root = new THREE.Group();
            Root.userData.token = Src.Token;
            const PosX = Entry.Centre[0] * S, PosY = Math.max(0.4, Entry.Centre[1] * S), PosZ = Entry.Centre[2] * S;
            Root.position.set(PosX, PosY, PosZ);

            // Physical Emitter Fixture with Entity Glow
            const Metal = new THREE.MeshStandardMaterial({ color: '#1b2028', roughness: 0.36, metalness: 0.82 });
            const GlowMat = new THREE.MeshBasicMaterial({ color: Col, transparent: true, opacity: Src.Enabled ? (Selected ? 0.98 : 0.82) : 0.24 });

            if (Src.Type === 'WindGenerator')
            {
                const Mast = new THREE.Mesh(new THREE.CylinderGeometry(0.18, 0.28, 2.2, 12), Metal);
                Mast.position.y = 1.1;
                const Nacelle = new THREE.Group();
                Nacelle.position.y = 2.2;
                Nacelle.rotation.y = -(Src.Bearing || 0) * Radian;
                const Pod = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 1.5, 14), Metal);
                Pod.rotation.x = Math.PI / 2;
                const Hub = new THREE.Group();
                Hub.position.z = -0.82;
                for (let B = 0; B < 3; ++B)
                {
                    const Blade = new THREE.Mesh(new THREE.BoxGeometry(0.14, 1.15, 0.05), GlowMat);
                    Blade.position.set(Math.sin((B * Math.PI * 2) / 3) * 0.6, Math.cos((B * Math.PI * 2) / 3) * 0.6, 0);
                    Blade.rotation.z = -(B * Math.PI * 2) / 3;
                    Hub.add(Blade);
                }
                Nacelle.add(Pod, Hub);
                Root.add(Mast, Nacelle);
                this.Rotors.push({ Obj: Hub, Speed: Src.Enabled ? 1.5 + Src.Strength * 0.22 : 0 });
            }
            else if (Src.Type === 'Tornado' || Src.Type === 'Orbit')
            {
                const Core = new THREE.Mesh(new THREE.TorusGeometry(1.1, 0.08, 10, 32), GlowMat);
                Core.rotation.x = Math.PI / 2;
                Core.position.y = 1.0;
                const Ring2 = new THREE.Mesh(new THREE.TorusGeometry(0.65, 0.06, 10, 28), GlowMat);
                Ring2.rotation.x = Math.PI / 2;
                Ring2.position.y = 2.0;
                Root.add(Core, Ring2);
                this.Swirls.push({ Obj: Root, Speed: Src.Enabled ? (Src.Spin < 0 ? -2.2 : 2.2) : 0 });
            }
            else
            {
                const Orb = new THREE.Mesh(new THREE.SphereGeometry(0.55, 18, 14), new THREE.MeshStandardMaterial({ color: Col, emissive: Col, emissiveIntensity: Src.Enabled ? 0.55 : 0.1, roughness: 0.35, metalness: 0.35 }));
                const Collar = new THREE.Mesh(new THREE.TorusGeometry(0.9, 0.07, 10, 28), GlowMat);
                Collar.rotation.x = Math.PI / 2;
                Root.add(Orb, Collar);
            }

            // Minimalist Finite Influence Boundary Shell + Soft Glowing Area Floor Disc
            const ShowBounds = SceneState.Visualization?.ShowInfluence !== false;
            if (ShowBounds)
            {
                const R = Clamp((Src.Radius || 180) * S, 1.5, 95);
                const EnvOpacity = Selected ? 0.68 : 0.28;
                const EnvMat = new THREE.LineBasicMaterial({ color: Col, transparent: true, opacity: EnvOpacity });
                if (Src.Reach === 'Box')
                {
                    const Ext = F.ResolveBoxExtents(Src);
                    const BoxW = Clamp(Ext.HalfX * 2 * S, 2, 95);
                    const BoxD = Clamp(Ext.HalfZ * 2 * S, 2, 95);
                    const BoxH = Clamp(Ext.Height * S, 2, 40);
                    const BoxGeo = new THREE.EdgesGeometry(new THREE.BoxGeometry(BoxW, BoxH, BoxD));
                    const Wire = new THREE.LineSegments(BoxGeo, EnvMat);
                    Wire.position.y = BoxH * 0.5 - PosY;
                    Wire.rotation.y = -(Src.Bearing || 0) * Radian;
                    const Pad = new THREE.Mesh(new THREE.PlaneGeometry(BoxW, BoxD), new THREE.MeshBasicMaterial({ color: Col, transparent: true, opacity: Selected ? 0.14 : 0.06, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
                    Pad.rotation.x = -Math.PI / 2;
                    Pad.rotation.z = (Src.Bearing || 0) * Radian;
                    Pad.position.y = 0.05 - PosY;
                    Root.add(Wire, Pad);
                }
                else
                {
                    const BaseRing = new THREE.Line(CreateRingPoints(R, 64), EnvMat);
                    BaseRing.position.y = SliceY - PosY;
                    const GlowDisc = new THREE.Mesh(new THREE.CircleGeometry(R, 48), new THREE.MeshBasicMaterial({ color: Col, transparent: true, opacity: Selected ? 0.15 : 0.07, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
                    GlowDisc.rotation.x = -Math.PI / 2;
                    GlowDisc.position.y = 0.05 - PosY;
                    Root.add(BaseRing, GlowDisc);
                    if (Selected)
                    {
                        const Meridian = new THREE.Line(CreateRingPoints(R, 48), new THREE.LineBasicMaterial({ color: Col, transparent: true, opacity: 0.32 }));
                        Meridian.rotation.z = Math.PI / 2;
                        Root.add(Meridian);
                    }
                }
            }
            this.SourceGroup.add(Root);
        }

        const ShowVectors = SceneState.Visualization?.Arrows !== false;
        const ShowFlowLines = SceneState.Visualization?.FlowLines !== false;
        const Contrib = SceneState.Visualization?.Contribution || 'Flow';
        const SampleOut = new Float64Array(3);
        const HalfW = Span * 0.44;

        // Non-cluttered 3D Sparse Vector Needles (only where field magnitude > cutoff!)
        if (ShowVectors && Contrib !== 'Damp')
        {
            const Segs = [];
            const Steps = 11;
            for (let IZ = 0; IZ < Steps; ++IZ)
            {
                for (let IX = 0; IX < Steps; ++IX)
                {
                    const WX = -HalfW + (IX / (Steps - 1)) * HalfW * 2;
                    const WZ = -HalfW + (IZ / (Steps - 1)) * HalfW * 2;
                    SampleOut[0] = SampleOut[1] = SampleOut[2] = 0;
                    F.EvaluatePrepared(SceneState, Prepared, Contrib, WX, SceneState.Visualization?.SliceHeight || 10, WZ, 2, SampleOut, SceneState.Visualization?.Solo);
                    const Mag = Math.hypot(SampleOut[0], SampleOut[1], SampleOut[2]);
                    if (Mag < 0.8) continue;
                    const Len = Clamp(Mag * 0.14, 1.1, 4.2);
                    const DX = (SampleOut[0] / Mag) * Len, DY = (SampleOut[1] / Mag) * Len, DZ = (SampleOut[2] / Mag) * Len;
                    const SX = WX * S, SY = SliceY + 0.6, SZ = WZ * S;
                    Segs.push(new THREE.Vector3(SX, SY, SZ), new THREE.Vector3(SX + DX, SY + DY, SZ + DZ));
                }
            }
            if (Segs.length)
            {
                const ArrowGeo = new THREE.BufferGeometry().setFromPoints(Segs);
                const ArrowMat = new THREE.LineBasicMaterial({ color: Contrib === 'Accelerate' ? '#e8c27a' : '#bfeef2', transparent: true, opacity: 0.72 });
                this.VectorGroup.add(new THREE.LineSegments(ArrowGeo, ArrowMat));
            }
        }

        // Smooth 3D RK2 Curved Flow Lines seeded around active sources (glowing in entity colour!)
        if (ShowFlowLines && Contrib !== 'Damp')
        {
            const StepWorld = Span * 0.028;
            for (const Entry of Prepared.Entries)
            {
                if (!Entry.Source.Enabled) continue;
                const LineMat = new THREE.LineBasicMaterial({ color: new THREE.Color(F.ResolveSourceColour(Entry.Source)), transparent: true, opacity: 0.78 });
                const R = Math.min(HalfW * 0.8, Entry.Source.Radius || 160);
                for (let AIdx = 0; AIdx < 6; ++AIdx)
                {
                    const Ang = (AIdx / 6) * Math.PI * 2;
                    let WX = Entry.Centre[0] + Math.cos(Ang) * R * 0.42;
                    let WY = Math.max(4, SceneState.Visualization?.SliceHeight || 10);
                    let WZ = Entry.Centre[2] + Math.sin(Ang) * R * 0.42;
                    const Pts = [];
                    for (let K = 0; K < 16; ++K)
                    {
                        SampleOut[0] = SampleOut[1] = SampleOut[2] = 0;
                        F.EvaluatePrepared(SceneState, Prepared, Contrib, WX, WY, WZ, 2, SampleOut, SceneState.Visualization?.Solo);
                        const M = Math.hypot(SampleOut[0], SampleOut[1], SampleOut[2]);
                        if (M < 0.6) break;
                        Pts.push(new THREE.Vector3(WX * S, WY * S + 0.5, WZ * S));
                        WX += (SampleOut[0] / M) * StepWorld;
                        WY = Clamp(WY + (SampleOut[1] / M) * StepWorld * 0.35, 1, 180);
                        WZ += (SampleOut[2] / M) * StepWorld;
                    }
                    if (Pts.length >= 3)
                    {
                        const Curve = new THREE.CatmullRomCurve3(Pts);
                        const TubeGeo = new THREE.BufferGeometry().setFromPoints(Curve.getPoints(28));
                        this.FlowLineGroup.add(new THREE.Line(TubeGeo, LineMat));
                    }
                }
            }
        }
    }

    _SyncHeatmap(SceneState, Prepared, Span, Scale, SliceY)
    {
        if (!this.HeatmapMesh || !this.HeatmapCanvas) return;
        const Enabled = Boolean(SceneState.Visualization?.Heatmap);
        this.HeatmapMesh.visible = Enabled;
        if (!Enabled) return;

        const Width = this.HeatmapCanvas.width, Height = this.HeatmapCanvas.height;
        const Ctx = this.HeatmapCanvas.getContext('2d');
        const Image = Ctx.createImageData(Width, Height);
        const Pixels = Image.data;
        const HalfSpan = Span * 0.44;
        const WorldY = SceneState.Visualization?.SliceHeight || 10;
        const Contribution = SceneState.Visualization?.Contribution || 'Flow';
        const Output = new Float64Array(3);
        let Peak = 0;
        const Values = new Float32Array(Width * Height);
        for (let Row = 0; Row < Height; ++Row)
        {
            const Z = HalfSpan - (Row / (Height - 1)) * HalfSpan * 2;
            for (let Column = 0; Column < Width; ++Column)
            {
                const X = -HalfSpan + (Column / (Width - 1)) * HalfSpan * 2;
                Output[0] = Output[1] = Output[2] = 0;
                F.EvaluatePrepared(SceneState, Prepared, Contribution, X, WorldY, Z, 2, Output, SceneState.Visualization?.Solo);
                const Magnitude = Contribution === 'Damp' ? Math.max(0, Output[0]) : Math.hypot(Output[0], Output[1], Output[2]);
                const Index = Row * Width + Column;
                Values[Index] = Magnitude;
                Peak = Math.max(Peak, Magnitude);
            }
        }
        const ScaleValue = Math.max(Peak * 0.72, Contribution === 'Damp' ? 0.01 : 0.5);
        for (let Index = 0; Index < Values.length; ++Index)
        {
            const T = Clamp(Values[Index] / ScaleValue, 0, 1);
            const Smooth = T * T * (3 - 2 * T);
            const Offset = Index * 4;
            // Deep blue -> teal -> amber -> coral, preserving dark negative space between field envelopes.
            if (Smooth < 0.5)
            {
                const U = Smooth * 2;
                Pixels[Offset] = 24 + 18 * U;
                Pixels[Offset + 1] = 96 + 126 * U;
                Pixels[Offset + 2] = 190 - 82 * U;
            }
            else
            {
                const U = (Smooth - 0.5) * 2;
                Pixels[Offset] = 42 + 213 * U;
                Pixels[Offset + 1] = 222 - 148 * U;
                Pixels[Offset + 2] = 108 - 74 * U;
            }
            Pixels[Offset + 3] = Math.round(Clamp(Smooth * 0.78, 0, 0.66) * 255);
        }
        Ctx.putImageData(Image, 0, 0);
        this.HeatmapTexture.needsUpdate = true;
        const PlaneSpan = Span * 0.88 * Scale;
        this.HeatmapMesh.geometry.dispose();
        this.HeatmapMesh.geometry = new THREE.PlaneGeometry(PlaneSpan, PlaneSpan);
        this.HeatmapMesh.position.set(0, SliceY + 0.12, 0);
    }

    Resize()
    {
        const W = Math.max(320, this.MountEl?.clientWidth || 800);
        const H = Math.max(240, this.MountEl?.clientHeight || 500);
        if (this.Renderer)
        {
            this.Renderer.setSize(W, H, false);
            this.Camera.aspect = W / H;
            this.Camera.updateProjectionMatrix();
        }
    }

    _Animate()
    {
        if (this.Disposed) return;
        requestAnimationFrame(() => this._Animate());
        const Now = performance.now();
        const Dt = Math.min(0.05, (Now - this._LastFrame) * 0.001);
        this._LastFrame = Now;

        for (const R of this.Rotors) R.Obj.rotation.z += R.Speed * Dt;
        for (const S of this.Swirls) S.Obj.rotation.y += S.Speed * Dt;

        if (this.Renderer && this.SceneState && this.MoteSystem)
        {
            const S = this.ScaleFactor;
            const Prepared = F.Prepare(this.SceneState, this.TimeSeconds);
            const Flow = new Float64Array(3), Acc = new Float64Array(3), Damp = new Float64Array(3);
            const PosAttr = this.MoteSystem.geometry.getAttribute('position');
            const ColAttr = this.MoteSystem.geometry.getAttribute('color');
            const Count = this.MoteAge.length;
            for (let I = 0; I < Count; ++I)
            {
                this.MoteAge[I] += Dt;
                if (this.MoteAge[I] > 6.5) { this._RespawnMote(I, false); continue; }
                let X = this.MotePosWorld[I * 3], Y = this.MotePosWorld[I * 3 + 1], Z = this.MotePosWorld[I * 3 + 2];
                Flow[0] = Flow[1] = Flow[2] = 0;
                Acc[0] = Acc[1] = Acc[2] = 0;
                Damp[0] = 0;
                F.EvaluatePrepared(this.SceneState, Prepared, 'Flow', X, Y, Z, 2, Flow);
                F.EvaluatePrepared(this.SceneState, Prepared, 'Accelerate', X, Y, Z, 2, Acc);
                F.EvaluatePrepared(this.SceneState, Prepared, 'Damp', X, Y, Z, 2, Damp);

                const DragK = 2.4 + Damp[0] * 3.2;
                let VX = this.MoteVelWorld[I * 3], VY = this.MoteVelWorld[I * 3 + 1], VZ = this.MoteVelWorld[I * 3 + 2];
                VX += ((Flow[0] - VX) * DragK + Acc[0]) * Dt * 4.5;
                VY += ((Flow[1] - VY) * DragK + Acc[1]) * Dt * 4.5;
                VZ += ((Flow[2] - VZ) * DragK + Acc[2]) * Dt * 4.5;
                X += VX * Dt;
                Y = Clamp(Y + VY * Dt, 1, 260);
                Z += VZ * Dt;
                this.MotePosWorld[I * 3] = X; this.MotePosWorld[I * 3 + 1] = Y; this.MotePosWorld[I * 3 + 2] = Z;
                this.MoteVelWorld[I * 3] = VX; this.MoteVelWorld[I * 3 + 1] = VY; this.MoteVelWorld[I * 3 + 2] = VZ;

                PosAttr.setXYZ(I, X * S, Y * S, Z * S);
                const Speed = Math.hypot(VX, VY, VZ);
                const Glow = Clamp(Speed / 18, 0.1, 1.0);
                ColAttr.setXYZ(I, 0.45 + Glow * 0.5, 0.78 + Glow * 0.18, 0.85);
            }
            PosAttr.needsUpdate = true;
            ColAttr.needsUpdate = true;

            this.Controls?.update();
            this.Renderer.render(this.Scene, this.Camera);
        }
    }

    Dispose()
    {
        this.Disposed = true;
        this._ResizeObserver?.disconnect();
        this.HeatmapTexture?.dispose?.();
        this.HeatmapMesh?.geometry?.dispose?.();
        this.HeatmapMesh?.material?.dispose?.();
        this.Renderer?.dispose?.();
    }
}
