#include "data/data.hpp"

#include "appstates/DataLoadingState.hpp"
#include "appstates/FadeState.hpp"
#include "data/DataContext.hpp"
#include "error.hpp"
#include "logging/logger.hpp"
#include "strings/strings.hpp"

#include <cstdio>
#include <json-c/json.h>
#include <switch.h>
#include <sys/stat.h>

namespace
{
    // clang-format off
    // This seems stupid, but it's the only way, really.
    struct StateDataStruct : sys::Task::DataStruct
    {
        bool clearCache{};
    };
    // clang-format on

    data::DataContext s_context{};

    constexpr const char *TITLE_MAP_PATH = "sdmc:/config/JKSV Cloud/title-map.json";

    std::string hex16(std::uint64_t value)
    {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
        return text;
    }

    void write_sync_title_map() noexcept
    {
        data::TitleInfoList titles{};
        s_context.get_title_info_list(titles);

        json_object *root       = json_object_new_object();
        json_object *titleNames = json_object_new_object();
        if (!root || !titleNames)
        {
            if (root) { json_object_put(root); }
            if (titleNames) { json_object_put(titleNames); }
            logger::log("Unable to allocate the JKSV Cloud title map.\n");
            return;
        }

        std::size_t writtenTitles{};
        for (const data::TitleInfo *title : titles)
        {
            if (!title || !title->get_title() || !title->get_title()[0])
            {
                continue;
            }
            const std::string titleId = hex16(title->get_application_id());
            const std::string titleName = title->get_title();
            // TitleInfo uses the hexadecimal ID as a placeholder when NACP
            // metadata is unavailable. Do not publish that placeholder as a
            // resolved name: the sysmodule must keep retrying instead.
            if (titleName == titleId) { continue; }
            json_object_object_add(titleNames,
                                   titleId.c_str(),
                                   json_object_new_string(titleName.c_str()));
            ++writtenTitles;
        }

        json_object_object_add(root, "schemaVersion", json_object_new_int(1));
        json_object_object_add(root, "titles", titleNames);
        ::mkdir("sdmc:/config", 0777);
        ::mkdir("sdmc:/config/JKSV Cloud", 0777);
        const std::string temporary = std::string{TITLE_MAP_PATH} + ".tmp";
        const bool written =
            json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0;
        json_object_put(root);
        if (!written)
        {
            std::remove(temporary.c_str());
            logger::log("Unable to write the JKSV Cloud title map.\n");
            return;
        }
        std::remove(TITLE_MAP_PATH);
        if (std::rename(temporary.c_str(), TITLE_MAP_PATH) != 0)
        {
            std::remove(temporary.c_str());
            logger::log("Unable to publish the JKSV Cloud title map.\n");
            return;
        }
        logger::log("JKSV Cloud title map updated with %zu titles.\n", writtenTitles);
    }
} // namespace

/// @brief The main routine for the task to load data.
static void data_initialize_task(sys::threadpool::JobData taskData);

void data::launch_initialization(bool clearCache, std::function<void()> onDestruction)
{
    auto taskData        = std::make_shared<StateDataStruct>();
    taskData->clearCache = clearCache;

    auto loadingState = DataLoadingState::create(s_context, onDestruction, data_initialize_task, taskData);
    StateManager::push_state(loadingState);
}

void data::get_users(data::UserList &userList) { s_context.get_users(userList); }

data::TitleInfo *data::get_title_info_by_id(uint64_t applicationID) noexcept
{
    return s_context.get_title_by_id(applicationID);
}

void data::load_title_to_map(uint64_t applicationID) { s_context.load_title(applicationID); }

bool data::title_exists_in_map(uint64_t applicationID) noexcept { return s_context.title_is_loaded(applicationID); }

void data::get_title_info_list(data::TitleInfoList &listOut) { s_context.get_title_info_list(listOut); }

void data::get_title_info_by_type(FsSaveDataType saveType, data::TitleInfoList &listOut)
{
    s_context.get_title_info_list_by_type(saveType, listOut);
}

static void data_initialize_task(sys::threadpool::JobData taskData)
{
    auto castData         = std::static_pointer_cast<StateDataStruct>(taskData);
    sys::Task *task       = castData->task;
    const bool clearCache = castData->clearCache;

    if (error::is_null(task)) { return; }
    const char *statusFinalizing = strings::get_by_name(strings::names::DATA_LOADING_STATUS, 6);

    if (clearCache) { s_context.delete_cache(); }
    s_context.read_cache(task);
    s_context.load_application_records(task);
    s_context.import_svi_files(task);
    s_context.load_create_users(task);
    s_context.load_user_save_info(task);
    s_context.write_cache(task);
    write_sync_title_map();

    task->set_status(statusFinalizing); // This is here so at least they know something is happening instead of a freeze.
    task->complete();
}
