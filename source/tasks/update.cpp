#include "tasks/update.hpp"

#include "JKSV.hpp"
#include "appstates/MainMenuState.hpp"
#include "cmdargs.hpp"
#include "curl/curl.hpp"
#include "error.hpp"
#include "fslib.hpp"
#include "json.hpp"
#include "logging/logger.hpp"
#include "remote/remote.hpp"
#include "strings/strings.hpp"
#include "stringutil.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view CURRENT_VERSION      = JKSV_CLOUD_VERSION;
    constexpr const char *URL_GIT_API               = "https://api.github.com/repos/SGMSteeL1/jksv-cloud/releases/latest";
    constexpr std::string_view RELEASE_ASSET_NAME   = "JKSV-Cloud.nro";
    constexpr std::string_view RELEASE_URL_PREFIX   =
        "https://github.com/SGMSteeL1/jksv-cloud/releases/download/";
    constexpr std::array<unsigned char, 4> NRO_MAGIC = {'N', 'R', 'O', '0'};
    constexpr std::size_t NRO_MAGIC_OFFSET           = 0x10;
    constexpr std::size_t NRO_HEADER_READ_SIZE       = NRO_MAGIC_OFFSET + NRO_MAGIC.size();

    struct SemanticVersion
    {
        std::array<std::uint64_t, 3> parts{};
    };

    struct UpdateDownload
    {
        fslib::File *file{};
        sys::ProgressTask *task{};
        std::uint64_t bytesWritten{};
    };

    static bool parse_semantic_version(std::string_view version, SemanticVersion &out) noexcept;
    static bool is_newer_version(std::string_view releaseVersion) noexcept;
    static std::string get_latest_release_json(curl::Handle &handle);
    static bool read_release_asset(json_object *release,
                                   std::string &versionOut,
                                   std::string &urlOut,
                                   std::uint64_t &sizeOut) noexcept;
    static size_t write_update_data(const char *buffer, size_t size, size_t count, UpdateDownload *download) noexcept;
    static bool validate_nro(std::string_view path, std::uint64_t expectedSize) noexcept;
    static bool replace_executable(std::string_view currentPath, std::string_view updatePath) noexcept;
} // namespace

void tasks::update::check_for_update(sys::threadpool::JobData jobData)
{
    auto castData                = std::static_pointer_cast<MainMenuState::DataStruct>(jobData);
    MainMenuState *spawningState = castData->spawningState;
    if (!spawningState || !remote::has_internet_connection()) { return; }

    curl::Handle curlHandle   = curl::new_handle();
    const std::string gitJson = get_latest_release_json(curlHandle);
    if (gitJson.empty()) { return; }

    json::Object parser = json::new_object(json_tokener_parse, gitJson.c_str());
    if (!parser) { return; }

    std::string releaseVersion{};
    std::string downloadUrl{};
    std::uint64_t downloadSize{};
    const bool validRelease = read_release_asset(parser.get(), releaseVersion, downloadUrl, downloadSize);
    if (!validRelease || !is_newer_version(releaseVersion)) { return; }

    castData->updateVersion = std::move(releaseVersion);
    castData->updateUrl     = std::move(downloadUrl);
    castData->updateSize    = downloadSize;
    spawningState->signal_update_found();
}

