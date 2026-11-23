//============================================================================================================================================
//                                                       TRANSFORMCOMPONENT.H
//============================================================================================================================================
// 🧩 A scene pose with explicit authored and presented states. Cameras use the authored pose as their movement
//    target and publish the presented pose after positional lag, allowing an inspector and a controller to edit
//    the same transform without either overwriting the other with a stale mirror.

#pragma once

#include <cstdint>

namespace Slate
{

class TransformComponent
{
public:
    /// Takes a UI-authored pose only when it differs from the last pose this component published.
    /// Returns true when a real external edit was consumed.
    bool ConsumeAuthored(const double Position[3], const double Rotation[3]);

    /// Publishes the current presented pose to a UI mirror and remembers exactly what was written.
    void Publish(const double Position[3], const double Rotation[3],
                 double PublishedPosition[3], double PublishedRotation[3]);

    /// Seeds both authored and presented state without classifying the operation as an edit.
    void Reset(const double Position[3], const double Rotation[3]);

    const double* AuthoredPosition() const { return PositionSlot; }
    const double* AuthoredRotation() const { return RotationSlot; }

private:
    static bool Differs(const double Left[3], const double Right[3]);

    double PositionSlot[3] = { 0.0, 0.0, 0.0 };
    double RotationSlot[3] = { 0.0, 0.0, 0.0 };
    double LastPublishedPosition[3] = { 0.0, 0.0, 0.0 };
    double LastPublishedRotation[3] = { 0.0, 0.0, 0.0 };
    bool   PublicationStanding = false;
};

} // namespace Slate
