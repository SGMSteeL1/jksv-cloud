#include "Config.hpp"
#include "Log.hpp"
#include "MbedWebDav.hpp"
#include "Status.hpp"
#include "Sync.hpp"
#include "TitleName.hpp"
#include "Vault.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <cstdlib>
#include <dirent.h>
#include <json-c/json.h>
#include <memory>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#include <string>
#include <string_view>
#include <switch.h>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr const char *SAVE_MOUNT = "jksv_sync";
    constexpr const char *SAVE_ROOT = "jksv_sync:/";
    constexpr const char *SAVE_META_NAME = ".nx_save_meta.bin";
    constexpr std::uint32_t SAVE_META_MAGIC = 0x56534B4A;
    constexpr std::size_t IO_BUFFER_SIZE = 0x10000;
    std::atomic_bool g_networkReady{};
    std::atomic_bool g_cryptoReady{};
    Mutex g_stateMutex{};
    std::string g_lastLoggedUploadFailure{};

    struct UploadResult
    {
        bool success{};
        std::string detail{};
    };

    struct SaveMetaData
    {
        std::uint32_t magic{};
        std::uint8_t revision{};
        std::uint64_t applicationID{};
        AccountUid accountID{};
        std::uint64_t systemSaveID{};
        std::uint8_t saveDataType{};
        std::uint8_t saveDataRank{};
        std::uint16_t saveDataIndex{};
        std::uint64_t ownerID{};
        std::uint64_t timestamp{};
        std::uint32_t flags{};
        std::int64_t saveDataSize{};
        std::int64_t journalSize{};
        std::uint64_t commitID{};
        std::uint8_t saveDataSpaceID{};
    } __attribute__((packed));

    static_assert(sizeof(SaveMetaData) == 86, "JKSV save metadata layout changed");

    struct SaveState
    {
        std::uint64_t titleId{};
        std::uint64_t uploadedCommit{};
        std::uint64_t pendingCommit{};
        std::string pendingPath{};
        std::string remoteName{};
    };

    using StateMap = std::unordered_map<std::uint64_t, SaveState>;

    class StateLock
    {
        public:
            StateLock() noexcept { mutexLock(&g_stateMutex); }
            ~StateLock() { mutexUnlock(&g_stateMutex); }
            StateLock(const StateLock &) = delete;
            StateLock &operator=(const StateLock &) = delete;
    };

    std::size_t pending_count(const StateMap &state) noexcept
    {
        return static_cast<std::size_t>(std::count_if(state.begin(), state.end(), [](const auto &entry) {
            return entry.second.pendingCommit != 0 && !entry.second.pendingPath.empty();
        }));
    }

    std::string hex16(std::uint64_t value)
    {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
        return text;
    }

    // URL-encode one WebDAV path segment. This is local to Sync.cpp because
    // the helper in MbedWebDav.cpp has internal linkage and cannot be reused.
    std::string encode_path_segment(std::string_view value)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string encoded{};
        encoded.reserve(value.size() * 3);
        for (const unsigned char character : value)
        {
            const bool safe = (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') ||
                              character == '-' || character == '_' || character == '.' ||
                              character == '~';
            if (safe)
            {
                encoded.push_back(static_cast<char>(character));
            }
            else
            {
                encoded.push_back('%');
                encoded.push_back(hex[character >> 4]);
                encoded.push_back(hex[character & 0x0F]);
            }
        }
        return encoded;
    }

    UploadResult upload_failure(std::string detail)
    {
        return {false, std::move(detail)};
    }

    UploadResult request_failure(const char *operation, CURLcode result, long response)
    {
        char detail[256]{};
        if (result != CURLE_OK)
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "%s: CURL %d (%s)",
                          operation,
                          static_cast<int>(result),
                          curl_easy_strerror(result));
        }
        else
        {
            std::snprintf(detail, sizeof(detail), "%s: HTTP %ld", operation, response);
        }
        return upload_failure(detail);
    }

    void report_upload_failure(std::string_view detail) noexcept
    {
        sync::status::update_detail(detail);
        if (detail != g_lastLoggedUploadFailure)
        {
            sync::log::write("Upload pending: %.*s",
                             static_cast<int>(detail.size()),
                             detail.data());
            g_lastLoggedUploadFailure.assign(detail);
        }
    }

    void clear_upload_failure() noexcept
    {
        g_lastLoggedUploadFailure.clear();
    }

    bool parse_hex(const char *text, std::uint64_t &valueOut) noexcept
    {
        if (!text || !text[0]) { return false; }
        char *end{};
        errno = 0;
        const unsigned long long value = std::strtoull(text, &end, 16);
        if (errno != 0 || end == text || *end != '\0') { return false; }
        valueOut = static_cast<std::uint64_t>(value);
        return true;
    }

    bool ensure_directory(std::string_view directory) noexcept
    {
        if (directory.empty()) { return false; }
        std::string path{directory};
        while (path.size() > 6 && path.back() == '/') { path.pop_back(); }

        std::size_t position = path.find(":/");
        position = position == std::string::npos ? 0 : position + 2;
        while ((position = path.find('/', position)) != std::string::npos)
        {
            const std::string parent = path.substr(0, position);
            if (!parent.empty() && mkdir(parent.c_str(), 0777) != 0 && errno != EEXIST) { return false; }
            ++position;
        }
        return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
    }

    bool file_exists(std::string_view path) noexcept
    {
        struct stat info{};
        const std::string value{path};
        return stat(value.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }

    bool archive_has_save_files(std::string_view path) noexcept
    {
        const std::string value{path};
        unzFile archive = unzOpen64(value.c_str());
        if (!archive) { return false; }

        bool found{};
        int result = unzGoToFirstFile(archive);
        while (result == UNZ_OK)
        {
            std::array<char, FS_MAX_PATH> filename{};
            unz_file_info64 info{};
            if (unzGetCurrentFileInfo64(archive,
                                        &info,
                                        filename.data(),
                                        filename.size(),
                                        nullptr,
                                        0,
                                        nullptr,
                                        0) != UNZ_OK)
            {
                found = false;
                break;
            }
            const std::string_view name{filename.data()};
            const bool directory = !name.empty() && name.back() == '/';
            if (!directory && name != SAVE_META_NAME)
            {
                found = true;
                break;
            }
            result = unzGoToNextFile(archive);
        }
        unzClose(archive);
        return found;
    }

    bool read_json_string(json_object *object, const char *key, std::string &out) noexcept
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

    std::uint64_t read_json_hex(json_object *object, const char *key) noexcept
    {
        std::string text{};
        std::uint64_t value{};
        return read_json_string(object, key, text) && parse_hex(text.c_str(), value) ? value : 0;
    }

    StateMap load_state() noexcept
    {
        StateMap state{};
        json_object *root = json_object_from_file(sync::STATE_PATH);
        if (!root)
        {
            const std::string backup = std::string{sync::STATE_PATH} + ".bak";
            root = json_object_from_file(backup.c_str());
            if (root) { sync::log::write("Recovered the synchronization queue from its backup state file."); }
        }
        json_object *saves{};
        if (!root || !json_object_object_get_ex(root, "saves", &saves) ||
            json_object_get_type(saves) != json_type_object)
        {
            if (root) { json_object_put(root); }
            return state;
        }

        json_object_object_foreach(saves, key, value)
        {
            std::uint64_t saveId{};
            if (!parse_hex(key, saveId) || !value) { continue; }
            SaveState record{};
            record.titleId = read_json_hex(value, "titleId");
            record.uploadedCommit = read_json_hex(value, "uploadedCommit");
            record.pendingCommit = read_json_hex(value, "pendingCommit");
            read_json_string(value, "pendingPath", record.pendingPath);
            read_json_string(value, "remoteName", record.remoteName);
            state[saveId] = std::move(record);
        }
        json_object_put(root);
        return state;
    }

    bool save_state(const StateMap &state) noexcept
    {
        if (!ensure_directory(sync::CONFIG_DIRECTORY)) { return false; }

        json_object *root = json_object_new_object();
        json_object *saves = json_object_new_object();
        if (!root || !saves)
        {
            if (root) { json_object_put(root); }
            if (saves) { json_object_put(saves); }
            return false;
        }

        json_object_object_add(root, "version", json_object_new_int(1));
        json_object_object_add(root, "saves", saves);
        for (const auto &[saveId, record] : state)
        {
            json_object *entry = json_object_new_object();
            json_object_object_add(entry, "titleId", json_object_new_string(hex16(record.titleId).c_str()));
            json_object_object_add(entry,
                                   "uploadedCommit",
                                   json_object_new_string(hex16(record.uploadedCommit).c_str()));
            json_object_object_add(entry,
                                   "pendingCommit",
                                   json_object_new_string(hex16(record.pendingCommit).c_str()));
            json_object_object_add(entry, "pendingPath", json_object_new_string(record.pendingPath.c_str()));
            json_object_object_add(entry, "remoteName", json_object_new_string(record.remoteName.c_str()));
            json_object_object_add(saves, hex16(saveId).c_str(), entry);
        }

        const std::string temporary = std::string{sync::STATE_PATH} + ".tmp";
        const std::string backup = std::string{sync::STATE_PATH} + ".bak";
        const bool written = json_object_to_file_ext(temporary.c_str(), root, JSON_C_TO_STRING_PRETTY) == 0;
        json_object_put(root);
        if (!written) { return false; }

        const bool hadCurrentState = file_exists(sync::STATE_PATH);
        std::remove(backup.c_str());
        if (hadCurrentState && std::rename(sync::STATE_PATH, backup.c_str()) != 0)
        {
            std::remove(temporary.c_str());
            return false;
        }
        if (std::rename(temporary.c_str(), sync::STATE_PATH) == 0)
        {
            std::remove(backup.c_str());
            return true;
        }

        if (hadCurrentState) { std::rename(backup.c_str(), sync::STATE_PATH); }
        std::remove(temporary.c_str());
        return false;
    }

    bool persist_upload_completion(StateMap &state, SaveState &record) noexcept
    {
        SaveState pendingRecord = record;
        record.uploadedCommit = record.pendingCommit;
        record.pendingCommit = 0;
        record.pendingPath.clear();
        record.remoteName.clear();
        if (save_state(state)) { return true; }

        record = std::move(pendingRecord);
        return false;
    }

    zip_fileinfo make_zip_file_info() noexcept
    {
        zip_fileinfo info{};
        info.tmz_date.tm_mday = 1;
        info.tmz_date.tm_year = 1980;
        return info;
    }

    bool open_zip_entry(zipFile archive, std::string_view name) noexcept
    {
        const std::string filename{name};
        const zip_fileinfo info = make_zip_file_info();
        return zipOpenNewFileInZip64(archive,
                                     filename.c_str(),
                                     &info,
                                     nullptr,
                                     0,
                                     nullptr,
                                     0,
                                     nullptr,
                                     Z_DEFLATED,
                                     6,
                                     0) == ZIP_OK;
    }

    bool add_buffer_to_zip(zipFile archive,
                           std::string_view name,
                           const void *data,
                           std::size_t size) noexcept
    {
        if (!open_zip_entry(archive, name)) { return false; }
        const bool written = size == 0 || zipWriteInFileInZip(archive, data, size) == ZIP_OK;
        const bool closed = zipCloseFileInZip(archive) == ZIP_OK;
        return written && closed;
    }

    bool add_file_to_zip(zipFile archive,
                         const std::string &sourcePath,
                         const std::string &archivePath,
                         std::size_t &fileCount,
                         std::uint64_t &byteCount) noexcept
    {
        std::FILE *source = std::fopen(sourcePath.c_str(), "rb");
        if (!source || !open_zip_entry(archive, archivePath))
        {
            if (source) { std::fclose(source); }
            return false;
        }

        std::array<unsigned char, IO_BUFFER_SIZE> buffer{};
        bool success = true;
        while (true)
        {
            const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), source);
            if (read > 0 && zipWriteInFileInZip(archive, buffer.data(), read) != ZIP_OK)
            {
                success = false;
                break;
            }
            if (read < buffer.size())
            {
                if (std::ferror(source)) { success = false; }
                break;
            }
        }
        std::fclose(source);
        if (zipCloseFileInZip(archive) != ZIP_OK) { success = false; }
        if (success)
        {
            struct stat info{};
            if (stat(sourcePath.c_str(), &info) == 0 && info.st_size >= 0)
            {
                ++fileCount;
                byteCount += static_cast<std::uint64_t>(info.st_size);
            }
            else
            {
                success = false;
            }
        }
        return success;
    }

    bool add_directory_to_zip(zipFile archive,
                              const std::string &sourceDirectory,
                              const std::string &relativeDirectory,
                              std::size_t &fileCount,
                              std::uint64_t &byteCount) noexcept
    {
        DIR *directory = opendir(sourceDirectory.c_str());
        if (!directory) { return false; }

        bool success = true;
        while (success)
        {
            errno = 0;
            dirent *entry = readdir(directory);
            if (!entry)
            {
                if (errno != 0) { success = false; }
                break;
            }
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) { continue; }

            const std::string sourcePath = sourceDirectory + entry->d_name;
            const std::string relativePath = relativeDirectory + entry->d_name;
            struct stat info{};
            if (stat(sourcePath.c_str(), &info) != 0)
            {
                success = false;
                break;
            }

            if (S_ISDIR(info.st_mode))
            {
                const std::string directoryEntry = relativePath + "/";
                if (!add_buffer_to_zip(archive, directoryEntry, nullptr, 0) ||
                    !add_directory_to_zip(archive,
                                          sourcePath + "/",
                                          directoryEntry,
                                          fileCount,
                                          byteCount))
                {
                    success = false;
                    break;
                }
            }
            else if (S_ISREG(info.st_mode) &&
                     !add_file_to_zip(archive,
                                      sourcePath,
                                      relativePath,
                                      fileCount,
                                      byteCount))
            {
                success = false;
                break;
            }
        }
        closedir(directory);
        return success;
    }

    bool read_extra_data(const FsSaveDataInfo &saveInfo, FsSaveDataExtraData &extraOut) noexcept
    {
        return R_SUCCEEDED(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
            &extraOut,
            sizeof(extraOut),
            static_cast<FsSaveDataSpaceId>(saveInfo.save_data_space_id),
            saveInfo.save_data_id));
    }

    SaveMetaData make_save_meta(const FsSaveDataInfo &saveInfo,
                                const FsSaveDataExtraData &extra) noexcept
    {
        SaveMetaData meta{};
        meta.magic = SAVE_META_MAGIC;
        meta.revision = 0x01;
        meta.applicationID = extra.attr.application_id;
        meta.accountID = extra.attr.uid;
        meta.systemSaveID = extra.attr.system_save_data_id;
        meta.saveDataType = extra.attr.save_data_type;
        meta.saveDataRank = extra.attr.save_data_rank;
        meta.saveDataIndex = extra.attr.save_data_index;
        meta.ownerID = extra.owner_id;
        meta.timestamp = extra.timestamp;
        meta.flags = extra.flags;
        meta.saveDataSize = extra.data_size;
        meta.journalSize = extra.journal_size;
        meta.commitID = extra.commit_id;
        meta.saveDataSpaceID = saveInfo.save_data_space_id;
        return meta;
    }

    bool create_archive(const FsSaveDataInfo &saveInfo,
                        const FsSaveDataExtraData &extra,
                        const std::string &destination,
                        bool &emptySaveOut) noexcept
    {
        emptySaveOut = false;
        const FsSaveDataAttribute attribute = {
            .application_id = saveInfo.application_id,
            .uid = saveInfo.uid,
            .system_save_data_id = saveInfo.system_save_data_id,
            .save_data_type = saveInfo.save_data_type,
            .save_data_rank = saveInfo.save_data_rank,
            .save_data_index = saveInfo.save_data_index,
        };

        FsFileSystem saveFileSystem{};
        Result result = fsOpenReadOnlySaveDataFileSystem(
            &saveFileSystem,
            static_cast<FsSaveDataSpaceId>(saveInfo.save_data_space_id),
            &attribute);
        if (R_FAILED(result))
        {
            sync::log::result("Open read-only save", result);
            return false;
        }

        if (fsdevMountDevice(SAVE_MOUNT, saveFileSystem) < 0)
        {
            sync::log::write("Could not mount save %016llX.",
                             static_cast<unsigned long long>(saveInfo.save_data_id));
            return false;
        }

        zipFile archive = zipOpen64(destination.c_str(), APPEND_STATUS_CREATE);
        bool success = archive != nullptr;
        std::size_t fileCount{};
        std::uint64_t byteCount{};
        if (success)
        {
            const SaveMetaData meta = make_save_meta(saveInfo, extra);
            success = add_buffer_to_zip(archive, SAVE_META_NAME, &meta, sizeof(meta)) &&
                      add_directory_to_zip(archive, SAVE_ROOT, "", fileCount, byteCount);
            if (zipClose(archive, nullptr) != ZIP_OK) { success = false; }
        }
        fsdevUnmountDevice(SAVE_MOUNT);

        if (success && fileCount == 0)
        {
            sync::log::write("Mounted save %016llX contains no files; skipping this save.",
                             static_cast<unsigned long long>(saveInfo.save_data_id));
            emptySaveOut = true;
            success = false;
        }
        if (!success) { std::remove(destination.c_str()); }
        else
        {
            sync::log::write("Archived %zu save files (%llu uncompressed bytes).",
                             fileCount,
                             static_cast<unsigned long long>(byteCount));
        }
        return success;
    }

    std::string make_identity(const FsSaveDataInfo &saveInfo)
    {
        if (saveInfo.save_data_type == FsSaveDataType_Device) { return "device"; }
        return "user-" + hex16(saveInfo.uid.uid[0]) + hex16(saveInfo.uid.uid[1]);
    }

    std::string make_archive_name(std::uint64_t titleId, const FsSaveDataInfo &saveInfo)
    {
        return sync::titles::path_name(titleId) + " - " + make_identity(saveInfo) + " - " +
               hex16(saveInfo.save_data_id) + " - T" + hex16(armGetSystemTick()) + ".zip";
    }

    std::size_t discard_response(char *, std::size_t size, std::size_t count, void *) noexcept
    {
        return size * count;
    }

    int refresh_heartbeat(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
    {
        sync::status::heartbeat();
        return 0;
    }

    void prepare_request(CURL *handle,
                         const sync::Credentials &credentials,
                         const std::string &url,
                         curl_slist *resolveList) noexcept
    {
        curl_easy_reset(handle);
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        // This Switch port of libcurl is built without IPv6 support (hostip4 only).
        // Keep it explicit so DNS results cannot select an unusable address family.
        curl_easy_setopt(handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "JKSV-Cloud-Sync/1.0.1");
        if (resolveList) { curl_easy_setopt(handle, CURLOPT_RESOLVE, resolveList); }
        curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(handle, CURLOPT_USERNAME, credentials.loginName.c_str());
        curl_easy_setopt(handle, CURLOPT_PASSWORD, credentials.appPassword.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discard_response);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, refresh_heartbeat);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(handle, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_CAINFO, sync::CA_BUNDLE);
#ifdef CURLOPT_PROTOCOLS_STR
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    }

    UploadResult create_remote_directory(CURL *handle,
                                         const sync::Credentials &credentials,
                                         const std::string &url,
                                         const char *operation,
                                         curl_slist *resolveList) noexcept
    {
        std::string collectionUrl = url;
        if (collectionUrl.empty() || collectionUrl.back() != '/') { collectionUrl.push_back('/'); }
        sync::log::write("%s started.", operation);
        prepare_request(handle, credentials, collectionUrl, resolveList);
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "MKCOL");
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, 0L);
        const CURLcode result = curl_easy_perform(handle);
        long response{};
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response);
        sync::log::write("%s finished: CURL %d, HTTP %ld.",
                         operation,
                         static_cast<int>(result),
                         response);
        if (result == CURLE_OK && (response == 201 || response == 405)) { return {true, {}}; }
        return request_failure(operation, result, response);
    }

    std::string base_url(const sync::Credentials &credentials)
    {
        std::string server = credentials.server;
        while (!server.empty() && server.back() == '/') { server.pop_back(); }
        std::string path = credentials.basePath;
        while (!path.empty() && path.front() == '/') { path.erase(path.begin()); }
        while (!path.empty() && path.back() == '/') { path.pop_back(); }
        return server + "/" + path;
    }

    curl_slist *make_resolve_list(const sync::Credentials &credentials) noexcept
    {
        if (credentials.resolvedAddress.empty()) { return nullptr; }
        constexpr std::string_view scheme{"https://"};
        if (!credentials.server.starts_with(scheme)) { return nullptr; }
        const std::size_t authorityBegin = scheme.size();
        const std::size_t authorityEnd = credentials.server.find('/', authorityBegin);
        std::string authority = credentials.server.substr(
            authorityBegin,
            authorityEnd == std::string::npos ? std::string::npos : authorityEnd - authorityBegin);
        if (authority.empty() || authority.find('@') != std::string::npos) { return nullptr; }

        std::string host = authority;
        std::string port{"443"};
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos)
        {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        }
        if (host.empty() || port.empty()) { return nullptr; }
        const std::string entry = host + ":" + port + ":" + credentials.resolvedAddress;
        return curl_slist_append(nullptr, entry.c_str());
    }

    [[maybe_unused]] UploadResult upload_archive(const sync::Credentials &credentials,
                                std::uint64_t titleId,
                                const std::string &sourcePath,
                                const std::string &remoteName) noexcept
    {
        if (!g_networkReady) { return upload_failure("Servicos de rede indisponiveis."); }
        if (!file_exists(sourcePath)) { return upload_failure("ZIP pendente nao encontrado no SD."); }
        if (credentials.resolvedAddress.empty())
        {
            return upload_failure(
                "Endereco IPv4 do servidor ausente; reconecte o Nextcloud no JKSV Cloud.");
        }

        NifmInternetConnectionType type{};
        std::uint32_t strength{};
        NifmInternetConnectionStatus status{};
        const Result connectionResult = nifmGetInternetConnectionStatus(&type, &strength, &status);
        if (R_FAILED(connectionResult))
        {
            char detail[96]{};
            std::snprintf(detail,
                          sizeof(detail),
                          "Consulta de rede falhou: 0x%08X.",
                          static_cast<unsigned int>(connectionResult));
            return upload_failure(detail);
        }
        if (status != NifmInternetConnectionStatus_Connected)
        {
            return upload_failure("Console sem conexao com a internet.");
        }

        CURL *handle = curl_easy_init();
        if (!handle) { return upload_failure("Inicializacao HTTP falhou."); }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolveList(
            make_resolve_list(credentials), curl_slist_free_all);
        if (!resolveList)
        {
            curl_easy_cleanup(handle);
            return upload_failure("Nao foi possivel preparar o endereco IPv4 do Nextcloud.");
        }
        const std::string syncDirectory = base_url(credentials) + "/Auto%20Sync";
        // The Title ID is an internal identifier only.  Use the same readable,
        // path-safe name for the remote directory that is used locally and in
        // notifications.  Keep the ID as the final fallback inside
        // titles::path_name() so a missing title map can never lose a backup.
        const std::string titleDirectory = syncDirectory + "/" +
                                            encode_path_segment(sync::titles::path_name(titleId));
        const UploadResult syncDirectoryResult = create_remote_directory(
            handle, credentials, syncDirectory, "Criar pasta Auto Sync", resolveList.get());
        if (!syncDirectoryResult.success)
        {
            curl_easy_cleanup(handle);
            return syncDirectoryResult;
        }
        const UploadResult titleDirectoryResult = create_remote_directory(
            handle, credentials, titleDirectory, "Criar pasta do jogo", resolveList.get());
        if (!titleDirectoryResult.success)
        {
            curl_easy_cleanup(handle);
            return titleDirectoryResult;
        }

        char *escaped = curl_easy_escape(handle, remoteName.c_str(), remoteName.size());
        if (!escaped)
        {
            curl_easy_cleanup(handle);
            return upload_failure("Codificacao do nome do ZIP falhou.");
        }
        const std::string uploadUrl = titleDirectory + "/" + escaped;
        curl_free(escaped);

        std::FILE *source = std::fopen(sourcePath.c_str(), "rb");
        if (!source)
        {
            curl_easy_cleanup(handle);
            return upload_failure("Nao foi possivel abrir o ZIP pendente.");
        }
        std::fseek(source, 0, SEEK_END);
        const long fileSize = std::ftell(source);
        std::rewind(source);
        if (fileSize < 0)
        {
            std::fclose(source);
            curl_easy_cleanup(handle);
            return upload_failure("Nao foi possivel medir o ZIP pendente.");
        }

        prepare_request(handle, credentials, uploadUrl, resolveList.get());
        curl_slist *headers = curl_slist_append(nullptr, "Expect:");
        if (headers) { curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers); }
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(handle, CURLOPT_READDATA, source);
        curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(fileSize));
        curl_easy_setopt(handle, CURLOPT_UPLOAD_BUFFERSIZE, static_cast<long>(IO_BUFFER_SIZE));
        sync::log::write("Uploading ZIP (%ld bytes).", fileSize);
        const CURLcode result = curl_easy_perform(handle);
        long response{};
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response);
        sync::log::write("Upload request finished: CURL %d, HTTP %ld.",
                         static_cast<int>(result),
                         response);
        if (headers) { curl_slist_free_all(headers); }
        std::fclose(source);
        curl_easy_cleanup(handle);
        if (result == CURLE_OK && (response == 200 || response == 201 || response == 204))
        {
            return {true, {}};
        }
        return request_failure("Enviar ZIP", result, response);
    }

    void finish_local_file(const sync::Settings &settings,
                           std::uint64_t titleId,
                           const std::string &sourcePath,
                           const std::string &remoteName) noexcept
    {
        if (!settings.keepLocalCopies)
        {
            if (std::remove(sourcePath.c_str()) != 0)
            {
                sync::log::write("Uploaded backup could not be removed from the queue: %s", sourcePath.c_str());
            }
            return;
        }

            const std::string destinationDirectory = std::string{sync::LOCAL_ROOT} + "/" +
                                                     sync::titles::path_name(titleId);
        if (!ensure_directory(destinationDirectory)) { return; }
        const std::string destination = destinationDirectory + "/" + remoteName;
        std::remove(destination.c_str());
        if (std::rename(sourcePath.c_str(), destination.c_str()) != 0)
        {
            sync::log::write("Uploaded backup could not be moved to local history: %s", sourcePath.c_str());
        }
    }

    std::vector<FsSaveDataInfo> enumerate_title_saves(std::uint64_t titleId)
    {
        constexpr std::array<FsSaveDataSpaceId, 6> SPACES = {
            FsSaveDataSpaceId_System,
            FsSaveDataSpaceId_User,
            FsSaveDataSpaceId_SdSystem,
            FsSaveDataSpaceId_Temporary,
            FsSaveDataSpaceId_SdUser,
            FsSaveDataSpaceId_SafeMode,
        };

        std::vector<FsSaveDataInfo> saves{};
        std::unordered_set<std::uint64_t> seen{};
        FsSaveDataFilter filter{};
        filter.filter_by_application_id = true;
        filter.attr.application_id = titleId;

        for (const FsSaveDataSpaceId space : SPACES)
        {
            FsSaveDataInfoReader reader{};
            if (R_FAILED(fsOpenSaveDataInfoReaderWithFilter(&reader, space, &filter))) { continue; }

            while (true)
            {
                std::array<FsSaveDataInfo, 32> buffer{};
                s64 count{};
                const Result result = fsSaveDataInfoReaderRead(&reader, buffer.data(), buffer.size(), &count);
                if (R_FAILED(result) || count <= 0) { break; }

                for (s64 index = 0; index < count; ++index)
                {
                    const FsSaveDataInfo &info = buffer[static_cast<std::size_t>(index)];
                    const bool supported = info.application_id == titleId &&
                                           (info.save_data_type == FsSaveDataType_Account ||
                                            info.save_data_type == FsSaveDataType_Device);
                    if (supported && seen.insert(info.save_data_id).second) { saves.push_back(info); }
                }
            }
            fsSaveDataInfoReaderClose(&reader);
        }
        return saves;
    }
} // namespace

