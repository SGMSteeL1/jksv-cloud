#include "remote/Nextcloud.hpp"

#include "curl/curl.hpp"
#include "fslib.hpp"
#include "json.hpp"
#include "logging/logger.hpp"
#include "remote/Form.hpp"
#include "remote/URL.hpp"
#include "security/DeviceSeal.hpp"

#include <algorithm>
#include <array>
#include <json-c/json.h>
#include <utility>

namespace
{
    constexpr const char *USER_AGENT = curl::STRING_USER_AGENT;

    void secure_clear(std::string &target) noexcept
    {
        volatile char *data = target.empty() ? nullptr : target.data();
        for (std::size_t i = 0; data && i < target.size(); ++i) { data[i] = 0; }
        target.clear();
    }

    bool normalize_server(std::string_view input, std::string &serverOut) noexcept
    {
        const std::size_t first = input.find_first_not_of(" \t\r\n");
        const std::size_t last  = input.find_last_not_of(" \t\r\n");
        if (first == std::string_view::npos || last == std::string_view::npos) { return false; }

        serverOut.assign(input.substr(first, last - first + 1));
        while (serverOut.size() > 8 && serverOut.back() == '/') { serverOut.pop_back(); }

        if (!serverOut.starts_with("https://") || serverOut.size() <= 8) { return false; }
        if (serverOut.find_first_of("\r\n?#") != std::string::npos) { return false; }
        return true;
    }

    std::string_view origin_of(std::string_view url) noexcept
    {
        const std::size_t schemeEnd = url.find("://");
        if (schemeEnd == std::string_view::npos) { return {}; }
        const std::size_t pathBegin = url.find('/', schemeEnd + 3);
        return pathBegin == std::string_view::npos ? url : url.substr(0, pathBegin);
    }

    bool same_origin(std::string_view left, std::string_view right) noexcept
    {
        const std::string_view leftOrigin  = origin_of(left);
        const std::string_view rightOrigin = origin_of(right);
        return !leftOrigin.empty() && leftOrigin == rightOrigin;
    }

