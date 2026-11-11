/*==========================================================================================================================================
                                                           UVVIEWPORT3D.JS
==========================================================================================================================================*/
// Synchronized Three.js WebGL2 3D mesh viewport for Frontier UV Studio:
//   - Live UV texture & UDIM tile mapping (RepeatWrapping + per-UDIM tint/checker/texel/importance/stretch shading)
//   - Real-time 3D Seam overlay & Wireframe edges
//   - Direct 3D face/island picking & 3D edge seam toggling synced with the 2D UV Stage

import * as THREE from '../Vendor/three.esm.min.js';
import { OrbitControls } from '../Vendor/orbit-controls.esm.min.js';
import { EdgeKey, UdimFromUV } from './TopologySurface.js';

function BuildCheckerTexture()
{
    const Size = 512;
    const Canvas = document.createElement('canvas');
    Canvas.width = Size;
    Canvas.height = Size;
    const Ctx = Canvas.getContext('2d');
    const Cells = 8;
    const Step = Size / Cells;

    const Palette = [
        '#36434b', '#3f4d47', '#4a4534', '#413a4b',
        '#344150', '#454a3b', '#4e3c3a', '#354845'
    ];
    for (let y = 0; y < Cells; y++)
    {
        for (let x = 0; x < Cells; x++)
        {
            Ctx.fillStyle = Palette[(x * 3 + y * 5) % Palette.length];
            Ctx.fillRect(x * Step, y * Step, Step, Step);
            const Half = Step * 0.5;
            Ctx.fillStyle = 'rgba(255,255,255,0.09)';
            Ctx.fillRect(x * Step, y * Step, Half, Half);
            Ctx.fillRect(x * Step + Half, y * Step + Half, Half, Half);
            Ctx.strokeStyle = 'rgba(255,255,255,0.12)';
            Ctx.strokeRect(x * Step + 0.5, y * Step + 0.5, Step - 1, Step - 1);
            Ctx.fillStyle = 'rgba(255,255,255,0.52)';
            Ctx.font = '600 13px ui-monospace, monospace';
            Ctx.fillText(`${String.fromCharCode(65 + x)}${Cells - y}`, x * Step + 7, y * Step + 20);
        }
    }

    const Tex = new THREE.CanvasTexture(Canvas);
    Tex.wrapS = THREE.RepeatWrapping;
    Tex.wrapT = THREE.RepeatWrapping;
    Tex.colorSpace = THREE.SRGBColorSpace;
    Tex.anisotropy = 4;
    return Tex;
}

function BuildMatcapTexture(Variant = 'clay')
{
    const Size = 512;
    const Canvas = document.createElement('canvas');
    Canvas.width = Size;
    Canvas.height = Size;
    const Ctx = Canvas.getContext('2d');
    const Img = Ctx.createImageData(Size, Size);
    const Data = Img.data;

    const IsSatin = Variant === 'matcap-satin';
    const BaseDark = IsSatin ? [24, 27, 32] : [74, 46, 36];
    const BaseMid = IsSatin ? [92, 100, 110] : [178, 120, 96];
    const SpecCol = IsSatin ? [239, 244, 207] : [255, 232, 214];
    const RimCol = IsSatin ? [138, 186, 224] : [158, 207, 255];

    const Lx = 0.40, Ly = 0.58, Lz = 0.707;
    const Hx = Lx * 0.5, Hy = Ly * 0.5, Hz = (Lz + 1) * 0.5;
    const HLen = Math.hypot(Hx, Hy, Hz);
    const Hnx = Hx / HLen, Hny = Hy / HLen, Hnz = Hz / HLen;

    const Fx = -0.58, Fy = -0.35, Fz = 0.73;

    for (let y = 0; y < Size; y++)
    {
        const Ny = 1 - (2 * (y + 0.5)) / Size;
        for (let x = 0; x < Size; x++)
        {
            const Nx = (2 * (x + 0.5)) / Size - 1;
            const R2 = Nx * Nx + Ny * Ny;
            const Nz = R2 <= 1 ? Math.sqrt(1 - R2) : 0;
            const idx = (y * Size + x) * 4;

            if (R2 > 1.0)
            {
                const Edge = Math.max(0, 1 - (R2 - 1) * 6);
                Data[idx] = BaseDark[0];
                Data[idx + 1] = BaseDark[1];
                Data[idx + 2] = BaseDark[2];
                Data[idx + 3] = Math.round(Edge * 255);
                continue;
            }

            const NdL = Math.max(0, Nx * Lx + Ny * Ly + Nz * Lz);
            const Wrap = Math.max(0, Math.min(1, (Nx * Lx + Ny * Ly + Nz * Lz + 0.45) / 1.45));
            const NdH = Math.max(0, Nx * Hnx + Ny * Hny + Nz * Hnz);
            const Spec = Math.pow(NdH, IsSatin ? 42 : 22) * (IsSatin ? 0.85 : 0.42);
            const Fill = Math.max(0, Nx * Fx + Ny * Fy + Nz * Fz) * 0.20;
            const Fresnel = Math.pow(1 - Nz, 2.8) * (IsSatin ? 0.42 : 0.24);

            const R = Math.min(255, BaseDark[0] + (BaseMid[0] - BaseDark[0]) * Wrap + SpecCol[0] * Spec + RimCol[0] * (Fresnel + Fill) + NdL * 24);
            const G = Math.min(255, BaseDark[1] + (BaseMid[1] - BaseDark[1]) * Wrap + SpecCol[1] * Spec + RimCol[1] * (Fresnel + Fill) + NdL * 22);
            const B = Math.min(255, BaseDark[2] + (BaseMid[2] - BaseDark[2]) * Wrap + SpecCol[2] * Spec + RimCol[2] * (Fresnel + Fill) + NdL * 20);

            Data[idx] = R;
            Data[idx + 1] = G;
            Data[idx + 2] = B;
            Data[idx + 3] = 255;
        }
    }
    Ctx.putImageData(Img, 0, 0);
    const Tex = new THREE.CanvasTexture(Canvas);
    Tex.colorSpace = THREE.SRGBColorSpace;
    return Tex;
}

function BuildVertexDotTexture()
{
    const Size = 64;
    const Canvas = document.createElement('canvas');
    Canvas.width = Size;
    Canvas.height = Size;
    const Ctx = Canvas.getContext('2d');
    Ctx.beginPath();
    Ctx.arc(Size * 0.5, Size * 0.5, Size * 0.42, 0, Math.PI * 2);
    Ctx.fillStyle = '#eff4cf';
    Ctx.fill();
    Ctx.lineWidth = 6;
    Ctx.strokeStyle = '#0b0b0b';
    Ctx.stroke();
    const Tex = new THREE.CanvasTexture(Canvas);
    Tex.colorSpace = THREE.SRGBColorSpace;
    return Tex;
}

function AppendEdgeTube(OutArr, A, B, Radius)
{
    const Dx = B[0] - A[0], Dy = B[1] - A[1], Dz = B[2] - A[2];
    const Len = Math.hypot(Dx, Dy, Dz);
    if (Len < 1e-7) return;
    const Nx = Dx / Len, Ny = Dy / Len, Nz = Dz / Len;
    const Upx = Math.abs(Ny) < 0.88 ? 0 : 1;
    const Upy = Math.abs(Ny) < 0.88 ? 1 : 0;
    const Upz = 0;

    let Ux = Ny * Upz - Nz * Upy;
    let Uy = Nz * Upx - Nx * Upz;
    let Uz = Nx * Upy - Ny * Upx;
    const UL = Math.hypot(Ux, Uy, Uz) || 1;
    Ux = (Ux / UL) * Radius;
    Uy = (Uy / UL) * Radius;
    Uz = (Uz / UL) * Radius;

    let Vx = Ny * Uz - Nz * Uy;
    let Vy = Nz * Ux - Nx * Uz;
    let Vz = Nx * Uy - Ny * Ux;
    const VL = Math.hypot(Vx, Vy, Vz) || 1;
    Vx = (Vx / VL) * Radius;
    Vy = (Vy / VL) * Radius;
    Vz = (Vz / VL) * Radius;

    const RingA = [
        [A[0] - Ux - Vx, A[1] - Uy - Vy, A[2] - Uz - Vz],
        [A[0] + Ux - Vx, A[1] + Uy - Vy, A[2] + Uz - Vz],
        [A[0] + Ux + Vx, A[1] + Uy + Vy, A[2] + Uz + Vz],
        [A[0] - Ux + Vx, A[1] - Uy + Vy, A[2] - Uz + Vz]
    ];
    const RingB = [
        [B[0] - Ux - Vx, B[1] - Uy - Vy, B[2] - Uz - Vz],
        [B[0] + Ux - Vx, B[1] + Uy - Vy, B[2] + Uz - Vz],
        [B[0] + Ux + Vx, B[1] + Uy + Vy, B[2] + Uz + Vz],
        [B[0] - Ux + Vx, B[1] - Uy + Vy, B[2] - Uz + Vz]
    ];
    for (let s = 0; s < 4; s++)
    {
        const s2 = (s + 1) % 4;
        const a0 = RingA[s], a1 = RingA[s2], b0 = RingB[s], b1 = RingB[s2];
        OutArr.push(
            a0[0], a0[1], a0[2], a1[0], a1[1], a1[2], b1[0], b1[1], b1[2],
            a0[0], a0[1], a0[2], b1[0], b1[1], b1[2], b0[0], b0[1], b0[2]
        );
    }
}

function ComputeCreaseCornerNormals(Surface, CreaseAngleDeg = 52)
{
    const CosCrease = Math.cos((CreaseAngleDeg * Math.PI) / 180);
    const VertFaces = new Map();
    for (let fi = 0; fi < Surface.faces.length; fi++)
    {
        for (const C of Surface.faces[fi].corners)
        {
            if (!VertFaces.has(C.v)) VertFaces.set(C.v, []);
            VertFaces.get(C.v).push(fi);
        }
    }
    const CornerNormals = new Array(Surface.faces.length);
    for (let fi = 0; fi < Surface.faces.length; fi++)
    {
        const Fn = Surface.faceNormals[fi] || [0, 1, 0];
        const C = Surface.faces[fi].corners;
        const List = new Array(C.length);
        for (let ci = 0; ci < C.length; ci++)
        {
            const Adj = VertFaces.get(C[ci].v) || [fi];
            let Sx = 0, Sy = 0, Sz = 0;
            for (const Afi of Adj)
            {
                const An = Surface.faceNormals[Afi] || Fn;
                if (Fn[0] * An[0] + Fn[1] * An[1] + Fn[2] * An[2] >= CosCrease)
                {
                    Sx += An[0]; Sy += An[1]; Sz += An[2];
                }
            }
            const L = Math.hypot(Sx, Sy, Sz) || 1;
            List[ci] = [Sx / L, Sy / L, Sz / L];
        }
        CornerNormals[fi] = List;
    }
    return CornerNormals;
}

