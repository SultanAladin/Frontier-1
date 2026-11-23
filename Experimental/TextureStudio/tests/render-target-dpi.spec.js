import { test, expect } from "@playwright/test";
test("real framebuffer pixels and nested UV viewport restoration are invariant at DPR 1, 1.25 and 2", async ({
  page,
}) => {
  await page.goto("/src/renderTargetViewport.js");
  const result = await page.evaluate(async () => {
    const { withTextureStackUVState } =
      await import("/src/textureStackUVController.js");
    const raw = await (await fetch("/src/textureStackUVController.js")).text();
    const threeURL = raw.match(/from\s*"([^"]+three[^"\n]+)"/)[1],
      THREE = await import(threeURL);
    const renderer = new THREE.WebGLRenderer({ antialias: false });
    const scene = new THREE.Scene(),
      camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.1, 10);
    camera.position.z = 1;
    const uniform = { value: 0 },
      material = new THREE.MeshBasicMaterial({ color: "#ff1030" });
    material.userData.textureStackUV = uniform;
    const geometry = new THREE.PlaneGeometry(0.08, 0.08),
      mesh = new THREE.Mesh(geometry, material);
    mesh.position.set(-0.75, -0.25, 0);
    scene.add(mesh);
    const rows = [];
    for (const dpr of [1, 1.25, 2]) {
      renderer.setPixelRatio(dpr);
      renderer.setSize(80, 40);
      renderer.setViewport(0, 0, 80, 40);
      const target = new THREE.WebGLRenderTarget(128, 64, {
        depthBuffer: false,
      });
      target.texture.colorSpace = THREE.SRGBColorSpace;
      let viewport;
      withTextureStackUVState({ renderer, scene }, target, uniform, () => {
        viewport = Array.from(
          renderer.getContext().getParameter(renderer.getContext().VIEWPORT),
        );
        renderer.render(scene, camera);
      });
      const bytes = new Uint8Array(128 * 64 * 4);
      renderer.readRenderTargetPixels(target, 0, 0, 128, 64, bytes);
      const k = (24 * 128 + 16) * 4;
      rows.push({
        dpr,
        viewport,
        pixel: Array.from(bytes.slice(k, k + 4)),
        restored: renderer.getViewport(new THREE.Vector4()).toArray(),
        targetRestored: renderer.getRenderTarget() === null,
        uniform: uniform.value,
        depth: [material.depthTest, material.depthWrite],
      });
      target.dispose();
    }
    geometry.dispose();
    material.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
    return rows;
  });
  for (const row of result) {
    expect(row.viewport).toEqual([0, 0, 128, 64]);
    expect(row.pixel[0]).toBeGreaterThan(240);
    expect(row.pixel[1]).toBeLessThan(30);
    expect(row.restored).toEqual([0, 0, 80, 40]);
    expect(row.targetRestored).toBe(true);
    expect(row.uniform).toBe(0);
    expect(row.depth).toEqual([true, true]);
  }
});
