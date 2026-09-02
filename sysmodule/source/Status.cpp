#include "Status.hpp"

#include "Config.hpp"
#include "TitleName.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <json-c/json.h>
#include <string>
#include <switch.h>
#include <sys/stat.h>

namespace
{
    constexpr std::uint64_t HEARTBEAT_INTERVAL_SECONDS = 30;

    struct StatusData
    {
        sync::status::Phase phase{sync::status::Phase::Starting};
        std::uint64_t activeTitleId{};
        std::uint64_t eventId{};
        std::string lastResult{"none"};
        std::uint64_t lastEventTick{};
        std::uint64_t lastSuccessTick{};
        std::uint64_t lastTitleId{};
        std::uint64_t lastSaveId{};
        std::string lastBackupName{};
        std::string lastDetail{};
        std::size_t pendingCount{};
        std::uint64_t startedTick{};
        std::uint64_t heartbeatTick{};
        std::uint64_t lastWriteTick{};
        bool initialized{};
    };

    StatusData g_status{};
    Mutex g_statusMutex{};

    const char *phase_name(sync::status::Phase phase) noexcept
    {
        switch (phase)
        {
            case sync::status::Phase::Starting:   return "starting";
            case sync::status::Phase::Disabled:   return "disabled";
            case sync::status::Phase::Idle:       return "idle";
            case sync::status::Phase::Monitoring: return "monitoring";
            case sync::status::Phase::Waiting:    return "waiting";
            case sync::status::Phase::BackingUp:  return "backing_up";
            case sync::status::Phase::Uploading:  return "uploading";
            case sync::status::Phase::Error:      return "error";
        }
        return "error";
    }

    const char *result_name(sync::status::Result result) noexcept
    {
        switch (result)
        {
            case sync::status::Result::Success: return "success";
            case sync::status::Result::Queued:  return "queued";
            case sync::status::Result::Error:   return "error";
        }
        return "error";
    }

    std::string hex16(std::uint64_t value)
    {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
        return text;
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
        errno = 0;
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        return errno == 0 && end != text && *end == '\0' ? static_cast<std::uint64_t>(parsed) : 0;
    }

