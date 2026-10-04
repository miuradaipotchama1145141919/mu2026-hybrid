#pragma once

#include "Common.h"

namespace standalone
{

    struct Packet
    {
        std::uint8_t port{};
        bool sysex{};
        std::uint8_t status{};
        std::uint8_t data1{};
        std::uint8_t data2{};
        double time{};
        std::uint64_t frame{};
        std::vector<std::uint8_t> bytes;
    };

    class MidiInputs
    {
    public:
        ~MidiInputs() { stop(); }

        int start(const int (&devices)[midiPortCount], bool verbose)
        {
            isVerbose = verbose;
            for (int p = 0; p < midiPortCount; ++p)
                deviceIds[p] = devices[p];
            readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            workerThread = std::thread([this]
                                       { serviceThread(); });
            WaitForSingleObject(readyEvent, 5000);

            return isOpened.load();
        }

        void stop()
        {
            if (!workerThread.joinable())
                return;
            PostThreadMessageW(serviceThreadId.load(), WM_QUIT, 0, 0);
            workerThread.join();
            if (readyEvent)
            {
                CloseHandle(readyEvent);
                readyEvent = nullptr;
            }
        }

        void drain(std::vector<Packet> &out)
        {
            out.clear();
            std::lock_guard<std::mutex> lock(queueMutex);
            out.swap(messageQueue);
        }

        std::uint64_t messages(int port) const { return messageCounts[port].load(); }
        std::uint64_t sysexes(int port) const { return sysexCountsByPort[port].load(); }
        std::uint64_t dropped() const { return droppedCount.load(); }

        static void listDevices()
        {
            const UINT n = midiInGetNumDevs();
            if (n == 0)
                std::printf("  (no MIDI input devices)\n");
            for (UINT i = 0; i < n; ++i)
            {
                MIDIINCAPSW caps{};
                if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
                    std::printf("  %u: %ls\n", i, caps.szPname);
            }
        }

    private:
        static constexpr int sysexBufferCount = 4;
        static constexpr DWORD sysexBufferSize = 8192;

        struct Device
        {
            HMIDIIN handle{};
            std::string name;
            MIDIHDR headers[sysexBufferCount]{};
            std::vector<char> storage[sysexBufferCount];
            std::vector<std::uint8_t> sysexAccum;
            bool inSysex{};
        };

        void serviceThread()
        {
            MSG msg;
            PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            serviceThreadId.store(GetCurrentThreadId());
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

            int opened = 0;
            for (int p = 0; p < midiPortCount; ++p)
            {
                if (deviceIds[p] < 0)
                    continue;
                Device &d = deviceStates[p];
                const MMRESULT r = midiInOpen(
                    &d.handle, static_cast<UINT>(deviceIds[p]),
                    static_cast<DWORD_PTR>(GetCurrentThreadId()), 0,
                    CALLBACK_THREAD);
                if (r != MMSYSERR_NOERROR)
                {
                    std::fprintf(stderr,
                                 "port %d: failed to open MIDI device %d (error %u)\n",
                                 p, deviceIds[p], static_cast<unsigned>(r));
                    d.handle = nullptr;
                    continue;
                }

                MIDIINCAPSW caps{};
                midiInGetDevCapsW(static_cast<UINT>(deviceIds[p]), &caps, sizeof(caps));
                char name[MAX_PATH]{};
                WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, sizeof(name),
                                    nullptr, nullptr);
                d.name = name;
                for (int b = 0; b < sysexBufferCount; ++b)
                {
                    d.storage[b].assign(sysexBufferSize, 0);
                    d.headers[b] = {};
                    d.headers[b].lpData = d.storage[b].data();
                    d.headers[b].dwBufferLength = sysexBufferSize;
                    midiInPrepareHeader(d.handle, &d.headers[b], sizeof(MIDIHDR));
                    midiInAddBuffer(d.handle, &d.headers[b], sizeof(MIDIHDR));
                }

                midiInStart(d.handle);
                ++opened;
                std::printf("port %d (MIDI IN %c): %s\n", p, char('A' + p),
                            d.name.c_str());
            }

