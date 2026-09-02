#pragma once

#include <cstdint>
#include <string>

namespace syncconfig
{
    inline constexpr const char *PROGRAM_ID = "420000000000C10D";
    inline constexpr const char *PATH_CONFIG = "sdmc:/config/JKSV Cloud/sync.json";
    inline constexpr const char *PATH_STATUS = "sdmc:/config/JKSV Cloud/sync-status.json";
    inline constexpr const char *PATH_APP_STATUS = "sdmc:/config/JKSV Cloud/sync-app-status.json";

    struct Status
    {
        bool filePresent{};
        bool running{};
        std::string sysmoduleVersion{};
        std::string phase{"unknown"};
        std::uint64_t activeTitleId{};
        std::uint64_t eventId{};
        std::string lastResult{"none"};
        std::int64_t lastEventAt{};
        std::int64_t lastSuccessAt{};
        std::uint64_t lastTitleId{};
        std::uint64_t lastSaveId{};
        std::string lastBackupName{};
        std::string lastDetail{};
        int pendingCount{};
    };

    /// @brief Returns whether automatic post-game synchronization is enabled.
    bool is_enabled() noexcept;

    /// @brief Changes only the enabled field, preserving advanced sysmodule options.
    bool set_enabled(bool enabled) noexcept;

    /// @brief Returns whether the optional Atmosphere sysmodule is installed.
    bool is_sysmodule_installed() noexcept;

    /// @brief Returns a localized menu label when available.
    std::string get_menu_label(bool enabled);

    /// @brief Reads the atomic heartbeat/result file written by the sysmodule.
    Status read_status() noexcept;

    /// @brief Returns a compact, color-coded status label for the Extras menu.
    std::string get_status_menu_label();

    /// @brief Returns the detailed synchronization panel contents.
    std::string get_status_details();

    /// @brief Shows a pop message once for each new result written by the sysmodule.
    void notify_new_result();

    /// @brief Publishes or clears the homebrew heartbeat used to exclude title takeover.
    void set_app_active(bool active) noexcept;
} // namespace syncconfig
