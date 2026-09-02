#pragma once

namespace sync::log
{
    void initialize() noexcept;
    void write(const char *format, ...) noexcept __attribute__((format(printf, 1, 2)));
    void result(const char *operation, unsigned int value) noexcept;
} // namespace sync::log