    std::uint64_t read_hex(json_object *object, const char *key) noexcept
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
        errno = 0;
        const unsigned long long parsed = std::strtoull(text, &end, 16);
        return errno == 0 && end != text && *end == '\0' ? static_cast<std::uint64_t>(parsed) : 0;
    }

    void read_string(json_object *object, const char *key, std::string &out) noexcept
    {
        json_object *value{};
        if (!object || !json_object_object_get_ex(object, key, &value) ||
            json_object_get_type(value) != json_type_string)
        {
            return;
        }
        const char *text = json_object_get_string(value);
        if (text) { out = text; }
    }

    void load_previous_result() noexcept
    {
        json_object *root = json_object_from_file(sync::STATUS_PATH);
        if (!root) { return; }
        g_status.eventId = read_u64_string(root, "eventId");
        read_string(root, "lastResult", g_status.lastResult);
        g_status.lastEventTick = read_u64_string(root, "lastEventTick");
        g_status.lastSuccessTick = read_u64_string(root, "lastSuccessTick");
        g_status.lastTitleId = read_hex(root, "lastTitleId");
        g_status.lastSaveId = read_hex(root, "lastSaveId");
        read_string(root, "lastBackupName", g_status.lastBackupName);
        read_string(root, "lastDetail", g_status.lastDetail);
        json_object *pending{};
        if (json_object_object_get_ex(root, "pendingCount", &pending))
        {
            const int value = json_object_get_int(pending);
            g_status.pendingCount = value > 0 ? static_cast<std::size_t>(value) : 0;
        }
        json_object_put(root);
    }

    bool ensure_directory() noexcept
    {
        mkdir("sdmc:/config", 0777);
        return mkdir(sync::CONFIG_DIRECTORY, 0777) == 0 || errno == EEXIST;
    }

    void publish_ultrahand_notification(sync::status::Result result,
                                        std::uint64_t titleId,
                                        std::uint64_t eventId,
                                        std::string_view detail) noexcept
    {
        constexpr const char *ULTRAHAND_DIRECTORY = "sdmc:/config/ultrahand";
        constexpr const char *NOTIFICATION_DIRECTORY = "sdmc:/config/ultrahand/notifications";
        mkdir("sdmc:/config", 0777);
        if ((mkdir(ULTRAHAND_DIRECTORY, 0777) != 0 && errno != EEXIST) ||
            (mkdir(NOTIFICATION_DIRECTORY, 0777) != 0 && errno != EEXIST))
        {
            return;
        }

        std::string message{};
        switch (result)
        {
            case sync::status::Result::Success:
                message = "Backup enviado ao Nextcloud com sucesso.";
                break;
            case sync::status::Result::Queued:
                message = "Backup criado. Envio ao Nextcloud pendente.";
                break;
            case sync::status::Result::Error:
                message = "Falha ao criar o backup automatico.";
                break;
        }
        message += "\nJogo: " + sync::titles::display_name(titleId);
        if (!detail.empty())
        {
            message += "\nDetalhe: ";
            message.append(detail.substr(0, 180));
        }

        json_object *root = json_object_new_object();
        if (!root) { return; }
        json_object_object_add(root, "text", json_object_new_string(message.c_str()));
        json_object_object_add(root, "font_size", json_object_new_int(24));
        json_object_object_add(root, "split_type", json_object_new_string("word"));
        json_object_object_add(root, "alignment", json_object_new_string("left"));
        json_object_object_add(root, "duration", json_object_new_int(6000));
        json_object_object_add(root, "title", json_object_new_string("JKSV Cloud"));
        json_object_object_add(root, "show_time", json_object_new_string("true"));
        json_object_object_add(root, "priority", json_object_new_int(20));

        const std::string destination = std::string{NOTIFICATION_DIRECTORY} +
                                        "/JKSV-Cloud-" + std::to_string(eventId) + ".notify";
        const std::string temporary = destination + ".tmp";
        const bool written = json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PLAIN) == 0;
        json_object_put(root);
        if (!written) { return; }
        std::remove(destination.c_str());
        if (std::rename(temporary.c_str(), destination.c_str()) != 0)
        {
            std::remove(temporary.c_str());
        }
    }

    void write_status(bool force) noexcept
    {
        if (!g_status.initialized || !ensure_directory()) { return; }
        const std::uint64_t now = armGetSystemTick();
        const std::uint64_t frequency = armGetSystemTickFreq();
        if (!force && now >= g_status.lastWriteTick && frequency > 0 &&
            now - g_status.lastWriteTick < HEARTBEAT_INTERVAL_SECONDS * frequency)
        {
            return;
        }

        g_status.heartbeatTick = now;
        json_object *root = json_object_new_object();
        if (!root) { return; }
        json_object_object_add(root, "schemaVersion", json_object_new_int(1));
        json_object_object_add(root, "sysmoduleVersion", json_object_new_string(sync::VERSION));
        json_object_object_add(root, "startedTick", json_object_new_string(std::to_string(g_status.startedTick).c_str()));
        json_object_object_add(root, "heartbeatTick", json_object_new_string(std::to_string(g_status.heartbeatTick).c_str()));
        json_object_object_add(root, "phase", json_object_new_string(phase_name(g_status.phase)));
        json_object_object_add(root, "activeTitleId", json_object_new_string(hex16(g_status.activeTitleId).c_str()));
        json_object_object_add(root, "eventId", json_object_new_string(std::to_string(g_status.eventId).c_str()));
        json_object_object_add(root, "lastResult", json_object_new_string(g_status.lastResult.c_str()));
        json_object_object_add(root, "lastEventTick", json_object_new_string(std::to_string(g_status.lastEventTick).c_str()));
        json_object_object_add(root, "lastSuccessTick", json_object_new_string(std::to_string(g_status.lastSuccessTick).c_str()));
        json_object_object_add(root, "lastTitleId", json_object_new_string(hex16(g_status.lastTitleId).c_str()));
        json_object_object_add(root, "lastSaveId", json_object_new_string(hex16(g_status.lastSaveId).c_str()));
        json_object_object_add(root, "lastBackupName", json_object_new_string(g_status.lastBackupName.c_str()));
        json_object_object_add(root, "lastDetail", json_object_new_string(g_status.lastDetail.c_str()));
        json_object_object_add(root, "pendingCount", json_object_new_int64(g_status.pendingCount));

        const std::string temporary = std::string{sync::STATUS_PATH} + ".tmp";
        const bool written = json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0;
        json_object_put(root);
        if (!written) { return; }
        std::remove(sync::STATUS_PATH);
        if (std::rename(temporary.c_str(), sync::STATUS_PATH) == 0)
        {
            g_status.lastWriteTick = now;
        }
        else
        {
            std::remove(temporary.c_str());
        }
    }
} // namespace

