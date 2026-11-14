/*============================================================================================================================================
                                                         GARMENTVIEWPORT3D.JS
============================================================================================================================================*/
// 🧩 WebGL2 Three.js PBR Atelier 3D Garment Draping Viewport.
//    Features 3-point couture lighting, parametric mannequin + live anthropometric tailor rings, DoubleSide PBR
//    cloth material (sheen/roughness per textile), 5 live diagnostic heatmaps, 3D seam threads, and live cloth grab/pin.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { evaluateHeatmapColorRGB } from './PatternCanvas2D.js?v=3';

export class GarmentViewport3D
{
    constructor(container, options = {})
    {
        this.container = container;
        this.onPinChange = options.onPinChange || null;

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
        this.scene.background = new THREE.Color(0x14161b);

        this.camera = new THREE.PerspectiveCamera(34, 1.0, 0.04, 25.0);
        this.camera.position.set(0.56, 1.18, 2.38);

        this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance', preserveDrawingBuffer: true });
        this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
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
        const hemi = new THREE.HemisphereLight(0xdbeafe, 0x1e2028, 0.82);
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
        keyLight.shadow.bias = -0.0005;
        this.scene.add(keyLight);

        const fillLight = new THREE.DirectionalLight(0xc7d2fe, 0.78);
        fillLight.position.set(-1.9, 1.55, 1.35);
        this.scene.add(fillLight);

        const rimLight = new THREE.DirectionalLight(0x93c5fd, 1.05);
        rimLight.position.set(-0.35, 1.95, -2.15);
        this.scene.add(rimLight);

        // Sleek studio turntable plinth & metric polar grid
        const podiumGeo = new THREE.CylinderGeometry(0.62, 0.66, 0.024, 64);
        const podiumMat = new THREE.MeshStandardMaterial({ color: 0x1d2027, roughness: 0.72, metalness: 0.18 });
        const podium = new THREE.Mesh(podiumGeo, podiumMat);
        podium.position.y = -0.012;
        podium.receiveShadow = true;
        this.scene.add(podium);

        const ringGeo = new THREE.RingGeometry(0.605, 0.62, 64);
        ringGeo.rotateX(-Math.PI * 0.5);
        const ringMat = new THREE.MeshBasicMaterial({ color: 0x3b82f6, transparent: true, opacity: 0.38 });
        const ring = new THREE.Mesh(ringGeo, ringMat);
        ring.position.y = 0.001;
        this.scene.add(ring);

        const grid = new THREE.GridHelper(2.4, 24, 0x2f3746, 0x1e232d);
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

        // Warm matte atelier linen/plaster mannequin finish; slight depth offset prevents grazing Z-fighting behind fitted bodices
        const mat = new THREE.MeshStandardMaterial({
            color: 0xd6cfc7,
            roughness: 0.68,
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
                // Push 0.5 mm outward from skin so tailor tape sits cleanly on bare mannequin without poking through 2.4 mm cloth!
                const push = (x, z) =>
                {
                    const dz = z - 0.035;
                    const len = Math.hypot(x, dz) || 1.0;
                    return [x + (x / len) * 0.0005, z + (dz / len) * 0.0005];
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
            const lineMat = new THREE.LineBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0.72 });
            this._measureLines = new THREE.LineSegments(lineGeo, lineMat);
            this._measureLines.visible = this.showMeasureRings && this.showMannequin;
            this.scene.add(this._measureLines);
        }
    }

    bindClothSolver(solver)
    {
        this.solver = solver;
        const net = solver.network;

        if (this._clothMesh) { this.scene.remove(this._clothMesh); this._clothMesh.geometry.dispose(); this._clothMesh.material.dispose(); }
        if (this._wireMesh)  { this.scene.remove(this._wireMesh);  this._wireMesh.geometry.dispose();  this._wireMesh.material.dispose(); }
        if (this._seamLines) { this.scene.remove(this._seamLines); this._seamLines.geometry.dispose(); this._seamLines.material.dispose(); }

        const geo = new THREE.BufferGeometry();
        this._posAttr = new THREE.BufferAttribute(solver.positions, 3);
        this._posAttr.setUsage(THREE.DynamicDrawUsage);
        this._nrmAttr = new THREE.BufferAttribute(solver.normals, 3);
        this._nrmAttr.setUsage(THREE.DynamicDrawUsage);

        this._colorBuf = new Float32Array(net.pointCount * 3);
        this._colAttr = new THREE.BufferAttribute(this._colorBuf, 3);
        this._colAttr.setUsage(THREE.DynamicDrawUsage);

        geo.setAttribute('position', this._posAttr);
        geo.setAttribute('normal', this._nrmAttr);
        geo.setAttribute('color', this._colAttr);
        geo.setIndex(new THREE.BufferAttribute(net.faceIndices, 1));

        this._clothMat = new THREE.MeshPhysicalMaterial({
            color: 0xffffff,
            vertexColors: true,
            side: THREE.DoubleSide,
            roughness: solver.textile.roughness ?? 0.65,
            metalness: 0.02,
            sheen: solver.textile.sheen ?? 0.35,
            sheenRoughness: 0.45,
            sheenColor: new THREE.Color(0xffffff),
        });

        this._clothMesh = new THREE.Mesh(geo, this._clothMat);
        this._clothMesh.castShadow = true;
        this._clothMesh.receiveShadow = true;
        this.scene.add(this._clothMesh);

        // Wireframe overlay
        const wireMat = new THREE.MeshBasicMaterial({
            color: 0xe2e8f0,
            wireframe: true,
            transparent: true,
            opacity: 0.18,
            depthTest: true,
        });
        this._wireMesh = new THREE.Mesh(geo, wireMat);
        this._wireMesh.visible = this.showWireframe;
        this.scene.add(this._wireMesh);

        // 3D Stitched Seam Overlay
        this._seamPosBuf = new Float32Array(Math.max(6, net.seamCount * 6));
        const seamGeo = new THREE.BufferGeometry();
        this._seamPosAttr = new THREE.BufferAttribute(this._seamPosBuf, 3);
        this._seamPosAttr.setUsage(THREE.DynamicDrawUsage);
        seamGeo.setAttribute('position', this._seamPosAttr);
        const seamMat = new THREE.LineBasicMaterial({ color: 0xfef08a, transparent: true, opacity: 0.65 });
        this._seamLines = new THREE.LineSegments(seamGeo, seamMat);
        this._seamLines.visible = this.showSeams3D && net.seamCount > 0;
        this.scene.add(this._seamLines);

        this.syncTextileAppearance();
        this.syncPinMarkers();
        this.updateClothBuffers();
    }

