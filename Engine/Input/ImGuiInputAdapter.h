/*============================================================================================================================================
                                                            IMGUIINPUTADAPTER.H
============================================================================================================================================*/
// 🧩 Downstream one-way EditorInputExchange -> Dear ImGui IO signal forwarder (never polled upstream by Engine or Editor classes)

#pragma once

#include "Engine/Input/InputExtension.h"

struct ImGuiIO;

namespace Frontier::Input
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOWNSTREAM ADAPTER
//------------------------------------------------------------------------------------------------------------------------

class ImGuiInputAdapter
{
public:
    explicit ImGuiInputAdapter(ImGuiIO& TargetIo) noexcept;

    void IntakeSignalPacket(const InputSignalPacket& Packet) noexcept;

    static std::size_t ForwardEditorExchangeToImGui(InputExtension& Coordinator, ImGuiIO& TargetIo) noexcept;

private:
    ImGuiIO* DestinationIo = nullptr; // [-] - Target Dear ImGui IO destination
};

static_assert(InputSignalSink<ImGuiInputAdapter>,
              "ImGuiInputAdapter must satisfy InputSignalSink for EditorInputExchange draining.");

} // namespace Frontier::Input
