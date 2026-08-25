/*
 * Console-bound sealed storage derived from Checkpoint's device_seal design.
 * Checkpoint copyright (C) 2017-2026 Bernardo Giordano, FlagBrew.
 * Both Checkpoint and JKSV are distributed under GPL-3.0. See NOTICE.md.
 */
#include "security/DeviceSeal.hpp"

#include "logging/logger.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <switch.h>

namespace
{
    constexpr std::array<std::uint8_t, 8> MAGIC = {'J', 'K', 'S', 'V', 'N', 'C', 'L', 'D'};
    constexpr std::uint8_t VERSION               = 1;
    constexpr std::size_t SALT_SIZE              = 16;
    constexpr std::size_t NONCE_SIZE             = 12;
    constexpr std::size_t TAG_SIZE               = 16;
    constexpr std::size_t KEY_SIZE               = 32;
    constexpr std::size_t HEADER_SIZE            = 56;
    constexpr std::size_t AAD_SIZE               = 40;
    constexpr std::size_t OFFSET_SOURCE           = 9;
    constexpr std::size_t OFFSET_SALT             = 12;
    constexpr std::size_t OFFSET_NONCE            = 28;
    constexpr std::size_t OFFSET_TAG              = 40;

    enum class DeviceKeySource : std::uint8_t
    {
        Spl    = 1,
        Serial = 2
    };

    bool hash_parts(std::string_view label,
                    const void *data,
                    std::size_t dataSize,
                    std::uint8_t out[KEY_SIZE]) noexcept
    {
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (!md) { return false; }

        mbedtls_md_context_t context;
        mbedtls_md_init(&context);
        const bool success = mbedtls_md_setup(&context, md, 0) == 0 &&
                             mbedtls_md_starts(&context) == 0 &&
                             mbedtls_md_update(&context,
                                               reinterpret_cast<const unsigned char *>(label.data()),
                                               label.size()) == 0 &&
                             mbedtls_md_update(&context,
                                               reinterpret_cast<const unsigned char *>(data),
                                               dataSize) == 0 &&
                             mbedtls_md_finish(&context, out) == 0;
        mbedtls_md_free(&context);
        return success;
    }

    bool get_spl_secret(std::uint8_t out[KEY_SIZE]) noexcept
    {
        static const u8 WRAPPED_KEK[16] = "JKSVNcSealKey1";
        static const u8 WRAPPED_KEY[16] = "JKSVNcVaultKey1";

        if (R_FAILED(splCryptoInitialize())) { return false; }

        u8 sealedKek[16] = {0};
        u8 deviceKey[16] = {0};
        const bool generated =
            R_SUCCEEDED(splCryptoGenerateAesKek(WRAPPED_KEK, 0, 1, sealedKek)) &&
            R_SUCCEEDED(splCryptoGenerateAesKey(sealedKek, WRAPPED_KEY, deviceKey));
        splCryptoExit();

        const bool hashed = generated && hash_parts("JKSV Nextcloud SPL key v1", deviceKey, sizeof(deviceKey), out);
        mbedtls_platform_zeroize(sealedKek, sizeof(sealedKek));
        mbedtls_platform_zeroize(deviceKey, sizeof(deviceKey));
        return hashed;
    }

    bool get_serial_secret(std::uint8_t out[KEY_SIZE]) noexcept
    {
        if (R_FAILED(setsysInitialize())) { return false; }

        SetSysSerialNumber serial{};
        const bool read = R_SUCCEEDED(setsysGetSerialNumber(&serial)) && serial.number[0] != '\0';
        setsysExit();

        const bool hashed = read && hash_parts("JKSV Nextcloud serial key v1", serial.number, sizeof(serial.number), out);
        mbedtls_platform_zeroize(&serial, sizeof(serial));
        return hashed;
    }

    bool get_device_secret(DeviceKeySource source, std::uint8_t out[KEY_SIZE]) noexcept
    {
        switch (source)
        {
            case DeviceKeySource::Spl:    return get_spl_secret(out);
            case DeviceKeySource::Serial: return get_serial_secret(out);
        }
        return false;
    }

    bool get_best_device_secret(DeviceKeySource &sourceOut, std::uint8_t out[KEY_SIZE]) noexcept
    {
        if (get_spl_secret(out))
        {
            sourceOut = DeviceKeySource::Spl;
            return true;
        }

        logger::log("Nextcloud vault: SPL key unavailable, using console serial fallback.");
        if (get_serial_secret(out))
        {
            sourceOut = DeviceKeySource::Serial;
            return true;
        }
        return false;
    }

    bool derive_key(DeviceKeySource source,
                    const std::uint8_t salt[SALT_SIZE],
                    std::uint8_t out[KEY_SIZE]) noexcept
    {
        std::array<std::uint8_t, KEY_SIZE> deviceSecret{};
        if (!get_device_secret(source, deviceSecret.data())) { return false; }

        std::array<std::uint8_t, 9 + KEY_SIZE> material{};
        std::memcpy(material.data(), "JKSVNCLD1", 9);
        std::memcpy(material.data() + 9, deviceSecret.data(), deviceSecret.size());

        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        const bool derived = md && mbedtls_md_hmac(md,
                                                   salt,
                                                   SALT_SIZE,
                                                   material.data(),
                                                   material.size(),
                                                   out) == 0;
        mbedtls_platform_zeroize(deviceSecret.data(), deviceSecret.size());
        mbedtls_platform_zeroize(material.data(), material.size());
        return derived;
    }

    bool looks_sealed(const std::vector<std::uint8_t> &data) noexcept
    {
        return data.size() >= HEADER_SIZE && std::equal(MAGIC.begin(), MAGIC.end(), data.begin());
    }
} // namespace

