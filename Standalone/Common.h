#pragma once

#include "Vst2Abi.h"
#include "MidiPortCable.h"
#include "EventClock.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

namespace standalone
{

    constexpr int midiPortCount = hybrid::MidiPortCable::portCount;
    constexpr std::size_t maxSysexBytes = 1u << 20;
    constexpr std::size_t maxEventsPerBlock = 4096;

    inline std::atomic<bool> quitRequested{false};

    inline double nowSeconds()
    {
        static const double period = []
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);

            return 1.0 / static_cast<double>(f.QuadPart);
        }();
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);

        return static_cast<double>(c.QuadPart) * period;
    }

    inline BOOL WINAPI consoleHandler(DWORD)
    {
        quitRequested.store(true);

        return TRUE;
    }

} // namespace standalone
