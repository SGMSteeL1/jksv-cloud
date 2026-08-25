#pragma once

#include "fslib.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace security
{
    /// @brief Authenticated, console-bound storage for small secrets.
    /// @note This protects a copied SD card, not against other homebrew running on the same console.
    enum class SealResult
    {
        Ok,
        InvalidData,
        UnsupportedVersion,
        NoDeviceKey,
        AuthenticationFailed,
        IoError,
        CryptoError
    };

    /// @brief Seals plaintext with AES-256-GCM under a console-bound key.
    SealResult seal(std::string_view plaintext, std::vector<std::uint8_t> &sealedOut) noexcept;

    /// @brief Authenticates and unseals a blob created by seal().
    SealResult unseal(const std::vector<std::uint8_t> &sealedData, std::string &plaintextOut) noexcept;

    /// @brief Seals plaintext and atomically replaces the target file as far as FsLib permits.
    SealResult seal_to_file(std::string_view plaintext, const fslib::Path &path) noexcept;

    /// @brief Reads and unseals a file.
    SealResult unseal_from_file(const fslib::Path &path, std::string &plaintextOut) noexcept;

    /// @brief Stable diagnostic string. It never includes secret material.
    const char *get_result_string(SealResult result) noexcept;
} // namespace security
