#pragma once

#include <cstdint>

namespace sync
{
    inline constexpr const char *VERSION = "1.0.1";
    inline constexpr std::uint64_t PROGRAM_ID = 0x420000000000C10DULL;
    inline constexpr const char *PROGRAM_ID_TEXT = "420000000000C10D";

    inline constexpr const char *CONFIG_DIRECTORY = "sdmc:/config/JKSV Cloud";
    inline constexpr const char *CONFIG_PATH = "sdmc:/config/JKSV Cloud/sync.json";
    inline constexpr const char *STATE_PATH = "sdmc:/config/JKSV Cloud/sync-state.json";
    inline constexpr const char *STATUS_PATH = "sdmc:/config/JKSV Cloud/sync-status.json";
    inline constexpr const char *APP_STATUS_PATH = "sdmc:/config/JKSV Cloud/sync-app-status.json";
    inline constexpr const char *TITLE_MAP_PATH = "sdmc:/config/JKSV Cloud/title-map.json";
    inline constexpr const char *VAULT_PATH = "sdmc:/config/JKSV Cloud/nextcloud.vault";
    inline constexpr const char *LOG_PATH = "sdmc:/config/JKSV Cloud/JKSV-Cloud-Sync.log";
    inline constexpr const char *QUEUE_ROOT = "sdmc:/JKSV Cloud/Sync Queue";
    inline constexpr const char *LOCAL_ROOT = "sdmc:/JKSV Cloud/Auto Sync";
    inline constexpr const char *CA_BUNDLE =
        "sdmc:/atmosphere/contents/420000000000C10D/cacert.pem";

    struct Settings
    {
        bool enabled{false};
        bool syncOnGameClose{true};
        bool keepLocalCopies{false};
        int pollSeconds{3};
        int settleSeconds{5};
    };

    Settings load_settings() noexcept;

    /// @brief Returns whether JKSV Cloud itself is currently running via title takeover.
    bool is_app_active() noexcept;
} // namespace sync
