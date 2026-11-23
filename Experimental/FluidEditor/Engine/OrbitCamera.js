//============================================================================================================================================
//                                                          ORBITCAMERA.JS
//                   Minimal Column-Major Matrix Toolkit and Orbit / Pan / Dolly Viewport Camera with Projection,
//                                      Unprojection, and Screen-Space Projection Helpers
//============================================================================================================================================

export const Vec3 = {
    add: (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]],
    sub: (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]],
    scale: (a, s) => [a[0] * s, a[1] * s, a[2] * s],
    dot: (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2],
    cross: (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]],
    length: (a) => Math.hypot(a[0], a[1], a[2]),
    normalize: (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; },
    anyPerpendicular: (a) =>
    {
        const n = Vec3.normalize(a);
        const helper = Math.abs(n[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
        return Vec3.normalize(Vec3.cross(n, helper));
    },
};

export const Mat4 = {
    perspective(fovY, aspect, near, far)
    {
        const f = 1 / Math.tan(fovY / 2);
        const out = new Float32Array(16);
        out[0] = f / aspect; out[5] = f; out[10] = (far + near) / (near - far); out[11] = -1; out[14] = (2 * far * near) / (near - far);
        return out;
    },
    lookAt(eye, target, up)
    {
        const z = Vec3.normalize(Vec3.sub(eye, target));
        const x = Vec3.normalize(Vec3.cross(up, z));
        const y = Vec3.cross(z, x);
        const out = new Float32Array(16);
        out[0] = x[0]; out[1] = y[0]; out[2] = z[0];
        out[4] = x[1]; out[5] = y[1]; out[6] = z[1];
        out[8] = x[2]; out[9] = y[2]; out[10] = z[2];
        out[12] = -Vec3.dot(x, eye); out[13] = -Vec3.dot(y, eye); out[14] = -Vec3.dot(z, eye); out[15] = 1;
        return out;
    },
    multiply(a, b)
    {
        const out = new Float32Array(16);
        for (let column = 0; column < 4; column += 1)
            for (let row = 0; row < 4; row += 1)
                out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] + a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
        return out;
    },
    invert(m)
    {
        const inv = new Float32Array(16);
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        let det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (Math.abs(det) < 1e-12) return inv;
        det = 1 / det;
        for (let i = 0; i < 16; i += 1) inv[i] *= det;
        return inv;
    },
    transformPoint(m, p)
    {
        const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        const y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        const z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
        const w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        return [x / w, y / w, z / w, w];
    },
};

export class OrbitCamera
{
    constructor()
    {
        this.target = [0, 0, 0];
        this.yaw = -0.65;
        this.pitch = 0.32;
        this.distance = 9.5;
        this.fovY = (42 * Math.PI) / 180;
        this.near = 0.05;
        this.far = 200;
        this.aspect = 1;
        this.viewport = [1, 1];
    }

    get position()
    {
        const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
        return [
            this.target[0] + this.distance * cp * Math.sin(this.yaw),
            this.target[1] + this.distance * sp,
            this.target[2] + this.distance * cp * Math.cos(this.yaw),
        ];
    }

    get forward() { return Vec3.normalize(Vec3.sub(this.target, this.position)); }
    get right() { return Vec3.normalize(Vec3.cross(this.forward, [0, 1, 0])); }
    get up() { return Vec3.cross(this.right, this.forward); }

    setViewport(width, height)
    {
        this.viewport = [Math.max(1, width), Math.max(1, height)];
        this.aspect = this.viewport[0] / this.viewport[1];
    }

    orbit(deltaX, deltaY)
    {
        this.yaw -= deltaX * 0.0075;
        this.pitch = Math.max(-1.45, Math.min(1.45, this.pitch + deltaY * 0.0075));
    }

    pan(deltaX, deltaY)
    {
        const scale = this.distance * 0.0016;
        this.target = Vec3.add(this.target, Vec3.add(Vec3.scale(this.right, -deltaX * scale), Vec3.scale(this.up, deltaY * scale)));
    }

    dolly(delta)
    {
        this.distance = Math.max(0.8, Math.min(80, this.distance * Math.exp(delta * 0.0012)));
    }

    frame(halfExtent, centre = [0, 0, 0])
    {
        const radius = Math.hypot(halfExtent[0], halfExtent[1], halfExtent[2]);
        this.target = [centre[0], centre[1], centre[2]];
        this.distance = radius / Math.sin(this.fovY / 2) * 1.05;
    }

    matrices()
    {
        const view = Mat4.lookAt(this.position, this.target, [0, 1, 0]);
        const projection = Mat4.perspective(this.fovY, this.aspect, this.near, this.far);
        const viewProjection = Mat4.multiply(projection, view);
        return { view, projection, viewProjection, inverseViewProjection: Mat4.invert(viewProjection) };
    }

    project(viewProjection, world)
    {
        const clip = Mat4.transformPoint(viewProjection, world);
        if (clip[3] <= 0) return null;
        return [(clip[0] * 0.5 + 0.5) * this.viewport[0], (1 - (clip[1] * 0.5 + 0.5)) * this.viewport[1], clip[2]];
    }

    rayFromScreen(inverseViewProjection, x, y)
    {
        const ndcX = (x / this.viewport[0]) * 2 - 1;
        const ndcY = 1 - (y / this.viewport[1]) * 2;
        const far = Mat4.transformPoint(inverseViewProjection, [ndcX, ndcY, 1]);
        const origin = this.position;
        return { origin, direction: Vec3.normalize(Vec3.sub([far[0], far[1], far[2]], origin)) };
    }
}
