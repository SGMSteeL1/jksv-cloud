#include "sync/SyncConfig.hpp"

#include "data/data.hpp"
#include "fslib.hpp"
#include "json.hpp"
#include "strings/names.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"
#include "ui/PopMessageManager.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <json-c/json.h>
#include <string_view>
#include <switch.h>

namespace
{
    constexpr const char *PATH_CONFIG_DIRECTORY = "sdmc:/config/JKSV Cloud";
    constexpr const char *PATH_UI_STATE = "sdmc:/config/JKSV Cloud/sync-ui.json";
    constexpr const char *PATH_SYS_MODULE =
        "sdmc:/atmosphere/contents/420000000000C10D/exefs.nsp";
    constexpr std::int64_t HEARTBEAT_FRESH_SECONDS = 75;

    json::Object load_or_create_config()
    {
        json::Object config = json::new_object(json_object_from_file, syncconfig::PATH_CONFIG);
        if (!config) { config = json::new_object(json_object_new_object); }
        return config;
    }

    void add_default_if_missing(json_object *config, const char *key, json_object *value)
    {
        json_object *existing{};
        if (json_object_object_get_ex(config, key, &existing))
        {
            json_object_put(value);
            return;
        }
        json_object_object_add(config, key, value);
    }

    const char *translated(int index, const char *fallback) noexcept
    {
        const char *value = strings::get_by_name(strings::names::NEXTCLOUD, index);
        return value ? value : fallback;
    }

    bool read_string(json_object *object, const char *key, std::string &out) noexcept
    {
        json_object *value{};
        if (!object || !json_object_object_get_ex(object, key, &value) ||
            json_object_get_type(value) != json_type_string)
        {
            return false;
        }
        const char *text = json_object_get_string(value);
        if (!text) { return false; }
        out = text;
        return true;
    }

    std::uint64_t parse_number(std::string_view text, int base) noexcept
    {
        if (text.empty()) { return 0; }
        std::string copy{text};
        char *end{};
        errno = 0;
        const unsigned long long value = std::strtoull(copy.c_str(), &end, base);
        return errno == 0 && end != copy.c_str() && *end == '\0'
                   ? static_cast<std::uint64_t>(value)
                   : 0;
    }

    std::uint64_t read_number_string(json_object *object, const char *key, int base) noexcept
    {
        std::string text{};
        return read_string(object, key, text) ? parse_number(text, base) : 0;
    }

    std::int64_t read_int64(json_object *object, const char *key) noexcept
    {
        json_object *value{};
        return object && json_object_object_get_ex(object, key, &value)
                   ? json_object_get_int64(value)
                   : 0;
    }

    std::string hex16(std::uint64_t value)
    {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
        return text;
    }

    std::string title_name(std::uint64_t titleId)
    {
        if (titleId == 0) { return translated(41, "None"); }
        data::TitleInfo *title = data::get_title_info_by_id(titleId);
        if (title && title->get_title() && title->get_title()[0]) { return title->get_title(); }
        return hex16(titleId);
    }

