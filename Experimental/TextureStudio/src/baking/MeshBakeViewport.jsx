import React, { useEffect, useRef, useState } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { makeEnvironment } from "../Viewport.jsx";

export default function MeshBakeViewport({
  geometry,
  highGeometry,
  label,
  normalURL,
  normalY = "+Y",
  overlay = false,
  wireframe = false,
  resetToken = 0,
}) {
  const host = useRef(null),
    engine = useRef(null),
    epoch = useRef(0);
  const [error, setError] = useState("");
  useEffect(() => {
    let renderer;
    try {
      renderer = new THREE.WebGLRenderer({
        antialias: true,
        alpha: true,
        preserveDrawingBuffer: true,
      });
    } catch {
      setError(
        "WebGL is unavailable. Mesh-map baking and 2D results remain available.",
      );
      return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.7));
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.2;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    host.current.append(renderer.domElement);
    const scene = new THREE.Scene(),
      camera = new THREE.PerspectiveCamera(38, 1, 0.001, 10000),
      environment = makeEnvironment(renderer);
    scene.environment = environment.texture;
    scene.add(new THREE.HemisphereLight("#f0f0eb", "#3b3c41", 1.25));
    const key = new THREE.DirectionalLight("#f2f2ee", 2.2);
    key.position.set(-3, 6, 5);
    key.castShadow = true;
    key.shadow.mapSize.set(1024, 1024);
    key.shadow.bias = -0.0003;
    scene.add(key);
    const rim = new THREE.DirectionalLight("#e1e9f3", 1.3);
    rim.position.set(4, 3, -4);
    scene.add(rim);
    const mesh = new THREE.Mesh(
      new THREE.BufferGeometry(),
      new THREE.MeshPhysicalMaterial({
        color: "#bfc1b8",
        roughness: 0.38,
        metalness: 0,
        clearcoat: 0.25,
        side: THREE.DoubleSide,
      }),
    );
    mesh.castShadow = true;
    scene.add(mesh);
    const ghost = new THREE.Mesh(
      new THREE.BufferGeometry(),
      new THREE.MeshBasicMaterial({
        color: "#d7b183",
        wireframe: true,
        transparent: true,
        opacity: 0.25,
        depthWrite: false,
      }),
    );
    ghost.visible = false;
    scene.add(ghost);
    const grid = new THREE.GridHelper(10, 20, "#4b4b44", "#343632");
    grid.material.transparent = true;
    grid.material.opacity = 0.2;
    scene.add(grid);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(100, 100),
      new THREE.ShadowMaterial({ opacity: 0.17 }),
    );
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    scene.add(floor);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.09;
    controls.screenSpacePanning = true;
    const state = {
      renderer,
      scene,
      camera,
      controls,
      mesh,
      ghost,
      grid,
      floor,
      key,
      environment,
      frame: 0,
      dirty: true,
      home: new THREE.Vector3(4, 3, 6),
      target: new THREE.Vector3(),
    };
    engine.current = state;
    const fit = () => {
      camera.position.copy(state.home);
      controls.target.copy(state.target);
      controls.update();
      state.dirty = true;
    };
    state.fit = fit;
    controls.addEventListener("change", () => {
      state.dirty = true;
    });
    const observer = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      if (width <= 0 || height <= 0) return;
      renderer.setSize(width, height);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      state.dirty = true;
    });
    observer.observe(host.current);
    const animate = () => {
      state.frame = requestAnimationFrame(animate);
      controls.update();
      if (state.dirty) {
        renderer.render(scene, camera);
        renderer.domElement.dataset.meshReady = "true";
        state.dirty = false;
      }
    };
    animate();
    return () => {
      epoch.current++;
      engine.current = null;
      cancelAnimationFrame(state.frame);
      observer.disconnect();
      controls.dispose();
      scene.traverse((o) => {
        o.geometry?.dispose();
        if (o.material) {
          o.material.normalMap?.dispose();
          o.material.dispose();
        }
      });
      environment.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
      renderer.domElement.remove();
    };
  }, []);
  useEffect(() => {
    const e = engine.current;
    if (!e || !geometry) return;
    e.mesh.geometry.dispose();
    e.mesh.geometry = geometry.clone();
    e.mesh.geometry.computeBoundingBox();
    const bounds = e.mesh.geometry.boundingBox,
      center = bounds.getCenter(new THREE.Vector3()),
      size = bounds.getSize(new THREE.Vector3()),
      span = Math.max(size.length(), 0.01);
    e.target.copy(center);
    e.home
      .copy(center)
      .add(new THREE.Vector3(0.8, 0.55, 1.35).multiplyScalar(span));
    e.controls.minDistance = span * 0.04;
    e.controls.maxDistance = span * 20;
    e.camera.near = span / 10000;
    e.camera.far = span * 100;
    e.camera.updateProjectionMatrix();
    e.floor.position.y = bounds.min.y - span * 0.004;
    e.grid.position.y = e.floor.position.y + 0.001;
    e.grid.scale.setScalar(span / 5);
    e.key.position
      .copy(center)
      .add(new THREE.Vector3(-1, 2, 1.5).multiplyScalar(span));
    e.key.target.position.copy(center);
    e.scene.add(e.key.target);
    e.key.shadow.camera.left = e.key.shadow.camera.bottom = -span;
    e.key.shadow.camera.right = e.key.shadow.camera.top = span;
    e.key.shadow.camera.far = span * 6;
    e.key.shadow.camera.updateProjectionMatrix();
    e.renderer.domElement.dataset.meshName = label || "Low mesh";
    e.renderer.domElement.dataset.triangleCount = String(
      (geometry.index?.count || geometry.attributes.position.count) / 3,
    );
    e.fit();
  }, [geometry, label]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.ghost.geometry.dispose();
    e.ghost.geometry = highGeometry
      ? highGeometry.clone()
      : new THREE.BufferGeometry();
    e.ghost.visible = overlay && !!highGeometry;
    e.dirty = true;
  }, [highGeometry, overlay]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    e.mesh.material.wireframe = wireframe;
    e.dirty = true;
  }, [wireframe]);
  useEffect(() => {
    const e = engine.current;
    if (!e) return;
    const ticket = ++epoch.current;
    const old = e.mesh.material.normalMap;
    e.mesh.material.normalMap = null;
    e.mesh.material.needsUpdate = true;
    old?.dispose();
    e.dirty = true;
    if (!normalURL) return;
    const texture = new THREE.TextureLoader().load(
      normalURL,
      () => {
        if (engine.current !== e || epoch.current !== ticket) {
          texture.dispose();
          return;
        }
        texture.colorSpace = THREE.NoColorSpace;
        e.mesh.material.normalMap = texture;
        e.mesh.material.normalScale.set(1, normalY === "-Y" ? -1 : 1);
        e.mesh.material.needsUpdate = true;
        e.dirty = true;
      },
      undefined,
      () => {
        if (engine.current === e && epoch.current === ticket)
          setError(
            "The normal-map preview could not load. The mesh and download remain available.",
          );
      },
    );
    return () => {
      if (e.mesh.material.normalMap !== texture) texture.dispose();
    };
  }, [normalURL, normalY]);
  useEffect(() => engine.current?.fit(), [resetToken]);
  return (
    <div
      className="bk-mesh-viewport"
      ref={host}
      aria-label="Baking mesh viewport"
    >
      {error && (
        <div className="bk-viewport-error" role="status">
          {error}
        </div>
      )}
    </div>
  );
}