void tasks::update::download_update(sys::threadpool::JobData jobData)
{
    auto castData           = std::static_pointer_cast<MainMenuState::DataStruct>(jobData);
    sys::ProgressTask *task = static_cast<sys::ProgressTask *>(castData->task);
    if (error::is_null(task)) { return; }

    const char *currentPath = cmdargs::get(0);
    const bool validPath    = currentPath && std::string_view{currentPath}.starts_with("sdmc:/");
    const bool validRelease = !castData->updateUrl.empty() &&
                              castData->updateUrl.starts_with(RELEASE_URL_PREFIX) &&
                              castData->updateSize >= NRO_HEADER_READ_SIZE &&
                              castData->updateSize <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!validPath || !validRelease)
    {
        logger::log("Updater refused an invalid executable path or release asset.");
        TASK_FINISH_RETURN(task);
    }

    const char *downloadingFormat = strings::get_by_name(strings::names::IO_STATUSES, 4);
    std::string status            = stringutil::get_formatted_string(downloadingFormat, RELEASE_ASSET_NAME.data());
    task->set_status(status);
    task->reset(static_cast<double>(castData->updateSize));

    const std::string updatePath = std::string{currentPath} + ".update";
    fslib::File updateFile{updatePath,
                           FsOpenMode_Create | FsOpenMode_Write,
                           static_cast<std::int64_t>(castData->updateSize)};
    if (error::fslib(updateFile.is_open())) { TASK_FINISH_RETURN(task); }

    UpdateDownload download{&updateFile, task, 0};
    curl::Handle downloadCurl = curl::new_handle();
    curl::prepare_get(downloadCurl);
    curl::set_option(downloadCurl, CURLOPT_URL, castData->updateUrl.c_str());
    curl::set_option(downloadCurl, CURLOPT_WRITEFUNCTION, write_update_data);
    curl::set_option(downloadCurl, CURLOPT_WRITEDATA, &download);
    curl::set_option(downloadCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl::set_option(downloadCurl, CURLOPT_MAXREDIRS, 5L);
    curl::set_option(downloadCurl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    curl::set_option(downloadCurl, CURLOPT_FAILONERROR, 1L);
    curl::set_option(downloadCurl, CURLOPT_TIMEOUT, 120L);
    curl::enable_tls_verification(downloadCurl);

    const bool downloaded = curl::perform(downloadCurl) && download.bytesWritten == castData->updateSize;
    updateFile.close();
    if (!downloaded || !validate_nro(updatePath, castData->updateSize))
    {
        logger::log("Updater rejected an incomplete or invalid NRO download.");
        fslib::delete_file(updatePath);
        TASK_FINISH_RETURN(task);
    }

    if (!replace_executable(currentPath, updatePath))
    {
        logger::log("Updater could not replace the running NRO.");
        TASK_FINISH_RETURN(task);
    }

    task->complete();
    JKSV::request_quit();
}

namespace
{
    static bool parse_semantic_version(std::string_view version, SemanticVersion &out) noexcept
    {
        if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) { version.remove_prefix(1); }

        for (std::size_t index = 0; index < out.parts.size(); ++index)
        {
            const std::size_t separator = version.find('.');
            const std::string_view part = separator == std::string_view::npos ? version : version.substr(0, separator);
            if (part.empty()) { return false; }

            std::uint64_t parsed{};
            const auto result = std::from_chars(part.data(), part.data() + part.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != part.data() + part.size()) { return false; }
            out.parts[index] = parsed;

            if (index + 1 < out.parts.size())
            {
                if (separator == std::string_view::npos) { return false; }
                version.remove_prefix(separator + 1);
            }
            else if (separator != std::string_view::npos) { return false; }
        }
        return true;
    }

    static bool is_newer_version(std::string_view releaseVersion) noexcept
    {
        SemanticVersion current{};
        SemanticVersion release{};
        if (!parse_semantic_version(CURRENT_VERSION, current) || !parse_semantic_version(releaseVersion, release))
        {
            return false;
        }
        return release.parts > current.parts;
    }

    static std::string get_latest_release_json(curl::Handle &handle)
    {
        if (!handle) { return {}; }

        std::string response{};
        curl::HeaderList headers = curl::new_header_list();
        curl::append_header(headers, "Accept: application/vnd.github+json");

        curl::prepare_get(handle);
        curl::set_option(handle, CURLOPT_URL, URL_GIT_API);
        curl::set_option(handle, CURLOPT_HTTPHEADER, headers.get());
        curl::set_option(handle, CURLOPT_WRITEFUNCTION, curl::write_response_string);
        curl::set_option(handle, CURLOPT_WRITEDATA, &response);
        curl::set_option(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl::set_option(handle, CURLOPT_TIMEOUT, 15L);
        curl::enable_tls_verification(handle);

        const bool success = curl::perform(handle) && curl::get_response_code(handle) == 200;
        return success ? response : std::string{};
    }

    static bool read_release_asset(json_object *release,
                                   std::string &versionOut,
                                   std::string &urlOut,
                                   std::uint64_t &sizeOut) noexcept
    {
        json_object *tagName{};
        json_object *assets{};
        if (!release || !json_object_object_get_ex(release, "tag_name", &tagName) || !tagName ||
            json_object_get_type(tagName) != json_type_string ||
            !json_object_object_get_ex(release, "assets", &assets) || !assets ||
            json_object_get_type(assets) != json_type_array)
        {
            return false;
        }

        const char *version = json_object_get_string(tagName);
        if (!version || !version[0]) { return false; }

        const std::size_t assetCount = json_object_array_length(assets);
        for (std::size_t index = 0; index < assetCount; ++index)
        {
            json_object *asset = json_object_array_get_idx(assets, index);
            json_object *name{};
            json_object *downloadUrl{};
            json_object *downloadSize{};
            if (!asset || !json_object_object_get_ex(asset, "name", &name) || !name ||
                !json_object_object_get_ex(asset, "browser_download_url", &downloadUrl) || !downloadUrl ||
                !json_object_object_get_ex(asset, "size", &downloadSize) || !downloadSize)
            {
                continue;
            }

            const char *assetName = json_object_get_string(name);
            const char *assetUrl  = json_object_get_string(downloadUrl);
            const std::uint64_t assetSize = json_object_get_uint64(downloadSize);
            if (!assetName || RELEASE_ASSET_NAME != assetName || !assetUrl ||
                !std::string_view{assetUrl}.starts_with(RELEASE_URL_PREFIX) ||
                assetSize < NRO_HEADER_READ_SIZE)
            {
                continue;
            }

            versionOut = version;
            urlOut     = assetUrl;
            sizeOut    = assetSize;
            return true;
        }
        return false;
    }

    static size_t write_update_data(const char *buffer, size_t size, size_t count, UpdateDownload *download) noexcept
    {
        if (!buffer || !download || !download->file || !download->task) { return 0; }

        const size_t byteCount = size * count;
        const ssize_t written  = download->file->write(buffer, byteCount);
        if (written < 0) { return 0; }

        download->bytesWritten += static_cast<std::uint64_t>(written);
        download->task->update_current(static_cast<double>(download->bytesWritten));
        return static_cast<size_t>(written);
    }

    static bool validate_nro(std::string_view path, std::uint64_t expectedSize) noexcept
    {
        const fslib::Path nroPath{path};
        if (fslib::get_file_size(nroPath) != static_cast<std::int64_t>(expectedSize)) { return false; }

        fslib::File nroFile{nroPath, FsOpenMode_Read};
        if (!nroFile.is_open()) { return false; }

        std::array<unsigned char, NRO_HEADER_READ_SIZE> header{};
        const ssize_t bytesRead = nroFile.read(header.data(), header.size());
        if (bytesRead != static_cast<ssize_t>(header.size())) { return false; }

        return std::equal(NRO_MAGIC.begin(),
                          NRO_MAGIC.end(),
                          header.begin() + static_cast<std::ptrdiff_t>(NRO_MAGIC_OFFSET));
    }

    static bool replace_executable(std::string_view currentPath, std::string_view updatePath) noexcept
    {
        const fslib::Path current{currentPath};
        const fslib::Path update{updatePath};
        const fslib::Path backup{std::string{currentPath} + ".bak"};

        if (fslib::file_exists(backup) && !fslib::delete_file(backup)) { return false; }

        error::libnx(romfsExit());
        if (!fslib::rename_file(current, backup))
        {
            JKSV::request_quit();
            return false;
        }

        if (!fslib::rename_file(update, current))
        {
            const bool rolledBack = fslib::rename_file(backup, current);
            logger::log("Updater rollback result: %s.", rolledBack ? "success" : "failure");
            JKSV::request_quit();
            return false;
        }

        fslib::commit_data_to_file_system(current.get_device_name());
        return true;
    }
} // namespace
