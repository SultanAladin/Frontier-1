import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 60000 });

test("UV footprint paints the shared textured dab at its exact logical position, clips charts, and resizes with zoom without painting the project", async ({
  page,
}) => {
  await page.goto("/src/digitalBrush.js");
  const result = await page.evaluate(async () => {
    const { normalizeDigitalBrush, digitalBrushDabs, digitalBrushTip } =
      await import("/src/digitalBrush.js");
    const { digitalBrushCursor, drawUVBrushCursor } =
      await import("/src/digitalBrushCursor.js");
    const { createTextureTeapotGeometry } =
      await import("/src/textureSurfaceModel.js");
    const { uvSurfaceHit } = await import("/src/textureUVGeometry.js");
    const geometry = createTextureTeapotGeometry(20),
      hit = uvSurfaceHit(geometry, 0.125, 0.375);
    const brush = normalizeDigitalBrush({
      shape: "square",
      size: 48,
      sizePressure: 0,
      hardness: 1,
      color: "#ff1030",
      grain: "checker",
      grainAmount: 1,
      grainScale: 8,
      roundness: 0.65,
      rotation: 30,
    });
    const cursor = digitalBrushCursor({
      hit,
      geometry,
      brush,
      target: { kind: "object", partId: "body" },
      channels: { baseColor: brush.color },
      seed: 88,
    });
    const canvas = document.createElement("canvas"),
      size = { width: 900, height: 500 },
      camera = { scale: 700, x: 100, y: 400 };
    const stats = () => {
      const data = canvas
        .getContext("2d")
        .getImageData(0, 0, canvas.width, canvas.height).data;
      let count = 0,
        x0 = canvas.width,
        x1 = -1,
        y0 = canvas.height,
        y1 = -1,
        low = 0,
        high = 0;
      for (let y = 0; y < canvas.height; y++)
        for (let x = 0; x < canvas.width; x++) {
          const i = (y * canvas.width + x) * 4,
            a = data[i + 3];
          if (a) {
            count++;
            x0 = Math.min(x0, x);
            x1 = Math.max(x1, x);
            y0 = Math.min(y0, y);
            y1 = Math.max(y1, y);
          }
          if (data[i] > data[i + 1] + 100) {
            if (a > 70 && a < 130) high++;
            if (a > 10 && a < 35) low++;
          }
        }
      return { count, x0, x1, y0, y1, low, high };
    };
    drawUVBrushCursor(canvas, size, camera, cursor, geometry);
    const first = stats();
    const centre = [
      camera.x + (cursor.dab.x / 256) * camera.scale,
      camera.y - (cursor.dab.y / 256) * camera.scale,
    ];
    const tip = digitalBrushTip(brush, brush.color),
      tipPixels = tip.getContext("2d").getImageData(0, 0, 64, 64).data;
    const grainAlpha = [
      tipPixels[(12 * 64 + 12) * 4 + 3],
      tipPixels[(12 * 64 + 20) * 4 + 3],
    ];
    drawUVBrushCursor(
      canvas,
      size,
      { ...camera, scale: 1000 },
      cursor,
      geometry,
    );
    const zoomed = stats();
    // Move close to the chart boundary. Alpha outside the allowed chart is zero.
    const boundaryHit = uvSurfaceHit(geometry, 0.018, 0.37);
    const boundary = digitalBrushCursor({
      hit: boundaryHit,
      geometry,
      brush,
      target: { kind: "object", partId: "body" },
      channels: { baseColor: brush.color },
      seed: 88,
    });
    drawUVBrushCursor(canvas, size, camera, boundary, geometry);
    const outside = Array.from(
      canvas
        .getContext("2d")
        .getImageData(
          Math.floor(camera.x - 0.005 * camera.scale),
          Math.floor(camera.y - 0.37 * camera.scale),
          1,
          1,
        ).data,
    );
    drawUVBrushCursor(canvas, size, camera, null, geometry);
    const empty = stats();
    geometry.dispose();
    return {
      first,
      centre,
      zoomed,
      grainAlpha,
      outside,
      empty,
      dab: cursor.dab,
      expected: digitalBrushDabs([[...hit.uvPosition, 1]], brush, 88)[0],
    };
  });
  expect(result.dab).toEqual(result.expected);
  expect(result.first.high).toBeGreaterThan(20);
  expect(result.first.low).toBeGreaterThan(20);
  expect(result.grainAlpha.sort((a, b) => a - b)).toEqual([51, 255]);
  expect((result.first.x0 + result.first.x1 + 1) / 2).toBeCloseTo(
    result.centre[0],
    0,
  );
  expect((result.first.y0 + result.first.y1 + 1) / 2).toBeCloseTo(
    result.centre[1],
    0,
  );
  expect(result.zoomed.x1 - result.zoomed.x0).toBeGreaterThan(
    result.first.x1 - result.first.x0,
  );
  expect(result.outside[3]).toBe(0);
  expect(result.empty.count).toBe(0);
});

