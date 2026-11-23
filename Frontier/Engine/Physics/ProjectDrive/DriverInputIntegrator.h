//============================================================================================================================================
//                                                        DRIVERINPUTINTEGRATOR.H
//============================================================================================================================================
// 📦 Engine-agnostic driver input → DriverInput mapper: ramps digital keys and shapes analog axes into a DriverCommand.
//
//    Turns raw device input (keyboard keys, gamepad axes, or a racing wheel + pedals) into the normalised
//    `DriverInput` the vehicle dynamics consume. It is deliberately ENGINE-AGNOSTIC and header-only: the game layer
//    forwards button/axis readings each frame and reads back a `DriverCommand`; it never mentions Unreal, SDL, Jolt or
//    any windowing/input API. The headless proofs forward synthetic readings the same way. This mirrors the role of
//    GRIT's input layer (UE APlayerController + Enhanced Input) but with the engine binding factored out.
//
//    STANDARD KEYBOARD LAYOUT (the default the user asked for):
//        W .............. throttle            S .............. brake / reverse creep
//        A / D .......... steer left / right   Space .......... handbrake
//        Left-Shift ..... sequential up-shift  Left-Ctrl ...... sequential down-shift
//        R .............. reset (surfaced as a pulse; wired by the scene)
//
//    Keyboard axes are DIGITAL, so throttle/brake ramp in and decay out at configurable rates and steering self-centres
//    when A/D are released — the feel expected from keyboard driving. A gamepad or wheel instead supplies ANALOG axes
//    which bypass the ramps (dead-zone + sensitivity are still applied). Select the active device with AssignDevice();
//    auto-switching on activity is left to the engine layer (see DeviceSwitchThreshold).
//
//    GAMEPAD / WHEEL EXTENSION POINTS (documented, ready for later phases):
//        • Gamepad: ForwardThrottleAxis/ForwardBrakeAxis with the analog triggers, ForwardSteerAxis with the stick X.
//          `SteeringLinearity` shapes stick response (1 = linear, >1 = finer around centre).
//        • Wheel + pedals: same analog forwards; `WheelRotationRange` maps the physical wheel angle to full lock, and
//          `ForceFeedbackStrength` is surfaced for the engine's force-feedback driver (this class computes no feedback).
//        • Sequential vs H-pattern: ShiftUp()/ShiftDown() are edge pulses; an H-pattern shifter maps each gear to a
//          direct SelectGear(n) call instead.

#pragma once

