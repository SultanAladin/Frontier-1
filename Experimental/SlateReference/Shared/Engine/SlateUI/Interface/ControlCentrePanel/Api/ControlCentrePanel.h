//============================================================================================================================================
//                                                       CONTROLCENTREPANEL.H
//============================================================================================================================================
// 🧩 The complete north-drawer dashboard, settings, display, theme,
// notification and input presentation.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/DrawerSpace/Api/DrawerSpace.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/InterfacePreferences/Api/InterfacePreferences.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/NoticeDialog/Api/NoticeDialog.h"
#include "SlateUI/Interface/ShortcutSpecification/Api/ShortcutSpecification.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

#include <cstdint>

namespace Slate
{

enum class ControlCentrePage : std::uint32_t
{
    Dashboard = 0u,
    Settings = 1u,
    Notifications = 2u,
    Display = 3u,
    Input = 4u,
    PageCount = 5u
};

enum class DisplayPreferencePage : std::uint32_t
{
    Display = 0u,
    Fonts = 1u,
    Theme = 2u,
    PageCount = 3u
};

enum class IconAppearance : std::uint32_t
{
    Monotone = 0u,
    Duotone = 1u,
    Coloured = 2u,
    AppearanceCount = 3u
};

struct ControlCentreConfiguration
{
    ControlCentrePage Page = ControlCentrePage::Dashboard;
    DisplayPreferencePage DisplayPage = DisplayPreferencePage::Fonts;
    ThemeSubject Theme = ThemeSubject::Oled;
    ShortcutPreset InputPreset = ShortcutPreset::Blender;
    IconAppearance Icons = IconAppearance::Monotone;
    AccentSubject Primary = AccentSubject::Blue;
    AccentSubject Secondary = AccentSubject::Violet;
    AccentSubject Information = AccentSubject::Cyan;
    AccentSubject Warning = AccentSubject::Amber;
    AccentSubject Alert = AccentSubject::Rose;
    AccentSubject SemanticColours[5] = {AccentSubject::Blue, AccentSubject::Violet, AccentSubject::Cyan,
                                        AccentSubject::Amber, AccentSubject::Rose};
    std::uint32_t Quality = 2u;
    InterfaceAntialiasing GeometryAntialiasing = InterfaceAntialiasing::Refined;
    FontRasterisation FontAntialiasing = FontRasterisation::Automatic;
    VectorTessellation VectorQuality = VectorTessellation::Balanced;
    std::uint32_t Resolution = 0u;
    std::uint32_t Scaling = 100u;
    std::uint32_t RefreshRate = 0u;
    std::uint32_t MultipleDisplays = 1u;
    std::uint32_t Font = 0u;
    std::uint32_t IconWeight = 1u;
    std::uint32_t IconSize = 24u;
    std::uint32_t Radius = 24u;
    std::uint32_t TypographySize[8] = {24u, 20u, 16u, 14u, 12u, 10u, 14u, 14u};
    // 📝 FontWeight values (100..900), not ordinals. `ApplyFontWeights` casts them straight back to
    //    `FontWeight`, and `FontLoader::Face` derives its slot from the 100-step — a small ordinal would
    //    underflow the slot arithmetic and read past the face run. The eight are Title, Header, Subheader,
    //    Body, Label, Caption, Warning, Alert, applied at the `FontProfile` defaults.
    std::uint32_t TypographyWeight[8] = {600u, 600u, 500u, 400u, 500u, 400u, 500u, 600u};
    std::uint32_t PointerSpeed = 5u;
    std::uint32_t MonitorLevel = 67u;
    std::uint32_t TouchAction = 0u;
    bool TransparentSidebar = false;
    bool VsyncEnabled = true;
    bool IlluminationEnabled = false;
    bool NotificationsEnabled = true;
    bool DisturbanceWithheld = false;
    bool SoundEnabled = true;
    bool AppNotifications[4] = {true, true, true, false};
    bool NotificationsPresent = true;
    bool InvertScroll = false;
    bool TouchGestures = true;
    bool PressureEnabled = true;
    std::uint32_t ListeningShortcut = 0xFFFFFFFFu;
};

class ControlCentrePanel
{
public:
    // 📝 192 applied every control the panel drew before the typography strips; each role row's family
    //    carousel adds ten (two arrows and up to eight visible weight tiles), and a strip whose press
    //    ordinals fell past the ceiling was drawn but could never be grabbed — the tiles looked selectable
    //    and were not. 256 applies the eight strips with the rest of the panel's controls intact.
    static constexpr std::uint32_t ControlCapacity = 256u;

