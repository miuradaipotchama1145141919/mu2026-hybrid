#pragma once

#include <array>
#include <cstdint>

namespace hybrid {

class MidiPortCable {
public:
    static constexpr std::uint8_t portCount = 4;
    static constexpr std::uint8_t cableStatus = 0xf5;

    // Selection
    bool observe(std::uint8_t status, std::uint8_t data1) noexcept
    {
        if (status != cableStatus)
            return false;

        if (data1 >= 1 && data1 <= portCount)
            portIndex = static_cast<std::uint8_t>(data1 - 1);

        return true;
    }

    void reset() noexcept { portIndex = 0; }

    // State
    std::uint8_t port() const noexcept { return portIndex; }

    bool isPrimary() const noexcept { return portIndex == 0; }

    static std::array<std::uint8_t, 2> selector(std::uint8_t port) noexcept
    {
        const std::uint8_t validPort = port < portCount ? port : 0;

        return { cableStatus, static_cast<std::uint8_t>(validPort + 1) };
    }

private:
    std::uint8_t portIndex {};
};

} // namespace hybrid
