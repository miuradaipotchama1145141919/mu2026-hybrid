#pragma once

#include <cstdint>
#include <span>

namespace hybrid {

[[nodiscard]] bool isSgConfiguration(
    std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool sgOwnsNote(std::uint32_t packedMessage,
                              std::uint32_t routeMask) noexcept;
constexpr std::uint8_t sgBankMsbSinging = 98;
constexpr std::uint8_t sgBankMsbAlternate = 82;

[[nodiscard]] constexpr bool isSgBankMsb(std::uint8_t bankMsb) noexcept
{
    return bankMsb == sgBankMsbSinging || bankMsb == sgBankMsbAlternate;
}

[[nodiscard]] bool isSgBankSelect(std::uint32_t packedMessage) noexcept;
[[nodiscard]] std::uint32_t updateSgBankMask(std::uint32_t packedMessage,
                                             std::uint32_t routeMask) noexcept;

} // namespace hybrid
