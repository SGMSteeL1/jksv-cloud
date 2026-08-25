#include "appstates/NextcloudLoginState.hpp"

#include "graphics/colors.hpp"
#include "graphics/screen.hpp"
#include "input.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"
#include "sdl.hpp"
#include "strings/strings.hpp"
#include "ui/PopMessageManager.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
    constexpr int FONT_SIZE = 20;
    constexpr int MAX_POLLS = 300; // Ten minutes at one request every two seconds.

    const char *nextcloud_string(int index, const char *fallback) noexcept
    {
        const char *translated = strings::get_by_name(strings::names::NEXTCLOUD, index);
        return translated ? translated : fallback;
    }

    void clear_secret(std::string &secret) noexcept
    {
        std::fill(secret.begin(), secret.end(), '\0');
        secret.clear();
    }
} // namespace

NextcloudLoginState::NextcloudLoginState(std::string server)
    : BaseState(true)
    , m_data(std::make_shared<LoginData>(std::move(server)))
{
    {
        std::lock_guard guard{m_data->mutex};
        m_data->status = nextcloud_string(3, "Starting secure Nextcloud sign-in...");
    }
    sys::threadpool::push_job(NextcloudLoginState::begin_login, m_data);
}

void NextcloudLoginState::update()
{
    if (input::button_pressed(HidNpadButton_B))
    {
        m_data->cancelled.store(true);
        m_data->phase.store(Phase::Cancelled);
    }

    switch (m_data->phase.load())
    {
        case Phase::QrReady:   NextcloudLoginState::start_qr_polling(); break;
        case Phase::Success:   NextcloudLoginState::finish_state(true); break;
        case Phase::Failed:    NextcloudLoginState::finish_state(false); break;
        case Phase::Cancelled: BaseState::deactivate(); break;
        default:               break;
    }
}

void NextcloudLoginState::render()
{
    std::string status{};
    {
        std::lock_guard guard{m_data->mutex};
        status = m_data->status;
    }

    sdl::render_rect_fill(sdl::Texture::Null,
                          0,
                          0,
                          graphics::SCREEN_WIDTH,
                          graphics::SCREEN_HEIGHT,
                          colors::DIM_BACKGROUND);

    int statusY = 351;
    if (m_qrReady)
    {
        constexpr int QUIET_ZONE = 4;
        constexpr int MODULE_SCALE = 8;
        constexpr int QR_PIXELS = (util::QrCode::SIZE + QUIET_ZONE * 2) * MODULE_SCALE;
        constexpr int QR_X = (graphics::SCREEN_WIDTH - QR_PIXELS) / 2;
        constexpr int QR_Y = 24;

        sdl::render_rect_fill(sdl::Texture::Null,
                              QR_X,
                              QR_Y,
                              QR_PIXELS,
                              QR_PIXELS,
                              colors::WHITE);
        for (int y = 0; y < util::QrCode::SIZE; ++y)
        {
            for (int x = 0; x < util::QrCode::SIZE; ++x)
            {
                if (!m_qr.get_module(x, y)) { continue; }
                sdl::render_rect_fill(sdl::Texture::Null,
                                      QR_X + (x + QUIET_ZONE) * MODULE_SCALE,
                                      QR_Y + (y + QUIET_ZONE) * MODULE_SCALE,
                                      MODULE_SCALE,
                                      MODULE_SCALE,
                                      colors::BLACK);
            }
        }
        statusY = QR_Y + QR_PIXELS + 18;
    }

    const int width = sdl::text::get_width(FONT_SIZE, status.c_str());
    const int x     = std::max(24, (graphics::SCREEN_WIDTH - width) / 2);
    sdl::text::render(sdl::Texture::Null,
                      x,
                      statusY,
                      FONT_SIZE,
                      graphics::SCREEN_WIDTH - 48,
                      colors::WHITE,
                      status);
}

