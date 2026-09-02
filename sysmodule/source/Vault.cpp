#include "Config.hpp"
#include "Log.hpp"
#include "Vault.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <json-c/json.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <switch.h>
#include <vector>

namespace
{
    constexpr std::array<std::uint8_t, 8> MAGIC = {'J', 'K', 'S', 'V', 'N', 'C', 'L', 'D'};
    constexpr std::uint8_t VERSION = 1;
    constexpr std::size_t SALT_SIZE = 16;
    constexpr std::size_t NONCE_SIZE = 12;
    constexpr std::size_t TAG_SIZE = 16;
    constexpr std::size_t KEY_SIZE = 32;
    constexpr std::size_t HEADER_SIZE = 56;
    constexpr std::size_t AAD_SIZE = 40;
    constexpr std::size_t OFFSET_SOURCE = 9;
    constexpr std::size_t OFFSET_SALT = 12;
    constexpr std::size_t OFFSET_NONCE = 28;
    constexpr std::size_t OFFSET_TAG = 40;

    enum class DeviceKeySource : std::uint8_t
    {
        Spl = 1,
        Serial = 2
    };

    void secure_clear(std::string &value) noexcept
    {
        if (!value.empty()) { mbedtls_platform_zeroize(value.data(), value.size()); }
        value.clear();
    }

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
        u8 sealedKek[16]{};
        u8 deviceKey[16]{};

        const bool generated =
            R_SUCCEEDED(splCryptoGenerateAesKek(WRAPPED_KEK, 0, 1, sealedKek)) &&
            R_SUCCEEDED(splCryptoGenerateAesKey(sealedKek, WRAPPED_KEY, deviceKey));
        const bool hashed = generated &&
                            hash_parts("JKSV Nextcloud SPL key v1", deviceKey, sizeof(deviceKey), out);
        mbedtls_platform_zeroize(sealedKek, sizeof(sealedKek));
        mbedtls_platform_zeroize(deviceKey, sizeof(deviceKey));
        return hashed;
    }

    bool get_serial_secret(std::uint8_t out[KEY_SIZE]) noexcept
    {
        SetSysSerialNumber serial{};
        const bool read = R_SUCCEEDED(setsysGetSerialNumber(&serial)) && serial.number[0] != '\0';
        const bool hashed = read &&
                            hash_parts("JKSV Nextcloud serial key v1", serial.number, sizeof(serial.number), out);
        mbedtls_platform_zeroize(&serial, sizeof(serial));
        return hashed;
    }

    bool derive_key(DeviceKeySource source,
                    const std::uint8_t salt[SALT_SIZE],
                    std::uint8_t out[KEY_SIZE]) noexcept
    {
        std::array<std::uint8_t, KEY_SIZE> deviceSecret{};
        const bool secretRead = source == DeviceKeySource::Spl
                                    ? get_spl_secret(deviceSecret.data())
                                    : source == DeviceKeySource::Serial
                                          ? get_serial_secret(deviceSecret.data())
                                          : false;
        if (!secretRead) { return false; }

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

    bool read_file(std::vector<std::uint8_t> &dataOut) noexcept
    {
        std::FILE *file = std::fopen(sync::VAULT_PATH, "rb");
        if (!file) { return false; }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::rewind(file);
        if (size < static_cast<long>(HEADER_SIZE))
        {
            std::fclose(file);
            return false;
        }

        dataOut.resize(static_cast<std::size_t>(size));
        const bool read = std::fread(dataOut.data(), 1, dataOut.size(), file) == dataOut.size();
        std::fclose(file);
        return read;
    }

    bool unseal(std::string &plaintextOut) noexcept
    {
        std::vector<std::uint8_t> sealed{};
        if (!read_file(sealed) || sealed.size() < HEADER_SIZE ||
            !std::equal(MAGIC.begin(), MAGIC.end(), sealed.begin()) || sealed[8] != VERSION)
        {
            return false;
        }

        const auto source = static_cast<DeviceKeySource>(sealed[OFFSET_SOURCE]);
        std::array<std::uint8_t, KEY_SIZE> key{};
        if (!derive_key(source, sealed.data() + OFFSET_SALT, key.data()))
        {
            mbedtls_platform_zeroize(sealed.data(), sealed.size());
            return false;
        }

        const std::size_t cipherSize = sealed.size() - HEADER_SIZE;
        plaintextOut.assign(cipherSize, '\0');
        mbedtls_gcm_context context;
        mbedtls_gcm_init(&context);
        int result = mbedtls_gcm_setkey(&context,
                                        MBEDTLS_CIPHER_ID_AES,
                                        key.data(),
                                        KEY_SIZE * 8);
        if (result == 0)
        {
            result = mbedtls_gcm_auth_decrypt(
                &context,
                cipherSize,
                sealed.data() + OFFSET_NONCE,
                NONCE_SIZE,
                sealed.data(),
                AAD_SIZE,
                sealed.data() + OFFSET_TAG,
                TAG_SIZE,
                sealed.data() + HEADER_SIZE,
                reinterpret_cast<unsigned char *>(plaintextOut.data()));
        }
        mbedtls_gcm_free(&context);
        mbedtls_platform_zeroize(key.data(), key.size());
        mbedtls_platform_zeroize(sealed.data(), sealed.size());
        if (result == 0) { return true; }

        secure_clear(plaintextOut);
        return false;
    }

    bool read_required_string(json_object *object, const char *key, std::string &out) noexcept
    {
        json_object *value{};
        if (!object || !json_object_object_get_ex(object, key, &value) ||
            json_object_get_type(value) != json_type_string)
        {
            return false;
        }
        const char *text = json_object_get_string(value);
        if (!text || !text[0]) { return false; }
        out = text;
        return true;
    }
} // namespace

sync::Credentials::~Credentials() { clear(); }

bool sync::Credentials::is_valid() const noexcept
{
    return server.starts_with("https://") && !loginName.empty() && !appPassword.empty() &&
           !basePath.empty();
}

void sync::Credentials::clear() noexcept
{
    secure_clear(server);
    secure_clear(loginName);
    secure_clear(appPassword);
    secure_clear(basePath);
    secure_clear(resolvedAddress);
}

bool sync::load_credentials(Credentials &credentialsOut) noexcept
{
    std::string plaintext{};
    if (!unseal(plaintext))
    {
        sync::log::write("Could not open the console-bound Nextcloud vault.");
        return false;
    }

    json_object *object = json_tokener_parse(plaintext.c_str());
    Credentials parsed{};
    const bool complete = object && read_required_string(object, "server", parsed.server) &&
                          read_required_string(object, "loginName", parsed.loginName) &&
                          read_required_string(object, "appPassword", parsed.appPassword) &&
                          read_required_string(object, "basePath", parsed.basePath) &&
                          parsed.is_valid();
    json_object *resolvedValue{};
    if (object && json_object_object_get_ex(object, "resolvedAddress", &resolvedValue) && resolvedValue &&
        json_object_get_type(resolvedValue) == json_type_string)
    {
        const char *value = json_object_get_string(resolvedValue);
        if (value) { parsed.resolvedAddress = value; }
    }
    json_object *passwordValue{};
    if (object && json_object_object_get_ex(object, "appPassword", &passwordValue) && passwordValue)
    {
        json_object_set_string(passwordValue, "");
    }
    if (object) { json_object_put(object); }
    secure_clear(plaintext);
    if (!complete) { return false; }

    credentialsOut = std::move(parsed);
    return true;
}