security::SealResult security::seal(std::string_view plaintext, std::vector<std::uint8_t> &sealedOut) noexcept
{
    sealedOut.clear();
    if (plaintext.empty()) { return SealResult::InvalidData; }

    sealedOut.assign(HEADER_SIZE + plaintext.size(), 0);
    std::copy(MAGIC.begin(), MAGIC.end(), sealedOut.begin());
    sealedOut[8] = VERSION;

    DeviceKeySource source{};
    std::array<std::uint8_t, KEY_SIZE> probe{};
    if (!get_best_device_secret(source, probe.data()))
    {
        mbedtls_platform_zeroize(probe.data(), probe.size());
        sealedOut.clear();
        return SealResult::NoDeviceKey;
    }
    mbedtls_platform_zeroize(probe.data(), probe.size());
    sealedOut[OFFSET_SOURCE] = static_cast<std::uint8_t>(source);

    randomGet(sealedOut.data() + OFFSET_SALT, SALT_SIZE);
    randomGet(sealedOut.data() + OFFSET_NONCE, NONCE_SIZE);

    std::array<std::uint8_t, KEY_SIZE> key{};
    if (!derive_key(source, sealedOut.data() + OFFSET_SALT, key.data()))
    {
        sealedOut.clear();
        return SealResult::NoDeviceKey;
    }

    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), KEY_SIZE * 8);
    if (result == 0)
    {
        result = mbedtls_gcm_crypt_and_tag(&context,
                                           MBEDTLS_GCM_ENCRYPT,
                                           plaintext.size(),
                                           sealedOut.data() + OFFSET_NONCE,
                                           NONCE_SIZE,
                                           sealedOut.data(),
                                           AAD_SIZE,
                                           reinterpret_cast<const unsigned char *>(plaintext.data()),
                                           sealedOut.data() + HEADER_SIZE,
                                           TAG_SIZE,
                                           sealedOut.data() + OFFSET_TAG);
    }
    mbedtls_gcm_free(&context);
    mbedtls_platform_zeroize(key.data(), key.size());

    if (result != 0)
    {
        sealedOut.clear();
        return SealResult::CryptoError;
    }
    return SealResult::Ok;
}

security::SealResult security::unseal(const std::vector<std::uint8_t> &sealedData,
                                      std::string &plaintextOut) noexcept
{
    plaintextOut.clear();
    if (!looks_sealed(sealedData)) { return SealResult::InvalidData; }
    if (sealedData[8] != VERSION) { return SealResult::UnsupportedVersion; }

    const auto source = static_cast<DeviceKeySource>(sealedData[OFFSET_SOURCE]);
    std::array<std::uint8_t, KEY_SIZE> key{};
    if (!derive_key(source, sealedData.data() + OFFSET_SALT, key.data())) { return SealResult::NoDeviceKey; }

    const std::size_t cipherSize = sealedData.size() - HEADER_SIZE;
    plaintextOut.assign(cipherSize, '\0');

    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), KEY_SIZE * 8);
    if (result == 0)
    {
        result = mbedtls_gcm_auth_decrypt(&context,
                                          cipherSize,
                                          sealedData.data() + OFFSET_NONCE,
                                          NONCE_SIZE,
                                          sealedData.data(),
                                          AAD_SIZE,
                                          sealedData.data() + OFFSET_TAG,
                                          TAG_SIZE,
                                          sealedData.data() + HEADER_SIZE,
                                          reinterpret_cast<unsigned char *>(plaintextOut.data()));
    }
    mbedtls_gcm_free(&context);
    mbedtls_platform_zeroize(key.data(), key.size());

    if (result != 0)
    {
        if (!plaintextOut.empty()) { mbedtls_platform_zeroize(plaintextOut.data(), plaintextOut.size()); }
        plaintextOut.clear();
        return SealResult::AuthenticationFailed;
    }
    return SealResult::Ok;
}

security::SealResult security::seal_to_file(std::string_view plaintext, const fslib::Path &path) noexcept
{
    std::vector<std::uint8_t> sealedData{};
    const SealResult sealed = security::seal(plaintext, sealedData);
    if (sealed != SealResult::Ok) { return sealed; }

    fslib::File file{path, FsOpenMode_Create | FsOpenMode_Write, static_cast<int64_t>(sealedData.size())};
    const bool written = file && file.write(sealedData.data(), sealedData.size()) == static_cast<ssize_t>(sealedData.size()) &&
                         file.flush();
    mbedtls_platform_zeroize(sealedData.data(), sealedData.size());
    return written ? SealResult::Ok : SealResult::IoError;
}

security::SealResult security::unseal_from_file(const fslib::Path &path, std::string &plaintextOut) noexcept
{
    fslib::File file{path, FsOpenMode_Read};
    if (!file || file.get_size() <= 0) { return SealResult::IoError; }

    std::vector<std::uint8_t> sealedData(static_cast<std::size_t>(file.get_size()));
    const bool read = file.read(sealedData.data(), sealedData.size()) == static_cast<ssize_t>(sealedData.size());
    if (!read) { return SealResult::IoError; }

    const SealResult result = security::unseal(sealedData, plaintextOut);
    mbedtls_platform_zeroize(sealedData.data(), sealedData.size());
    return result;
}

const char *security::get_result_string(SealResult result) noexcept
{
    switch (result)
    {
        case SealResult::Ok:                   return "success";
        case SealResult::InvalidData:          return "invalid sealed data";
        case SealResult::UnsupportedVersion:   return "unsupported vault version";
        case SealResult::NoDeviceKey:          return "console key unavailable";
        case SealResult::AuthenticationFailed: return "vault authentication failed";
        case SealResult::IoError:              return "vault I/O error";
        case SealResult::CryptoError:          return "vault cryptography error";
    }
    return "unknown vault error";
}
