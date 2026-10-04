#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace standalone
{

    class EventClock
    {
    public:
        EventClock(double sampleRate, int blockFrames, double bandwidthHz = 0.5)
            : sampleRateHz(sampleRate),
              nominalPeriod(blockFrames / sampleRate),
              period(blockFrames / sampleRate)
        {
            const double omega = 2.0 * 3.14159265358979323846 * bandwidthHz * nominalPeriod;

            phaseGain = std::sqrt(2.0) * omega;
            periodGain = omega * omega;
        }

        // Block timing
        void beginBlock(double nowSeconds, std::uint64_t startFrame, bool prefill)
        {
            blockStart = startFrame;

            if (prefill)
                return;

            if (!isLocked)
            {
                current = nowSeconds;
                previous = nowSeconds - nominalPeriod;
                period = nominalPeriod;
                isLocked = true;

                return;
            }

            const double predicted = current + period;
            const double error = nowSeconds - predicted;

            previous = current;
            current = predicted + phaseGain * error;
            period = std::clamp(period + periodGain * error, nominalPeriod * 0.98, nominalPeriod * 1.02);

            if (current <= previous)
                current = previous + nominalPeriod * 0.5;
        }

        // Event placement
        std::uint64_t frameFor(double eventSeconds)
        {
            std::uint64_t frame = blockStart;

            if (isLocked)
            {
                const double offset = (eventSeconds - previous) * sampleRateHz;

                if (offset < 0.0)
                    ++earlyCount;
                else
                    frame = blockStart + static_cast<std::uint64_t>(offset + 0.5);
            }

            frame = std::max(frame, blockStart);
            frame = std::max(frame, lastFrame);
            lastFrame = frame;

            return frame;
        }

        // State
        bool locked() const noexcept { return isLocked; }

        double periodRatio() const noexcept { return period / nominalPeriod; }

        std::uint64_t earlyClamped() const noexcept { return earlyCount; }

    private:
        double sampleRateHz;
        double nominalPeriod;
        double period;
        double phaseGain{};
        double periodGain{};
        double previous{};
        double current{};
        bool isLocked{};
        std::uint64_t blockStart{};
        std::uint64_t lastFrame{};
        std::uint64_t earlyCount{};
    };

} // namespace standalone
