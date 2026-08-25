#include "appstates/ExtrasMenuState.hpp"

#include "appstates/FileModeState.hpp"
#include "appstates/MainMenuState.hpp"
#include "appstates/NextcloudLoginState.hpp"
#include "appstates/TaskState.hpp"
#include "data/data.hpp"
#include "error.hpp"
#include "graphics/colors.hpp"
#include "input.hpp"
#include "keyboard/keyboard.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

#include <array>
#include <string>
#include <string_view>

namespace
{
    // This target is shared be a lot of states.
    constexpr std::string_view SECONDARY_TARGET = "SecondaryTarget";

    // Enum for switch case readability.
    enum
    {
        REINIT_DATA,
        SD_TO_SD_BROWSER,
        BIS_PRODINFO_F,
        BIS_SAFE,
        BIS_SYSTEM,
        BIS_USER,
        TERMINATE_PROCESS,
        NEXTCLOUD_CONNECT,
        NEXTCLOUD_DISCONNECT
    };
} // namespace

// Definition at bottom.
static void finish_reinitialization();
static void disconnect_nextcloud_task(sys::threadpool::JobData data);

//                      ---- Construction ----

ExtrasMenuState::ExtrasMenuState()
    : m_renderTarget(sdl::TextureManager::load(SECONDARY_TARGET, 1080, 555, SDL_TEXTUREACCESS_TARGET))
    , m_controlGuide(ui::ControlGuide::create(strings::get_by_name(strings::names::CONTROL_GUIDES, 5)))
{
    ExtrasMenuState::initialize_menu();
}

//                      ---- Public functions ----

void ExtrasMenuState::update()
{
    const bool hasFocus = BaseState::has_focus();
    const bool aPressed = input::button_pressed(HidNpadButton_A);
    const bool bPressed = input::button_pressed(HidNpadButton_B);

    m_extrasMenu->update(hasFocus);
    m_controlGuide->update(hasFocus);

    if (aPressed)
    {
        switch (m_extrasMenu->get_selected())
        {
            case REINIT_DATA:       ExtrasMenuState::reinitialize_data(); break;
            case SD_TO_SD_BROWSER:  ExtrasMenuState::sd_to_sd_browser(); break;
            case BIS_PRODINFO_F:    ExtrasMenuState::prodinfof_to_sd(); break;
            case BIS_SAFE:          ExtrasMenuState::safe_to_sd(); break;
            case BIS_SYSTEM:        ExtrasMenuState::system_to_sd(); break;
            case BIS_USER:          ExtrasMenuState::user_to_sd(); break;
            case TERMINATE_PROCESS: ExtrasMenuState::terminate_process(); break;
            case NEXTCLOUD_CONNECT: ExtrasMenuState::connect_nextcloud(); break;
            case NEXTCLOUD_DISCONNECT: ExtrasMenuState::disconnect_nextcloud(); break;
        }
    }
    else if (bPressed) { BaseState::deactivate(); }
}

void ExtrasMenuState::sub_update() { m_controlGuide->sub_update(); }

void ExtrasMenuState::render()
{
    const bool hasFocus = BaseState::has_focus();

    m_renderTarget->clear(colors::TRANSPARENT);
    m_extrasMenu->render(m_renderTarget, hasFocus);
    m_renderTarget->render(sdl::Texture::Null, 201, 91);
    m_controlGuide->render(sdl::Texture::Null, hasFocus);
}

//                      ---- Private functions ----

void ExtrasMenuState::initialize_menu()
{
    if (!m_extrasMenu) { m_extrasMenu = ui::Menu::create(32, 10, 1000, 23, 555); }

    for (int i = 0; const char *option = strings::get_by_name(strings::names::EXTRASMENU_MENU, i); i++)
    {
        m_extrasMenu->add_option(option);
    }

    const char *connect = strings::get_by_name(strings::names::NEXTCLOUD, 0);
    const char *disconnect = strings::get_by_name(strings::names::NEXTCLOUD, 1);
    m_extrasMenu->add_option(connect ? connect : "Connect Nextcloud");
    m_extrasMenu->add_option(disconnect ? disconnect : "Disconnect Nextcloud");
}

void ExtrasMenuState::reinitialize_data() { data::launch_initialization(true, finish_reinitialization); }

void ExtrasMenuState::sd_to_sd_browser() { FileModeState::create_and_push("sdmc", "sdmc", false); }

void ExtrasMenuState::prodinfof_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("prodinfo-f", FsBisPartitionId_CalibrationFile));
    if (mountError) { return; }

    FileModeState::create_and_push("prodinfo-f", "sdmc", false, true);
}

void ExtrasMenuState::safe_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("safe", FsBisPartitionId_SafeMode));
    if (mountError) { return; }

    FileModeState::create_and_push("safe", "sdmc", false, true);
}

void ExtrasMenuState::system_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("system", FsBisPartitionId_System));
    if (mountError) { return; }

    FileModeState::create_and_push("system", "sdmc", false, true);
}

void ExtrasMenuState::user_to_sd()
{
    const bool mountError = error::fslib(fslib::open_bis_filesystem("user", FsBisPartitionId_User));
    if (mountError) { return; }

    FileModeState::create_and_push("user", "sdmc", false, true);
}

void ExtrasMenuState::terminate_process() {}

void ExtrasMenuState::connect_nextcloud()
{
    if (!remote::has_internet_connection())
    {
        const char *message = strings::get_by_name(strings::names::REMOTE_POPS, 0);
        ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS,
                                            message ? message : "No internet connection available!");
        return;
    }

    std::array<char, 0x400> server{};
    const char *header = strings::get_by_name(strings::names::NEXTCLOUD, 2);
    const bool entered = keyboard::get_input(SwkbdType_QWERTY,
                                              "https://",
                                              header ? header : "Enter the address of your Nextcloud server",
                                              server.data(),
                                              server.size());
    if (entered) { NextcloudLoginState::create_and_push(server.data()); }
}

void ExtrasMenuState::disconnect_nextcloud()
{
    auto data = std::make_shared<sys::Task::DataStruct>();
    TaskState::create_push_fade(disconnect_nextcloud_task, data);
}

static void finish_reinitialization()
{
    const int popTicks     = ui::PopMessageManager::DEFAULT_TICKS;
    const char *popSuccess = strings::get_by_name(strings::names::EXTRASMENU_POPS, 0);

    MainMenuState::refresh_view_states();
    ui::PopMessageManager::push_message(popTicks, popSuccess);
}

static void disconnect_nextcloud_task(sys::threadpool::JobData baseData)
{
    auto data = std::static_pointer_cast<sys::Task::DataStruct>(baseData);
    const char *status = strings::get_by_name(strings::names::NEXTCLOUD, 15);
    data->task->set_status(status ? status : "Disconnecting Nextcloud securely...");

    const bool removed = remote::disconnect_nextcloud();
    const char *message = strings::get_by_name(strings::names::NEXTCLOUD, removed ? 7 : 8);
    if (!message) { message = removed ? "Nextcloud disconnected." : "Failed to disconnect Nextcloud."; }
    ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, message);
    data->task->complete();
}