void sync::set_runtime_capabilities(bool networkReady, bool cryptoReady) noexcept
{
    g_networkReady = networkReady;
    g_cryptoReady = cryptoReady;
}

std::uint64_t sync::get_running_application() noexcept
{
    if (sync::is_app_active()) { return 0; }

    u64 processId{};
    Result result = pglGetApplicationProcessId(&processId);
    if (R_FAILED(result)) { result = pmdmntGetApplicationProcessId(&processId); }
    if (R_FAILED(result) || processId == 0) { return 0; }

    u64 programId{};
    result = pminfoGetProgramId(&programId, processId);
    if (R_FAILED(result)) { result = pmdmntGetProgramId(&programId, processId); }
    if (R_FAILED(result) || programId == sync::PROGRAM_ID) { return 0; }
    return programId;
}

void sync::retry_pending_uploads() noexcept
{
    if (!g_cryptoReady)
    {
        report_upload_failure("Servico de criptografia indisponivel.");
        return;
    }
    Credentials credentials{};
    if (!load_credentials(credentials))
    {
        report_upload_failure("Credenciais do Nextcloud indisponiveis; reconecte a conta.");
        return;
    }
    const Settings settings = load_settings();

    while (true)
    {
        std::uint64_t saveId{};
        SaveState job{};
        {
            StateLock lock{};
            StateMap state = load_state();
            status::set_pending_count(pending_count(state));
            bool changed{};
            for (auto &[candidateId, record] : state)
            {
                if (record.pendingCommit == 0 || record.pendingPath.empty() || record.remoteName.empty())
                {
                    continue;
                }
                if (!file_exists(record.pendingPath))
                {
                    log::write("Pending file for save %016llX is missing; clearing its queue record.",
                               static_cast<unsigned long long>(candidateId));
                    record.pendingCommit = 0;
                    record.pendingPath.clear();
                    record.remoteName.clear();
                    changed = true;
                    continue;
                }
                if (!archive_has_save_files(record.pendingPath))
                {
                    log::write("Pending ZIP for save %016llX is invalid or contains only metadata; discarding it.",
                               static_cast<unsigned long long>(candidateId));
                    status::record_result(status::Result::Error,
                                          record.titleId,
                                          candidateId,
                                          record.remoteName,
                                          "ZIP pendente invalido; feche o jogo novamente para recriar o backup.");
                    std::remove(record.pendingPath.c_str());
                    record.pendingCommit = 0;
                    record.pendingPath.clear();
                    record.remoteName.clear();
                    changed = true;
                    continue;
                }
                saveId = candidateId;
                job = record;
                break;
            }
            if (changed && !save_state(state))
            {
                log::write("Could not save sync state after cleaning the queue.");
                return;
            }
            status::set_pending_count(pending_count(state));
        }

        if (saveId == 0) { return; }

        UploadResult upload{};
        if (!g_networkReady)
        {
            upload = upload_failure("Servicos de rede indisponiveis.");
        }
        else
        {
            webdav::Result result = webdav::upload(credentials,
                                                   job.titleId,
                                                   job.pendingPath,
                                                   job.remoteName);
            upload = {result.success, std::move(result.detail)};
        }
        if (!upload.success)
        {
            report_upload_failure(upload.detail);
            return;
        }

        clear_upload_failure();
        bool committed{};
        {
            StateLock lock{};
            StateMap state = load_state();
            const auto found = state.find(saveId);
            if (found == state.end() || found->second.pendingPath != job.pendingPath ||
                found->second.remoteName != job.remoteName)
            {
                log::write("Upload completed, but queue entry %016llX changed; preserving local ZIP.",
                           static_cast<unsigned long long>(saveId));
                return;
            }
            committed = persist_upload_completion(state, found->second);
            status::set_pending_count(pending_count(state));
        }
        if (!committed)
        {
            constexpr std::string_view detail =
                "Upload concluido, mas a fila nao foi salva; ZIP local preservado para nova tentativa.";
            log::write("Upload succeeded but queue state persistence failed for save %016llX; keeping the ZIP.",
                       static_cast<unsigned long long>(saveId));
            status::record_result(status::Result::Queued,
                                  job.titleId,
                                  saveId,
                                  job.remoteName,
                                  detail);
            report_upload_failure(detail);
            return;
        }

        log::write("Queued save %016llX uploaded for title %016llX.",
                   static_cast<unsigned long long>(saveId),
                   static_cast<unsigned long long>(job.titleId));
        status::record_result(status::Result::Success,
                              job.titleId,
                              saveId,
                              job.remoteName,
                              {});
        finish_local_file(settings, job.titleId, job.pendingPath, job.remoteName);
    }
}