void NextcloudLoginState::begin_login(sys::threadpool::JobData baseData)
{
    auto data = std::static_pointer_cast<LoginData>(baseData);
    remote::NextcloudLoginSession session{};
    const remote::NextcloudResult result = remote::nextcloud_begin_login(data->server, session);

    if (data->cancelled.load()) { return; }

    std::lock_guard guard{data->mutex};
    data->result = result;
    if (result == remote::NextcloudResult::Ok)
    {
        data->session = std::move(session);
        data->status  = nextcloud_string(9, "Preparing the QR code for phone authorization...");
        data->phase.store(Phase::QrReady);
    }
    else
    {
        logger::log("Nextcloud login initialization failed: %s", remote::get_nextcloud_result_string(result));
        if (result == remote::NextcloudResult::InvalidServer)
        {
            data->status = nextcloud_string(13, "Invalid server address. Use a complete HTTPS address.");
        }
        else if (result == remote::NextcloudResult::NetworkError)
        {
            data->status = nextcloud_string(14, "Network or certificate error while connecting to Nextcloud.");
        }
        else { data->status = nextcloud_string(6, "Nextcloud connection failed. Check the JKSV log."); }
        data->phase.store(Phase::Failed);
    }
}

void NextcloudLoginState::finish_login(sys::threadpool::JobData baseData)
{
    auto data = std::static_pointer_cast<LoginData>(baseData);
    remote::NextcloudCredentials credentials{};
    remote::NextcloudResult result = remote::NextcloudResult::Pending;

    for (int poll = 0; poll < MAX_POLLS && !data->cancelled.load(); ++poll)
    {
        result = remote::nextcloud_poll_login(data->session, credentials);
        if (result == remote::NextcloudResult::Pending)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        break;
    }

    if (!data->cancelled.load() && result == remote::NextcloudResult::Pending)
    {
        result = remote::NextcloudResult::AuthenticationFailed;
    }
    if (!data->cancelled.load() && result == remote::NextcloudResult::Ok)
    {
        {
            std::lock_guard guard{data->mutex};
            data->status = nextcloud_string(10, "Finishing the secure connection...");
        }
        result = remote::nextcloud_prepare_storage(credentials);
    }
    if (!data->cancelled.load() && result == remote::NextcloudResult::Ok)
    {
        result = remote::nextcloud_save_credentials(credentials);
    }
    if (!data->cancelled.load() && result == remote::NextcloudResult::Ok && !remote::reload_nextcloud())
    {
        result = remote::NextcloudResult::StorageSetupFailed;
    }

    clear_secret(credentials.appPassword);
    if (data->cancelled.load()) { return; }

    std::lock_guard guard{data->mutex};
    data->result = result;
    if (result == remote::NextcloudResult::Ok)
    {
        data->status = nextcloud_string(5, "Nextcloud connected successfully!");
        data->phase.store(Phase::Success);
    }
    else
    {
        logger::log("Nextcloud login failed: %s", remote::get_nextcloud_result_string(result));
        data->status = nextcloud_string(6, "Nextcloud connection failed. Check the JKSV log.");
        data->phase.store(Phase::Failed);
    }
}

void NextcloudLoginState::start_qr_polling()
{
    if (m_pollStarted) { return; }
    m_pollStarted = true;

    std::string loginUrl{};
    {
        std::lock_guard guard{m_data->mutex};
        loginUrl = m_data->session.loginUrl;
    }

    if (!m_qr.encode_text(loginUrl))
    {
        logger::log("Nextcloud QR generation failed for a %zu-byte URL.", loginUrl.size());
        std::lock_guard guard{m_data->mutex};
        m_data->result = remote::NextcloudResult::UnexpectedResponse;
        m_data->status = nextcloud_string(12, "The authorization QR code could not be generated.");
        m_data->phase.store(Phase::Failed);
        return;
    }

    m_qrReady = true;
    {
        std::lock_guard guard{m_data->mutex};
        m_data->status = nextcloud_string(
            4,
            "Scan the QR code with your phone. Waiting for authorization... [B] Cancel");
    }
    m_data->phase.store(Phase::Polling);
    sys::threadpool::push_job(NextcloudLoginState::finish_login, m_data);
}

void NextcloudLoginState::finish_state(bool success)
{
    std::string message{};
    {
        std::lock_guard guard{m_data->mutex};
        message = m_data->status;
    }
    if (message.empty())
    {
        message = success
                      ? nextcloud_string(5, "Nextcloud connected successfully!")
                      : nextcloud_string(6, "Nextcloud connection failed. Check the JKSV log.");
    }
    ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, message.c_str());
    BaseState::deactivate();
}
