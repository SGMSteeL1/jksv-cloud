#include "util/QrCode.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace
{
    constexpr int VERSION = 10;
    constexpr int FORMAT_ECC_LOW = 1;
    constexpr std::array<int, 4> BLOCK_DATA_LENGTHS{68, 68, 69, 69};

    constexpr int max_int(int a, int b) noexcept { return a > b ? a : b; }
} // namespace

bool util::QrCode::encode_text(std::string_view text) noexcept
{
    if (text.empty() || text.size() > MAX_TEXT_BYTES) { return false; }

    m_modules.fill(false);
    m_function.fill(false);

    std::array<std::uint8_t, DATA_CODEWORDS> data{};
    int bitLength = 0;

    // Byte mode followed by the 16-bit character count used by versions 10-26.
    if (!append_bits(0x4, 4, data, bitLength) ||
        !append_bits(static_cast<std::uint32_t>(text.size()), 16, data, bitLength))
    {
        return false;
    }

    for (const unsigned char value : text)
    {
        if (!append_bits(value, 8, data, bitLength)) { return false; }
    }

    const int capacityBits = DATA_CODEWORDS * 8;
    append_bits(0, std::min(4, capacityBits - bitLength), data, bitLength);
    append_bits(0, (8 - bitLength % 8) % 8, data, bitLength);

    int dataLength = bitLength / 8;
    for (std::uint8_t pad = 0xEC; dataLength < DATA_CODEWORDS; pad ^= 0xFD)
    {
        data[dataLength++] = pad;
    }

    draw_function_patterns();
    const auto allCodewords = add_error_correction(data);
    draw_codewords(allCodewords);

    // Mask 0 is deterministic and performs well for HTTPS Login Flow URLs.
    apply_mask(0);
    draw_format_bits(0);
    return true;
}

bool util::QrCode::get_module(int x, int y) const noexcept
{
    return 0 <= x && x < SIZE && 0 <= y && y < SIZE && m_modules[y * SIZE + x];
}

void util::QrCode::set_function_module(int x, int y, bool dark) noexcept
{
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) { return; }
    m_modules[y * SIZE + x] = dark;
    m_function[y * SIZE + x] = true;
}

void util::QrCode::draw_function_patterns() noexcept
{
    for (int i = 0; i < SIZE; ++i)
    {
        set_function_module(6, i, i % 2 == 0);
        set_function_module(i, 6, i % 2 == 0);
    }

    draw_finder_pattern(3, 3);
    draw_finder_pattern(SIZE - 4, 3);
    draw_finder_pattern(3, SIZE - 4);

    constexpr std::array<int, 3> alignmentPositions{6, 28, 50};
    for (int yIndex = 0; yIndex < static_cast<int>(alignmentPositions.size()); ++yIndex)
    {
        for (int xIndex = 0; xIndex < static_cast<int>(alignmentPositions.size()); ++xIndex)
        {
            const bool overlapsFinder =
                (xIndex == 0 && yIndex == 0) ||
                (xIndex == 0 && yIndex == static_cast<int>(alignmentPositions.size()) - 1) ||
                (xIndex == static_cast<int>(alignmentPositions.size()) - 1 && yIndex == 0);
            if (!overlapsFinder)
            {
                draw_alignment_pattern(alignmentPositions[xIndex], alignmentPositions[yIndex]);
            }
        }
    }

    // Writing placeholder format information also reserves its modules.
    draw_format_bits(0);
    draw_version();
}

void util::QrCode::draw_finder_pattern(int centerX, int centerY) noexcept
{
    for (int dy = -4; dy <= 4; ++dy)
    {
        for (int dx = -4; dx <= 4; ++dx)
        {
            const int distance = max_int(std::abs(dx), std::abs(dy));
            set_function_module(centerX + dx, centerY + dy, distance != 2 && distance != 4);
        }
    }
}

void util::QrCode::draw_alignment_pattern(int centerX, int centerY) noexcept
{
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            set_function_module(centerX + dx,
                                centerY + dy,
                                max_int(std::abs(dx), std::abs(dy)) != 1);
        }
    }
}

void util::QrCode::draw_format_bits(int mask) noexcept
{
    const int data = (FORMAT_ECC_LOW << 3) | mask;
    int remainder = data;
    for (int i = 0; i < 10; ++i)
    {
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    }
    const int bits = ((data << 10) | remainder) ^ 0x5412;

    for (int i = 0; i <= 5; ++i) { set_function_module(8, i, ((bits >> i) & 1) != 0); }
    set_function_module(8, 7, ((bits >> 6) & 1) != 0);
    set_function_module(8, 8, ((bits >> 7) & 1) != 0);
    set_function_module(7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; ++i) { set_function_module(14 - i, 8, ((bits >> i) & 1) != 0); }

    for (int i = 0; i < 8; ++i)
    {
        set_function_module(SIZE - 1 - i, 8, ((bits >> i) & 1) != 0);
    }
    for (int i = 8; i < 15; ++i)
    {
        set_function_module(8, SIZE - 15 + i, ((bits >> i) & 1) != 0);
    }
    set_function_module(8, SIZE - 8, true);
}

void util::QrCode::draw_version() noexcept
{
    int remainder = VERSION;
    for (int i = 0; i < 12; ++i)
    {
        remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
    }
    const int bits = (VERSION << 12) | remainder;
    for (int i = 0; i < 18; ++i)
    {
        const bool bit = ((bits >> i) & 1) != 0;
        const int a = SIZE - 11 + i % 3;
        const int b = i / 3;
        set_function_module(a, b, bit);
        set_function_module(b, a, bit);
    }
}

