//============================================================================================================================================
//                                                     EDITORCAMERACOMPONENT.H
//============================================================================================================================================
// 🧩 Editor-specialised camera identity. It uses CameraComponent's common pose, movement and positional
//    lag today; editor-only selection, bookmarks and UI remain in EditorHost / SceneDirectoryPanel.
//    PlayerCameraComponent and SpectatorCameraComponent can later provide different controllers without
//    inheriting editor behavior or duplicating the base camera law.

#pragma once

#include "SlateWorld/World/CameraComponent/Api/CameraComponent.h"

namespace Slate
{

class EditorCameraComponent final : public CameraComponent
{
public:
    EditorCameraComponent() = default;
    ~EditorCameraComponent() = default;

    /// 🧩 Changes the editor fly gear by wheel steps, using Unreal's persistent multiplicative speed.
    /// note  Player and spectator cameras do not inherit this property; it belongs to editor navigation.
    void AdjustFlySpeed(double Steps);

    double FlySpeed = 50.0; // [m/s] - persistent editor navigation rate
};

} // namespace Slate