void sync::synchronize_title(std::uint64_t titleId) noexcept
{
    if (titleId == 0) { return; }
    const Settings settings = load_settings();
    if (!settings.enabled || !settings.syncOnGameClose) { return; }
    StateLock stateLock{};

    const std::vector<FsSaveDataInfo> saves = enumerate_title_saves(titleId);
    if (saves.empty())
    {
        log::write("No Account or Device save found for title %016llX.",
                   static_cast<unsigned long long>(titleId));
        return;
    }

    StateMap state = load_state();
    status::set_pending_count(pending_count(state));
    for (const FsSaveDataInfo &saveInfo : saves)
    {
        FsSaveDataExtraData extra{};
        if (!read_extra_data(saveInfo, extra))
        {
            log::write("Could not read commit metadata for save %016llX.",
                       static_cast<unsigned long long>(saveInfo.save_data_id));
            continue;
        }

        const std::uint64_t changeToken = extra.commit_id != 0
                                              ? extra.commit_id
                                              : (extra.timestamp != 0 ? extra.timestamp : 1);
        SaveState &record = state[saveInfo.save_data_id];
        record.titleId = titleId;

        std::string supersededPath{};
        if (record.pendingCommit != 0 && file_exists(record.pendingPath))
        {
            supersededPath = record.pendingPath;
            log::write("Save %016llX has an older queued backup; creating a fresh replacement.",
                       static_cast<unsigned long long>(saveInfo.save_data_id));
        }
        else if (record.pendingCommit != 0)
        {
            record.pendingCommit = 0;
            record.pendingPath.clear();
            record.remoteName.clear();
        }
        if (supersededPath.empty() && changeToken != 0 && record.uploadedCommit == changeToken)
        {
            log::write("Save %016llX is unchanged; upload skipped.",
                       static_cast<unsigned long long>(saveInfo.save_data_id));
            continue;
        }

        const std::string titleDirectory = std::string{QUEUE_ROOT} + "/" +
                                           titles::path_name(titleId);
        if (!ensure_directory(titleDirectory))
        {
            log::write("Could not create queue directory for title %016llX.",
                       static_cast<unsigned long long>(titleId));
            continue;
        }

        const std::string remoteName = make_archive_name(titleId, saveInfo);
        const std::string archivePath = titleDirectory + "/" + remoteName;
        status::set_phase(status::Phase::BackingUp, titleId);
        log::write("Creating read-only backup for save %016llX.",
                   static_cast<unsigned long long>(saveInfo.save_data_id));
        bool emptySave{};
        if (!create_archive(saveInfo, extra, archivePath, emptySave))
        {
            if (emptySave) { continue; }
            log::write("Backup creation failed for save %016llX.",
                       static_cast<unsigned long long>(saveInfo.save_data_id));
            status::record_result(status::Result::Error,
                                  titleId,
                                  saveInfo.save_data_id,
                                  remoteName,
                                  "Falha ao criar o ZIP no cartao SD.");
            continue;
        }

        record.pendingCommit = changeToken;
        record.pendingPath = archivePath;
        record.remoteName = remoteName;
        if (!save_state(state))
        {
            log::write("Could not persist the queue state; keeping the archive on SD.");
            continue;
        }
        if (!supersededPath.empty() && supersededPath != archivePath)
        {
            std::remove(supersededPath.c_str());
        }

        status::set_pending_count(pending_count(state));
        log::write("Save %016llX backed up locally and queued for the upload worker.",
                   static_cast<unsigned long long>(saveInfo.save_data_id));
        status::record_result(status::Result::Queued,
                              titleId,
                              saveInfo.save_data_id,
                              remoteName,
                              "Backup local concluido; envio em segundo plano.");
    }
}
