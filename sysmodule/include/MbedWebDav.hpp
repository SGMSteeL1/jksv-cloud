#pragma once

#include "Vault.hpp"

#include <cstdint>
#include <string>

namespace sync::webdav
{
    struct Result
    {
        bool success{};
        std::string detail{};
    };

    Result upload(const Credentials &credentials,
                  std::uint64_t titleId,
                  const std::string &sourcePath,
                  const std::string &remoteName) noexcept;
}
