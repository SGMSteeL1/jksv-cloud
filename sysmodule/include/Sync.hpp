#pragma once

#include <cstdint>

namespace sync
{
    /// @brief Reports optional runtime services that may become ready after boot.
    void set_runtime_capabilities(bool networkReady, bool cryptoReady) noexcept;

    /// @brief Returns the application title ID currently running, or zero.
    std::uint64_t get_running_application() noexcept;

    /// @brief Retries queued uploads while no game is running.
    void retry_pending_uploads() noexcept;

    /// @brief Creates and uploads changed Account/Device saves for exactly one title.
    void synchronize_title(std::uint64_t titleId) noexcept;
} // namespace sync
