/*============================================================================================================================================
                                                         GARMENTVIEWPORT3D.JS
============================================================================================================================================*/
// WebGL2 Three.js PBR Atelier 3D Garment Draping Viewport.
// Features 3-point couture lighting, faceless atelier mannequin + live tailor tape rings, DoubleSide PBR cloth
// with woven procedural micro-bump texture, 5 diagnostic heatmaps, 3D seam threads, and live grab/pin tools.

import * as THREE from 'three';
import { OrbitControls } from '../Vendor/orbit-controls.esm.min.js';
import { evaluateHeatmapColorRGB } from './PatternCanvas2D.js';

function createWeaveNormalCanvasTexture()
{
    if (typeof document === 'undefined') return null;
    const size = 64;
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    const img = ctx.createImageData(size, size);
    for (let y = 0; y < size; ++y)
    {
        for (let x = 0; x < size; ++x)
        {
            const warpWave = Math.sin((x / size) * Math.PI * 16.0);
            const weftWave = Math.sin((y / size) * Math.PI * 16.0);
            const h = 0.5 + 0.25 * warpWave + 0.25 * weftWave;
            const g = Math.round(clampVal(h * 255, 0, 255));
            const idx = (y * size + x) * 4;
            img.data[idx] = g;
            img.data[idx + 1] = g;
            img.data[idx + 2] = g;
            img.data[idx + 3] = 255;
        }
    }
    ctx.putImageData(img, 0, 0);
    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = THREE.RepeatWrapping;
    tex.wrapT = THREE.RepeatWrapping;
    tex.repeat.set(28, 28);
    return tex;
}