void util::QrCode::draw_codewords(const std::array<std::uint8_t, TOTAL_CODEWORDS> &data) noexcept
{
    int bitIndex = 0;
    for (int right = SIZE - 1; right >= 1; right -= 2)
    {
        if (right == 6) { right = 5; }
        for (int vertical = 0; vertical < SIZE; ++vertical)
        {
            const bool upward = ((right + 1) & 2) == 0;
            const int y = upward ? SIZE - 1 - vertical : vertical;
            for (int column = 0; column < 2; ++column)
            {
                const int x = right - column;
                if (m_function[y * SIZE + x]) { continue; }
                if (bitIndex < TOTAL_CODEWORDS * 8)
                {
                    m_modules[y * SIZE + x] =
                        ((data[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1) != 0;
                    ++bitIndex;
                }
            }
        }
    }
}

void util::QrCode::apply_mask(int mask) noexcept
{
    for (int y = 0; y < SIZE; ++y)
    {
        for (int x = 0; x < SIZE; ++x)
        {
            if (!m_function[y * SIZE + x] && mask == 0 && (x + y) % 2 == 0)
            {
                m_modules[y * SIZE + x] = !m_modules[y * SIZE + x];
            }
        }
    }
}

bool util::QrCode::append_bits(std::uint32_t value,
                               int length,
                               std::array<std::uint8_t, DATA_CODEWORDS> &data,
                               int &bitLength) noexcept
{
    if (length < 0 || length > 31 || bitLength + length > DATA_CODEWORDS * 8) { return false; }
    for (int i = length - 1; i >= 0; --i, ++bitLength)
    {
        if (((value >> i) & 1U) != 0)
        {
            data[bitLength >> 3] |= static_cast<std::uint8_t>(1U << (7 - (bitLength & 7)));
        }
    }
    return true;
}

std::uint8_t util::QrCode::reed_solomon_multiply(std::uint8_t x, std::uint8_t y) noexcept
{
    int product = 0;
    for (int i = 7; i >= 0; --i)
    {
        product = (product << 1) ^ ((product >> 7) * 0x11D);
        product ^= ((x >> i) & 1U) * y;
    }
    return static_cast<std::uint8_t>(product);
}

std::array<std::uint8_t, util::QrCode::ECC_CODEWORDS_PER_BLOCK>
    util::QrCode::reed_solomon_divisor() noexcept
{
    std::array<std::uint8_t, ECC_CODEWORDS_PER_BLOCK> result{};
    result.back() = 1;
    std::uint8_t root = 1;
    for (int i = 0; i < ECC_CODEWORDS_PER_BLOCK; ++i)
    {
        for (int j = 0; j < ECC_CODEWORDS_PER_BLOCK; ++j)
        {
            result[j] = reed_solomon_multiply(result[j], root);
            if (j + 1 < ECC_CODEWORDS_PER_BLOCK) { result[j] ^= result[j + 1]; }
        }
        root = reed_solomon_multiply(root, 0x02);
    }
    return result;
}

std::array<std::uint8_t, util::QrCode::ECC_CODEWORDS_PER_BLOCK>
    util::QrCode::reed_solomon_remainder(const std::uint8_t *data, int length) noexcept
{
    const auto divisor = reed_solomon_divisor();
    std::array<std::uint8_t, ECC_CODEWORDS_PER_BLOCK> result{};
    for (int byteIndex = 0; byteIndex < length; ++byteIndex)
    {
        const std::uint8_t factor = data[byteIndex] ^ result.front();
        for (int i = 0; i + 1 < ECC_CODEWORDS_PER_BLOCK; ++i) { result[i] = result[i + 1]; }
        result.back() = 0;
        for (int i = 0; i < ECC_CODEWORDS_PER_BLOCK; ++i)
        {
            result[i] ^= reed_solomon_multiply(divisor[i], factor);
        }
    }
    return result;
}

std::array<std::uint8_t, util::QrCode::TOTAL_CODEWORDS>
    util::QrCode::add_error_correction(const std::array<std::uint8_t, DATA_CODEWORDS> &data) noexcept
{
    std::array<std::array<std::uint8_t, 69>, BLOCK_COUNT> blocks{};
    std::array<std::array<std::uint8_t, ECC_CODEWORDS_PER_BLOCK>, BLOCK_COUNT> ecc{};

    int dataOffset = 0;
    for (int block = 0; block < BLOCK_COUNT; ++block)
    {
        const int blockLength = BLOCK_DATA_LENGTHS[block];
        std::copy_n(data.begin() + dataOffset, blockLength, blocks[block].begin());
        ecc[block] = reed_solomon_remainder(blocks[block].data(), blockLength);
        dataOffset += blockLength;
    }

    std::array<std::uint8_t, TOTAL_CODEWORDS> result{};
    int resultOffset = 0;
    for (int i = 0; i < 69; ++i)
    {
        for (int block = 0; block < BLOCK_COUNT; ++block)
        {
            if (i < BLOCK_DATA_LENGTHS[block]) { result[resultOffset++] = blocks[block][i]; }
        }
    }
    for (int i = 0; i < ECC_CODEWORDS_PER_BLOCK; ++i)
    {
        for (int block = 0; block < BLOCK_COUNT; ++block)
        {
            result[resultOffset++] = ecc[block][i];
        }
    }
    return result;
}
