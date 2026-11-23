// Inspection orbit: vehicle-centred translation, world-space angles. No chase spring,
// steering alignment, automatic yaw return, speed FOV or driving shake.
export class VehicleOrbitCamera {
    constructor() { this.Reset(); }
    Reset() { this.Yaw = Math.PI + 0.4; this.Pitch = 0.30; this.Distance = 8; }
    Rotate(Across, Down) {
        this.Yaw = ((this.Yaw - Across * 0.006) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI);
        this.Pitch = Math.max(-0.15, Math.min(1.50, this.Pitch + Down * 0.005));
    }
    Zoom(Delta) { this.Distance = Math.max(4.5, Math.min(40, this.Distance * Math.exp(Math.max(-1000, Math.min(1000, Delta)) * 0.001))); }
    Resolve(Position, HeightAt = () => 0) {
        const Target = [Position[0], Position[1], Position[2] + 0.35];
        const R = this.Distance * Math.cos(this.Pitch);
        const Eye = [Target[0] + R * Math.cos(this.Yaw), Target[1] + R * Math.sin(this.Yaw), Target[2] + this.Distance * Math.sin(this.Pitch)];
        Eye[2] = Math.max(Eye[2], HeightAt(Eye[0], Eye[1]) + 0.2);
        return { Eye, Target, Fov: 60 };
    }
}
