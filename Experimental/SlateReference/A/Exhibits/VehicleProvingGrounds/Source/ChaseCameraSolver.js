//============================================================================================================================================
//                                                           CHASECAMERASOLVER.JS
//============================================================================================================================================
// 🧩 Critically damped follow projection behind the vehicle, with an orbit offset the pointer can drag around it.

// 📝 Frontier's ChaseCameraSolver: eye FollowDistance behind and FollowHeight above, exponential follow at
//    PositionSpring, aim LookAtHeight above the chassis, field of view widened with speed. The orbit offset is the
//    exhibit's addition so the distance field and the vehicle can be inspected from any side while driving.
import { NormalizeVector, SubtractVector, AddVector, ScaleVector, MultiplyAddVector, ClampScalar } from "./TransformProjection.js";

export const ChaseCameraConfiguration = Object.freeze({
    FollowDistance:   7.0,    // [m]
    FollowHeight:     2.6,    // [m]
    LookAtHeight:     0.8,    // [m]
    PositionSpring:   6.0,    // [1/s]
    RestFieldOfView:  60.0,   // [deg]
    SpeedFieldOfView: 12.0,   // [deg]
    SpeedForFullFov:  60.0    // [m/s]
});

export class ChaseCameraSolver
{
    constructor()
    {
        this.Eye          = [-7.0, 0.0, 3.1];
        this.Target       = [0.0, 0.0, 1.3];
        this.FieldOfView  = ChaseCameraConfiguration.RestFieldOfView;
        this.Seeded       = false;
        this.OrbitYaw     = 0.0;   // [rad] - pointer offset around the vehicle
        this.OrbitPitch   = 0.0;   // [rad]
        this.OrbitScale   = 1.0;   // [-]   - wheel zoom
    }

    ResolveIdealEye(Position, Forward)
    {
        const Yaw = Math.atan2(Forward[1], Forward[0]) + Math.PI + this.OrbitYaw;
        const Distance = ChaseCameraConfiguration.FollowDistance * this.OrbitScale;
        const Height = ChaseCameraConfiguration.FollowHeight * this.OrbitScale + Distance * Math.sin(this.OrbitPitch);
        const Horizontal = Distance * Math.cos(this.OrbitPitch);
        return [Position[0] + Math.cos(Yaw) * Horizontal, Position[1] + Math.sin(Yaw) * Horizontal, Position[2] + Height];
    }

    SnapTo(Position, Forward)
    {
        this.Eye = this.ResolveIdealEye(Position, Forward);
        this.Target = [Position[0], Position[1], Position[2] + ChaseCameraConfiguration.LookAtHeight];
        this.FieldOfView = ChaseCameraConfiguration.RestFieldOfView;
        this.Seeded = true;
    }

    AdvanceChase(Position, Forward, Speed, Δτ)
    {
        if (!this.Seeded)
        {
            this.SnapTo(Position, Forward);
            return;
        }
        const Ideal = this.ResolveIdealEye(Position, Forward);
        const Follow = 1.0 - Math.exp(-ChaseCameraConfiguration.PositionSpring * Δτ);
        this.Eye = AddVector(this.Eye, ScaleVector(SubtractVector(Ideal, this.Eye), Follow));
        this.Eye[2] = Math.max(this.Eye[2], 0.35);
        this.Target = [Position[0], Position[1], Position[2] + ChaseCameraConfiguration.LookAtHeight];
        const Widen = ClampScalar(Math.abs(Speed) / Math.max(1.0, ChaseCameraConfiguration.SpeedForFullFov), 0.0, 1.0);
        this.FieldOfView = ChaseCameraConfiguration.RestFieldOfView + ChaseCameraConfiguration.SpeedFieldOfView * Widen;
        // User camera rotation is preserved without automatically springing back behind the car
    }
}

// 📝 Free flight (Project-Drive's F8): WASD along the view, Q/E down/up, pointer drag to look, Shift to hurry.
export class FreeCameraSolver
{
    constructor()
    {
        this.Eye   = [-18.0, -14.0, 9.0];
        this.Yaw   = Math.atan2(14.0, 18.0);
        this.Pitch = -0.32;
        this.FieldOfView = 60.0;
    }

    ResolveForward()
    {
        return [Math.cos(this.Pitch) * Math.cos(this.Yaw), Math.cos(this.Pitch) * Math.sin(this.Yaw), Math.sin(this.Pitch)];
    }

    AdvanceFlight(Keys, Δτ)
    {
        const Forward = this.ResolveForward();
        const Right = NormalizeVector([Forward[1], -Forward[0], 0.0]);
        const Speed = (Keys.has("ShiftLeft") || Keys.has("ShiftRight")) ? 30.0 : 9.0;
        let Move = [0.0, 0.0, 0.0];
        if (Keys.has("KeyW") || Keys.has("ArrowUp"))    Move = AddVector(Move, Forward);
        if (Keys.has("KeyS") || Keys.has("ArrowDown"))  Move = SubtractVector(Move, Forward);
        if (Keys.has("KeyD") || Keys.has("ArrowRight")) Move = AddVector(Move, Right);
        if (Keys.has("KeyA") || Keys.has("ArrowLeft"))  Move = SubtractVector(Move, Right);
        if (Keys.has("KeyE")) Move[2] += 1.0;
        if (Keys.has("KeyQ")) Move[2] -= 1.0;
        this.Eye = MultiplyAddVector(this.Eye, Move, Speed * Δτ);
        this.Eye[2] = Math.max(this.Eye[2], 0.2);
    }

    AssignFromChase(Eye, Target)
    {
        this.Eye = Eye.slice();
        const Direction = NormalizeVector(SubtractVector(Target, Eye));
        this.Yaw = Math.atan2(Direction[1], Direction[0]);
        this.Pitch = Math.asin(ClampScalar(Direction[2], -1.0, 1.0));
    }
}
