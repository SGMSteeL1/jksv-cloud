#include "MbedWebDav.hpp"

#include "Config.hpp"
#include "Log.hpp"
#include "TitleName.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mbedtls/base64.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <poll.h>
#include <string_view>
#include <switch.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace
{
    constexpr int CONNECT_TIMEOUT_MS = 10'000;
    constexpr int TLS_TIMEOUT_MS = 15'000;
    constexpr int IO_TIMEOUT_MS = 30'000;
    constexpr std::size_t HEADER_LIMIT = 64 * 1024;
    constexpr std::size_t BUFFER_SIZE = 64 * 1024;
    constexpr std::string_view USER_AGENT =
        "Mozilla/5.0 (Android) Nextcloud-android/3.30.0 JKSV-Cloud-Sync/1.0.1";

    struct Endpoint
    {
        std::string host{};
        std::string hostHeader{};
        std::uint16_t port{443};
        std::string address{};
    };

    struct TlsConnection
    {
        int socket{-1};
        mbedtls_ssl_context ssl{};
        mbedtls_ssl_config config{};
        mbedtls_x509_crt ca{};

        TlsConnection()
        {
            mbedtls_ssl_init(&ssl);
            mbedtls_ssl_config_init(&config);
            mbedtls_x509_crt_init(&ca);
        }

        ~TlsConnection()
        {
            if (socket >= 0) { close(socket); }
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&config);
            mbedtls_x509_crt_free(&ca);
        }
    };

    std::uint64_t now_ms() noexcept
    {
        const std::uint64_t frequency = armGetSystemTickFreq();
        return frequency ? (armGetSystemTick() * 1000ULL) / frequency : 0;
    }

    bool wait_socket(int socket, short events, std::uint64_t deadline) noexcept
    {
        while (true)
        {
            const std::uint64_t now = now_ms();
            if (now >= deadline) { return false; }
            pollfd descriptor{socket, events, 0};
            const std::uint64_t remaining = deadline - now;
            const int timeout = static_cast<int>(std::min<std::uint64_t>(remaining, 1000));
            const int result = poll(&descriptor, 1, timeout);
            if (result > 0) { return (descriptor.revents & events) != 0; }
            if (result < 0 && errno != EINTR) { return false; }
        }
    }

    int tls_send(void *context, const unsigned char *buffer, std::size_t size) noexcept
    {
        const int socket = *static_cast<int *>(context);
        const ssize_t sent = send(socket, buffer, size, 0);
        if (sent >= 0) { return static_cast<int>(sent); }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return MBEDTLS_ERR_SSL_WANT_WRITE; }
        if (errno == EINTR) { return MBEDTLS_ERR_SSL_WANT_WRITE; }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    int tls_receive(void *context, unsigned char *buffer, std::size_t size) noexcept
    {
        const int socket = *static_cast<int *>(context);
        const ssize_t received = recv(socket, buffer, size, 0);
        if (received > 0) { return static_cast<int>(received); }
        if (received == 0) { return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return MBEDTLS_ERR_SSL_WANT_READ; }
        if (errno == EINTR) { return MBEDTLS_ERR_SSL_WANT_READ; }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    int random_bytes(void *, unsigned char *buffer, std::size_t size) noexcept
    {
        randomGet(buffer, size);
        return 0;
    }

    std::string tls_error(int result)
    {
        std::array<char, 160> message{};
        mbedtls_strerror(result, message.data(), message.size());
        return message.data();
    }

    bool parse_endpoint(const sync::Credentials &credentials, Endpoint &out) noexcept
    {
        constexpr std::string_view scheme{"https://"};
        if (!credentials.server.starts_with(scheme) || credentials.resolvedAddress.empty()) { return false; }
        const std::size_t begin = scheme.size();
        const std::size_t end = credentials.server.find('/', begin);
        std::string authority = credentials.server.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (authority.empty() || authority.find('@') != std::string::npos) { return false; }

        out.hostHeader = authority;
        out.host = authority;
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos)
        {
            out.host = authority.substr(0, colon);
            char *tail{};
            errno = 0;
            const unsigned long port = std::strtoul(authority.c_str() + colon + 1, &tail, 10);
            if (errno || !tail || *tail || port == 0 || port > 65535) { return false; }
            out.port = static_cast<std::uint16_t>(port);
        }
        out.address = credentials.resolvedAddress;
        in_addr parsed{};
        return !out.host.empty() && inet_pton(AF_INET, out.address.c_str(), &parsed) == 1;
    }

    sync::webdav::Result connect_tls(const Endpoint &endpoint, TlsConnection &connection) noexcept
    {
        const std::time_t currentTime = std::time(nullptr);
        if (currentTime < 1'577'836'800)
        {
            return {false, "Relogio do sistema indisponivel para validar o certificado TLS."};
        }
        sync::log::write("WebDAV mbedTLS: certificate time Unix %lld.",
                         static_cast<long long>(currentTime));
        connection.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connection.socket < 0) { return {false, "Falha ao criar socket TCP."}; }
        const int flags = fcntl(connection.socket, F_GETFL, 0);
        if (flags < 0 || fcntl(connection.socket, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            return {false, "Falha ao configurar socket nao bloqueante."};
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(endpoint.port);
        inet_pton(AF_INET, endpoint.address.c_str(), &target.sin_addr);
        const int connectResult = connect(connection.socket,
                                          reinterpret_cast<sockaddr *>(&target),
                                          sizeof(target));
        if (connectResult != 0 && errno != EINPROGRESS)
        {
            return {false, "Conexao TCP recusada ou indisponivel."};
        }
        if (connectResult != 0)
        {
            const std::uint64_t deadline = now_ms() + CONNECT_TIMEOUT_MS;
            if (!wait_socket(connection.socket, POLLOUT, deadline))
            {
                return {false, "Timeout na conexao TCP."};
            }
            int socketError{};
            socklen_t length = sizeof(socketError);
            if (getsockopt(connection.socket, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0 || socketError)
            {
                return {false, "Conexao TCP falhou."};
            }
        }

        int result = mbedtls_x509_crt_parse_file(&connection.ca, sync::CA_BUNDLE);
        if (result < 0) { return {false, "Falha ao carregar certificados TLS: " + tls_error(result)}; }
        result = mbedtls_ssl_config_defaults(&connection.config,
                                             MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT);
        if (result != 0) { return {false, "Falha na configuracao TLS: " + tls_error(result)}; }
        mbedtls_ssl_conf_authmode(&connection.config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&connection.config, &connection.ca, nullptr);
        mbedtls_ssl_conf_rng(&connection.config, random_bytes, nullptr);
        mbedtls_ssl_conf_min_version(&connection.config,
                                     MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_max_version(&connection.config,
                                     MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        result = mbedtls_ssl_setup(&connection.ssl, &connection.config);
        if (result != 0) { return {false, "Falha ao preparar TLS: " + tls_error(result)}; }
        result = mbedtls_ssl_set_hostname(&connection.ssl, endpoint.host.c_str());
        if (result != 0) { return {false, "Falha ao configurar SNI TLS: " + tls_error(result)}; }
        mbedtls_ssl_set_bio(&connection.ssl,
                            &connection.socket,
                            tls_send,
                            tls_receive,
                            nullptr);

        const std::uint64_t deadline = now_ms() + TLS_TIMEOUT_MS;
        while ((result = mbedtls_ssl_handshake(&connection.ssl)) != 0)
        {
            if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const std::uint32_t flags = mbedtls_ssl_get_verify_result(&connection.ssl);
                std::array<char, 512> verification{};
                if (flags != 0)
                {
                    mbedtls_x509_crt_verify_info(verification.data(), verification.size(), "", flags);
                    sync::log::write("WebDAV mbedTLS: X509 flags 0x%08X: %s",
                                     static_cast<unsigned int>(flags), verification.data());
                }
                return {false, "Handshake TLS falhou: " + tls_error(result) +
                               (flags ? " | " + std::string{verification.data()} : std::string{})};
            }
            const short event = result == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
            if (!wait_socket(connection.socket, event, deadline))
            {
                return {false, "Timeout no handshake TLS 1.2."};
            }
        }
        if (mbedtls_ssl_get_verify_result(&connection.ssl) != 0)
        {
            return {false, "Certificado TLS do servidor nao foi validado."};
        }
        return {true, {}};
    }

    sync::webdav::Result write_tls(TlsConnection &connection,
                                   const unsigned char *data,
                                   std::size_t size,
                                   std::uint64_t deadline) noexcept
    {
        std::size_t offset{};
        while (offset < size)
        {
            const int result = mbedtls_ssl_write(&connection.ssl, data + offset, size - offset);
            if (result > 0)
            {
                offset += static_cast<std::size_t>(result);
                continue;
            }
            if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                return {false, "Envio TLS falhou: " + tls_error(result)};
            }
            const short event = result == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
            if (!wait_socket(connection.socket, event, deadline)) { return {false, "Timeout no envio TLS."}; }
        }
        return {true, {}};
    }

    sync::webdav::Result read_status(TlsConnection &connection, int &statusOut) noexcept
    {
        std::string response{};
        std::array<unsigned char, 2048> buffer{};
        const std::uint64_t deadline = now_ms() + IO_TIMEOUT_MS;
        while (response.find("\r\n\r\n") == std::string::npos && response.size() < HEADER_LIMIT)
        {
            const int result = mbedtls_ssl_read(&connection.ssl, buffer.data(), buffer.size());
            if (result > 0)
            {
                response.append(reinterpret_cast<const char *>(buffer.data()), result);
                continue;
            }
            if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                const short event = result == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT;
                if (!wait_socket(connection.socket, event, deadline))
                {
                    return {false, "Timeout aguardando resposta HTTP."};
                }
                continue;
            }
            return {false, "Leitura da resposta TLS falhou: " + tls_error(result)};
        }
        int status{};
        if (std::sscanf(response.c_str(), "HTTP/%*u.%*u %d", &status) != 1)
        {
            return {false, "Resposta HTTP invalida."};
        }
        statusOut = status;
        return {true, {}};
    }

    bool basic_authorization(const sync::Credentials &credentials, std::string &out) noexcept
    {
        std::string plain = credentials.loginName + ":" + credentials.appPassword;
        std::size_t required{};
        mbedtls_base64_encode(nullptr, 0, &required,
                              reinterpret_cast<const unsigned char *>(plain.data()), plain.size());
        std::string encoded(required, '\0');
        const int result = mbedtls_base64_encode(
            reinterpret_cast<unsigned char *>(encoded.data()), encoded.size(), &required,
            reinterpret_cast<const unsigned char *>(plain.data()), plain.size());
        std::fill(plain.begin(), plain.end(), '\0');
        if (result != 0) { return false; }
        encoded.resize(required);
        out = "Basic " + encoded;
        std::fill(encoded.begin(), encoded.end(), '\0');
        return true;
    }

    sync::webdav::Result request(const Endpoint &endpoint,
                                 std::string_view authorization,
                                 std::string_view method,
                                 std::string_view path,
                                 const std::string *filePath,
                                 int &statusOut) noexcept
    {
        TlsConnection connection{};
        sync::webdav::Result result = connect_tls(endpoint, connection);
        if (!result.success) { return result; }

        long fileSize{};
        std::FILE *file{};
        if (filePath)
        {
            file = std::fopen(filePath->c_str(), "rb");
            if (!file) { return {false, "Nao foi possivel abrir o ZIP pendente."}; }
            std::fseek(file, 0, SEEK_END);
            fileSize = std::ftell(file);
            std::rewind(file);
            if (fileSize < 0)
            {
                std::fclose(file);
                return {false, "Nao foi possivel medir o ZIP pendente."};
            }
        }

        std::string header = std::string{method} + " " + std::string{path} + " HTTP/1.1\r\n" +
                             "Host: " + endpoint.hostHeader + "\r\n" +
                             "Authorization: " + std::string{authorization} + "\r\n" +
                             "User-Agent: " + std::string{USER_AGENT} + "\r\n" +
                             "Accept: */*\r\n" +
                             "Connection: close\r\n" +
                             "Content-Length: " + std::to_string(filePath ? fileSize : 0) + "\r\n";
        if (filePath) { header += "Content-Type: application/zip\r\n"; }
        header += "\r\n";
        const std::uint64_t writeDeadline = now_ms() + IO_TIMEOUT_MS;
        result = write_tls(connection,
                           reinterpret_cast<const unsigned char *>(header.data()),
                           header.size(),
                           writeDeadline);
        if (!result.success)
        {
            if (file) { std::fclose(file); }
            return result;
        }

        if (file)
        {
            std::array<unsigned char, BUFFER_SIZE> buffer{};
            while (true)
            {
                const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
                if (read == 0) { break; }
                result = write_tls(connection, buffer.data(), read, now_ms() + IO_TIMEOUT_MS);
                if (!result.success) { break; }
            }
            const bool fileError = std::ferror(file) != 0;
            std::fclose(file);
            if (!result.success) { return result; }
            if (fileError) { return {false, "Falha ao ler o ZIP durante o envio."}; }
        }
        return read_status(connection, statusOut);
    }

    std::string encode_path_segment(std::string_view value)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string encoded{};
        encoded.reserve(value.size() * 3);
        for (const unsigned char character : value)
        {
            const bool safe = (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') ||
                              character == '-' || character == '_' || character == '.' || character == '~';
            if (safe) { encoded.push_back(static_cast<char>(character)); }
            else
            {
                encoded.push_back('%');
                encoded.push_back(hex[character >> 4]);
                encoded.push_back(hex[character & 0x0F]);
            }
        }
        return encoded;
    }
}

sync::webdav::Result sync::webdav::upload(const Credentials &credentials,
                                           std::uint64_t titleId,
                                           const std::string &sourcePath,
                                           const std::string &remoteName) noexcept
{
    Endpoint endpoint{};
    if (!parse_endpoint(credentials, endpoint))
    {
        return {false, "Endereco IPv4 do servidor ausente ou invalido; reconecte o Nextcloud."};
    }
    std::string authorization{};
    if (!basic_authorization(credentials, authorization))
    {
        return {false, "Falha ao preparar autenticacao WebDAV."};
    }

    std::string basePath = credentials.basePath;
    while (!basePath.empty() && basePath.front() == '/') { basePath.erase(basePath.begin()); }
    while (!basePath.empty() && basePath.back() == '/') { basePath.pop_back(); }
    const std::string syncPath = "/" + basePath + "/Auto%20Sync/";
    const std::string titlePath = syncPath + encode_path_segment(titles::path_name(titleId)) + "/";
    const std::string uploadPath = titlePath + encode_path_segment(remoteName);

    int status{};
    log::write("WebDAV mbedTLS: TCP/TLS/MKCOL Auto Sync started.");
    Result result = request(endpoint, authorization, "MKCOL", syncPath, nullptr, status);
    log::write("WebDAV mbedTLS: MKCOL Auto Sync finished: HTTP %d%s.",
               status, result.success ? "" : " (failed)");
    if (!result.success) { return result; }
    if (status != 201 && status != 405) { return {false, "Criar pasta Auto Sync: HTTP " + std::to_string(status)}; }

    status = 0;
    log::write("WebDAV mbedTLS: MKCOL title started.");
    result = request(endpoint, authorization, "MKCOL", titlePath, nullptr, status);
    log::write("WebDAV mbedTLS: MKCOL title finished: HTTP %d%s.",
               status, result.success ? "" : " (failed)");
    if (!result.success) { return result; }
    if (status != 201 && status != 405) { return {false, "Criar pasta do jogo: HTTP " + std::to_string(status)}; }

    status = 0;
    log::write("WebDAV mbedTLS: PUT ZIP started.");
    result = request(endpoint, authorization, "PUT", uploadPath, &sourcePath, status);
    log::write("WebDAV mbedTLS: PUT ZIP finished: HTTP %d%s.",
               status, result.success ? "" : " (failed)");
    std::fill(authorization.begin(), authorization.end(), '\0');
    if (!result.success) { return result; }
    if (status == 200 || status == 201 || status == 204) { return {true, {}}; }
    return {false, "Enviar ZIP: HTTP " + std::to_string(status)};
}