test("Lit footprint is a disposable depth-tested overlay, with shared grain and exact face clipping; atlas scopes exclude it", async ({
  page,
}) => {
  await page.goto("/src/digitalBrush.js");
  const result = await page.evaluate(async () => {
    const { createDigitalBrushCursorController } =
      await import("/src/digitalBrushCursorController.js");
    const { createTextureTeapotGeometry } =
      await import("/src/textureSurfaceModel.js");
    const { normalizeDigitalBrush, digitalBrushTip } =
      await import("/src/digitalBrush.js");
    const { digitalBrushCursor } = await import("/src/digitalBrushCursor.js");
    const { uvTopology } = await import("/src/textureUVGeometry.js");
    const raw = await (
      await fetch("/src/digitalBrushCursorController.js")
    ).text();
    const threeURL = raw.match(/from\s*"([^"]+three[^"\n]+)"/)[1];
    const THREE = await import(threeURL);
    const geometry = createTextureTeapotGeometry(20),
      specimen = new THREE.Mesh(geometry, new THREE.MeshBasicMaterial());
    specimen.position.set(0, 0.2, 0);
    specimen.rotation.set(0.05, -0.15, 0);
    const scene = new THREE.Scene();
    scene.add(specimen);
    const camera = new THREE.PerspectiveCamera(37, 1.6, 0.01, 100);
    camera.position.set(4, 3, 6);
    camera.lookAt(0, 1, 0);
    camera.updateMatrixWorld();
    const canvas = document.createElement("canvas");
    canvas.width = 800;
    canvas.height = 500;
    canvas.style.width = "800px";
    canvas.style.height = "500px";
    document.body.append(canvas);
    const rect = canvas.getBoundingClientRect();
    const brush = normalizeDigitalBrush({
      size: 32,
      roundness: 0.3,
      rotation: 40,
      grain: "noise",
      grainAmount: 0.8,
    });
    let enabled = true,
      current = null,
      region = null;
    const controller = createDigitalBrushCursorController(
      { specimen, camera, renderer: { domElement: canvas } },
      () => ({
        active: enabled,
        digital: true,
        getCursor(hit) {
          if (!hit) return null;
          region = {
            kind: "face",
            partId: hit.target.partId,
            id: hit.faceIndex,
          };
          return digitalBrushCursor({
            hit,
            geometry,
            brush,
            target: region,
            channels: { baseColor: brush.color },
            seed: 42,
          });
        },
      }),
      (c) => {
        current = c;
      },
    );
    controller.move({
      clientX: rect.left + 400,
      clientY: rect.top + 250,
      pointerType: "mouse",
      buttons: 0,
    });
    controller.update(true);
    const mesh = specimen.getObjectByName("digital-brush-footprint"),
      u = mesh.material.uniforms;
    const data = {
      present: !!current,
      overlay: mesh.userData.textureStackOverlay,
      pass: !!mesh.userData.textureStackPass,
      uv: !!mesh.material.userData.textureStackUV,
      order: mesh.renderOrder,
      depthTest: mesh.material.depthTest,
      depthWrite: mesh.material.depthWrite,
      sameGeometry: mesh.geometry === geometry,
      sameTip: u.uTip.value.image === digitalBrushTip(brush, current.dab.color),
      faceA: u.uFaceA.value.toArray(),
      expectedFaceA: uvTopology(geometry).triangles[region.id].points[0],
      face: u.uFace.value,
      radius: u.uRadius.value.toArray(),
      expectedRadius: [
        current.dab.radius / 256,
        (current.dab.radius * brush.roundness) / 256,
      ],
      shader:
        mesh.material.fragmentShader.includes("fwidth") &&
        mesh.material.fragmentShader.includes("uTip"),
    };
    let disposedGeometry = 0,
      disposedMaterial = 0,
      disposedTexture = 0;
    geometry.addEventListener("dispose", () => disposedGeometry++);
    mesh.material.addEventListener("dispose", () => disposedMaterial++);
    u.uTip.value.addEventListener("dispose", () => disposedTexture++);
    enabled = false;
    controller.invalidate();
    controller.update();
    data.hidden = !mesh.visible && current === null;
    controller.dispose();
    data.released =
      specimen.children.length === 0 &&
      disposedMaterial === 1 &&
      disposedTexture === 1 &&
      disposedGeometry === 0;
    geometry.dispose();
    specimen.material.dispose();
    return data;
  });
  expect(result.present).toBe(true);
  expect(result.overlay).toBe(true);
  expect(result.pass).toBe(false);
  expect(result.uv).toBe(false);
  expect(result.order).toBeGreaterThan(10000);
  expect(result.depthTest).toBe(true);
  expect(result.depthWrite).toBe(false);
  expect(result.sameGeometry).toBe(true);
  expect(result.sameTip).toBe(true);
  expect(result.face).toBe(1);
  expect(result.faceA).toEqual(result.expectedFaceA);
  expect(result.radius).toEqual(result.expectedRadius);
  expect(result.hidden).toBe(true);
  expect(result.released).toBe(true);
});
