#include "Config.hpp"

#include <algorithm>
#include <cstdlib>
#include <json-c/json.h>
#include <switch.h>

namespace
{
    bool read_bool(json_object *object, const char *key, bool fallback) noexcept
    {
        json_object *value{};
        return object && json_object_object_get_ex(object, key, &value)
                   ? json_object_get_boolean(value)
                   : fallback;
    }

    int read_int(json_object *object, const char *key, int fallback, int minimum, int maximum) noexcept
    {
        json_object *value{};
        if (!object || !json_object_object_get_ex(object, key, &value)) { return fallback; }
        return std::clamp(json_object_get_int(value), minimum, maximum);
    }

    std::uint64_t read_u64_string(json_object *object, const char *key) noexcept
    {
        json_object *value{};
        if (!object || !json_object_object_get_ex(object, key, &value) ||
            json_object_get_type(value) != json_type_string)
        {
            return 0;
        }
        const char *text = json_object_get_string(value);
        if (!text || !text[0]) { return 0; }
        char *end{};
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        return end != text && *end == '\0' ? static_cast<std::uint64_t>(parsed) : 0;
    }
} // namespace

sync::Settings sync::load_settings() noexcept
{
    Settings settings{};
    json_object *object = json_object_from_file(CONFIG_PATH);
    if (!object) { return settings; }

    settings.enabled = read_bool(object, "enabled", false);
    settings.syncOnGameClose = read_bool(object, "syncOnGameClose", true);
    settings.keepLocalCopies = read_bool(object, "keepLocalCopies", false);
    settings.pollSeconds = read_int(object, "pollSeconds", 3, 2, 30);
    settings.settleSeconds = read_int(object, "settleSeconds", 5, 3, 60);
    json_object_put(object);
    return settings;
}

bool sync::is_app_active() noexcept
{
    constexpr std::uint64_t FRESH_SECONDS = 45;
    json_object *object = json_object_from_file(APP_STATUS_PATH);
    if (!object) { return false; }

    json_object *activeValue{};
    const bool active = json_object_object_get_ex(object, "active", &activeValue) &&
                        json_object_get_boolean(activeValue);
    const std::uint64_t heartbeat = read_u64_string(object, "heartbeatTick");
    json_object_put(object);

    const std::uint64_t now = armGetSystemTick();
    const std::uint64_t frequency = armGetSystemTickFreq();
    return active && heartbeat > 0 && now >= heartbeat && frequency > 0 &&
           now - heartbeat <= FRESH_SECONDS * frequency;
}
