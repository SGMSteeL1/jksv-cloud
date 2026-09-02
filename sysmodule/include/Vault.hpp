#pragma once

#include <string>

namespace sync
{
    struct Credentials
    {
        std::string server{};
        std::string loginName{};
        std::string appPassword{};
        std::string basePath{};
        std::string resolvedAddress{};

        Credentials() = default;
        Credentials(const Credentials &) = delete;
        Credentials &operator=(const Credentials &) = delete;
        Credentials(Credentials &&) noexcept = default;
        Credentials &operator=(Credentials &&) noexcept = default;
        ~Credentials();

        bool is_valid() const noexcept;
        void clear() noexcept;
    };

    bool load_credentials(Credentials &credentialsOut) noexcept;
} // namespace sync