    syncTextileAppearance()
    {
        if (!this._clothMat || !this.solver) return;
        const tex = this.solver.textile;
        this._clothMat.roughness = this.heatmapMode === 'textile' ? (tex.roughness ?? 0.65) : 0.58;
        this._clothMat.sheen = this.heatmapMode === 'textile' ? (tex.sheen ?? 0.35) : 0.12;
        this._clothMat.needsUpdate = true;
    }

    syncPinMarkers()
    {
        while (this._pinGroup.children.length > 0)
        {
            const ch = this._pinGroup.children.pop();
            ch.geometry.dispose();
            ch.material.dispose();
        }
        if (!this.solver) return;
        const sphereGeo = new THREE.SphereGeometry(0.0085, 12, 12);
        const sphereMat = new THREE.MeshStandardMaterial({ color: 0xf59e0b, emissive: 0x78350f, roughness: 0.3 });
        this._pinIndices = [];
        for (let i = 0; i < this.solver.network.pointCount; ++i)
        {
            if (this.solver.userPinned[i])
            {
                const m = new THREE.Mesh(sphereGeo.clone(), sphereMat.clone());
                this._pinGroup.add(m);
                this._pinIndices.push(i);
            }
        }
    }

    updateClothBuffers()
    {
        if (!this.solver || !this._clothMesh) return;
        const net = this.solver.network;
        const pos = this.solver.positions;
        const col = this._colorBuf;
        const rgb = this._rgbScratch;

        const baseColor = new THREE.Color(this.fabricHex);
        if (this.heatmapMode === 'textile')
        {
            for (let i = 0; i < net.pointCount; ++i)
            {
                const layerShade = net.layerOrders[i] > 0 ? 1.04 : 0.96;
                col[i * 3]     = Math.min(1.0, baseColor.r * layerShade);
                col[i * 3 + 1] = Math.min(1.0, baseColor.g * layerShade);
                col[i * 3 + 2] = Math.min(1.0, baseColor.b * layerShade);
            }
        }
        else
        {
            for (let i = 0; i < net.pointCount; ++i)
            {
                evaluateHeatmapColorRGB(this.heatmapMode, this.solver, i, rgb);
                col[i * 3]     = rgb[0];
                col[i * 3 + 1] = rgb[1];
                col[i * 3 + 2] = rgb[2];
            }
        }

        this._posAttr.needsUpdate = true;
        this._nrmAttr.needsUpdate = true;
        this._colAttr.needsUpdate = true;
        this._clothMesh.geometry.computeBoundingSphere();

        if (this._seamLines && net.seamCount > 0)
        {
            const sIdx = net.seamIndices;
            const sb = this._seamPosBuf;
            for (let s = 0; s < net.seamCount; ++s)
            {
                const a3 = sIdx[s * 2] * 3, b3 = sIdx[s * 2 + 1] * 3;
                const o = s * 6;
                // Midpoint stitch highlight slightly offset along surface normal
                const nax = this.solver.normals[a3] * 0.0012;
                const nay = this.solver.normals[a3 + 1] * 0.0012;
                const naz = this.solver.normals[a3 + 2] * 0.0012;
                sb[o]     = pos[a3] + nax;     sb[o + 1] = pos[a3 + 1] + nay; sb[o + 2] = pos[a3 + 2] + naz;
                sb[o + 3] = pos[b3] + nax;     sb[o + 4] = pos[b3 + 1] + nay; sb[o + 5] = pos[b3 + 2] + naz;
            }
            this._seamPosAttr.needsUpdate = true;
        }

        if (this._pinIndices)
        {
            for (let k = 0; k < this._pinIndices.length; ++k)
            {
                const v3 = this._pinIndices[k] * 3;
                this._pinGroup.children[k].position.set(pos[v3], pos[v3 + 1], pos[v3 + 2]);
            }
        }
    }

