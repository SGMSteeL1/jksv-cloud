#include "Config.hpp"
#include "Log.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <switch.h>
#include <sys/stat.h>

namespace
{
    Mutex g_logMutex{};

    void ensure_config_directory() noexcept
    {
        mkdir("sdmc:/config", 0777);
        mkdir(sync::CONFIG_DIRECTORY, 0777);
    }
} // namespace

void sync::log::initialize() noexcept
{
    ensure_config_directory();
    write("JKSV Cloud Sync v%s started.", sync::VERSION);
}

void sync::log::write(const char *format, ...) noexcept
{
    if (!format) { return; }
    mutexLock(&g_logMutex);
    ensure_config_directory();

    std::FILE *file = std::fopen(sync::LOG_PATH, "ab");
    if (!file)
    {
        mutexUnlock(&g_logMutex);
        return;
    }

    const std::uint64_t frequency = armGetSystemTickFreq();
    const std::uint64_t uptime = frequency > 0 ? armGetSystemTick() / frequency : 0;
    std::fprintf(file, "[uptime %llu s] ", static_cast<unsigned long long>(uptime));

    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fclose(file);
    mutexUnlock(&g_logMutex);
}

void sync::log::result(const char *operation, unsigned int value) noexcept
{
    write("%s failed: 0x%08X.", operation ? operation : "Operation", value);
}