    std::string format_time(std::int64_t timestamp)
    {
        if (timestamp <= 0) { return translated(42, "Never"); }
        const std::time_t raw = static_cast<std::time_t>(timestamp);
        std::tm local{};
        if (!localtime_r(&raw, &local)) { return translated(42, "Never"); }
        char text[32]{};
        if (std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", &local) == 0)
        {
            return translated(42, "Never");
        }
        return text;
    }

    std::string module_state_text(const syncconfig::Status &status)
    {
        if (!syncconfig::is_sysmodule_installed()) { return translated(28, "*Not installed*"); }
        if (status.running && status.phase == "disabled") { return translated(27, "<Running, but disabled>"); }
        if (status.running) { return translated(24, ">Running>"); }
        if (status.filePresent) { return translated(26, "*Not detected*"); }
        return translated(25, "<Waiting for startup>");
    }

    std::string phase_text(const syncconfig::Status &status)
    {
        if (!status.running) { return translated(26, "Not detected"); }
        if (status.phase == "starting") { return translated(33, "Starting"); }
        if (status.phase == "disabled") { return translated(34, "Disabled"); }
        if (status.phase == "idle") { return translated(35, "Waiting for a game"); }
        if (status.phase == "monitoring")
        {
            return stringutil::get_formatted_string(translated(36, "Monitoring %s"),
                                                    title_name(status.activeTitleId).c_str());
        }
        if (status.phase == "waiting") { return translated(37, "Waiting for safe game close"); }
        if (status.phase == "backing_up") { return translated(38, "Creating read-only backup"); }
        if (status.phase == "uploading") { return translated(39, "Uploading to the cloud"); }
        return translated(40, "Service unavailable; retrying");
    }

    std::string result_text(const syncconfig::Status &status)
    {
        if (status.lastResult == "success") { return translated(43, ">Sent successfully>"); }
        if (status.lastResult == "queued") { return translated(44, "<Waiting in upload queue>"); }
        if (status.lastResult == "error") { return translated(45, "*Backup failed*"); }
        return translated(46, "No automatic backup yet");
    }

    std::uint64_t read_notified_event() noexcept
    {
        json_object *root = json_object_from_file(PATH_UI_STATE);
        if (!root) { return 0; }
        const std::uint64_t eventId = read_number_string(root, "lastNotifiedEventId", 10);
        json_object_put(root);
        return eventId;
    }

    bool save_notified_event(std::uint64_t eventId) noexcept
    {
        json_object *root = json_object_new_object();
        if (!root) { return false; }
        json_object_object_add(root,
                               "lastNotifiedEventId",
                               json_object_new_string(std::to_string(eventId).c_str()));
        const std::string temporary = std::string{PATH_UI_STATE} + ".tmp";
        const bool written = json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0;
        json_object_put(root);
        if (!written) { return false; }
        std::remove(PATH_UI_STATE);
        if (std::rename(temporary.c_str(), PATH_UI_STATE) == 0) { return true; }
        std::remove(temporary.c_str());
        return false;
    }
} // namespace

bool syncconfig::is_enabled() noexcept
{
    json::Object config = json::new_object(json_object_from_file, PATH_CONFIG);
    if (!config) { return false; }

    json_object *enabled{};
    return json_object_object_get_ex(config.get(), "enabled", &enabled) &&
           json_object_get_boolean(enabled);
}

bool syncconfig::set_enabled(bool enabled) noexcept
{
    const fslib::Path configDirectory{PATH_CONFIG_DIRECTORY};
    if (!fslib::directory_exists(configDirectory) &&
        !fslib::create_directories_recursively(configDirectory))
    {
        return false;
    }

    json::Object config = load_or_create_config();
    if (!config) { return false; }

    json_object_object_add(config.get(), "enabled", json_object_new_boolean(enabled));
    add_default_if_missing(config.get(), "syncOnGameClose", json_object_new_boolean(true));
    add_default_if_missing(config.get(), "keepLocalCopies", json_object_new_boolean(false));
    add_default_if_missing(config.get(), "pollSeconds", json_object_new_int(3));
    add_default_if_missing(config.get(), "settleSeconds", json_object_new_int(5));

    return json_object_to_file_ext(PATH_CONFIG, config.get(), JSON_C_TO_STRING_PRETTY) == 0;
}

bool syncconfig::is_sysmodule_installed() noexcept
{
    return fslib::file_exists(fslib::Path{PATH_SYS_MODULE});
}

std::string syncconfig::get_menu_label(bool enabled)
{
    const char *format = strings::get_by_name(strings::names::NEXTCLOUD, enabled ? 17 : 16);
    if (format) { return format; }
    return enabled ? "Background sync: Enabled" : "Background sync: Disabled";
}

syncconfig::Status syncconfig::read_status() noexcept
{
    Status status{};
    json_object *root = json_object_from_file(PATH_STATUS);
    if (!root) { return status; }
    status.filePresent = true;
    read_string(root, "sysmoduleVersion", status.sysmoduleVersion);
    read_string(root, "phase", status.phase);
    status.activeTitleId = read_number_string(root, "activeTitleId", 16);
    status.eventId = read_number_string(root, "eventId", 10);
    read_string(root, "lastResult", status.lastResult);
    status.lastEventAt = read_int64(root, "lastEventAt");
    status.lastSuccessAt = read_int64(root, "lastSuccessAt");
    const std::uint64_t eventTick = read_number_string(root, "lastEventTick", 10);
    const std::uint64_t successTick = read_number_string(root, "lastSuccessTick", 10);
    status.lastTitleId = read_number_string(root, "lastTitleId", 16);
    status.lastSaveId = read_number_string(root, "lastSaveId", 16);
    read_string(root, "lastBackupName", status.lastBackupName);
    read_string(root, "lastDetail", status.lastDetail);
    status.pendingCount = static_cast<int>(read_int64(root, "pendingCount"));
    const std::uint64_t heartbeatTick = read_number_string(root, "heartbeatTick", 10);
    const std::int64_t legacyHeartbeat = read_int64(root, "lastHeartbeat");
    json_object_put(root);

    const std::uint64_t nowTick = armGetSystemTick();
    const std::uint64_t frequency = armGetSystemTickFreq();
    if (eventTick > 0 && nowTick >= eventTick && frequency > 0)
    {
        const std::uint64_t elapsed = (nowTick - eventTick) / frequency;
        const std::time_t now = std::time(nullptr);
        if (now > 0 && elapsed <= static_cast<std::uint64_t>(now))
        {
            status.lastEventAt = static_cast<std::int64_t>(now - elapsed);
        }
    }
    if (successTick > 0 && nowTick >= successTick && frequency > 0)
    {
        const std::uint64_t elapsed = (nowTick - successTick) / frequency;
        const std::time_t now = std::time(nullptr);
        if (now > 0 && elapsed <= static_cast<std::uint64_t>(now))
        {
            status.lastSuccessAt = static_cast<std::int64_t>(now - elapsed);
        }
    }

    if (heartbeatTick > 0 && nowTick >= heartbeatTick && frequency > 0)
    {
        status.running = nowTick - heartbeatTick <=
                         static_cast<std::uint64_t>(HEARTBEAT_FRESH_SECONDS) * frequency;
    }
    else
    {
    const std::time_t now = std::time(nullptr);
    const std::int64_t difference = now > 0 && legacyHeartbeat > 0
                                        ? std::llabs(static_cast<long long>(now) - legacyHeartbeat)
                                        : HEARTBEAT_FRESH_SECONDS + 1;
    status.running = difference <= HEARTBEAT_FRESH_SECONDS;
    }
    if (status.pendingCount < 0) { status.pendingCount = 0; }
    return status;
}

std::string syncconfig::get_status_menu_label()
{
    const Status status = read_status();
    return stringutil::get_formatted_string(translated(23, "Sync status: %s"),
                                            module_state_text(status).c_str());
}

std::string syncconfig::get_status_details()
{
    const Status status = read_status();
    const std::string version = status.sysmoduleVersion.empty() ? "--" : status.sysmoduleVersion;
    const std::string module = module_state_text(status);
    const std::string activity = phase_text(status);
    const std::string result = result_text(status);
    const std::string game = title_name(status.lastTitleId);
    const std::string eventTime = format_time(status.lastEventAt);

    std::string details = stringutil::get_formatted_string(
        translated(47,
                   "JKSV Cloud Sync %s\nModule: %s\nActivity: %s\nLast result: %s\nGame: %s - %s\nPending uploads: %d"),
        version.c_str(),
        module.c_str(),
        activity.c_str(),
        result.c_str(),
        game.c_str(),
        eventTime.c_str(),
        status.pendingCount);
    if (!status.lastDetail.empty())
    {
        details += "\nDetalhe: " + status.lastDetail;
    }
    return details;
}

void syncconfig::notify_new_result()
{
    const Status status = read_status();
    if (status.eventId == 0 || status.eventId <= read_notified_event()) { return; }
    if (status.lastResult != "success" && status.lastResult != "queued" && status.lastResult != "error") { return; }
    if (!save_notified_event(status.eventId)) { return; }

    const char *message = translated(32, "The automatic backup result was updated.");
    if (status.lastResult == "success")
    {
        message = translated(29, "Automatic backup uploaded successfully!");
    }
    else if (status.lastResult == "queued")
    {
        message = translated(30, "Automatic backup saved and waiting for network upload.");
    }
    else if (status.lastResult == "error")
    {
        message = translated(31, "Automatic backup failed. Open Sync Status for details.");
    }
    ui::PopMessageManager::push_message(ui::PopMessageManager::DEFAULT_TICKS, message);
}

void syncconfig::set_app_active(bool active) noexcept
{
    if (!active)
    {
        std::remove(PATH_APP_STATUS);
        return;
    }

    const fslib::Path configDirectory{PATH_CONFIG_DIRECTORY};
    if (!fslib::directory_exists(configDirectory) &&
        !fslib::create_directories_recursively(configDirectory))
    {
        return;
    }

    json_object *root = json_object_new_object();
    if (!root) { return; }
    json_object_object_add(root, "active", json_object_new_boolean(true));
    json_object_object_add(root,
                           "heartbeatTick",
                           json_object_new_string(std::to_string(armGetSystemTick()).c_str()));
    const std::string temporary = std::string{PATH_APP_STATUS} + ".tmp";
    const bool written = json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0;
    json_object_put(root);
    if (!written) { return; }
    std::remove(PATH_APP_STATUS);
    if (std::rename(temporary.c_str(), PATH_APP_STATUS) != 0) { std::remove(temporary.c_str()); }
}