function HeatRgb(T)
{
    const C = Math.max(0, Math.min(1, T));
    if (C < 0.35)
    {
        const K = C / 0.35;
        return [(68 + K * 37) / 255, (148 + K * 64) / 255, (235 - K * 65) / 255];
    }
    if (C < 0.7)
    {
        const K = (C - 0.35) / 0.35;
        return [(105 + K * 137) / 255, (212 - K * 18) / 255, (170 - K * 74) / 255];
    }
    const K = (C - 0.7) / 0.3;
    return [(242 - K * 10) / 255, (194 - K * 99) / 255, (96 - K * 14) / 255];
}

function HexToRgb01(Hex)
{
    const Clean = (Hex || '#eff4cf').replace('#', '');
    const Num = parseInt(Clean.length === 3 ? Clean.split('').map(C => C + C).join('') : Clean, 16);
    return [((Num >> 16) & 255) / 255, ((Num >> 8) & 255) / 255, (Num & 255) / 255];
}

export class UVViewport3D
{
    constructor(Container, { onPickFace = null, onPickEdge = null, onPickVertex = null, onToggleEdgeSeam = null, onContextMenu = null, onBoxSelect3D = null, onPaintSelect3D = null } = {})
    {
        this.container = Container;
        this.onPickFace = onPickFace;
        this.onPickEdge = onPickEdge;
        this.onPickVertex = onPickVertex;
        this.onToggleEdgeSeam = onToggleEdgeSeam;
        this.onContextMenu = onContextMenu;
        this.onBoxSelect3D = onBoxSelect3D;
        this.onPaintSelect3D = onPaintSelect3D;
        this.shadingMode = 'checker'; // 'checker' | 'flat' | 'matcap' | 'matcap-satin' | 'importance' | 'texel' | 'stretch'
        this.selectionMode = 'island';
        this.activeTool = 'select';
        this.paintRadiusPx = 36;
        this.showWireframe = true;
        this.showSeams = true;
        this.showBoundaries = true;
        this.webglReady = false;
        this.triToFace = [];
        this.surface = null;
        this.hoveredEdge = null;
        this.edgeNormalMap = new Map();
        this.edgeScaleRadius = 0.008;
        this.edgeList = [];
        this.edgeKeyToId = new Map();
        this._lastMeshTopoSig = null;

        this.InitScene();
    }