function clampVal(v, lo, hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

export class GarmentViewport3D
{
    constructor(container, options = {})
    {
        this.container = container;
        this.onPinChange = options.onPinChange || null;
        this.onSelectPiece = options.onSelectPiece || null;

        this.toolMode = 'orbit'; // 'orbit' | 'grab' | 'pin'
        this.heatmapMode = 'textile';
        this.fabricHex = '#c4513d';
        this.showMannequin = true;
        this.showMeasureRings = true;
        this.showWireframe = false;
        this.showSeams3D = true;

        this.mannequin = null;
        this.solver = null;

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x111216);

        this.camera = new THREE.PerspectiveCamera(34, 1.0, 0.04, 25.0);
        this.camera.position.set(0.58, 1.18, 2.38);

        this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance', preserveDrawingBuffer: true });
        this.renderer.setPixelRatio(Math.min(2, (typeof window !== 'undefined' ? window.devicePixelRatio : 1) || 1));
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFShadowMap;
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.08;
        container.appendChild(this.renderer.domElement);

        this.controls = new OrbitControls(this.camera, this.renderer.domElement);
        this.controls.target.set(0.0, 0.96, 0.03);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.08;
        this.controls.minDistance = 0.55;
        this.controls.maxDistance = 5.5;
        this.controls.update();

        this._weaveBumpTex = createWeaveNormalCanvasTexture();
        this._setupAtelierStage();
        this._setupInteraction();

        this._rgbScratch = [0, 0, 0];
        this._mannequinMesh = null;
        this._measureLines = null;
        this._clothMesh = null;
        this._wireMesh = null;
        this._seamLines = null;
        this._pinGroup = new THREE.Group();
        this.scene.add(this._pinGroup);
    }

    _setupAtelierStage()
    {
        const hemi = new THREE.HemisphereLight(0xe0ebff, 0x1a1c22, 0.85);
        this.scene.add(hemi);

        const keyLight = new THREE.DirectionalLight(0xfff7ed, 2.05);
        keyLight.position.set(1.65, 2.65, 2.25);
        keyLight.castShadow = true;
        keyLight.shadow.mapSize.set(2048, 2048);
        keyLight.shadow.camera.near = 0.5;
        keyLight.shadow.camera.far = 7.0;
        keyLight.shadow.camera.left = -1.1;
        keyLight.shadow.camera.right = 1.1;
        keyLight.shadow.camera.top = 2.0;
        keyLight.shadow.camera.bottom = -0.2;
        keyLight.shadow.bias = -0.0002;
        keyLight.shadow.normalBias = 0.008;
        this.scene.add(keyLight);

        const fillLight = new THREE.DirectionalLight(0xc7d2fe, 0.76);
        fillLight.position.set(-1.9, 1.55, 1.35);
        this.scene.add(fillLight);

        const rimLight = new THREE.DirectionalLight(0x93c5fd, 1.02);
        rimLight.position.set(-0.35, 1.95, -2.15);
        this.scene.add(rimLight);

        const podiumGeo = new THREE.CylinderGeometry(0.62, 0.66, 0.024, 64);
        const podiumMat = new THREE.MeshStandardMaterial({ color: 0x18191d, roughness: 0.72, metalness: 0.18 });
        const podium = new THREE.Mesh(podiumGeo, podiumMat);
        podium.position.y = -0.012;
        podium.receiveShadow = true;
        this.scene.add(podium);

        const ringGeo = new THREE.RingGeometry(0.605, 0.62, 64);
        ringGeo.rotateX(-Math.PI * 0.5);
        const ringMat = new THREE.MeshBasicMaterial({ color: 0xffb454, transparent: true, opacity: 0.32 });
        const ring = new THREE.Mesh(ringGeo, ringMat);
        ring.position.y = 0.001;
        this.scene.add(ring);

        const grid = new THREE.GridHelper(2.4, 24, 0x2c2e35, 0x1b1c20);
        grid.position.y = 0.0005;
        this.scene.add(grid);
    }

    setCameraPreset(preset)
    {
        if (preset === 'front')
        {
            this.camera.position.set(0.0, 1.06, 2.42);
            this.controls.target.set(0.0, 0.96, 0.03);
        }
        else if (preset === 'threeQuarter')
        {
            this.camera.position.set(0.68, 1.18, 2.28);
            this.controls.target.set(0.0, 0.96, 0.03);
        }
        else if (preset === 'side')
        {
            this.camera.position.set(2.35, 1.06, 0.06);
            this.controls.target.set(0.0, 0.96, 0.03);
        }
        else if (preset === 'back')
        {
            this.camera.position.set(0.0, 1.08, -2.35);
            this.controls.target.set(0.0, 0.96, 0.03);
        }
        else if (preset === 'bodice')
        {
            this.camera.position.set(0.32, 1.24, 1.22);
            this.controls.target.set(0.0, 1.14, 0.04);
        }
        this.controls.update();
    }

    bindMannequin(mannequin)
    {
        this.mannequin = mannequin;
        if (this._mannequinMesh)
        {
            this.scene.remove(this._mannequinMesh);
            this._mannequinMesh.geometry.dispose();
        }

        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.BufferAttribute(mannequin.positions, 3));
        geo.setAttribute('normal', new THREE.BufferAttribute(mannequin.normals, 3));
        geo.setIndex(new THREE.BufferAttribute(mannequin.faceIndices, 1));

        // Warm matte featureless tailor form finish
        const mat = new THREE.MeshStandardMaterial({
            color: 0xd3ccc4,
            roughness: 0.72,
            metalness: 0.04,
            polygonOffset: true,
            polygonOffsetFactor: 1.5,
            polygonOffsetUnits: 2,
        });
        this._mannequinMesh = new THREE.Mesh(geo, mat);
        this._mannequinMesh.castShadow = true;
        this._mannequinMesh.receiveShadow = true;
        this._mannequinMesh.visible = this.showMannequin;
        this.scene.add(this._mannequinMesh);

        this.refreshMannequinGeometry();
    }

    refreshMannequinGeometry()
    {
        if (!this.mannequin || !this._mannequinMesh) return;
        const geo = this._mannequinMesh.geometry;
        geo.attributes.position.needsUpdate = true;
        geo.attributes.normal.needsUpdate = true;
        geo.computeBoundingSphere();

        if (this._measureLines)
        {
            this.scene.remove(this._measureLines);
            this._measureLines.geometry.dispose();
            this._measureLines = null;
        }

        const segCoords = [];
        for (const ring of (this.mannequin.measureRings || []))
        {
            for (const seg of ring.segs)
            {
                const push = (x, z) =>
                {
                    const dz = z - 0.035;
                    const len = Math.hypot(x, dz) || 1.0;
                    return [x + (x / len) * 0.0006, z + (dz / len) * 0.0006];
                };
                const [x0, z0] = push(seg[0], seg[2]);
                const [x1, z1] = push(seg[3], seg[5]);
                segCoords.push(x0, seg[1], z0, x1, seg[4], z1);
            }
        }
        if (segCoords.length > 0)
        {
            const lineGeo = new THREE.BufferGeometry();
            lineGeo.setAttribute('position', new THREE.Float32BufferAttribute(segCoords, 3));
            const lineMat = new THREE.LineBasicMaterial({ color: 0xffb454, transparent: true, opacity: 0.72 });
            this._measureLines = new THREE.LineSegments(lineGeo, lineMat);
            this._measureLines.visible = this.showMannequin && this.showMeasureRings;
            this.scene.add(this._measureLines);
        }
    }

    bindClothSolver(solver)
    {
        this.solver = solver;
        const net = solver.network;

        if (this._clothMesh)
        {
            this.scene.remove(this._clothMesh);
            this._clothMesh.geometry.dispose();
        }
        if (this._wireMesh)
        {
            this.scene.remove(this._wireMesh);
            this._wireMesh.geometry.dispose();
        }
        if (this._seamLines)
        {
            this.scene.remove(this._seamLines);
            this._seamLines.geometry.dispose();
        }

        const geo = new THREE.BufferGeometry();
        geo.setAttribute('position', new THREE.BufferAttribute(solver.positions, 3));
        geo.setAttribute('normal', new THREE.BufferAttribute(solver.normals, 3));
        geo.setAttribute('uv', new THREE.BufferAttribute(net.patternUV, 2));
        const colorAttr = new Float32Array(net.pointCount * 3);
        geo.setAttribute('color', new THREE.BufferAttribute(colorAttr, 3));
        geo.setIndex(new THREE.BufferAttribute(net.faceIndices, 1));

        const clothMat = new THREE.MeshPhysicalMaterial({
            color: 0xffffff,
            vertexColors: true,
            roughness: solver.textile.roughness ?? 0.62,
            metalness: 0.02,
            sheen: solver.textile.sheen ?? 0.35,
            sheenRoughness: 0.55,
            sheenColor: new THREE.Color(0xffffff),
            bumpMap: this._weaveBumpTex,
            bumpScale: 0.0012,
            side: THREE.DoubleSide,
            shadowSide: THREE.DoubleSide,
        });
        this._clothMesh = new THREE.Mesh(geo, clothMat);
        this._clothMesh.castShadow = true;
        this._clothMesh.receiveShadow = true;
        this.scene.add(this._clothMesh);

        const wireMat = new THREE.MeshBasicMaterial({
            color: 0xffffff,
            wireframe: true,
            transparent: true,
            opacity: 0.16,
            depthTest: true,
        });
        this._wireMesh = new THREE.Mesh(geo, wireMat);
        this._wireMesh.visible = this.showWireframe;
        this.scene.add(this._wireMesh);

        if (net.seamCount > 0)
        {
            const sGeo = new THREE.BufferGeometry();
            sGeo.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(net.seamCount * 6), 3));
            const sMat = new THREE.LineBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0.85 });
            this._seamLines = new THREE.LineSegments(sGeo, sMat);
            this._seamLines.visible = this.showSeams3D;
            this.scene.add(this._seamLines);
        }

        this.refreshClothGeometry();
        this.refreshPins();
    }

    setTextileVisuals(textile, fabricHex)
    {
        this.fabricHex = fabricHex || this.fabricHex;
        if (this._clothMesh && this._clothMesh.material)
        {
            this._clothMesh.material.roughness = textile.roughness ?? 0.62;
            this._clothMesh.material.sheen = textile.sheen ?? 0.35;
            if (this._weaveBumpTex)
            {
                const sc = (textile.weaveScale || 60.0) * 0.45;
                this._weaveBumpTex.repeat.set(sc, sc);
            }
            this._clothMesh.material.needsUpdate = true;
        }
    }

    refreshClothGeometry()
    {
        if (!this.solver || !this._clothMesh) return;
        const solver = this.solver;
        const net = solver.network;
        const geo = this._clothMesh.geometry;

        geo.attributes.position.needsUpdate = true;
        geo.attributes.normal.needsUpdate = true;

        const col = geo.attributes.color.array;
        const baseColor = new THREE.Color(this.fabricHex);
        const rgb = this._rgbScratch;
        for (let i = 0; i < net.pointCount; ++i)
        {
            const i3 = i * 3;
            if (this.heatmapMode === 'textile')
            {
                const layerTint = net.layerOrders[i] === 0 ? 1.0 : (net.layerOrders[i] === 1 ? 0.90 : 1.08);
                col[i3]     = clampVal(baseColor.r * layerTint, 0, 1);
                col[i3 + 1] = clampVal(baseColor.g * layerTint, 0, 1);
                col[i3 + 2] = clampVal(baseColor.b * layerTint, 0, 1);
            }
            else
            {
                evaluateHeatmapColorRGB(this.heatmapMode, solver, i, rgb);
                col[i3] = rgb[0]; col[i3 + 1] = rgb[1]; col[i3 + 2] = rgb[2];
            }
        }
        geo.attributes.color.needsUpdate = true;
        geo.computeBoundingSphere();

        if (this._seamLines && net.seamCount > 0)
        {
            const arr = this._seamLines.geometry.attributes.position.array;
            const sIds = net.seamIndices;
            const pos = solver.positions;
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a3 = sIds[s * 2] * 3, b3 = sIds[s * 2 + 1] * 3;
                const o = s * 6;
                arr[o]     = pos[a3]; arr[o + 1] = pos[a3 + 1]; arr[o + 2] = pos[a3 + 2];
                arr[o + 3] = pos[b3]; arr[o + 4] = pos[b3 + 1]; arr[o + 5] = pos[b3 + 2];
            }
            this._seamLines.geometry.attributes.position.needsUpdate = true;
            this._seamLines.visible = this.showSeams3D;
        }

        if (this._mannequinMesh) this._mannequinMesh.visible = this.showMannequin;
        if (this._measureLines) this._measureLines.visible = this.showMannequin && this.showMeasureRings;
        if (this._wireMesh) this._wireMesh.visible = this.showWireframe;
    }

    refreshPins()
    {
        while (this._pinGroup.children.length > 0)
        {
            const m = this._pinGroup.children.pop();
            m.geometry.dispose();
            m.material.dispose();
        }
        if (!this.solver) return;
        const pinGeo = new THREE.SphereGeometry(0.0075, 10, 10);
        for (let i = 0; i < this.solver.network.pointCount; ++i)
        {
            if (!this.solver.pinned[i]) continue;
            const pinMat = new THREE.MeshBasicMaterial({ color: 0xffb454 });
            const sphere = new THREE.Mesh(pinGeo.clone(), pinMat);
            sphere.position.set(this.solver.positions[i * 3], this.solver.positions[i * 3 + 1], this.solver.positions[i * 3 + 2]);
            this._pinGroup.add(sphere);
        }
        pinGeo.dispose();
    }

    _setupInteraction()
    {
        this._raycaster = new THREE.Raycaster();
        this._ndc = new THREE.Vector2();
        this._grabPlane = new THREE.Plane();
        this._grabHitPt = new THREE.Vector3();
        this._isGrabbing = false;

        const dom = this.renderer.domElement;
        const updateNdc = (e) =>
        {
            const rect = dom.getBoundingClientRect();
            this._ndc.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            this._ndc.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
        };

        dom.addEventListener('pointerdown', (e) =>
        {
            if (e.button !== 0 || !this.solver || !this._clothMesh) return;
            updateNdc(e);
            this._raycaster.setFromCamera(this._ndc, this.camera);
            const hits = this._raycaster.intersectObject(this._clothMesh, false);
            if (hits.length === 0) return;

            const face = hits[0].face;
            const hitPt = hits[0].point;
            const candidates = [face.a, face.b, face.c];
            let bestV = candidates[0], bestD = 1e9;
            for (const vid of candidates)
            {
                const d = Math.hypot(
                    this.solver.positions[vid * 3] - hitPt.x,
                    this.solver.positions[vid * 3 + 1] - hitPt.y,
                    this.solver.positions[vid * 3 + 2] - hitPt.z
                );
                if (d < bestD) { bestD = d; bestV = vid; }
            }

            if (this.toolMode === 'pin')
            {
                this.solver.togglePin(bestV);
                this.refreshPins();
                if (this.onPinChange) this.onPinChange();
                return;
            }
            if (this.toolMode === 'grab')
            {
                this._isGrabbing = true;
                this.controls.enabled = false;
                this.solver.grabVertex = bestV;
                this.solver.grabTarget = [hitPt.x, hitPt.y, hitPt.z];
                const camDir = new THREE.Vector3();
                this.camera.getWorldDirection(camDir);
                this._grabPlane.setFromNormalAndCoplanarPoint(camDir.negate(), hitPt);
                dom.setPointerCapture(e.pointerId);
            }
        });

        dom.addEventListener('pointermove', (e) =>
        {
            if (!this._isGrabbing || !this.solver) return;
            updateNdc(e);
            this._raycaster.setFromCamera(this._ndc, this.camera);
            if (this._raycaster.ray.intersectPlane(this._grabPlane, this._grabHitPt))
            {
                this.solver.grabTarget = [
                    this._grabHitPt.x,
                    Math.max(0.03, this._grabHitPt.y),
                    this._grabHitPt.z,
                ];
            }
        });

        const endGrab = (e) =>
        {
            if (!this._isGrabbing) return;
            this._isGrabbing = false;
            this.controls.enabled = true;
            if (this.solver) this.solver.grabVertex = -1;
            try { dom.releasePointerCapture(e.pointerId); } catch {}
        };
        dom.addEventListener('pointerup', endGrab);
        dom.addEventListener('pointercancel', endGrab);
    }

    render()
    {
        const w = this.container.clientWidth || 600;
        const h = this.container.clientHeight || 500;
        if (this.renderer.domElement.width !== w || this.renderer.domElement.height !== h)
        {
            this.renderer.setSize(w, h, false);
            this.camera.aspect = w / Math.max(1, h);
            this.camera.updateProjectionMatrix();
        }
        this.controls.update();
        this.renderer.render(this.scene, this.camera);
    }
}
