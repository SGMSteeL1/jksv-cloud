#pragma once

#include <cstdint>
#include <string>

namespace sync::titles
{
    void set_service_ready(bool ready) noexcept;
    std::string display_name(std::uint64_t titleId) noexcept;
    std::string path_name(std::uint64_t titleId) noexcept;
}