    Deliver<bool> ConstructControlCentrePanel(MotionIntegrator& Motion, RecordingSurface& Surface,
                            const ThemeProfile& Appearance);
    void Advance(const PointerCondition& Sampled, double Elapsed);
    Deliver<bool> Record(const PlaneExtent& Interior, ControlCentreConfiguration& Configuration);
    void Exclude(DrawerSpace& Drawers) const;
    void Reset();
    void SetFontFamilies(FontLoader& Loader);

private:
    void RetainExclusion(const PlaneExtent& Extent);
    bool Pressed(std::uint32_t Index, const PlaneExtent& Extent);
    bool Slider(std::uint32_t Index, const PlaneExtent& Extent, std::uint32_t Minimum, std::uint32_t Maximum,
                std::uint32_t& Reading, const char* UnitGlyph, ThemeToken Rail, ThemeToken Accent);
    void Toggle(std::uint32_t Index, const PlaneExtent& Extent, bool& Enabled, ThemeToken Quiet, ThemeToken Accent);
    void Symbol(const PlaneExtent& Extent, ThemeToken Colour);
    void DashboardPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                       ThemeToken Accent);
    void SettingsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                      ThemeToken Accent);
    void NotificationsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                           ThemeToken Accent);
    void DisplayPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                     ThemeToken Accent);
    void InputPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                   ThemeToken Accent);
    void ThemePage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                   ThemeToken Accent);
    void FontsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration, const ThemeDeclaration& Theme,
                   ThemeToken Accent);
    void DisplayHardwarePage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                             const ThemeDeclaration& Theme, ThemeToken Accent);
    void RecordSettingsFooter(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                              const ThemeDeclaration& Theme, ThemeToken Accent);
    void Navigate(ControlCentrePage Incoming);

    MotionIntegrator* Motion = nullptr;
    RecordingSurface* Surface = nullptr;
    const ThemeProfile* Appearance = nullptr;
    FontLoader* FontArchive = nullptr;
    ControlIndex Interaction = {};
    ComponentSpecification SharedControls = {};
    NoticeDialog SettingsNotice = {};
    ControlIdentity Controls[ControlCapacity] = {};
    PlaneExtent Exclusions[ControlCapacity] = {};
    std::uint32_t ExclusionCount = 0u;
    PointerCondition Pointer = {};
    ControlCentrePage CurrentPage = ControlCentrePage::Dashboard;
    ControlCentrePage PreviousPage = ControlCentrePage::Dashboard;
    std::uint32_t PageMotion = 0u;
    std::uint32_t TabMotion = 0u;
    std::uint32_t ThemeMotion = 0u;
    std::uint32_t FontMotion = 0u;
    std::uint32_t RoleFontMotion[8] = {};
    ThemeSubject CurrentTheme = ThemeSubject::Oled;
    ThemeSubject PreviousTheme = ThemeSubject::Oled;
    bool PageForward = true;
    DisplayPreferencePage CurrentTab = DisplayPreferencePage::Fonts;
    DisplayPreferencePage PreviousTab = DisplayPreferencePage::Fonts;
    bool TabForward = true;
    std::uint32_t ScrollMotion[static_cast<std::uint32_t>(ControlCentrePage::PageCount)] = {};
    float Scroll[static_cast<std::uint32_t>(ControlCentrePage::PageCount)] = {};
    float ScrollFrom[static_cast<std::uint32_t>(ControlCentrePage::PageCount)] = {};
    float ScrollTarget[static_cast<std::uint32_t>(ControlCentrePage::PageCount)] = {};
    std::uint32_t DisplayScrollMotion[3] = {};
    float DisplayScroll[3] = {};
    float DisplayScrollFrom[3] = {};
    float DisplayScrollTarget[3] = {};
    float FontScroll = 0.0f;
    float FontFrom = 0.0f;
    float FontTarget = 0.0f;
    float RoleFontScroll[8] = {};
    float RoleFontFrom[8] = {};
    float RoleFontTarget[8] = {};
    std::uint32_t OpenPalette = 5u;
    bool InputPresetOpen = false;
    bool WorkingConfigurationReady = false;
    ControlCentreConfiguration WorkingConfiguration = {};
    ControlCentreConfiguration AppliedConfiguration = {};
};

} // namespace Slate
