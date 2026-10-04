#pragma once

#include "Common.h"

namespace standalone
{

    struct HostConfig
    {
        float sampleRate = 44100.0f;
        int blockSize = 256;
    };
    inline HostConfig activeHost;

    inline vst2::IntPtr hostCallback(vst2::AEffect *, std::int32_t opcode, std::int32_t,
                                     vst2::IntPtr, void *, float)
    {
        switch (opcode)
        {
        case vst2::hostVersion:
            return 2400;
        case vst2::hostGetSampleRate:
            return static_cast<vst2::IntPtr>(activeHost.sampleRate);
        case vst2::hostGetBlockSize:
            return activeHost.blockSize;
        default:
            return 0;
        }
    }

    inline void diagnoseInitFailure(const std::filesystem::path &plugin)
    {
        namespace fs = std::filesystem;
        const fs::path dir = plugin.parent_path();
        std::fprintf(stderr, "\nchecking %s:\n", dir.string().c_str());

        const fs::path engine = dir / L"mu2000-engine.bin";
        if (!fs::is_regular_file(engine))
        {
            std::fprintf(stderr, "  [MISSING] mu2000-engine.bin (the Mu2026 S-MU2000 "
                                 "fork DLL, renamed)\n");
        }
        else if (HMODULE m = LoadLibraryW(engine.c_str()))
        {
            const bool hasEntry = GetProcAddress(m, "VSTPluginMain") != nullptr;
            const bool hasNative =
                GetProcAddress(m, "Mu2026SetNativeEngine") != nullptr;
            FreeLibrary(m);
            std::fprintf(stderr, "  [ok]      mu2000-engine.bin loads\n");
            if (!hasEntry)
                std::fprintf(stderr, "  [BAD]     it has no VSTPluginMain export\n");
            if (!hasNative)
                std::fprintf(stderr,
                             "  [WARN]    no Mu2026SetNativeEngine export: this looks like "
                             "upstream S-MU2000, not the Mu2026 fork the wrapper needs\n");
        }
        else
        {
            const DWORD e = GetLastError();
            std::fprintf(stderr, "  [BAD]     mu2000-engine.bin will not load "
                                 "(Win32 error %lu%s)\n",
                         e,
                         e == 193   ? ": wrong bitness, it must be a 32-bit DLL"
                         : e == 126 ? ": a DLL it depends on is missing (often the "
                                      "Visual C++ runtime)"
                                    : "");
        }

        fs::path roms = dir / L"roms";
        if (!fs::is_directory(roms))
        {
            std::ifstream pointer(dir / L"roms.txt");
            std::string line;
            if (std::getline(pointer, line))
            {
                const fs::path specified(std::u8string(line.begin(), line.end()));
                roms = specified.is_relative()
                           ? (dir / specified).lexically_normal()
                           : specified;
                std::fprintf(stderr, "  roms.txt points to %s\n",
                             roms.string().c_str());
            }
        }

        std::error_code ec;
        const fs::path flash = roms / L"mu2000_flash.bin";
        const auto size = fs::is_regular_file(flash, ec)
                              ? fs::file_size(flash, ec)
                              : std::uintmax_t(0);
        if (!fs::is_regular_file(flash))
            std::fprintf(stderr, "  [MISSING] %s\n", flash.string().c_str());
        else if (size != 4u * 1024u * 1024u)
            std::fprintf(stderr, "  [BAD]     mu2000_flash.bin is %llu bytes, "
                                 "expected 4194304\n",
                         (unsigned long long)size);
        else
            std::fprintf(stderr, "  [ok]      mu2000_flash.bin (4 MB)\n");

        std::fprintf(stderr,
                     "  [?]       the engine also needs its four 8 MB wave ROMs; the "
                     "standalone cannot check them.\n"
                     "  (Missing VL/SG workers or .vxd files do not cause this failure; they "
                     "only disable VL/SG.)\n\n");
    }

    class PluginHost
    {
    public:
        ~PluginHost() { unload(); }

        bool load(const std::filesystem::path &path, std::string &error)
        {
            pluginModule = LoadLibraryW(path.c_str());
            if (!pluginModule)
            {
                error = "cannot load " + path.string() + " (Win32 error " + std::to_string(GetLastError()) + ")";
                return false;
            }

            const auto entry = reinterpret_cast<vst2::EntryPoint>(
                GetProcAddress(pluginModule, "VSTPluginMain"));
            if (!entry)
            {
                error = "no VSTPluginMain export in " + path.string();

                return false;
            }

            pluginEffect = entry(hostCallback);
            if (!pluginEffect || pluginEffect->magic != vst2::effectMagic)
            {
                diagnoseInitFailure(path);
                error = "plugin failed to initialise. Check that mu2000-engine.bin, "
                        "the ROMs and the worker executables sit beside the DLL.";
                pluginEffect = nullptr;

                return false;
            }

            pluginEffect->dispatcher(pluginEffect, vst2::open, 0, 0, nullptr, 0.0f);
            pluginEffect->dispatcher(pluginEffect, vst2::setSampleRate, 0, 0, nullptr,
                                     activeHost.sampleRate);
            pluginEffect->dispatcher(pluginEffect, vst2::setBlockSize, 0, activeHost.blockSize,
                                     nullptr, 0.0f);
            pluginEffect->dispatcher(pluginEffect, vst2::mainsChanged, 0, 1, nullptr, 0.0f);

            return true;
        }