            isOpened.store(opened);
            SetEvent(readyEvent);

            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                switch (msg.message)
                {
                case MM_MIM_DATA:
                    onShort(portOf(reinterpret_cast<HMIDIIN>(msg.wParam)),
                            static_cast<std::uint32_t>(msg.lParam));
                    break;
                case MM_MIM_LONGDATA:
                case MM_MIM_LONGERROR:
                    onLong(reinterpret_cast<HMIDIIN>(msg.wParam),
                           reinterpret_cast<MIDIHDR *>(msg.lParam));
                    break;
                default:
                    break;
                }
            }

            for (int p = 0; p < midiPortCount; ++p)
            {
                Device &d = deviceStates[p];
                if (!d.handle)
                    continue;
                midiInStop(d.handle);
                midiInReset(d.handle);
                for (int b = 0; b < sysexBufferCount; ++b)
                    midiInUnprepareHeader(d.handle, &d.headers[b], sizeof(MIDIHDR));
                midiInClose(d.handle);
                d.handle = nullptr;
            }
        }

        int portOf(HMIDIIN handle) const
        {
            for (int p = 0; p < midiPortCount; ++p)
                if (deviceStates[p].handle == handle)
                    return p;
            return -1;
        }

        void push(Packet &&packet)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            packet.time = nowSeconds();
            if (messageQueue.size() >= 65536)
            {
                droppedCount.fetch_add(1);

                return;
            }

            messageQueue.push_back(std::move(packet));
        }

        void onShort(int port, std::uint32_t message)
        {
            if (port < 0)
                return;
            const std::uint8_t status = message & 0xff;
            if (status < 0x80 || status >= 0xf0)
                return;
            Packet p;
            p.port = static_cast<std::uint8_t>(port);
            p.status = status;
            p.data1 = (message >> 8) & 0x7f;
            p.data2 = (message >> 16) & 0x7f;
            messageCounts[port].fetch_add(1);
            push(std::move(p));
        }

        void onLong(HMIDIIN handle, MIDIHDR *header)
        {
            const int port = portOf(handle);
            if (port < 0 || header == nullptr)
                return;
            Device &d = deviceStates[port];
            const auto *data = reinterpret_cast<const std::uint8_t *>(header->lpData);
            const DWORD size = header->dwBytesRecorded;

            for (DWORD i = 0; i < size; ++i)
            {
                const std::uint8_t byte = data[i];
                if (byte == 0xf0)
                {
                    d.sysexAccum.clear();
                    d.inSysex = true;
                    d.sysexAccum.push_back(byte);
                }
                else if (!d.inSysex)
                {
                    continue;
                }
                else if (byte == 0xf7)
                {
                    d.sysexAccum.push_back(byte);
                    Packet p;
                    p.port = static_cast<std::uint8_t>(port);
                    p.sysex = true;
                    p.bytes = std::move(d.sysexAccum);
                    d.sysexAccum.clear();
                    d.inSysex = false;
                    sysexCountsByPort[port].fetch_add(1);
                    push(std::move(p));
                }
                else if (byte & 0x80)
                {
                    d.sysexAccum.clear();
                    d.inSysex = false;
                }
                else
                {
                    if (d.sysexAccum.size() >= maxSysexBytes)
                    {
                        d.sysexAccum.clear();
                        d.inSysex = false;
                        droppedCount.fetch_add(1);
                    }
                    else
                    {
                        d.sysexAccum.push_back(byte);
                    }
                }
            }

            if (!quitRequested.load())
                midiInAddBuffer(handle, header, sizeof(MIDIHDR));
        }

        int deviceIds[midiPortCount]{-1, -1, -1, -1};
        Device deviceStates[midiPortCount];
        bool isVerbose{};
        HANDLE readyEvent{};
        std::thread workerThread;
        std::atomic<DWORD> serviceThreadId{0};
        std::atomic<int> isOpened{0};

        std::mutex queueMutex;
        std::vector<Packet> messageQueue;

        std::atomic<std::uint64_t> messageCounts[midiPortCount]{};
        std::atomic<std::uint64_t> sysexCountsByPort[midiPortCount]{};
        std::atomic<std::uint64_t> droppedCount{0};
    };

} // namespace standalone
