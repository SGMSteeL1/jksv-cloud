#pragma once
#include "remote/Storage.hpp"
#include "sys/threadpool.hpp"

#include <memory>

namespace remote
{
    // Both of these are needed in two different places.
    static constexpr std::string_view PATH_GOOGLE_DRIVE_CONFIG = "sdmc:/config/JKSV Cloud/client_secret.json";
    static constexpr std::string_view PATH_WEBDAV_CONFIG       = "sdmc:/config/JKSV Cloud/webdav.json";

    /// @brief Returns whether or not the console has an active internet connection.
    bool has_internet_connection() noexcept;

    /// @brief Initializes the remote service according to the config on the sdmc.
    void initialize(sys::threadpool::JobData jobData);

    /// @brief Returns the pointer to the Storage instance.
    remote::Storage *get_remote_storage() noexcept;

    /// @brief Replaces the active remote with the credentials in the Nextcloud vault.
    bool reload_nextcloud() noexcept;

    /// @brief Removes the Nextcloud vault and clears the active remote.
    bool disconnect_nextcloud() noexcept;

    /// @brief Returns whether a Nextcloud vault exists on the SD card.
    bool nextcloud_is_configured() noexcept;
} // namespace remote