    _setupInteraction()
    {
        this._raycaster = new THREE.Raycaster();
        this._ndc = new THREE.Vector2();
        this._dragPlane = new THREE.Plane();
        this._dragHit = new THREE.Vector3();
        this._draggingCloth = false;

        const dom = this.renderer.domElement;
        dom.addEventListener('pointerdown', (e) =>
        {
            if (e.button !== 0 || !this.solver || !this._clothMesh) return;
            const wantGrab = this.toolMode === 'grab' || e.altKey;
            const wantPin = this.toolMode === 'pin';
            if (!wantGrab && !wantPin) return;

            const rect = dom.getBoundingClientRect();
            this._ndc.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            this._ndc.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
            this._raycaster.setFromCamera(this._ndc, this.camera);

            const hits = this._raycaster.intersectObject(this._clothMesh, false);
            if (hits.length === 0) return;

            const hit = hits[0];
            const face = hit.face;
            const candidates = [face.a, face.b, face.c];
            let bestV = face.a, bestD = 1e9;
            for (const vid of candidates)
            {
                const d = Math.hypot(
                    this.solver.positions[vid * 3] - hit.point.x,
                    this.solver.positions[vid * 3 + 1] - hit.point.y,
                    this.solver.positions[vid * 3 + 2] - hit.point.z
                );
                if (d < bestD) { bestD = d; bestV = vid; }
            }

            if (wantPin)
            {
                const nextPinned = !this.solver.userPinned[bestV];
                this.solver.togglePinVertex(bestV, nextPinned);
                this.syncPinMarkers();
                if (this.onPinChange) this.onPinChange();
                e.stopPropagation();
                return;
            }

            // Live 3D cloth grab & pull
            this.controls.enabled = false;
            this._draggingCloth = true;
            this.solver.grabVertex = bestV;
            this.solver.grabTarget = [hit.point.x, hit.point.y, hit.point.z];
            const camDir = new THREE.Vector3();
            this.camera.getWorldDirection(camDir);
            this._dragPlane.setFromNormalAndCoplanarPoint(camDir.negate(), hit.point);
            dom.setPointerCapture(e.pointerId);
        }, true);

        dom.addEventListener('pointermove', (e) =>
        {
            if (!this._draggingCloth || !this.solver) return;
            const rect = dom.getBoundingClientRect();
            this._ndc.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            this._ndc.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
            this._raycaster.setFromCamera(this._ndc, this.camera);
            if (this._raycaster.ray.intersectPlane(this._dragPlane, this._dragHit))
                this.solver.grabTarget = [this._dragHit.x, this._dragHit.y, this._dragHit.z];
        });

        const releaseGrab = () =>
        {
            if (!this._draggingCloth) return;
            this._draggingCloth = false;
            if (this.solver) this.solver.grabVertex = -1;
            this.controls.enabled = true;
        };
        dom.addEventListener('pointerup', releaseGrab);
        dom.addEventListener('pointercancel', releaseGrab);
    }

    setVisibility({ showMannequin, showMeasureRings, showWireframe, showSeams3D })
    {
        if (showMannequin !== undefined) this.showMannequin = showMannequin;
        if (showMeasureRings !== undefined) this.showMeasureRings = showMeasureRings;
        if (showWireframe !== undefined) this.showWireframe = showWireframe;
        if (showSeams3D !== undefined) this.showSeams3D = showSeams3D;

        if (this._mannequinMesh) this._mannequinMesh.visible = this.showMannequin;
        if (this._measureLines) this._measureLines.visible = this.showMeasureRings && this.showMannequin;
        if (this._wireMesh) this._wireMesh.visible = this.showWireframe;
        if (this._seamLines) this._seamLines.visible = this.showSeams3D;
    }

    render()
    {
        const rect = this.container.getBoundingClientRect();
        const w = Math.max(120, Math.floor(rect.width));
        const h = Math.max(120, Math.floor(rect.height));
        const dpr = Math.min(2.5, window.devicePixelRatio || 1);
        if (this.renderer.getPixelRatio() !== dpr)
            this.renderer.setPixelRatio(dpr);

        const targetW = Math.floor(w * dpr);
        const targetH = Math.floor(h * dpr);
        const canvas = this.renderer.domElement;
        if (canvas.width !== targetW || canvas.height !== targetH || this._lastCssW !== w || this._lastCssH !== h)
        {
            this._lastCssW = w;
            this._lastCssH = h;
            this.renderer.setSize(w, h, false);
            this.camera.aspect = w / h;
            this.camera.updateProjectionMatrix();
        }
        this.controls.update();
        this.renderer.render(this.scene, this.camera);
    }
}