void sync::status::initialize() noexcept
{
    mutexLock(&g_statusMutex);
    if (g_status.initialized)
    {
        mutexUnlock(&g_statusMutex);
        return;
    }
    load_previous_result();
    g_status.startedTick = armGetSystemTick();
    g_status.phase = Phase::Starting;
    g_status.activeTitleId = 0;
    g_status.initialized = true;
    write_status(true);
    mutexUnlock(&g_statusMutex);
}

void sync::status::set_phase(Phase phase, std::uint64_t activeTitleId) noexcept
{
    mutexLock(&g_statusMutex);
    const bool changed = g_status.phase != phase || g_status.activeTitleId != activeTitleId;
    g_status.phase = phase;
    g_status.activeTitleId = activeTitleId;
    write_status(changed);
    mutexUnlock(&g_statusMutex);
}

void sync::status::heartbeat() noexcept
{
    mutexLock(&g_statusMutex);
    write_status(false);
    mutexUnlock(&g_statusMutex);
}

void sync::status::record_result(Result result,
                                 std::uint64_t titleId,
                                 std::uint64_t saveId,
                                 std::string_view backupName,
                                 std::string_view detail) noexcept
{
    mutexLock(&g_statusMutex);
    ++g_status.eventId;
    if (g_status.eventId == 0) { g_status.eventId = 1; }
    g_status.lastResult = result_name(result);
    g_status.lastEventTick = armGetSystemTick();
    if (result == Result::Success) { g_status.lastSuccessTick = g_status.lastEventTick; }
    g_status.lastTitleId = titleId;
    g_status.lastSaveId = saveId;
    g_status.lastBackupName.assign(backupName);
    g_status.lastDetail.assign(detail);
    write_status(true);
    publish_ultrahand_notification(result, titleId, g_status.eventId, detail);
    mutexUnlock(&g_statusMutex);
}

void sync::status::update_detail(std::string_view detail) noexcept
{
    mutexLock(&g_statusMutex);
    if (g_status.lastDetail == detail)
    {
        mutexUnlock(&g_statusMutex);
        return;
    }
    g_status.lastDetail.assign(detail);
    write_status(true);
    if (g_status.lastResult == "queued" && g_status.eventId != 0)
    {
        publish_ultrahand_notification(Result::Queued,
                                       g_status.lastTitleId,
                                       g_status.eventId,
                                       detail);
    }
    mutexUnlock(&g_statusMutex);
}

void sync::status::set_pending_count(std::size_t count) noexcept
{
    mutexLock(&g_statusMutex);
    if (g_status.pendingCount == count)
    {
        mutexUnlock(&g_statusMutex);
        return;
    }
    g_status.pendingCount = count;
    write_status(true);
    mutexUnlock(&g_statusMutex);
}