    bool prepare_secure_request(curl::Handle &handle, const char *url, std::string &response) noexcept
    {
        if (!handle || !url || !url[0]) { return false; }
        curl::set_option(handle, CURLOPT_URL, url);
        curl::set_option(handle, CURLOPT_USERAGENT, USER_AGENT);
        curl::set_option(handle, CURLOPT_WRITEFUNCTION, curl::write_response_string);
        curl::set_option(handle, CURLOPT_WRITEDATA, &response);
        curl::set_option(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl::set_option(handle, CURLOPT_TIMEOUT, 15L);
        curl::enable_tls_verification(handle);
        return true;
    }

    bool read_required_string(json_object *parent, const char *key, std::string &out) noexcept
    {
        json_object *value{};
        if (!parent || !json_object_object_get_ex(parent, key, &value) || !value ||
            json_object_get_type(value) != json_type_string)
        {
            return false;
        }

        const char *text = json_object_get_string(value);
        if (!text || !text[0]) { return false; }
        out = text;
        return true;
    }

    remote::NextcloudResult parse_login_response(std::string_view response,
                                                  std::string_view requestedServer,
                                                  remote::NextcloudLoginSession &sessionOut) noexcept
    {
        json::Object parser = json::new_object(json_tokener_parse, response.data());
        if (!parser) { return remote::NextcloudResult::InvalidResponse; }

        json_object *poll{};
        if (!json_object_object_get_ex(parser.get(), "poll", &poll) || !poll) {
            return remote::NextcloudResult::InvalidResponse;
        }

        remote::NextcloudLoginSession parsed{};
        parsed.server = requestedServer;
        const bool complete = read_required_string(parser.get(), "login", parsed.loginUrl) &&
                              read_required_string(poll, "endpoint", parsed.pollEndpoint) &&
                              read_required_string(poll, "token", parsed.pollToken);
        if (!complete || !parsed.loginUrl.starts_with("https://") ||
            !parsed.pollEndpoint.starts_with("https://") ||
            !same_origin(parsed.server, parsed.loginUrl) ||
            !same_origin(parsed.server, parsed.pollEndpoint))
        {
            return remote::NextcloudResult::InvalidResponse;
        }

        sessionOut = std::move(parsed);
        return remote::NextcloudResult::Ok;
    }

    remote::NextcloudResult parse_poll_response(std::string_view response,
                                                 const remote::NextcloudLoginSession &session,
                                                 remote::NextcloudCredentials &credentialsOut) noexcept
    {
        json::Object parser = json::new_object(json_tokener_parse, response.data());
        if (!parser) { return remote::NextcloudResult::InvalidResponse; }

        remote::NextcloudCredentials parsed{};
        const bool complete = read_required_string(parser.get(), "server", parsed.server) &&
                              read_required_string(parser.get(), "loginName", parsed.loginName) &&
                              read_required_string(parser.get(), "appPassword", parsed.appPassword);
        if (!complete || !parsed.server.starts_with("https://") || !same_origin(session.server, parsed.server))
        {
            secure_clear(parsed.appPassword);
            return remote::NextcloudResult::InvalidResponse;
        }

        while (parsed.server.size() > 8 && parsed.server.back() == '/') { parsed.server.pop_back(); }
        credentialsOut = std::move(parsed);
        return remote::NextcloudResult::Ok;
    }

    void append_basic_credentials(curl::Handle &handle,
                                  const remote::NextcloudCredentials &credentials) noexcept
    {
        curl::set_option(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl::set_option(handle, CURLOPT_USERNAME, credentials.loginName.c_str());
        curl::set_option(handle, CURLOPT_PASSWORD, credentials.appPassword.c_str());
    }

    remote::NextcloudResult resolve_user_id(remote::NextcloudCredentials &credentials) noexcept
    {
        remote::URL url{credentials.server};
        url.append_path("ocs/v2.php/cloud/user").append_parameter("format", "json");

        curl::Handle handle = curl::new_handle();
        curl::prepare_get(handle);
        curl::HeaderList headers = curl::new_header_list();
        curl::append_header(headers, "OCS-APIRequest: true");
        std::string response{};
        prepare_secure_request(handle, url.get(), response);
        append_basic_credentials(handle, credentials);
        curl::set_option(handle, CURLOPT_HTTPHEADER, headers.get());

        if (!curl::perform(handle)) { return remote::NextcloudResult::NetworkError; }
        if (curl::get_response_code(handle) != 200) { return remote::NextcloudResult::AuthenticationFailed; }

        json::Object parser = json::new_object(json_tokener_parse, response.c_str());
        json_object *ocs{}, *data{};
        if (!parser || !json_object_object_get_ex(parser.get(), "ocs", &ocs) || !ocs ||
            !json_object_object_get_ex(ocs, "data", &data) || !data ||
            !read_required_string(data, "id", credentials.userId))
        {
            return remote::NextcloudResult::InvalidResponse;
        }
        return remote::NextcloudResult::Ok;
    }

    remote::NextcloudResult ensure_backup_directory(remote::NextcloudCredentials &credentials) noexcept
    {
        curl::Handle handle = curl::new_handle();
        std::string escapedUser{};
        if (!curl::escape_string(handle, credentials.userId, escapedUser))
        {
            return remote::NextcloudResult::StorageSetupFailed;
        }

        credentials.basePath = "remote.php/dav/files/" + escapedUser + "/JKSV%20Cloud";
        remote::URL url{credentials.server};
        url.append_path(credentials.basePath).append_slash();

        curl::reset_handle(handle);
        std::string response{};
        prepare_secure_request(handle, url.get(), response);
        append_basic_credentials(handle, credentials);
        curl::set_option(handle, CURLOPT_CUSTOMREQUEST, "MKCOL");

        if (!curl::perform(handle)) { return remote::NextcloudResult::NetworkError; }
        const long code = curl::get_response_code(handle);
        return code == 201 || code == 405 ? remote::NextcloudResult::Ok
                                          : remote::NextcloudResult::StorageSetupFailed;
    }
} // namespace

bool remote::NextcloudCredentials::is_valid() const noexcept
{
    return server.starts_with("https://") && !loginName.empty() && !appPassword.empty() &&
           !userId.empty() && !basePath.empty();
}

remote::NextcloudResult remote::nextcloud_begin_login(std::string_view server,
                                                       NextcloudLoginSession &sessionOut) noexcept
{
    std::string normalized{};
    if (!normalize_server(server, normalized)) { return NextcloudResult::InvalidServer; }

    remote::URL url{normalized};
    url.append_path("index.php/login/v2");

    curl::Handle handle = curl::new_handle();
    curl::prepare_post(handle);
    std::string response{};
    prepare_secure_request(handle, url.get(), response);
    curl::set_option(handle, CURLOPT_POSTFIELDS, "");
    curl::set_option(handle, CURLOPT_POSTFIELDSIZE, 0L);

    if (!curl::perform(handle)) { return NextcloudResult::NetworkError; }
    if (curl::get_response_code(handle) != 200) { return NextcloudResult::UnexpectedResponse; }
    return parse_login_response(response, normalized, sessionOut);
}

remote::NextcloudResult remote::nextcloud_poll_login(const NextcloudLoginSession &session,
                                                      NextcloudCredentials &credentialsOut) noexcept
{
    if (session.pollEndpoint.empty() || session.pollToken.empty()) { return NextcloudResult::InvalidResponse; }

    remote::Form form{};
    form.append_parameter("token", session.pollToken);

    curl::Handle handle = curl::new_handle();
    curl::prepare_post(handle);
    std::string response{};
    prepare_secure_request(handle, session.pollEndpoint.c_str(), response);
    curl::set_option(handle, CURLOPT_POSTFIELDS, form.get());
    curl::set_option(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.length()));

    if (!curl::perform(handle)) { return NextcloudResult::NetworkError; }
    const long code = curl::get_response_code(handle);
    if (code == 404) { return NextcloudResult::Pending; }
    if (code != 200) { return NextcloudResult::AuthenticationFailed; }
    return parse_poll_response(response, session, credentialsOut);
}

remote::NextcloudResult remote::nextcloud_prepare_storage(NextcloudCredentials &credentials) noexcept
{
    NextcloudResult result = resolve_user_id(credentials);
    if (result != NextcloudResult::Ok) { return result; }
    return ensure_backup_directory(credentials);
}

remote::NextcloudResult remote::nextcloud_save_credentials(const NextcloudCredentials &credentials) noexcept
{
    if (!credentials.is_valid()) { return NextcloudResult::InvalidResponse; }

    json::Object object = json::new_object(json_object_new_object);
    json::add_object(object, "server", json_object_new_string(credentials.server.c_str()));
    json::add_object(object, "loginName", json_object_new_string(credentials.loginName.c_str()));
    json::add_object(object, "appPassword", json_object_new_string(credentials.appPassword.c_str()));
    json::add_object(object, "userId", json_object_new_string(credentials.userId.c_str()));
    json::add_object(object, "basePath", json_object_new_string(credentials.basePath.c_str()));

    std::string plaintext = json_object_to_json_string_ext(object.get(), JSON_C_TO_STRING_PLAIN);
    const security::SealResult sealed = security::seal_to_file(plaintext, fslib::Path{PATH_NEXTCLOUD_VAULT});
    secure_clear(plaintext);
    if (sealed != security::SealResult::Ok)
    {
        logger::log("Nextcloud vault save failed: %s", security::get_result_string(sealed));
        return NextcloudResult::VaultError;
    }
    return NextcloudResult::Ok;
}

remote::NextcloudResult remote::nextcloud_load_credentials(NextcloudCredentials &credentialsOut) noexcept
{
    std::string plaintext{};
    const security::SealResult unsealed =
        security::unseal_from_file(fslib::Path{PATH_NEXTCLOUD_VAULT}, plaintext);
    if (unsealed != security::SealResult::Ok)
    {
        logger::log("Nextcloud vault load failed: %s", security::get_result_string(unsealed));
        return NextcloudResult::VaultError;
    }

    json::Object parser = json::new_object(json_tokener_parse, plaintext.c_str());
    NextcloudCredentials parsed{};
    const bool complete = parser && read_required_string(parser.get(), "server", parsed.server) &&
                          read_required_string(parser.get(), "loginName", parsed.loginName) &&
                          read_required_string(parser.get(), "appPassword", parsed.appPassword) &&
                          read_required_string(parser.get(), "userId", parsed.userId) &&
                          read_required_string(parser.get(), "basePath", parsed.basePath) && parsed.is_valid();
    secure_clear(plaintext);
    if (!complete)
    {
        secure_clear(parsed.appPassword);
        return NextcloudResult::InvalidResponse;
    }

    credentialsOut = std::move(parsed);
    return NextcloudResult::Ok;
}

bool remote::nextcloud_delete_credentials() noexcept
{
    const fslib::Path path{PATH_NEXTCLOUD_VAULT};
    return !fslib::file_exists(path) || fslib::delete_file(path);
}

bool remote::nextcloud_revoke_credentials(const NextcloudCredentials &credentials) noexcept
{
    if (!credentials.is_valid()) { return false; }

    remote::URL url{credentials.server};
    url.append_path("ocs/v2.php/core/apppassword");

    curl::Handle handle = curl::new_handle();
    curl::reset_handle(handle);
    curl::HeaderList headers = curl::new_header_list();
    curl::append_header(headers, "OCS-APIRequest: true");
    std::string response{};
    prepare_secure_request(handle, url.get(), response);
    append_basic_credentials(handle, credentials);
    curl::set_option(handle, CURLOPT_HTTPHEADER, headers.get());
    curl::set_option(handle, CURLOPT_CUSTOMREQUEST, "DELETE");

    return curl::perform(handle) && curl::get_response_code(handle) == 200;
}

const char *remote::get_nextcloud_result_string(NextcloudResult result) noexcept
{
    switch (result)
    {
        case NextcloudResult::Ok:                  return "success";
        case NextcloudResult::Pending:             return "authorization pending";
        case NextcloudResult::Cancelled:           return "cancelled";
        case NextcloudResult::InvalidServer:       return "invalid server URL";
        case NextcloudResult::NetworkError:        return "network or TLS error";
        case NextcloudResult::UnexpectedResponse:  return "unexpected HTTP response";
        case NextcloudResult::InvalidResponse:     return "invalid Nextcloud response";
        case NextcloudResult::AuthenticationFailed:return "authentication failed";
        case NextcloudResult::StorageSetupFailed:  return "could not create the backup folder";
        case NextcloudResult::VaultError:           return "could not protect the credentials";
    }
    return "unknown Nextcloud error";
}