        void unload()
        {
            if (pluginEffect)
            {
                pluginEffect->dispatcher(pluginEffect, vst2::mainsChanged, 0, 0, nullptr, 0.0f);
                pluginEffect->dispatcher(pluginEffect, vst2::close, 0, 0, nullptr, 0.0f);
                pluginEffect = nullptr;
            }

            if (pluginModule)
            {
                FreeLibrary(pluginModule);
                pluginModule = nullptr;
            }
        }

        vst2::AEffect *effect() const { return pluginEffect; }

    private:
        HMODULE pluginModule{};
        vst2::AEffect *pluginEffect{};
    };

    class BlockEvents
    {
    public:
        BlockEvents()
            : eventStorageBytes(sizeof(vst2::Events) + sizeof(vst2::Event *) * maxEventsPerBlock * 2),
              eventStorage(eventStorageBytes)
        {
            midiInput.reserve(maxEventsPerBlock * 2);
            sysexAssemblers.reserve(maxEventsPerBlock);
            pendingMessages.reserve(maxEventsPerBlock);
            inFlightBuffers.reserve(maxEventsPerBlock);
        }

        void add(std::vector<Packet> &drained, EventClock &clock,
                 std::uint64_t blockStart)
        {
            for (auto &p : drained)
            {
                p.frame = clock.frameFor(p.time);
                pendingMessages.push_back(std::move(p));
            }

            drained.clear();
            (void)blockStart;
        }

        void addFrames(std::vector<Packet> &packets)
        {
            for (auto &p : packets)
                pendingMessages.push_back(std::move(p));
            packets.clear();
        }

        vst2::Events *build(std::uint64_t blockStart, std::int32_t frames)
        {
            midiInput.clear();
            sysexAssemblers.clear();
            inFlightBuffers.clear();
            if (pendingMessages.empty())
                return nullptr;

            auto *list = reinterpret_cast<vst2::Events *>(eventStorage.data());
            std::memset(list, 0, sizeof(vst2::Events));
            auto **slots = list->events;
            std::int32_t count = 0;

            int cable = -1;
            std::size_t used = 0;
            for (; used < pendingMessages.size(); ++used)
            {
                if (count + 2 >= static_cast<std::int32_t>(maxEventsPerBlock))
                    break;
                const Packet &p = pendingMessages[used];
                const std::int32_t delta = p.frame <= blockStart
                                               ? 0
                                               : static_cast<std::int32_t>(std::min<std::uint64_t>(
                                                     p.frame - blockStart,
                                                     static_cast<std::uint64_t>(frames)));
                if (delta >= frames)
                    break;

                if (cable != p.port)
                {
                    cable = p.port;
                    const auto sel = hybrid::MidiPortCable::selector(p.port);
                    vst2::MidiEvent e;
                    e.midiData[0] = static_cast<char>(sel[0]);
                    e.midiData[1] = static_cast<char>(sel[1]);
                    e.deltaFrames = delta;
                    midiInput.push_back(e);
                    slots[count++] = reinterpret_cast<vst2::Event *>(&midiInput.back());
                }

                if (p.sysex)
                {
                    vst2::SysexEvent e;
                    e.deltaFrames = delta;
                    e.dumpBytes = static_cast<std::int32_t>(p.bytes.size());
                    e.sysexDump = const_cast<char *>(
                        reinterpret_cast<const char *>(p.bytes.data()));
                    sysexAssemblers.push_back(e);
                    slots[count++] = reinterpret_cast<vst2::Event *>(&sysexAssemblers.back());
                }
                else
                {
                    vst2::MidiEvent e;
                    e.midiData[0] = static_cast<char>(p.status);
                    e.midiData[1] = static_cast<char>(p.data1);
                    e.midiData[2] = static_cast<char>(p.data2);
                    e.deltaFrames = delta;
                    midiInput.push_back(e);
                    slots[count++] = reinterpret_cast<vst2::Event *>(&midiInput.back());
                }
            }

            inFlightBuffers.assign(std::make_move_iterator(pendingMessages.begin()),
                                   std::make_move_iterator(pendingMessages.begin() + used));
            pendingMessages.erase(pendingMessages.begin(), pendingMessages.begin() + used);
            list->numEvents = count;

            return list;
        }

    private:
        std::size_t eventStorageBytes;
        std::vector<char> eventStorage;
        std::vector<vst2::MidiEvent> midiInput;
        std::vector<vst2::SysexEvent> sysexAssemblers;
        std::vector<Packet> pendingMessages;
        std::vector<Packet> inFlightBuffers;
    };

} // namespace standalone
