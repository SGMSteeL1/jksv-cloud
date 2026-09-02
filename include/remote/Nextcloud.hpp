#pragma once

#include <string>
#include <string_view>

namespace remote
{
    inline constexpr std::string_view PATH_NEXTCLOUD_VAULT = "sdmc:/config/JKSV Cloud/nextcloud.vault";

    struct NextcloudLoginSession
    {
        std::string server{};
        std::string loginUrl{};
        std::string pollEndpoint{};
        std::string pollToken{};
    };

    struct NextcloudCredentials
    {
        std::string server{};
        std::string loginName{};
        std::string appPassword{};
        std::string userId{};
        std::string basePath{};
        // IPv4 address observed during the authenticated setup request. The
        // sysmodule uses it with CURLOPT_RESOLVE so its synchronous resolver
        // cannot block the background worker indefinitely.
        std::string resolvedAddress{};

        bool is_valid() const noexcept;
    };

    enum class NextcloudResult
    {
        Ok,
        Pending,
        Cancelled,
        InvalidServer,
        NetworkError,
        UnexpectedResponse,
        InvalidResponse,
        AuthenticationFailed,
        StorageSetupFailed,
        VaultError
    };

    /// @brief Starts Nextcloud Login Flow v2 and returns the URL that must be opened by the user.
    NextcloudResult nextcloud_begin_login(std::string_view server,
                                          NextcloudLoginSession &sessionOut) noexcept;

    /// @brief Polls Login Flow v2 once. A 404 response maps to Pending.
    NextcloudResult nextcloud_poll_login(const NextcloudLoginSession &session,
                                         NextcloudCredentials &credentialsOut) noexcept;

    /// @brief Resolves the real Nextcloud user ID and creates the JKSV folder if needed.
    NextcloudResult nextcloud_prepare_storage(NextcloudCredentials &credentials) noexcept;

    /// @brief Stores credentials in an authenticated, console-bound vault.
    NextcloudResult nextcloud_save_credentials(const NextcloudCredentials &credentials) noexcept;

    /// @brief Loads credentials from the console-bound vault.
    NextcloudResult nextcloud_load_credentials(NextcloudCredentials &credentialsOut) noexcept;

    /// @brief Removes the per-console Nextcloud credential.
    bool nextcloud_delete_credentials() noexcept;

    /// @brief Revokes the current app password on the Nextcloud server.
    bool nextcloud_revoke_credentials(const NextcloudCredentials &credentials) noexcept;

    /// @brief Returns a stable diagnostic string with no credential material.
    const char *get_nextcloud_result_string(NextcloudResult result) noexcept;
} // namespace remote
