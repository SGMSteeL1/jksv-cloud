#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace util
{
    /// @brief Small fixed-version QR encoder for HTTPS login URLs.
    ///
    /// Version 10-L is intentionally used for every code. It supports up to
    /// 271 UTF-8 bytes and avoids any runtime dependency on a browser applet.
    class QrCode final
    {
        public:
            static constexpr int SIZE = 57;
            static constexpr int MAX_TEXT_BYTES = 271;

            bool encode_text(std::string_view text) noexcept;
            bool get_module(int x, int y) const noexcept;

        private:
            static constexpr int DATA_CODEWORDS = 274;
            static constexpr int TOTAL_CODEWORDS = 346;
            static constexpr int ECC_CODEWORDS_PER_BLOCK = 18;
            static constexpr int BLOCK_COUNT = 4;

            std::array<bool, SIZE * SIZE> m_modules{};
            std::array<bool, SIZE * SIZE> m_function{};

            void set_function_module(int x, int y, bool dark) noexcept;
            void draw_function_patterns() noexcept;
            void draw_finder_pattern(int centerX, int centerY) noexcept;
            void draw_alignment_pattern(int centerX, int centerY) noexcept;
            void draw_format_bits(int mask) noexcept;
            void draw_version() noexcept;
            void draw_codewords(const std::array<std::uint8_t, TOTAL_CODEWORDS> &data) noexcept;
            void apply_mask(int mask) noexcept;

            static bool append_bits(std::uint32_t value,
                                    int length,
                                    std::array<std::uint8_t, DATA_CODEWORDS> &data,
                                    int &bitLength) noexcept;
            static std::uint8_t reed_solomon_multiply(std::uint8_t x, std::uint8_t y) noexcept;
            static std::array<std::uint8_t, ECC_CODEWORDS_PER_BLOCK> reed_solomon_divisor() noexcept;
            static std::array<std::uint8_t, ECC_CODEWORDS_PER_BLOCK>
                reed_solomon_remainder(const std::uint8_t *data, int length) noexcept;
            static std::array<std::uint8_t, TOTAL_CODEWORDS>
                add_error_correction(const std::array<std::uint8_t, DATA_CODEWORDS> &data) noexcept;
    };
} // namespace util