    InitScene()
    {
        this.canvas = document.createElement('canvas');
        this.canvas.className = 'viewport-webgl-canvas';
        this.container.append(this.canvas);

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color('#101214');
        this.camera = new THREE.PerspectiveCamera(40, 1, 0.05, 220);
        this.camera.position.set(5.4, 2.6, 6.2);

        try
        {
            this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: true, powerPreference: 'high-performance' });
            this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
            this.renderer.outputColorSpace = THREE.SRGBColorSpace;
            this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
            this.renderer.toneMappingExposure = 1.08;
            this.webglReady = true;
        }
        catch
        {
            this.webglReady = false;
            return;
        }

        this.controls = new OrbitControls(this.camera, this.canvas);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.08;
        this.controls.target.set(0, 0.85, 0);

        this.scene.add(new THREE.HemisphereLight('#dce6ec', '#2e2b25', 1.45));
        const Key = new THREE.DirectionalLight('#fff2dd', 2.4);
        Key.position.set(5, 8, 5);
        this.scene.add(Key);
        const Fill = new THREE.DirectionalLight('#9ecfff', 1.2);
        Fill.position.set(-5, 4, -4);
        this.scene.add(Fill);
        const Rim = new THREE.DirectionalLight('#9bdac5', 0.9);
        Rim.position.set(0, 3, -6);
        this.scene.add(Rim);

        const Grid = new THREE.GridHelper(16, 32, '#3d4346', '#212527');
        Grid.position.y = -0.002;
        this.scene.add(Grid);

        this.checkerTex = BuildCheckerTexture();
        this.matcapClayTex = BuildMatcapTexture('clay');
        this.matcapSatinTex = BuildMatcapTexture('matcap-satin');
        this.vertexDotTex = BuildVertexDotTexture();

        this.meshMat = new THREE.MeshStandardMaterial({
            map: this.checkerTex,
            vertexColors: true,
            roughness: 0.48,
            metalness: 0.08,
            flatShading: false,
            side: THREE.DoubleSide
        });
        this.matcapMat = new THREE.MeshMatcapMaterial({
            matcap: this.matcapClayTex,
            vertexColors: false,
            side: THREE.DoubleSide
        });

        this.geometry = new THREE.BufferGeometry();
        this.mesh = new THREE.Mesh(this.geometry, this.meshMat);
        this.scene.add(this.mesh);

        // GPU Face-Selection Mask Texture (256 x H, 1 texel per mesh face -> 0.02ms instant live updates!)
        this.gpuSelTexW = 256;
        this.gpuSelTexH = 16;
        this.gpuSelectionTex = new THREE.DataTexture(
            new Uint8Array(this.gpuSelTexW * this.gpuSelTexH * 4),
            this.gpuSelTexW,
            this.gpuSelTexH,
            THREE.RGBAFormat
        );
        this.gpuSelectionTex.minFilter = THREE.NearestFilter;
        this.gpuSelectionTex.magFilter = THREE.NearestFilter;
        this.gpuSelectionTex.generateMipmaps = false;
        this.gpuSelectionTex.needsUpdate = true;

        this.gpuSelOverlayMat = new THREE.ShaderMaterial({
            uniforms: {
                uSelTex: { value: this.gpuSelectionTex },
                uTexSize: { value: new THREE.Vector2(this.gpuSelTexW, this.gpuSelTexH) }
            },
            vertexShader: `
                attribute float aFaceId;
                varying float vFaceId;
                void main() {
                    vFaceId = aFaceId;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                }
            `,
            fragmentShader: `
                varying float vFaceId;
                uniform sampler2D uSelTex;
                uniform vec2 uTexSize;
                void main() {
                    float fid = floor(vFaceId + 0.5);
                    float tx = mod(fid, uTexSize.x);
                    float ty = floor(fid / uTexSize.x);
                    float sel = texture2D(uSelTex, (vec2(tx, ty) + 0.5) / uTexSize).r;
                    if (sel < 0.5) discard;
                    gl_FragColor = vec4(0.96, 0.72, 0.24, 0.38);
                }
            `,
            transparent: true,
            depthWrite: false,
            polygonOffset: true,
            polygonOffsetFactor: -3,
            polygonOffsetUnits: -3,
            toneMapped: false,
            side: THREE.DoubleSide
        });
        this.selectedFaceOverlayMesh = new THREE.Mesh(this.geometry, this.gpuSelOverlayMat);
        this.selectedFaceOverlayMesh.renderOrder = 2;
        this.scene.add(this.selectedFaceOverlayMesh);

        // GPU Barycentric Quad/N-Gon Wireframe Shader (zero diagonal lines, zero LineSegments buffer!)
        this.gpuWireMat = new THREE.ShaderMaterial({
            vertexShader: `
                attribute vec3 aBary;
                attribute vec3 aEdgeMask;
                varying vec3 vBary;
                varying vec3 vEdgeMask;
                void main() {
                    vBary = aBary;
                    vEdgeMask = aEdgeMask;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                }
            `,
            fragmentShader: `
                varying vec3 vBary;
                varying vec3 vEdgeMask;
                void main() {
                    vec3 d = fwidth(vBary);
                    vec3 a3 = smoothstep(vec3(0.0), d * 1.15, vBary + (1.0 - vEdgeMask) * 10.0);
                    float edgeFactor = 1.0 - min(min(a3.x, a3.y), a3.z);
                    if (edgeFactor < 0.02) discard;
                    gl_FragColor = vec4(1.0, 1.0, 1.0, edgeFactor * 0.20);
                }
            `,
            transparent: true,
            depthWrite: false,
            polygonOffset: true,
            polygonOffsetFactor: -2,
            polygonOffsetUnits: -2,
            toneMapped: false,
            side: THREE.DoubleSide
        });
        this.gpuWireMesh = new THREE.Mesh(this.geometry, this.gpuWireMat);
        this.gpuWireMesh.renderOrder = 2;
        this.scene.add(this.gpuWireMesh);

        // GPU Edge State Texture (256 x H RGBA: R=selected, G=seam, B=boundary, A=active)
        this.gpuEdgeTexW = 256;
        this.gpuEdgeTexH = 16;
        this.gpuEdgeStateTex = new THREE.DataTexture(
            new Uint8Array(this.gpuEdgeTexW * this.gpuEdgeTexH * 4),
            this.gpuEdgeTexW,
            this.gpuEdgeTexH,
            THREE.RGBAFormat
        );
        this.gpuEdgeStateTex.minFilter = THREE.NearestFilter;
        this.gpuEdgeStateTex.magFilter = THREE.NearestFilter;
        this.gpuEdgeStateTex.generateMipmaps = false;
        this.gpuEdgeStateTex.needsUpdate = true;

        this.gpuEdgeGeo = new THREE.BufferGeometry();
        const EdgeVertShader = `
            attribute vec3 aPosB;
            attribute vec2 aCorner;
            attribute float aEdgeId;
            uniform sampler2D uEdgeTex;
            uniform vec2 uEdgeTexSize;
            uniform vec2 uResolution;
            uniform float uHoverEdgeId;
            uniform float uShowSeams;
            uniform float uShowBoundaries;
            uniform float uGhostPass;
            varying vec4 vState;
            varying float vIsHover;
            varying float vSide;
            void main() {
                float eid = floor(aEdgeId + 0.5);
                float tx = mod(eid, uEdgeTexSize.x);
                float ty = floor(eid / uEdgeTexSize.x);
                vec4 state = texture2D(uEdgeTex, (vec2(tx, ty) + 0.5) / uEdgeTexSize);
                bool isHover = abs(eid - uHoverEdgeId) < 0.5;

                float halfW = 0.0;
                if (uGhostPass > 0.5) {
                    if (state.a > 0.5 || state.r > 0.5) halfW = 1.1;
                } else {
                    if (isHover) halfW = 2.85;
                    else if (state.a > 0.5) halfW = 2.75;
                    else if (state.r > 0.5) halfW = 2.25;
                    else if (state.g > 0.5 && uShowSeams > 0.5) halfW = 1.85;
                    else if (state.b > 0.5 && uShowBoundaries > 0.5) halfW = 1.15;
                }

                vState = state;
                vIsHover = isHover ? 1.0 : 0.0;
                vSide = aCorner.y;

                if (halfW <= 0.01) {
                    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                    return;
                }

                vec4 clipA = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                vec4 clipB = projectionMatrix * modelViewMatrix * vec4(aPosB, 1.0);
                if (clipA.w <= 0.05 || clipB.w <= 0.05) {
                    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                    return;
                }
                vec2 ndcA = clipA.xy / clipA.w;
                vec2 ndcB = clipB.xy / clipB.w;
                vec2 dirPx = (ndcB - ndcA) * uResolution * 0.5;
                float segLen = length(dirPx);
                vec2 tangent = segLen > 0.001 ? dirPx / segLen : vec2(1.0, 0.0);
                vec2 perp = vec2(-tangent.y, tangent.x);

                vec4 clipPos = mix(clipA, clipB, aCorner.x);
                clipPos.z -= (isHover || state.a > 0.5 ? 0.00075 : 0.00045) * clipPos.w;
                clipPos.xy += (perp * aCorner.y * halfW) * (2.0 / max(vec2(1.0), uResolution)) * clipPos.w;
                gl_Position = clipPos;
            }
        `;
        const EdgeFragShader = `
            uniform float uShowSeams;
            uniform float uShowBoundaries;
            uniform float uGhostPass;
            uniform vec3 uHoverColor;
            varying vec4 vState;
            varying float vIsHover;
            varying float vSide;
            void main() {
                float aa = 1.0 - smoothstep(0.68, 1.0, abs(vSide));
                if (aa < 0.02) discard;
                if (uGhostPass > 0.5) {
                    if (vState.a < 0.5 && vState.r < 0.5) discard;
                    vec3 gc = vState.a > 0.5 ? vec3(1.0, 0.616, 0.231) : vec3(0.937, 0.957, 0.812);
                    gl_FragColor = vec4(gc, aa * 0.24);
                    return;
                }
                vec3 col;
                float alpha = aa;
                if (vIsHover > 0.5) {
                    col = uHoverColor;
                } else if (vState.a > 0.5) {
                    col = vec3(1.0, 0.616, 0.231);
                } else if (vState.r > 0.5) {
                    col = vec3(0.937, 0.957, 0.812);
                } else if (vState.g > 0.5 && uShowSeams > 0.5) {
                    col = vec3(1.0, 0.365, 0.278);
                } else if (vState.b > 0.5 && uShowBoundaries > 0.5) {
                    col = vec3(0.482, 0.831, 0.722);
                    alpha *= 0.82;
                } else {
                    discard;
                }
                gl_FragColor = vec4(col, alpha);
            }
        `;
        this.gpuEdgeMat = new THREE.ShaderMaterial({
            uniforms: {
                uEdgeTex: { value: this.gpuEdgeStateTex },
                uEdgeTexSize: { value: new THREE.Vector2(this.gpuEdgeTexW, this.gpuEdgeTexH) },
                uResolution: { value: new THREE.Vector2(800, 600) },
                uHoverEdgeId: { value: -10.0 },
                uHoverColor: { value: new THREE.Color('#7dd3fc') },
                uShowSeams: { value: 1.0 },
                uShowBoundaries: { value: 1.0 },
                uGhostPass: { value: 0.0 }
            },
            vertexShader: EdgeVertShader,
            fragmentShader: EdgeFragShader,
            transparent: true,
            depthTest: true,
            depthWrite: false,
            toneMapped: false,
            side: THREE.DoubleSide
        });
        this.gpuEdgeMesh = new THREE.Mesh(this.gpuEdgeGeo, this.gpuEdgeMat);
        this.gpuEdgeMesh.renderOrder = 4;
        this.gpuEdgeMesh.frustumCulled = false;
        this.scene.add(this.gpuEdgeMesh);

        this.gpuEdgeGhostMat = new THREE.ShaderMaterial({
            uniforms: {
                uEdgeTex: { value: this.gpuEdgeStateTex },
                uEdgeTexSize: { value: new THREE.Vector2(this.gpuEdgeTexW, this.gpuEdgeTexH) },
                uResolution: this.gpuEdgeMat.uniforms.uResolution,
                uHoverEdgeId: { value: -10.0 },
                uHoverColor: this.gpuEdgeMat.uniforms.uHoverColor,
                uShowSeams: { value: 0.0 },
                uShowBoundaries: { value: 0.0 },
                uGhostPass: { value: 1.0 }
            },
            vertexShader: EdgeVertShader,
            fragmentShader: EdgeFragShader,
            transparent: true,
            depthTest: false,
            depthWrite: false,
            toneMapped: false,
            side: THREE.DoubleSide
        });
        this.gpuEdgeGhostMesh = new THREE.Mesh(this.gpuEdgeGeo, this.gpuEdgeGhostMat);
        this.gpuEdgeGhostMesh.renderOrder = 3;
        this.gpuEdgeGhostMesh.frustumCulled = false;
        this.scene.add(this.gpuEdgeGhostMesh);

        // GPU Selected/Active Vertex Point Cloud (256 x H RGBA: R=selected, G=active)
        this.gpuVertTexW = 256;
        this.gpuVertTexH = 16;
        this.gpuVertSelTex = new THREE.DataTexture(
            new Uint8Array(this.gpuVertTexW * this.gpuVertTexH * 4),
            this.gpuVertTexW,
            this.gpuVertTexH,
            THREE.RGBAFormat
        );
        this.gpuVertSelTex.minFilter = THREE.NearestFilter;
        this.gpuVertSelTex.magFilter = THREE.NearestFilter;
        this.gpuVertSelTex.generateMipmaps = false;
        this.gpuVertSelTex.needsUpdate = true;

        this.gpuVertGeo = new THREE.BufferGeometry();
        this.gpuVertMat = new THREE.ShaderMaterial({
            uniforms: {
                uVertTex: { value: this.gpuVertSelTex },
                uVertTexSize: { value: new THREE.Vector2(this.gpuVertTexW, this.gpuVertTexH) },
                uPixelRatio: { value: Math.min(2, window.devicePixelRatio || 1) }
            },
            vertexShader: `
                attribute float aVertId;
                uniform sampler2D uVertTex;
                uniform vec2 uVertTexSize;
                uniform float uPixelRatio;
                varying float vActive;
                void main() {
                    float vid = floor(aVertId + 0.5);
                    float tx = mod(vid, uVertTexSize.x);
                    float ty = floor(vid / uVertTexSize.x);
                    vec4 state = texture2D(uVertTex, (vec2(tx, ty) + 0.5) / uVertTexSize);
                    if (state.r < 0.5) {
                        gl_PointSize = 0.0;
                        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                        return;
                    }
                    vActive = state.g;
                    vec4 clipPos = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                    clipPos.z -= 0.0009 * clipPos.w;
                    gl_Position = clipPos;
                    gl_PointSize = (state.g > 0.5 ? 14.5 : 12.0) * uPixelRatio;
                }
            `,
            fragmentShader: `
                varying float vActive;
                void main() {
                    vec2 uv = gl_PointCoord * 2.0 - 1.0;
                    float d = length(uv);
                    if (d > 1.0) discard;
                    if (d > 0.68) {
                        gl_FragColor = vec4(0.04, 0.05, 0.06, 1.0);
                    } else {
                        vec3 col = vActive > 0.5 ? vec3(1.0, 0.68, 0.25) : vec3(0.937, 0.957, 0.812);
                        gl_FragColor = vec4(col, 1.0);
                    }
                }
            `,
            transparent: false,
            depthTest: true,
            depthWrite: true,
            toneMapped: false
        });
        this.gpuVertPoints = new THREE.Points(this.gpuVertGeo, this.gpuVertMat);
        this.gpuVertPoints.renderOrder = 7;
        this.gpuVertPoints.frustumCulled = false;
        this.scene.add(this.gpuVertPoints);

        // GPU Offscreen Face-ID Picker Scene (hardware Z-buffer occlusion, 24-bit face index)
        this.pickScene = new THREE.Scene();
        this.pickScene.background = null;
        this.pickMat = new THREE.ShaderMaterial({
            vertexShader: `
                attribute float aFaceId;
                varying float vFaceId;
                void main() {
                    vFaceId = aFaceId;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                }
            `,
            fragmentShader: `
                varying float vFaceId;
                void main() {
                    float id = floor(vFaceId + 0.5) + 1.0;
                    float b = floor((id + 0.001) / 65536.0);
                    float rem = id - b * 65536.0;
                    float g = floor((rem + 0.001) / 256.0);
                    float r = rem - g * 256.0;
                    gl_FragColor = vec4(r / 255.0, g / 255.0, b / 255.0, 1.0);
                }
            `,
            side: THREE.DoubleSide,
            depthTest: true,
            depthWrite: true,
            dithering: false,
            toneMapped: false
        });
        this.pickMesh = new THREE.Mesh(this.geometry, this.pickMat);
        this.pickScene.add(this.pickMesh);
        this.gpuPickRT = new THREE.WebGLRenderTarget(1, 1, {
            minFilter: THREE.NearestFilter,
            magFilter: THREE.NearestFilter,
            format: THREE.RGBAFormat,
            type: THREE.UnsignedByteType,
            depthBuffer: true,
            generateMipmaps: false
        });
        this.pickW = 0;
        this.pickH = 0;
        this.cachedPickPixels = null;
        this.pickBufferDirty = true;
        this._strokeSeenFaces = new Set();
        this._islandByFaceCache = new Map();
        this.controls.addEventListener('change', () => { this.pickBufferDirty = true; });

        // 3D Box Select Marquee & 3D Paint Select Brush Ring DOM Overlays
        this.marqueeEl = document.createElement('div');
        this.marqueeEl.className = 'uv-marquee-3d';
        this.marqueeEl.hidden = true;
        this.brushEl = document.createElement('div');
        this.brushEl.className = 'uv-brush-3d';
        this.brushEl.hidden = true;
        this.container.append(this.marqueeEl, this.brushEl);

        // Pointer picking, hover edge preview, 3D Box Select & 3D Paint Select
        let DownPt = null;
        let RightDownPt = null;
        let BoxDrag = null;
        let PaintDrag = null;

        const UpdateBrushRing = (E) =>
        {
            if (this.activeTool !== 'circle') { this.brushEl.hidden = true; return; }
            const R = this.container.getBoundingClientRect();
            const X = E.clientX - R.left, Y = E.clientY - R.top;
            const Diam = Math.max(16, this.paintRadiusPx * 2);
            this.brushEl.hidden = false;
            this.brushEl.style.left = `${X}px`;
            this.brushEl.style.top = `${Y}px`;
            this.brushEl.style.width = `${Diam}px`;
            this.brushEl.style.height = `${Diam}px`;
            this.brushEl.classList.toggle('subtract', !!E.ctrlKey);
        };

        this.canvas.addEventListener('pointerdown', E =>
        {
            if (E.button === 2) RightDownPt = [E.clientX, E.clientY];
            if (E.button !== 0) return;
            DownPt = [E.clientX, E.clientY];

            // Alt+LMB always falls through to orbit even when Box or Paint Select is active
            if (E.altKey)
            {
                if (this.activeTool === 'box' || this.activeTool === 'circle')
                {
                    this.controls.mouseButtons.LEFT = THREE.MOUSE.ROTATE;
                }
                return;
            }

            const R = this.container.getBoundingClientRect();
            const X = E.clientX - R.left, Y = E.clientY - R.top;

            if (this.activeTool === 'box')
            {
                this.controls.enabled = false;
                BoxDrag = { x0: X, y0: Y, x1: X, y1: Y, additive: E.shiftKey, subtract: E.ctrlKey };
                this.marqueeEl.hidden = false;
                this.marqueeEl.style.left = `${X}px`;
                this.marqueeEl.style.top = `${Y}px`;
                this.marqueeEl.style.width = '0px';
                this.marqueeEl.style.height = '0px';
                this.canvas.setPointerCapture(E.pointerId);
                return;
            }
            if (this.activeTool === 'circle')
            {
                this.controls.enabled = false;
                this._strokeSeenFaces.clear();
                this.EnsureGpuPickBuffer(R);
                PaintDrag = { subtract: E.ctrlKey };
                UpdateBrushRing(E);
                this.DispatchPaintSelect3D(X, Y, R, { subtract: E.ctrlKey, commit: false });
                this.canvas.setPointerCapture(E.pointerId);
            }
        });

        this.canvas.addEventListener('pointermove', E =>
        {
            if (BoxDrag)
            {
                const R = this.container.getBoundingClientRect();
                BoxDrag.x1 = E.clientX - R.left;
                BoxDrag.y1 = E.clientY - R.top;
                const MinX = Math.min(BoxDrag.x0, BoxDrag.x1);
                const MinY = Math.min(BoxDrag.y0, BoxDrag.y1);
                this.marqueeEl.style.left = `${MinX}px`;
                this.marqueeEl.style.top = `${MinY}px`;
                this.marqueeEl.style.width = `${Math.abs(BoxDrag.x1 - BoxDrag.x0)}px`;
                this.marqueeEl.style.height = `${Math.abs(BoxDrag.y1 - BoxDrag.y0)}px`;
                return;
            }
            if (PaintDrag)
            {
                const R = this.container.getBoundingClientRect();
                const X = E.clientX - R.left, Y = E.clientY - R.top;
                UpdateBrushRing(E);
                this.DispatchPaintSelect3D(X, Y, R, { subtract: PaintDrag.subtract || E.ctrlKey, commit: false });
                return;
            }
            if (this.activeTool === 'circle') UpdateBrushRing(E);
            else if (!this.brushEl.hidden) this.brushEl.hidden = true;

            if (E.buttons !== 0) return;
            this.UpdateHover(E);
        });

        this.canvas.addEventListener('pointerleave', () =>
        {
            this.ClearHover();
            if (this.brushEl) this.brushEl.hidden = true;
        });

        this.canvas.addEventListener('pointerup', E =>
        {
            if (BoxDrag && E.button === 0)
            {
                const B = BoxDrag;
                BoxDrag = null;
                this.marqueeEl.hidden = true;
                this.controls.enabled = true;
                try { this.canvas.releasePointerCapture(E.pointerId); } catch {}
                const MinX = Math.min(B.x0, B.x1), MaxX = Math.max(B.x0, B.x1);
                const MinY = Math.min(B.y0, B.y1), MaxY = Math.max(B.y0, B.y1);
                if (MaxX - MinX < 5 && MaxY - MinY < 5)
                {
                    this.HandleClick(E);
                }
                else
                {
                    const R = this.container.getBoundingClientRect();
                    this.DispatchBoxSelect3D(MinX, MinY, MaxX, MaxY, R, { additive: B.additive, subtract: B.subtract, xray: E.altKey });
                }
                DownPt = null;
                return;
            }
            if (PaintDrag && E.button === 0)
            {
                const Sub = PaintDrag.subtract || E.ctrlKey;
                PaintDrag = null;
                this._strokeSeenFaces.clear();
                this.controls.enabled = true;
                try { this.canvas.releasePointerCapture(E.pointerId); } catch {}
                const R = this.container.getBoundingClientRect();
                this.DispatchPaintSelect3D(E.clientX - R.left, E.clientY - R.top, R, { subtract: Sub, commit: true });
                DownPt = null;
                return;
            }
            if (E.button === 0)
            {
                if (this.activeTool === 'box' || this.activeTool === 'circle')
                {
                    this.controls.mouseButtons.LEFT = null;
                }
                const Moved = !DownPt || Math.hypot(E.clientX - DownPt[0], E.clientY - DownPt[1]) > 5;
                DownPt = null;
                if (!Moved) this.HandleClick(E);
            }
        });

        this.canvas.addEventListener('wheel', E =>
        {
            if (this.activeTool === 'circle' && E.shiftKey)
            {
                E.preventDefault();
                E.stopPropagation();
                this.paintRadiusPx = Math.max(12, Math.min(160, this.paintRadiusPx + (E.deltaY < 0 ? 6 : -6)));
                UpdateBrushRing(E);
            }
        }, { passive: false, capture: true });
        this.canvas.addEventListener('dblclick', E =>
        {
            if (this.activeTool === 'seam') return;
            if (this.selectionMode === 'edge')
            {
                const HitEdge = this.FindHitEdge(E);
                if (HitEdge && this.onPickEdge)
                {
                    this.onPickEdge(HitEdge.va, HitEdge.vb, { additive: E.shiftKey, loop: true, ring: false, path: false, faceIndex: HitEdge.faceIndex });
                }
                return;
            }
            if (this.selectionMode === 'face' || this.selectionMode === 'island')
            {
                const Pick = this.RaycastMesh(E);
                if (Pick && this.onPickFace)
                {
                    this.onPickFace(Pick.faceIndex, E.shiftKey, { island: true });
                }
            }
        });
        this.canvas.addEventListener('contextmenu', E =>
        {
            const Moved = RightDownPt && Math.hypot(E.clientX - RightDownPt[0], E.clientY - RightDownPt[1]) > 5;
            RightDownPt = null;
            if (!Moved && this.onContextMenu)
            {
                E.preventDefault();
                const HitEdge = this.FindHitEdge(E);
                this.onContextMenu(E.clientX, E.clientY, HitEdge);
            }
        });

        const Resize = () =>
        {
            const R = this.container.getBoundingClientRect();
            if (!R.width || !R.height || !this.webglReady) return;
            this.renderer.setSize(R.width, R.height, false);
            this.camera.aspect = R.width / R.height;
            this.camera.updateProjectionMatrix();
            this.gpuEdgeMat.uniforms.uResolution.value.set(R.width, R.height);
            this.pickBufferDirty = true;
        };
        new ResizeObserver(Resize).observe(this.container);
        Resize();

        const Loop = () =>
        {
            requestAnimationFrame(Loop);
            if (!this.webglReady) return;
            this.controls.update();
            this.renderer.render(this.scene, this.camera);
        };
        requestAnimationFrame(Loop);
    }

    SetActiveTool(Tool)
    {
        this.activeTool = Tool;
        if (this.controls)
        {
            const IsMarqueeOrBrush = Tool === 'box' || Tool === 'circle';
            this.controls.mouseButtons.LEFT = IsMarqueeOrBrush ? null : THREE.MOUSE.ROTATE;
            this.controls.mouseButtons.MIDDLE = IsMarqueeOrBrush ? THREE.MOUSE.ROTATE : THREE.MOUSE.DOLLY;
        }
        if (this.canvas)
        {
            this.canvas.style.cursor = Tool === 'box' ? 'crosshair' : Tool === 'circle' ? 'none' : Tool === 'seam' ? 'crosshair' : 'default';
        }
        if (this.brushEl && Tool !== 'circle') this.brushEl.hidden = true;
    }

    _ProjectScreen(PosArr, Rect)
    {
        const V = new THREE.Vector3(PosArr[0], PosArr[1], PosArr[2]).project(this.camera);
        if (V.z < -1 || V.z > 1) return null;
        return [(V.x * 0.5 + 0.5) * Rect.width, (-V.y * 0.5 + 0.5) * Rect.height, V.z];
    }

    _IsFaceFrontFacing(FaceIdx)
    {
        const F = this.surface?.faces?.[FaceIdx];
        const N = this.surface?.faceNormals?.[FaceIdx];
        if (!F || !N) return true;
        let Cx = 0, Cy = 0, Cz = 0;
        for (const C of F.corners)
        {
            const P = this.surface.positions[C.v];
            Cx += P[0]; Cy += P[1]; Cz += P[2];
        }
        const Inv = 1 / Math.max(1, F.corners.length);
        const Vx = this.camera.position.x - Cx * Inv;
        const Vy = this.camera.position.y - Cy * Inv;
        const Vz = this.camera.position.z - Cz * Inv;
        return (N[0] * Vx + N[1] * Vy + N[2] * Vz) > -0.04;
    }

    EnsureGpuPickBuffer(Rect)
    {
        if (!this.webglReady || !this.surface || !Rect?.width || !Rect?.height) return false;
        const Scale = Math.min(1, 640 / Math.max(Rect.width, Rect.height));
        const W = Math.max(64, Math.round(Rect.width * Scale));
        const H = Math.max(64, Math.round(Rect.height * Scale));
        if (!this.pickBufferDirty && this.cachedPickPixels && this.pickW === W && this.pickH === H)
        {
            return true;
        }
        if (this.pickW !== W || this.pickH !== H)
        {
            this.gpuPickRT.setSize(W, H);
            this.pickW = W;
            this.pickH = H;
            this.cachedPickPixels = new Uint8Array(W * H * 4);
        }
        this.camera.updateMatrixWorld();
        const PrevRT = this.renderer.getRenderTarget();
        const PrevClear = this.renderer.getClearColor(new THREE.Color());
        const PrevAlpha = this.renderer.getClearAlpha();
        this.renderer.setRenderTarget(this.gpuPickRT);
        this.renderer.setClearColor(0x000000, 0);
        this.renderer.clear(true, true, false);
        this.renderer.render(this.pickScene, this.camera);
        this.renderer.readRenderTargetPixels(this.gpuPickRT, 0, 0, W, H, this.cachedPickPixels);
        this.renderer.setRenderTarget(PrevRT);
        this.renderer.setClearColor(PrevClear, PrevAlpha);
        this.pickBufferDirty = false;
        return true;
    }

    FastUpdateGpuFaceMask(Surface, Islands = null)
    {
        if (!this.webglReady || !Surface || !this.gpuSelectionTex) return;
        const UseIslands = Islands || this._lastIslands || null;
        if (Islands) this._lastIslands = Islands;
        const FaceCount = Surface.faces.length;
        const W = this.gpuSelTexW;
        const NeedH = Math.max(1, Math.ceil(FaceCount / W));
        if (NeedH !== this.gpuSelTexH || this.gpuSelectionTex.image.data.length !== W * NeedH * 4)
        {
            this.gpuSelTexH = NeedH;
            this.gpuSelectionTex.dispose();
            this.gpuSelectionTex = new THREE.DataTexture(new Uint8Array(W * NeedH * 4), W, NeedH, THREE.RGBAFormat);
            this.gpuSelectionTex.minFilter = THREE.NearestFilter;
            this.gpuSelectionTex.magFilter = THREE.NearestFilter;
            this.gpuSelectionTex.generateMipmaps = false;
            this.gpuSelOverlayMat.uniforms.uSelTex.value = this.gpuSelectionTex;
            this.gpuSelOverlayMat.uniforms.uTexSize.value.set(W, NeedH);
        }
        const Data = this.gpuSelectionTex.image.data;
        Data.fill(0);
        if (this.selectionMode === 'island' && UseIslands)
        {
            for (const Isl of UseIslands)
            {
                if (!Isl.hidden && Surface.selectedIslands.has(Isl.id))
                {
                    for (const fi of Isl.faceIndices) Data[fi * 4] = 255;
                }
            }
        }
        for (const fi of Surface.selectedFaces)
        {
            if (fi >= 0 && fi < FaceCount) Data[fi * 4] = 255;
        }
        if (this.selectionMode === 'vertex' && Surface.selectedUVs.size)
        {
            const Adj = Surface.EnsureAdjacencyIndices();
            for (const UvId of Surface.selectedUVs)
            {
                const Faces = Adj.uvToFaces[UvId];
                if (!Faces) continue;
                for (let i = 0; i < Faces.length; i++)
                {
                    const fi = Faces[i];
                    if (fi >= 0 && fi < FaceCount) Data[fi * 4] = 255;
                }
            }
        }
        else if (this.selectionMode === 'edge' && Surface.selectedEdges3D.size)
        {
            const Adj = Surface.EnsureAdjacencyIndices();
            for (const K3 of Surface.selectedEdges3D)
            {
                const Faces = Adj.edge3dFaces.get(K3);
                if (!Faces) continue;
                for (let i = 0; i < Faces.length; i++)
                {
                    const fi = Faces[i];
                    if (fi >= 0 && fi < FaceCount) Data[fi * 4] = 255;
                }
            }
        }
        this.gpuSelectionTex.needsUpdate = true;
        this.selectedFaceOverlayMesh.visible = true;
        this.FastUpdateGpuEdgeAndVertMasks(Surface, UseIslands);
    }

    FastUpdateUvsOnly(Surface)
    {
        if (!this.webglReady || !Surface || !this.geometry) return;
        const UvAttr = this.geometry.getAttribute('uv');
        if (!UvAttr) return;
        const Arr = UvAttr.array;
        const IslandMap = this._lastIslands ? new Int32Array(Surface.faces.length).fill(-1) : null;
        const HiddenIslands = new Set();
        if (this._lastIslands)
        {
            for (const Isl of this._lastIslands)
            {
                if (Isl.hidden) HiddenIslands.add(Isl.id);
                for (const fi of Isl.faceIndices) IslandMap[fi] = Isl.id;
            }
        }
        let W = 0;
        for (let fi = 0; fi < Surface.faces.length; fi++)
        {
            if (IslandMap && HiddenIslands.has(IslandMap[fi])) continue;
            const Cr = Surface.faces[fi].corners;
            for (let k = 1; k < Cr.length - 1; k++)
            {
                const Idxs = [0, k, k + 1];
                for (let b = 0; b < 3; b++)
                {
                    const Uv = Surface.uvCoords[Cr[Idxs[b]].uv];
                    Arr[W++] = Uv[0];
                    Arr[W++] = Uv[1];
                }
            }
        }
        UvAttr.needsUpdate = true;
    }

    FastUpdateGpuEdgeAndVertMasks(Surface, Islands = null)
    {
        if (!this.webglReady || !Surface) return;
        const EdgeCount = this.edgeList.length;
        if (EdgeCount && this.gpuEdgeStateTex)
        {
            const W = this.gpuEdgeTexW;
            const NeedH = Math.max(1, Math.ceil(EdgeCount / W));
            if (NeedH !== this.gpuEdgeTexH || this.gpuEdgeStateTex.image.data.length !== W * NeedH * 4)
            {
                this.gpuEdgeTexH = NeedH;
                this.gpuEdgeStateTex.dispose();
                this.gpuEdgeStateTex = new THREE.DataTexture(new Uint8Array(W * NeedH * 4), W, NeedH, THREE.RGBAFormat);
                this.gpuEdgeStateTex.minFilter = THREE.NearestFilter;
                this.gpuEdgeStateTex.magFilter = THREE.NearestFilter;
                this.gpuEdgeStateTex.generateMipmaps = false;
                this.gpuEdgeMat.uniforms.uEdgeTex.value = this.gpuEdgeStateTex;
                this.gpuEdgeMat.uniforms.uEdgeTexSize.value.set(W, NeedH);
                this.gpuEdgeGhostMat.uniforms.uEdgeTex.value = this.gpuEdgeStateTex;
                this.gpuEdgeGhostMat.uniforms.uEdgeTexSize.value.set(W, NeedH);
            }
            const Data = this.gpuEdgeStateTex.image.data;
            Data.fill(0);
            const IsRegionMode = this.selectionMode === 'face' || this.selectionMode === 'island';
            const Adj = Surface.EnsureAdjacencyIndices();

            let IslandByFace = null;
            if (IsRegionMode && Islands)
            {
                IslandByFace = new Int32Array(Surface.faces.length).fill(-1);
                for (const Isl of Islands)
                {
                    if (!Isl.hidden && Surface.selectedIslands.has(Isl.id))
                    {
                        for (const fi of Isl.faceIndices) IslandByFace[fi] = Isl.id;
                    }
                }
            }

            for (let ei = 0; ei < EdgeCount; ei++)
            {
                const E = this.edgeList[ei];
                const Base = ei * 4;
                if (Surface.seams.has(E.key)) Data[Base + 1] = 255;
                if (E.isBoundary) Data[Base + 2] = 255;
                if (IsRegionMode)
                {
                    const Faces = Adj.edge3dFaces.get(E.key);
                    let SelCnt = 0;
                    if (Faces)
                    {
                        for (let i = 0; i < Faces.length; i++)
                        {
                            const Fi = Faces[i];
                            if (Surface.selectedFaces.has(Fi) || (IslandByFace && IslandByFace[Fi] >= 0)) SelCnt++;
                        }
                    }
                    if (SelCnt === 1) Data[Base] = 255;
                }
                else
                {
                    if (Surface.selectedEdges3D.has(E.key)) Data[Base] = 255;
                    if (Surface.activeEdge3D === E.key) Data[Base + 3] = 255;
                }
            }
            this.gpuEdgeStateTex.needsUpdate = true;
            this.gpuEdgeMat.uniforms.uShowSeams.value = this.showSeams ? 1.0 : 0.0;
            this.gpuEdgeMat.uniforms.uShowBoundaries.value = this.showBoundaries ? 1.0 : 0.0;
            this.gpuEdgeGhostMesh.visible = !IsRegionMode;
        }

        const VertCount = Surface.positions.length;
        if (VertCount && this.gpuVertSelTex)
        {
            const W = this.gpuVertTexW;
            const NeedH = Math.max(1, Math.ceil(VertCount / W));
            if (NeedH !== this.gpuVertTexH || this.gpuVertSelTex.image.data.length !== W * NeedH * 4)
            {
                this.gpuVertTexH = NeedH;
                this.gpuVertSelTex.dispose();
                this.gpuVertSelTex = new THREE.DataTexture(new Uint8Array(W * NeedH * 4), W, NeedH, THREE.RGBAFormat);
                this.gpuVertSelTex.minFilter = THREE.NearestFilter;
                this.gpuVertSelTex.magFilter = THREE.NearestFilter;
                this.gpuVertSelTex.generateMipmaps = false;
                this.gpuVertMat.uniforms.uVertTex.value = this.gpuVertSelTex;
                this.gpuVertMat.uniforms.uVertTexSize.value.set(W, NeedH);
            }
            const VData = this.gpuVertSelTex.image.data;
            VData.fill(0);
            if (this.selectionMode === 'vertex' && Surface.selectedUVs.size)
            {
                for (const UvId of Surface.selectedUVs)
                {
                    const Adj = Surface.EnsureAdjacencyIndices();
                    const Faces = Adj.uvToFaces[UvId];
                    if (!Faces) continue;
                    for (let i = 0; i < Faces.length; i++)
                    {
                        const Cr = Surface.faces[Faces[i]].corners;
                        for (let k = 0; k < Cr.length; k++)
                        {
                            if (Cr[k].uv === UvId)
                            {
                                VData[Cr[k].v * 4] = 255;
                                if (Surface.activeUV === UvId) VData[Cr[k].v * 4 + 1] = 255;
                            }
                        }
                    }
                }
            }
            this.gpuVertSelTex.needsUpdate = true;
            this.gpuVertPoints.visible = this.selectionMode === 'vertex' && Surface.selectedUVs.size > 0;
        }
    }

    DispatchBoxSelect3D(MinX, MinY, MaxX, MaxY, Rect, { additive = false, subtract = false, xray = false } = {})
    {
        if (!this.surface || !this.onBoxSelect3D || !Rect.width || !Rect.height) return;
        const InBox = (Pt) => Pt && Pt[0] >= MinX && Pt[0] <= MaxX && Pt[1] >= MinY && Pt[1] <= MaxY;
        const VertHits = new Set();
        const EdgeHits = new Set();

        if (!xray && this.EnsureGpuPickBuffer(Rect))
        {
            const Sx = this.pickW / Rect.width;
            const Sy = this.pickH / Rect.height;
            const X0 = Math.max(0, Math.floor(MinX * Sx));
            const X1 = Math.min(this.pickW - 1, Math.ceil(MaxX * Sx));
            const Y0 = Math.max(0, Math.floor((Rect.height - MaxY) * Sy));
            const Y1 = Math.min(this.pickH - 1, Math.ceil((Rect.height - MinY) * Sy));
            const FaceSet = new Set();
            const Px = this.cachedPickPixels;
            const W = this.pickW;
            const MaxFace = this.surface.faces.length;

            for (let y = Y0; y <= Y1; y++)
            {
                let idx = (y * W + X0) * 4;
                for (let x = X0; x <= X1; x++, idx += 4)
                {
                    if (Px[idx + 3] === 0) continue;
                    const Id = Px[idx] + (Px[idx + 1] << 8) + (Px[idx + 2] << 16);
                    if (Id > 0 && Id <= MaxFace) FaceSet.add(Id - 1);
                }
            }
            const FaceHits = [...FaceSet];
            if (this.selectionMode === 'vertex' || this.selectionMode === 'edge')
            {
                this.camera.updateMatrixWorld();
                for (const fi of FaceHits)
                {
                    const Cr = this.surface.faces[fi].corners;
                    for (const C of Cr)
                    {
                        const Sp = this._ProjectScreen(this.surface.positions[C.v], Rect);
                        if (InBox(Sp)) VertHits.add(C.v);
                    }
                    if (this.selectionMode === 'edge')
                    {
                        for (let k = 0; k < Cr.length; k++)
                        {
                            const Va = Cr[k].v, Vb = Cr[(k + 1) % Cr.length].v;
                            const Pa = this.surface.positions[Va], Pb = this.surface.positions[Vb];
                            const MidSp = this._ProjectScreen([0.5 * (Pa[0] + Pb[0]), 0.5 * (Pa[1] + Pb[1]), 0.5 * (Pa[2] + Pb[2])], Rect);
                            if (InBox(MidSp) || (VertHits.has(Va) && VertHits.has(Vb))) EdgeHits.add(EdgeKey(Va, Vb));
                        }
                    }
                }
            }
            this.onBoxSelect3D({ faces: FaceHits, verts3d: [...VertHits], edges3d: [...EdgeHits] }, { additive, subtract });
            return;
        }

        this.camera.updateMatrixWorld();
        const FaceHits = [];
        for (let fi = 0; fi < this.surface.faces.length; fi++)
        {
            if (!xray && !this._IsFaceFrontFacing(fi)) continue;
            const F = this.surface.faces[fi];
            let Cx = 0, Cy = 0, Cz = 0;
            let AnyCornerIn = false;
            for (const C of F.corners)
            {
                const P = this.surface.positions[C.v];
                Cx += P[0]; Cy += P[1]; Cz += P[2];
                const Sp = this._ProjectScreen(P, Rect);
                if (InBox(Sp)) { AnyCornerIn = true; VertHits.add(C.v); }
            }
            const Inv = 1 / Math.max(1, F.corners.length);
            const CenterSp = this._ProjectScreen([Cx * Inv, Cy * Inv, Cz * Inv], Rect);
            if (InBox(CenterSp) || AnyCornerIn)
            {
                FaceHits.push(fi);
                for (let k = 0; k < F.corners.length; k++)
                {
                    const Va = F.corners[k].v, Vb = F.corners[(k + 1) % F.corners.length].v;
                    const Pa = this.surface.positions[Va], Pb = this.surface.positions[Vb];
                    const MidSp = this._ProjectScreen([0.5 * (Pa[0] + Pb[0]), 0.5 * (Pa[1] + Pb[1]), 0.5 * (Pa[2] + Pb[2])], Rect);
                    if (InBox(MidSp) || (VertHits.has(Va) && VertHits.has(Vb))) EdgeHits.add(EdgeKey(Va, Vb));
                }
            }
        }
        this.onBoxSelect3D({ faces: FaceHits, verts3d: [...VertHits], edges3d: [...EdgeHits] }, { additive, subtract });
    }

    DispatchPaintSelect3D(Sx, Sy, Rect, { subtract = false, commit = false } = {})
    {
        if (!this.surface || !this.onPaintSelect3D || !Rect.width || !Rect.height) return;
        if (commit && this.EnsureGpuPickBuffer(Rect) === false) return;

        const Rad = Math.max(8, this.paintRadiusPx);
        const FaceSet = new Set();

        if (this.EnsureGpuPickBuffer(Rect))
        {
            const ScaleX = this.pickW / Rect.width;
            const ScaleY = this.pickH / Rect.height;
            const Cx = Sx * ScaleX;
            const Cy = (Rect.height - Sy) * ScaleY;
            const Rx = Math.max(2, Rad * ScaleX);
            const Ry = Math.max(2, Rad * ScaleY);
            const X0 = Math.max(0, Math.floor(Cx - Rx));
            const X1 = Math.min(this.pickW - 1, Math.ceil(Cx + Rx));
            const Y0 = Math.max(0, Math.floor(Cy - Ry));
            const Y1 = Math.min(this.pickH - 1, Math.ceil(Cy + Ry));
            const InvRx2 = 1 / (Rx * Rx);
            const InvRy2 = 1 / (Ry * Ry);
            const Px = this.cachedPickPixels;
            const W = this.pickW;
            const MaxFace = this.surface.faces.length;

            for (let y = Y0; y <= Y1; y++)
            {
                const Dy = y - Cy;
                const DyTerm = Dy * Dy * InvRy2;
                if (DyTerm > 1) continue;
                let idx = (y * W + X0) * 4;
                for (let x = X0; x <= X1; x++, idx += 4)
                {
                    const Dx = x - Cx;
                    if (Dx * Dx * InvRx2 + DyTerm > 1) continue;
                    if (Px[idx + 3] === 0) continue;
                    const Id = Px[idx] + (Px[idx + 1] << 8) + (Px[idx + 2] << 16);
                    if (Id > 0 && Id <= MaxFace)
                    {
                        const Fi = Id - 1;
                        if (!commit && this._strokeSeenFaces.has(Fi)) continue;
                        FaceSet.add(Fi);
                        if (!commit) this._strokeSeenFaces.add(Fi);
                    }
                }
            }
        }

        const FaceHits = [...FaceSet];
        if (!commit && FaceHits.length === 0) return;

        const VertHits = new Set();
        const EdgeHits = new Set();
        if (FaceHits.length && (this.selectionMode === 'vertex' || this.selectionMode === 'edge'))
        {
            this.camera.updateMatrixWorld();
            const InBrush = (Pt) => Pt && Math.hypot(Pt[0] - Sx, Pt[1] - Sy) <= Rad;
            for (const fi of FaceHits)
            {
                const Cr = this.surface.faces[fi].corners;
                for (const C of Cr)
                {
                    const Sp = this._ProjectScreen(this.surface.positions[C.v], Rect);
                    if (InBrush(Sp)) VertHits.add(C.v);
                }
                if (this.selectionMode === 'edge')
                {
                    for (let k = 0; k < Cr.length; k++)
                    {
                        const Va = Cr[k].v, Vb = Cr[(k + 1) % Cr.length].v;
                        const Pa = this.surface.positions[Va], Pb = this.surface.positions[Vb];
                        const MidSp = this._ProjectScreen([0.5 * (Pa[0] + Pb[0]), 0.5 * (Pa[1] + Pb[1]), 0.5 * (Pa[2] + Pb[2])], Rect);
                        if (InBrush(MidSp)) EdgeHits.add(EdgeKey(Va, Vb));
                    }
                }
            }
        }
        this.onPaintSelect3D({ faces: FaceHits, verts3d: [...VertHits], edges3d: [...EdgeHits] }, { subtract, commit });
    }

    RaycastMesh(Event)
    {
        if (!this.webglReady || !this.surface || !this.triToFace.length) return null;
        const Rect = this.canvas.getBoundingClientRect();
        if (!Rect.width || !Rect.height) return null;
        const Sx = Event.clientX - Rect.left;
        const Sy = Event.clientY - Rect.top;
        if (Sx < 0 || Sy < 0 || Sx >= Rect.width || Sy >= Rect.height) return null;

        let FaceIndex = -1;
        if (this.EnsureGpuPickBuffer(Rect))
        {
            const PxX = Math.max(0, Math.min(this.pickW - 1, Math.floor((Sx / Rect.width) * this.pickW)));
            const PxY = Math.max(0, Math.min(this.pickH - 1, Math.floor(((Rect.height - Sy) / Rect.height) * this.pickH)));
            const idx = (PxY * this.pickW + PxX) * 4;
            const Px = this.cachedPickPixels;
            if (Px && Px[idx + 3] > 0)
            {
                const Id = Px[idx] + (Px[idx + 1] << 8) + (Px[idx + 2] << 16);
                if (Id > 0 && Id <= this.surface.faces.length) FaceIndex = Id - 1;
            }
            if (FaceIndex < 0) return null;
        }
        else
        {
            return null;
        }

        const Ptr = new THREE.Vector2(
            (Sx / Rect.width) * 2 - 1,
            -(Sy / Rect.height) * 2 + 1
        );
        const Ray = new THREE.Raycaster();
        Ray.setFromCamera(Ptr, this.camera);
        const F = this.surface.faces[FaceIndex];
        const Pt = new THREE.Vector3();
        let HitPt = null;
        if (F && F.corners.length >= 3)
        {
            const P0 = this.surface.positions[F.corners[0].v];
            const V0 = new THREE.Vector3(P0[0], P0[1], P0[2]);
            for (let k = 1; k < F.corners.length - 1; k++)
            {
                const Pa = this.surface.positions[F.corners[k].v];
                const Pb = this.surface.positions[F.corners[k + 1].v];
                const V1 = new THREE.Vector3(Pa[0], Pa[1], Pa[2]);
                const V2 = new THREE.Vector3(Pb[0], Pb[1], Pb[2]);
                if (Ray.ray.intersectTriangle(V0, V1, V2, false, Pt))
                {
                    HitPt = Pt.clone();
                    break;
                }
            }
            if (!HitPt)
            {
                let Cx = 0, Cy = 0, Cz = 0;
                for (const C of F.corners) { const P = this.surface.positions[C.v]; Cx += P[0]; Cy += P[1]; Cz += P[2]; }
                const Inv = 1 / F.corners.length;
                HitPt = new THREE.Vector3(Cx * Inv, Cy * Inv, Cz * Inv);
            }
        }
        return HitPt ? { hit: { point: HitPt, faceIndex: FaceIndex }, faceIndex: FaceIndex } : null;
    }

    FindHitEdge(Event)
    {
        const Pick = this.RaycastMesh(Event);
        if (!Pick) return null;
        const { hit, faceIndex } = Pick;
        const F = this.surface.faces[faceIndex];
        if (!F) return null;
        const Hp = hit.point;
        let BestEdge = null, BestD = Infinity;
        for (let k = 0; k < F.corners.length; k++)
        {
            const Va = F.corners[k].v, Vb = F.corners[(k + 1) % F.corners.length].v;
            const A = this.surface.positions[Va], B = this.surface.positions[Vb];
            const D = PointToSeg3D(Hp.x, Hp.y, Hp.z, A, B);
            if (D < BestD)
            {
                BestD = D;
                BestEdge = { va: Va, vb: Vb, key: EdgeKey(Va, Vb), faceIndex, point: Hp };
            }
        }
        return BestEdge;
    }

    FindHitVertex(Event)
    {
        const Pick = this.RaycastMesh(Event);
        if (!Pick) return null;
        const { hit, faceIndex } = Pick;
        const F = this.surface.faces[faceIndex];
        if (!F) return null;
        const Hp = hit.point;
        let BestV = null, BestD = Infinity;
        for (const C of F.corners)
        {
            const P = this.surface.positions[C.v];
            const D = Math.hypot(Hp.x - P[0], Hp.y - P[1], Hp.z - P[2]);
            if (D < BestD) { BestD = D; BestV = C.v; }
        }
        return BestV !== null ? { v: BestV, faceIndex } : null;
    }

    UpdateHover(Event)
    {
        const WantEdgeHover = this.activeTool === 'seam' || (this.selectionMode === 'edge' && this.activeTool !== 'box' && this.activeTool !== 'circle') || (Event.altKey && this.activeTool === 'select');
        if (!WantEdgeHover)
        {
            this.ClearHover();
            this.canvas.style.cursor = this.activeTool === 'box' ? 'crosshair' : this.activeTool === 'circle' ? 'none' : this.activeTool === 'seam' ? 'crosshair' : 'default';
            return;
        }
        const Edge = this.FindHitEdge(Event);
        this.hoveredEdge = Edge;
        if (!Edge)
        {
            this.gpuEdgeMat.uniforms.uHoverEdgeId.value = -10.0;
            this.canvas.style.cursor = this.activeTool === 'seam' ? 'crosshair' : 'default';
            return;
        }
        const EdgeId = this.edgeKeyToId.get(Edge.key);
        this.gpuEdgeMat.uniforms.uHoverColor.value.set(this.activeTool === 'seam' ? '#ff8b78' : '#7dd3fc');
        this.gpuEdgeMat.uniforms.uHoverEdgeId.value = EdgeId !== undefined ? EdgeId : -10.0;
        this.canvas.style.cursor = this.activeTool === 'seam' ? 'crosshair' : 'pointer';
    }

    ClearHover()
    {
        if (!this.hoveredEdge && this.gpuEdgeMat.uniforms.uHoverEdgeId.value < 0) return;
        this.hoveredEdge = null;
        this.gpuEdgeMat.uniforms.uHoverEdgeId.value = -10.0;
    }

    HandleClick(Event)
    {
        if (!this.webglReady || !this.surface) return;

        // 1. Dedicated Seam Brush tool: click edge = toggle seam, Alt+click = toggle seam loop
        if (this.activeTool === 'seam')
        {
            const Edge = this.FindHitEdge(Event);
            if (Edge && this.onToggleEdgeSeam)
            {
                this.onToggleEdgeSeam(Edge.va, Edge.vb, { loop: Event.altKey, faceIndex: Edge.faceIndex });
            }
            return;
        }

        // 2. Edge Selection Mode (Blender-faithful modifiers: Click, Shift+Click, Alt+Click Loop, Ctrl+Alt+Click Ring, Ctrl+Click Path)
        if (this.selectionMode === 'edge')
        {
            const Edge = this.FindHitEdge(Event);
            if (!Edge) return;
            if (this.onPickEdge)
            {
                this.onPickEdge(Edge.va, Edge.vb, {
                    additive: Event.shiftKey,
                    loop: Event.altKey && !Event.ctrlKey,
                    ring: Event.ctrlKey && Event.altKey,
                    path: Event.ctrlKey && !Event.altKey,
                    faceIndex: Edge.faceIndex
                });
            }
            return;
        }

        // 3. Vertex Selection Mode (Click, Shift+Click, Alt+Click Edge Loop, Ctrl+Click Shortest Path)
        if (this.selectionMode === 'vertex')
        {
            if (Event.altKey && !Event.ctrlKey)
            {
                const Edge = this.FindHitEdge(Event);
                if (Edge && this.onPickVertex)
                {
                    this.onPickVertex(Edge.va, { additive: Event.shiftKey, loop: true, edgeVa: Edge.va, edgeVb: Edge.vb, faceIndex: Edge.faceIndex });
                    return;
                }
            }
            const VHit = this.FindHitVertex(Event);
            if (VHit && this.onPickVertex)
            {
                this.onPickVertex(VHit.v, { additive: Event.shiftKey, path: Event.ctrlKey && !Event.altKey, faceIndex: VHit.faceIndex });
                return;
            }
        }

        // 4. Face Mode Blender Modifiers: Alt+Click = Face Loop, Ctrl+Click = Shortest Face Path, Shift+Alt+Click = Quick Seam
        if (this.selectionMode === 'face' && (Event.altKey || Event.ctrlKey))
        {
            if (Event.shiftKey && Event.altKey)
            {
                const Edge = this.FindHitEdge(Event);
                if (Edge && this.onToggleEdgeSeam)
                {
                    this.onToggleEdgeSeam(Edge.va, Edge.vb, { loop: true, faceIndex: Edge.faceIndex });
                }
                return;
            }
            if (Event.altKey && !Event.ctrlKey)
            {
                const Edge = this.FindHitEdge(Event);
                if (Edge && this.onPickFace)
                {
                    this.onPickFace(Edge.faceIndex, Event.shiftKey, { loop: true, edgeVa: Edge.va, edgeVb: Edge.vb });
                }
                return;
            }
            if (Event.ctrlKey && !Event.altKey)
            {
                const Pick = this.RaycastMesh(Event);
                if (Pick && this.onPickFace)
                {
                    this.onPickFace(Pick.faceIndex, Event.shiftKey, { path: true });
                }
                return;
            }
        }

        // 5. Quick Seam shortcut in Island mode via Alt+Click / Ctrl+Click
        if (Event.altKey || Event.ctrlKey)
        {
            const Edge = this.FindHitEdge(Event);
            if (Edge && this.onToggleEdgeSeam)
            {
                this.onToggleEdgeSeam(Edge.va, Edge.vb, { loop: Event.shiftKey && Event.altKey, faceIndex: Edge.faceIndex });
            }
            return;
        }

        // 6. Face / Island picking
        const Pick = this.RaycastMesh(Event);
        if (Pick && this.onPickFace) this.onPickFace(Pick.faceIndex, Event.shiftKey, {});
    }

    CameraPreset(Preset)
    {
        if (!this.webglReady) return;
        let Target = new THREE.Vector3(0, 0.85, 0);
        let Radius = 1.9;
        if (this.surface && this.surface.positions.length)
        {
            let MinX = Infinity, MaxX = -Infinity;
            let MinY = Infinity, MaxY = -Infinity;
            let MinZ = Infinity, MaxZ = -Infinity;
            for (const P of this.surface.positions)
            {
                if (P[0] < MinX) MinX = P[0]; if (P[0] > MaxX) MaxX = P[0];
                if (P[1] < MinY) MinY = P[1]; if (P[1] > MaxY) MaxY = P[1];
                if (P[2] < MinZ) MinZ = P[2]; if (P[2] > MaxZ) MaxZ = P[2];
            }
            Target.set(0.5 * (MinX + MaxX), 0.5 * (MinY + MaxY), 0.5 * (MinZ + MaxZ));
            Radius = Math.max(1.1, 0.5 * Math.hypot(MaxX - MinX, MaxY - MinY, MaxZ - MinZ));
        }
        const Scale = Radius / 1.85;
        this.controls.target.copy(Target);
        if (Preset === 'Front') this.camera.position.set(Target.x + 5.2 * Scale, Target.y + 0.4 * Scale, Target.z + 0.01);
        else if (Preset === 'Side') this.camera.position.set(Target.x, Target.y + 0.4 * Scale, Target.z + 5.2 * Scale);
        else if (Preset === 'Top') this.camera.position.set(Target.x, Target.y + 5.4 * Scale, Target.z + 0.01);
        else this.camera.position.set(Target.x + 3.8 * Scale, Target.y + 1.8 * Scale, Target.z + 4.4 * Scale);
        this.controls.update();
    }

    SyncOverlayStates()
    {
        if (!this.webglReady) return;
        this.gpuWireMesh.visible = this.showWireframe;
        this.gpuEdgeMat.uniforms.uShowSeams.value = this.showSeams ? 1.0 : 0.0;
        this.gpuEdgeMat.uniforms.uShowBoundaries.value = this.showBoundaries ? 1.0 : 0.0;
    }

    SyncFromSurface(Surface, Islands, ImportanceGroups, TargetTexelDensity = 10)
    {
        this.surface = Surface;
        if (!this.webglReady || !Surface) return;

        const IsMatcap = this.shadingMode === 'matcap' || this.shadingMode === 'matcap-satin';
        const IsFlat = this.shadingMode === 'flat';
        if (IsMatcap)
        {
            this.matcapMat.matcap = this.shadingMode === 'matcap-satin' ? this.matcapSatinTex : this.matcapClayTex;
            this.matcapMat.needsUpdate = true;
            this.mesh.material = this.matcapMat;
        }
        else
        {
            this.meshMat.map = this.shadingMode === 'checker' ? this.checkerTex : null;
            this.meshMat.flatShading = IsFlat;
            this.meshMat.needsUpdate = true;
            this.mesh.material = this.meshMat;
        }

        let UvHash = 0;
        const ULen = Surface.uvCoords.length;
        const Step = Math.max(1, (ULen / 32) | 0);
        for (let i = 0; i < ULen; i += Step)
        {
            const P = Surface.uvCoords[i];
            UvHash = ((UvHash * 1.00013) + P[0] * 13.7 + P[1] * 29.3) % 1e9;
        }
        const DataSig = `${Surface.faces.length}:${ULen}:${Surface.hiddenIslands.size}:${Surface.importanceByFace.size}:${this.shadingMode}:${TargetTexelDensity}:${ImportanceGroups.length}:${UvHash.toFixed(4)}`;

        // Fast path: topology, UVs, and shading didn't change -> ONLY upload compact GPU masks (0.02ms!)
        if (this._lastMeshTopoSig === DataSig && this.triToFace.length > 0)
        {
            this.FastUpdateGpuFaceMask(Surface, Islands);
            this.SyncOverlayStates();
            return;
        }
        this._lastMeshTopoSig = DataSig;

        const PosArr = [];
        const NormArr = [];
        const UvArr = [];
        const ColArr = [];
        const FaceIdArr = [];
        const BaryArr = [];
        const EdgeMaskArr = [];
        this.triToFace = [];
        this.edgeNormalMap.clear();
        this.edgeList = [];
        this.edgeKeyToId.clear();
        this.pickBufferDirty = true;

        const IslandByFace = new Array(Surface.faces.length);
        for (const Isl of Islands)
        {
            for (const Fi of Isl.faceIndices) IslandByFace[Fi] = Isl;
        }
        const GroupColors = new Map(ImportanceGroups.map(G => [G.id, HexToRgb01(G.color)]));
        const UdimTints = [
            [0.95, 0.96, 0.92],
            [0.78, 0.90, 1.00],
            [0.98, 0.88, 0.70],
            [0.74, 0.94, 0.85]
        ];

        const CreaseNormals = IsMatcap ? ComputeCreaseCornerNormals(Surface, 52) : null;
        const EdgeAccum = new Map();

        for (let fi = 0; fi < Surface.faces.length; fi++)
        {
            const F = Surface.faces[fi];
            const Isl = IslandByFace[fi];
            if (Isl?.hidden) continue;

            const Corners = F.corners;
            const FaceN = Surface.faceNormals[fi] || [0, 1, 0];
            const SmoothN = CreaseNormals ? CreaseNormals[fi] : null;

            // Pure surface color without selection tint (gpuSelectionTex renders selection overlay on GPU)
            let R = 0.85, G = 0.86, B = 0.85;
            if (this.shadingMode === 'checker')
            {
                const TileIdx = Math.max(0, (Isl?.udimTile || 1001) - 1001) % UdimTints.length;
                [R, G, B] = UdimTints[TileIdx];
            }
            else if (this.shadingMode === 'flat')
            {
                R = 0.76; G = 0.78; B = 0.80;
            }
            else if (this.shadingMode === 'importance')
            {
                const GrpRgb = Isl?.importanceGroupId ? GroupColors.get(Isl.importanceGroupId) : null;
                [R, G, B] = GrpRgb || [0.42, 0.45, 0.48];
            }
            else if (this.shadingMode === 'texel')
            {
                const Ratio = (Isl?.texelDensity || 0) / Math.max(0.1, TargetTexelDensity);
                const T = Math.max(0, Math.min(1, 0.35 + Math.log2(Math.max(0.125, Ratio)) * 0.28));
                [R, G, B] = HeatRgb(T);
            }
            else if (this.shadingMode === 'stretch')
            {
                const D = typeof Surface.GetFaceDistortionFast === 'function' ? Surface.GetFaceDistortionFast(fi) : Surface.FaceTrianglesDistortion(fi);
                [R, G, B] = HeatRgb(Math.min(1, D.angleStretch * 2.2));
            }

            const LastTriIdx = Corners.length - 2;
            for (let k = 1; k < Corners.length - 1; k++)
            {
                // Polygon-boundary edge mask so barycentric GPU wireframe hides internal quad/ngon diagonals
                const M0 = 1.0; // edge (k, k+1) across from v0 is ALWAYS a real edge
                const M1 = k === LastTriIdx ? 1.0 : 0.0; // edge (k+1, 0) across from v1
                const M2 = k === 1 ? 1.0 : 0.0; // edge (0, k) across from v2

                const Idxs = [0, k, k + 1];
                const Barys = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
                for (let b = 0; b < 3; b++)
                {
                    const C = Corners[Idxs[b]];
                    const Pt = Surface.positions[C.v];
                    const Uv = Surface.uvCoords[C.uv];
                    const Nrm = SmoothN ? SmoothN[Idxs[b]] : FaceN;
                    PosArr.push(Pt[0], Pt[1], Pt[2]);
                    NormArr.push(Nrm[0], Nrm[1], Nrm[2]);
                    UvArr.push(Uv[0], Uv[1]);
                    ColArr.push(R, G, B);
                    FaceIdArr.push(fi);
                    BaryArr.push(Barys[b][0], Barys[b][1], Barys[b][2]);
                    EdgeMaskArr.push(M0, M1, M2);
                }
                this.triToFace.push(fi);
            }

            for (let k = 0; k < Corners.length; k++)
            {
                const Va = Corners[k].v, Vb = Corners[(k + 1) % Corners.length].v;
                const K3 = EdgeKey(Va, Vb);
                let Acc = EdgeAccum.get(K3);
                if (!Acc)
                {
                    EdgeAccum.set(K3, { va: Va, vb: Vb, nx: FaceN[0], ny: FaceN[1], nz: FaceN[2], count: 1 });
                }
                else
                {
                    Acc.nx += FaceN[0]; Acc.ny += FaceN[1]; Acc.nz += FaceN[2];
                    Acc.count++;
                }
            }
        }

        this.geometry.setAttribute('position', new THREE.Float32BufferAttribute(PosArr, 3));
        this.geometry.setAttribute('normal', new THREE.Float32BufferAttribute(NormArr, 3));
        this.geometry.setAttribute('uv', new THREE.Float32BufferAttribute(UvArr, 2));
        this.geometry.setAttribute('color', new THREE.Float32BufferAttribute(ColArr, 3));
        this.geometry.setAttribute('aFaceId', new THREE.Float32BufferAttribute(FaceIdArr, 1));
        this.geometry.setAttribute('aBary', new THREE.Float32BufferAttribute(BaryArr, 3));
        this.geometry.setAttribute('aEdgeMask', new THREE.Float32BufferAttribute(EdgeMaskArr, 3));
        this.geometry.computeBoundingSphere();

        const ModelRadius = this.geometry.boundingSphere?.radius || 2.0;
        const BaseR = Math.max(0.0038, Math.min(0.022, ModelRadius * 0.0042));
        this.edgeScaleRadius = BaseR;

        // Build static GPU Edge Ribbon geometry ONCE per topology
        const EdgeCount = EdgeAccum.size;
        const EPos = new Float32Array(EdgeCount * 6 * 3);
        const EPosB = new Float32Array(EdgeCount * 6 * 3);
        const ECorner = new Float32Array(EdgeCount * 6 * 2);
        const EId = new Float32Array(EdgeCount * 6);
        let ei = 0;
        const Lift = BaseR * 0.25;
        const TriCorners = [[0, -1], [0, 1], [1, 1], [0, -1], [1, 1], [1, -1]];

        for (const [K3, Acc] of EdgeAccum)
        {
            const NLen = Math.hypot(Acc.nx, Acc.ny, Acc.nz) || 1;
            const Nx = Acc.nx / NLen, Ny = Acc.ny / NLen, Nz = Acc.nz / NLen;
            this.edgeNormalMap.set(K3, [Nx, Ny, Nz]);
            this.edgeKeyToId.set(K3, ei);
            this.edgeList.push({ key: K3, va: Acc.va, vb: Acc.vb, isBoundary: Acc.count === 1 });

            const A = Surface.positions[Acc.va], Bpt = Surface.positions[Acc.vb];
            const Ax = A[0] + Nx * Lift, Ay = A[1] + Ny * Lift, Az = A[2] + Nz * Lift;
            const Bx = Bpt[0] + Nx * Lift, By = Bpt[1] + Ny * Lift, Bz = Bpt[2] + Nz * Lift;
            for (let v = 0; v < 6; v++)
            {
                const vi3 = (ei * 6 + v) * 3;
                const vi2 = (ei * 6 + v) * 2;
                EPos[vi3] = Ax; EPos[vi3 + 1] = Ay; EPos[vi3 + 2] = Az;
                EPosB[vi3] = Bx; EPosB[vi3 + 1] = By; EPosB[vi3 + 2] = Bz;
                ECorner[vi2] = TriCorners[v][0];
                ECorner[vi2 + 1] = TriCorners[v][1];
                EId[ei * 6 + v] = ei;
            }
            ei++;
        }
        this.gpuEdgeGeo.setAttribute('position', new THREE.BufferAttribute(EPos, 3));
        this.gpuEdgeGeo.setAttribute('aPosB', new THREE.BufferAttribute(EPosB, 3));
        this.gpuEdgeGeo.setAttribute('aCorner', new THREE.BufferAttribute(ECorner, 2));
        this.gpuEdgeGeo.setAttribute('aEdgeId', new THREE.BufferAttribute(EId, 1));

        // Build static GPU Vertex Points geometry ONCE per topology
        const VertCount = Surface.positions.length;
        const VPos = new Float32Array(VertCount * 3);
        const VId = new Float32Array(VertCount);
        for (let v = 0; v < VertCount; v++)
        {
            const P = Surface.positions[v];
            VPos[v * 3] = P[0]; VPos[v * 3 + 1] = P[1]; VPos[v * 3 + 2] = P[2];
            VId[v] = v;
        }
        this.gpuVertGeo.setAttribute('position', new THREE.BufferAttribute(VPos, 3));
        this.gpuVertGeo.setAttribute('aVertId', new THREE.BufferAttribute(VId, 1));

        const Rect = this.container.getBoundingClientRect();
        if (Rect.width && Rect.height) this.gpuEdgeMat.uniforms.uResolution.value.set(Rect.width, Rect.height);

        this.FastUpdateGpuFaceMask(Surface, Islands);
        this.SyncOverlayStates();
    }
}

function PointToSeg3D(Px, Py, Pz, A, B)
{
    const Abx = B[0] - A[0], Aby = B[1] - A[1], Abz = B[2] - A[2];
    const Len2 = Abx * Abx + Aby * Aby + Abz * Abz || 1e-8;
    const T = Math.max(0, Math.min(1, ((Px - A[0]) * Abx + (Py - A[1]) * Aby + (Pz - A[2]) * Abz) / Len2));
    return Math.hypot(Px - (A[0] + T * Abx), Py - (A[1] + T * Aby), Pz - (A[2] + T * Abz));
}