#include "VehicleSolver.h"   // DriverInput

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle {

enum class InputDeviceCategory { Keyboard, Gamepad, Wheel };

//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct InputConfiguration
{
    // Sensitivity multipliers (applied after dead-zone, before clamp).
    float SteeringSensitivity = 1.0f;      // [-]   scales resolved steer
    float ThrottleSensitivity = 1.0f;      // [-]   scales resolved throttle
    float BrakeSensitivity    = 1.0f;      // [-]   scales resolved brake

    // Dead zones (analog devices).
    float SteeringDeadZone = 0.05f;        // [-]   analog steer band ignored around centre
    float ThrottleDeadZone = 0.02f;        // [-]   analog throttle band ignored at rest
    float BrakeDeadZone    = 0.02f;        // [-]   analog brake band ignored at rest

    // Keyboard ramp rates (digital keys → smooth analog), in units-per-second.
    float ThrottleRiseRate = 3.0f;         // [1/s] how fast throttle builds while W held
    float ThrottleFallRate = 6.0f;         // [1/s] how fast it releases
    float BrakeRiseRate    = 4.0f;         // [1/s] how fast brake builds while S held
    float BrakeFallRate    = 8.0f;         // [1/s] how fast it releases
    float SteerRate        = 3.5f;         // [1/s] steer build toward ±1 while A/D held
    float SteerReturnRate  = 5.0f;         // [1/s] self-centring rate when neither A nor D held

    // Analog shaping.
    float SteeringLinearity   = 1.0f;      // [-]   exponent for analog steer curve (>1 finer near centre)
    float WheelRotationRange  = 900.0f;    // [deg] physical wheel range mapped to full lock (wheel only)
    float ForceFeedbackStrength = 0.7f;    // [0..1] surfaced for the engine force-feedback driver (not used here)

    // Auto device-switch hint for the engine layer (this class does not switch by itself).
    float DeviceSwitchThreshold = 0.1f;    // [-]   activity above which the engine may switch device
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DRIVER COMMAND
//------------------------------------------------------------------------------------------------------------------------
// 📝 `Drive` feeds VehicleSolver::AssignInput; ShiftUp/ShiftDown/SelectedGear are consumed by a manual-gearbox
//    layer (the current dynamics run an AMT and auto-shift, so these are latched for the phase that wires manual shift).

struct DriverCommand
{
    DriverInput Drive{};
    bool ShiftUp      = false;    // [-]  edge pulse this frame
    bool ShiftDown    = false;    // [-]  edge pulse this frame
    int  SelectedGear = -1;       // [-]  >=0 ⇒ H-pattern direct gear select this frame (else -1)
    bool ResetVehicle = false;    // [-]  R pressed this frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DRIVER INPUT INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class DriverInputIntegrator
{
public:
    explicit DriverInputIntegrator(const InputConfiguration& Configuration = {}) noexcept
        : ActiveConfiguration(Configuration) {}

    void AssignConfiguration(const InputConfiguration& Configuration) noexcept { ActiveConfiguration = Configuration; }
    [[nodiscard]] const InputConfiguration& Configuration() const noexcept { return ActiveConfiguration; }
    void AssignDevice(InputDeviceCategory Device) noexcept { SelectedDevice = Device; }
    [[nodiscard]] InputDeviceCategory ActiveDevice() const noexcept { return SelectedDevice; }

    //-- Keyboard (digital) --------------------------------------------------------------------------------------------
    void ForwardThrottleKey(bool Down)   noexcept { ThrottleKeyDown  = Down; }   // W
    void ForwardBrakeKey(bool Down)      noexcept { BrakeKeyDown     = Down; }    // S
    void ForwardSteerLeftKey(bool Down)  noexcept { SteerLeftKeyDown = Down; }    // A
    void ForwardSteerRightKey(bool Down) noexcept { SteerRightKeyDown = Down; }   // D
    void ForwardHandbrakeKey(bool Down)  noexcept { HandbrakeKeyDown = Down; }    // Space

    //-- Gamepad / wheel (analog) — pedals in [0..1], steer in [-1..1]; +1 steer = LEFT (matches DriverInput) ----------
    void ForwardThrottleAxis(float Reading) noexcept { ThrottleAxisRaw = Reading; AnalogPedalsPresent = true; }
    void ForwardBrakeAxis(float Reading)    noexcept { BrakeAxisRaw    = Reading; AnalogPedalsPresent = true; }
    void ForwardSteerAxis(float Reading)    noexcept { SteerAxisRaw    = Reading; AnalogSteerPresent  = true; }
    void ForwardHandbrakeAxis(float Reading) noexcept { HandbrakeAxisRaw = Reading; }
    // Racing wheel: forward the physical wheel angle in degrees; mapped through WheelRotationRange to [-1..1].
    void ForwardWheelAngle(float Degrees) noexcept
    {
        SteerAxisRaw = Clamp(Degrees / (0.5f * ActiveConfiguration.WheelRotationRange), -1.0f, 1.0f);
        AnalogSteerPresent = true;
    }

    //-- Transmission / utility pulses ---------------------------------------------------------------------------------
    void ShiftUp()   noexcept { ShiftUpPending   = true; }    // Left-Shift
    void ShiftDown() noexcept { ShiftDownPending = true; }     // Left-Ctrl
    void SelectGear(int Gear) noexcept { PendingGear = Gear; } // H-pattern
    void RequestReset() noexcept { ResetPending = true; }      // R

    //------------------------------------------------------------------------------------------------------------------
    //                                               PER-FRAME ADVANCE
    //------------------------------------------------------------------------------------------------------------------

    // Advance the smoothing by Δτ and produce this frame's command. Call once per frame BEFORE AssignInput().
    [[nodiscard]] DriverCommand Advance(float Δτ) noexcept
    {
        Δτ = std::max(0.0f, Δτ);
        DriverCommand Command;

        if (SelectedDevice == InputDeviceCategory::Keyboard || (!AnalogPedalsPresent && !AnalogSteerPresent))
        {
            // Digital ramps.
            ThrottleSmoothed = Ramp(ThrottleSmoothed, ThrottleKeyDown ? 1.0f : 0.0f,
                                    ActiveConfiguration.ThrottleRiseRate, ActiveConfiguration.ThrottleFallRate, Δτ);
            BrakeSmoothed    = Ramp(BrakeSmoothed,    BrakeKeyDown    ? 1.0f : 0.0f,
                                    ActiveConfiguration.BrakeRiseRate,    ActiveConfiguration.BrakeFallRate,    Δτ);

            const float SteerTarget = (SteerLeftKeyDown ? 1.0f : 0.0f) - (SteerRightKeyDown ? 1.0f : 0.0f); // +1 = left
            if (SteerLeftKeyDown || SteerRightKeyDown)
                SteerSmoothed += (SteerTarget - SteerSmoothed) * std::min(1.0f, Δτ * ActiveConfiguration.SteerRate);
            else
                SteerSmoothed += (0.0f - SteerSmoothed) * std::min(1.0f, Δτ * ActiveConfiguration.SteerReturnRate);

            Command.Drive.Throttle  = Clamp(ThrottleSmoothed * ActiveConfiguration.ThrottleSensitivity, 0.0f, 1.0f);
            Command.Drive.Brake     = Clamp(BrakeSmoothed    * ActiveConfiguration.BrakeSensitivity,    0.0f, 1.0f);
            Command.Drive.Steer     = Clamp(SteerSmoothed    * ActiveConfiguration.SteeringSensitivity, -1.0f, 1.0f);
            Command.Drive.Handbrake = HandbrakeKeyDown;
        }
        else
        {
            // Analog device: dead-zone → sensitivity → (steer) linearity curve.
            const float ThrottleSample = DeadZone(ThrottleAxisRaw, ActiveConfiguration.ThrottleDeadZone);
            const float BrakeSample    = DeadZone(BrakeAxisRaw,    ActiveConfiguration.BrakeDeadZone);
            float       SteerSample    = DeadZone(SteerAxisRaw,    ActiveConfiguration.SteeringDeadZone);
            if (ActiveConfiguration.SteeringLinearity != 1.0f)
                SteerSample = std::copysign(std::pow(std::fabs(SteerSample), ActiveConfiguration.SteeringLinearity),
                                            SteerSample);

            ThrottleSmoothed = ThrottleSample;   // keep smoothing coherent for a later device switch
            BrakeSmoothed    = BrakeSample;
            SteerSmoothed    = SteerSample;
            Command.Drive.Throttle  = Clamp(ThrottleSample * ActiveConfiguration.ThrottleSensitivity, 0.0f, 1.0f);
            Command.Drive.Brake     = Clamp(BrakeSample    * ActiveConfiguration.BrakeSensitivity,    0.0f, 1.0f);
            Command.Drive.Steer     = Clamp(SteerSample    * ActiveConfiguration.SteeringSensitivity, -1.0f, 1.0f);
            Command.Drive.Handbrake = (HandbrakeAxisRaw > 0.5f) || HandbrakeKeyDown;
        }

        // Latch discrete pulses (consumed once).
        Command.ShiftUp      = ShiftUpPending;
        Command.ShiftDown    = ShiftDownPending;
        Command.SelectedGear = PendingGear;
        Command.ResetVehicle = ResetPending;
        ShiftUpPending = ShiftDownPending = ResetPending = false;
        PendingGear = -1;
        return Command;
    }

private:
    [[nodiscard]] static float Clamp(float Quantity, float Lower, float Upper) noexcept
    {
        return Quantity < Lower ? Lower : (Quantity > Upper ? Upper : Quantity);
    }
    [[nodiscard]] static float Ramp(float Current, float Target, float RiseRate, float FallRate, float Δτ) noexcept
    {
        const float Rate = (Target > Current) ? RiseRate : FallRate;
        const float Step = Rate * Δτ;
        if (std::fabs(Target - Current) <= Step) return Target;
        return Current + std::copysign(Step, Target - Current);
    }
    [[nodiscard]] static float DeadZone(float Sample, float DeadZoneWidth) noexcept
    {
        const float Magnitude = std::fabs(Sample);
        if (Magnitude <= DeadZoneWidth) return 0.0f;
        return std::copysign((Magnitude - DeadZoneWidth) / (1.0f - DeadZoneWidth), Sample); // rescale to reach ±1
    }

    InputConfiguration  ActiveConfiguration{};
    InputDeviceCategory SelectedDevice = InputDeviceCategory::Keyboard;

    // Keyboard button readings.
    bool ThrottleKeyDown  = false;
    bool BrakeKeyDown     = false;
    bool SteerLeftKeyDown = false;
    bool SteerRightKeyDown = false;
    bool HandbrakeKeyDown = false;

    // Analog raw readings.
    float ThrottleAxisRaw  = 0.0f;
    float BrakeAxisRaw     = 0.0f;
    float SteerAxisRaw     = 0.0f;
    float HandbrakeAxisRaw = 0.0f;
    bool  AnalogPedalsPresent = false;
    bool  AnalogSteerPresent  = false;

    // Smoothed readings.
    float ThrottleSmoothed = 0.0f;
    float BrakeSmoothed    = 0.0f;
    float SteerSmoothed    = 0.0f;

    // Discrete pulses.
    bool ShiftUpPending   = false;
    bool ShiftDownPending = false;
    bool ResetPending     = false;
    int  PendingGear      = -1;
};

} // namespace Frontier::Vehicle
