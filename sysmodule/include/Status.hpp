#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sync::status
{
    enum class Phase
    {
        Starting,
        Disabled,
        Idle,
        Monitoring,
        Waiting,
        BackingUp,
        Uploading,
        Error,
    };

    enum class Result
    {
        Success,
        Queued,
        Error,
    };

    /// @brief Loads the previous result and publishes a fresh heartbeat.
    void initialize() noexcept;

    /// @brief Changes the current activity and writes immediately when it changes.
    void set_phase(Phase phase, std::uint64_t activeTitleId = 0) noexcept;

    /// @brief Refreshes the on-disk heartbeat at a write-rate safe interval.
    void heartbeat() noexcept;

    /// @brief Records a user-visible synchronization result.
    void record_result(Result result,
                       std::uint64_t titleId,
                       std::uint64_t saveId,
                       std::string_view backupName,
                       std::string_view detail = {}) noexcept;

    /// @brief Refreshes the diagnostic detail without generating a new notification event.
    void update_detail(std::string_view detail) noexcept;

    /// @brief Updates how many saves are waiting for upload.
    void set_pending_count(std::size_t count) noexcept;
} // namespace sync::status
