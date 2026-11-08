#pragma once
#include <cstdint>

namespace nssm_next {
namespace detail {
constexpr std::uint32_t MAX_APPLICATION_BYTES = 64 * 1024;
constexpr std::uint32_t UTF16_CHARACTER_BYTES = 2;

constexpr bool IsValidApplicationBytes(std::uint32_t bytes) noexcept {
    return bytes >= UTF16_CHARACTER_BYTES && bytes <= MAX_APPLICATION_BYTES &&
           bytes % UTF16_CHARACTER_BYTES == 0;
}
} // namespace detail
} // namespace nssm_next
