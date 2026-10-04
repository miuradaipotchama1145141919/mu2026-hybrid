#include "Common.h"
#include "MidiInput.h"
#include "Vst2Host.h"
#include "OfflineMidi.h"

using namespace standalone;

namespace
{

    struct Options
    {
        std::filesystem::path plugin;
        int portDevice[midiPortCount]{-1, -1, -1, -1};
        int outDevice = static_cast<int>(WAVE_MAPPER);
        int buffers = 6;
        bool verbose = false;
        bool list = false;
        bool render = false;
        bool traceMidi = false;
        std::filesystem::path renderInput;
        std::filesystem::path renderOutput;
        double renderSeconds = 0.0;
    };

    void printUsage()
    {
        std::printf(
            "mu2026-standalone -- mu2026-hybrid with real four-port MIDI\n"
            "\n"
            "  mu2026-standalone [--port0 N] [--port1 N] [--port2 N] [--port3 N]\n"
            "                    [--plugin PATH] [--out N] [--rate 44100|48000]\n"
            "                    [--block FRAMES] [--buffers N]\n"
            "                    [--verbose] [--list]\n"
            "\n"
            "  --portN N     open MIDI input device N as MU2000 MIDI IN A/B/C/D\n"
            "                (N = 0..3). Omit a port to leave it unconnected.\n"
            "  --plugin P    path to mu2026-hybrid.dll (default: beside this exe)\n"
            "  --out N       waveOut device index (default: system default)\n"
            "  --rate R      44100 (default, the engine's native rate) or 48000\n"
            "  --block F     frames per block, 64..4096 (default 256)\n"
            "  --buffers N   number of output buffers, 2..16 (default 6)\n"
            "  --render IN OUT  render a Standard MIDI File directly to a stereo WAV\n"
            "  --seconds N   offline render duration (default: last event + 3 seconds)\n"
            "  --trace-midi  print MIDI events during offline rendering\n"
            "  --verbose     print per-port message counts every 5 seconds\n"
            "  --list        list MIDI input and audio output devices, then exit\n");
    }

    Packet makePacket(const OfflineEvent &event)
    {
        const std::vector<std::uint8_t> &bytes = event.bytes;
        Packet packet;
        packet.port = static_cast<std::uint8_t>(event.port);
        packet.frame = event.frame;
        packet.sysex = bytes.size() > 3 || (!bytes.empty() && bytes[0] >= 0xf0);

        if (packet.sysex)
        {
            packet.bytes = bytes;

            return packet;
        }

        packet.status = bytes.size() > 0 ? bytes[0] : 0;
        packet.data1 = bytes.size() > 1 ? bytes[1] : 0;
        packet.data2 = bytes.size() > 2 ? bytes[2] : 0;

        return packet;
    }

    void traceEvent(std::size_t index, const OfflineEvent &event)
    {
        std::printf("MIDI event %zu: frame %llu port %d bytes:", index, (unsigned long long)event.frame, event.port + 1);

        for (std::uint8_t byte : event.bytes)
            std::printf(" %02X", byte);

        std::putchar('\n');
    }

    std::int16_t toSample(float value)
    {
        return static_cast<std::int16_t>(std::clamp(std::lrintf(value * 32768.0f), -32768l, 32767l));
    }

    int runOfflineRender(const Options &opt)
    {
        OfflineFile file;

        try
        {
            file = loadOfflineMidi(opt.renderInput, activeHost.sampleRate);
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "render: %s\n", error.what());

            return 1;
        }

        const std::uint64_t lastFrame = file.events.empty() ? 0 : file.events.back().frame;
        const auto sampleRate = static_cast<std::uint64_t>(activeHost.sampleRate);
        const std::uint64_t stopFrame = opt.renderSeconds > 0
                                            ? static_cast<std::uint64_t>(std::llround(opt.renderSeconds * activeHost.sampleRate))
                                            : lastFrame + 3ull * sampleRate;

        PluginHost plugin;
        std::string loadError;

        if (!plugin.load(opt.plugin, loadError))
        {
            std::fprintf(stderr, "%s\n", loadError.c_str());

            return 1;
        }

        vst2::AEffect *effect = plugin.effect();
        const int blockSize = activeHost.blockSize;
        const int channelCount = std::max(2, effect->numOutputs);

        std::vector<std::vector<float>> planes(channelCount, std::vector<float>(blockSize));
        std::vector<float *> outputs(channelCount);

        for (int channel = 0; channel < channelCount; ++channel)
            outputs[channel] = planes[channel].data();

        std::vector<float> silence(blockSize);
        float *inputs[2] = {silence.data(), silence.data()};

        std::vector<std::int16_t> pcm;
        pcm.reserve(static_cast<std::size_t>(stopFrame + blockSize) * 2);

        BlockEvents blockEvents;
        std::size_t nextEvent = 0;

        while (nextEvent < file.events.size() || pcm.size() / 2 < stopFrame)
        {
            const std::uint64_t blockStart = pcm.size() / 2;
            const std::uint64_t blockEnd = blockStart + blockSize;
            std::vector<Packet> packets;

            while (nextEvent < file.events.size() && file.events[nextEvent].frame < blockEnd)
            {
                const OfflineEvent &event = file.events[nextEvent];
                packets.push_back(makePacket(event));

                if (opt.traceMidi)
                    traceEvent(nextEvent, event);

                ++nextEvent;
            }

            blockEvents.addFrames(packets);

            if (auto *eventList = blockEvents.build(blockStart, blockSize))
                effect->dispatcher(effect, vst2::processEvents, 0, 0, eventList, 0.0f);

            effect->processReplacing(effect, inputs, outputs.data(), blockSize);

            for (int i = 0; i < blockSize; ++i)
            {
                pcm.push_back(toSample(planes[0][i]));
                pcm.push_back(toSample(planes[1][i]));
            }

            if (blockEnd >= stopFrame && nextEvent >= file.events.size())
                break;
        }

        try
        {
            writeWav16(opt.renderOutput, pcm, static_cast<std::uint32_t>(activeHost.sampleRate));
        }
        catch (const std::exception &error)
        {
            std::fprintf(stderr, "render: %s\n", error.what());
            plugin.unload();

            return 1;
        }

        std::printf("wrote %s (%.3f s)\n", opt.renderOutput.string().c_str(), double(pcm.size() / 2) / activeHost.sampleRate);
        plugin.unload();

        return 0;
    }

    bool parse(int argc, char **argv, Options &o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            auto next = [&](int &into)
            {
                if (i + 1 >= argc)
                    return false;
                into = std::atoi(argv[++i]);

                return true;
            };
            bool matchedPort = false;
            for (int p = 0; p < midiPortCount; ++p)
            {
                if (a == "--port" + std::to_string(p))
                {
                    matchedPort = true;
                    if (!next(o.portDevice[p]))
                        return false;
                }
            }

            if (matchedPort)
                continue;
            int v = 0;
            if (a == "--render" && i + 2 < argc)
            {
                o.render = true;
                o.renderInput = argv[++i];
                o.renderOutput = argv[++i];
            }
            else if (a == "--seconds" && i + 1 < argc)
            {
                o.renderSeconds = std::atof(argv[++i]);
            }
            else if (a == "--trace-midi")
            {
                o.traceMidi = true;
            }
            else if (a == "--plugin" && i + 1 < argc)
            {
                o.plugin = std::filesystem::path(argv[++i]);
            }
            else if (a == "--out")
            {
                if (!next(o.outDevice))
                    return false;
            }
            else if (a == "--rate")
            {
                if (!next(v))
                    return false;
                if (v != 44100 && v != 48000)
                {
                    std::fprintf(stderr, "--rate must be 44100 or 48000\n");

                    return false;
                }

                activeHost.sampleRate = static_cast<float>(v);
            }
            else if (a == "--block")
            {
                if (!next(v))
                    return false;
                activeHost.blockSize = std::clamp(v, 64, 4096);
            }
            else if (a == "--buffers")
            {
                if (!next(v))
                    return false;
                o.buffers = std::clamp(v, 2, 16);
            }
            else if (a == "--verbose")
            {
                o.verbose = true;
            }
            else if (a == "--list")
            {
                o.list = true;
            }
            else if (a == "--help" || a == "-h" || a == "/?")
            {
                printUsage();
                std::exit(0);
            }
            else
            {
                std::fprintf(stderr, "unknown option: %s\n", a.c_str());

                return false;
            }
        }

        return true;
    }

    void listDevices()
    {
        std::printf("MIDI inputs:\n");
        MidiInputs::listDevices();
        std::printf("Audio outputs:\n");
        const UINT n = waveOutGetNumDevs();
        for (UINT i = 0; i < n; ++i)
        {
            WAVEOUTCAPSW caps{};
            if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
                std::printf("  %u: %ls\n", i, caps.szPname);
        }
    }

    std::int16_t toPcm(float f)
    {
        const float s = std::clamp(f * 32767.0f, -32768.0f, 32767.0f);

        return static_cast<std::int16_t>(s);
    }

}

int main(int argc, char **argv)
{
    Options opt;
    if (!parse(argc, argv, opt))
    {
        printUsage();

        return 2;
    }

    if (opt.list)
    {
        listDevices();

        return 0;
    }

    if (opt.plugin.empty())
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        opt.plugin = std::filesystem::path(exe).parent_path() / L"mu2026-hybrid.dll";
    }

    if (opt.render)
    {
        return runOfflineRender(opt);
    }

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    PluginHost plugin;
    std::string error;
    std::printf("loading %s ...\n", opt.plugin.string().c_str());
    if (!plugin.load(opt.plugin, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());

        return 1;
    }

    vst2::AEffect *fx = plugin.effect();
    const int outChannels = std::max(2, fx->numOutputs);

    MidiInputs midi;
    const int opened = midi.start(opt.portDevice, opt.verbose);
    if (opened == 0)
        std::printf("no MIDI ports open (use --list, then --port0 N ...); "
                    "audio will run silent until something is connected.\n");

    const int block = activeHost.blockSize;
    const int rate = static_cast<int>(activeHost.sampleRate);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HANDLE bufferDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT out{};
    if (waveOutOpen(&out, static_cast<UINT>(opt.outDevice), &format,
                    reinterpret_cast<DWORD_PTR>(bufferDone), 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR)
    {
        std::fprintf(stderr, "cannot open audio output device %d\n", opt.outDevice);

        return 1;
    }

    std::vector<std::vector<std::int16_t>> pcm(
        opt.buffers, std::vector<std::int16_t>(block * 2));
    std::vector<WAVEHDR> headers(opt.buffers);
    for (int i = 0; i < opt.buffers; ++i)
    {
        headers[i] = {};
        headers[i].lpData = reinterpret_cast<LPSTR>(pcm[i].data());
        headers[i].dwBufferLength = static_cast<DWORD>(pcm[i].size() * sizeof(std::int16_t));
        waveOutPrepareHeader(out, &headers[i], sizeof(WAVEHDR));
    }

    std::vector<std::vector<float>> planes(outChannels, std::vector<float>(block));
    std::vector<float *> outPtrs(outChannels);
    for (int c = 0; c < outChannels; ++c)
        outPtrs[c] = planes[c].data();
    std::vector<float> silence(block, 0.0f);
    float *inPtrs[2] = {silence.data(), silence.data()};

    BlockEvents blockEvents;
    EventClock eventClock(rate, block);
    std::uint64_t streamFrame = 0;
    std::vector<Packet> drained;
    drained.reserve(maxEventsPerBlock);

    auto renderInto = [&](int index, bool prefill)
    {
        const double wake = nowSeconds();
        midi.drain(drained);
        eventClock.beginBlock(wake, streamFrame, prefill);
        blockEvents.add(drained, eventClock, streamFrame);
        if (auto *events = blockEvents.build(streamFrame, block))
            fx->dispatcher(fx, vst2::processEvents, 0, 0, events, 0.0f);

        fx->processReplacing(fx, inPtrs, outPtrs.data(), block);

        std::int16_t *dst = pcm[index].data();
        for (int i = 0; i < block; ++i)
        {
            dst[2 * i + 0] = toPcm(planes[0][i]);
            dst[2 * i + 1] = toPcm(planes[1][i]);
        }

        waveOutWrite(out, &headers[index], sizeof(WAVEHDR));
        streamFrame += static_cast<std::uint64_t>(block);
    };

    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    for (int i = 0; i < opt.buffers; ++i)
        renderInto(i, true);

    std::printf("audio: %d Hz, %d-frame blocks x %d buffers (~%.0f ms), "
                "%s timing. Ctrl+C to quit.\n",
                rate, block, opt.buffers,
                1000.0 * block * opt.buffers / rate,
                "sample-accurate");

    ULONGLONG nextReport = GetTickCount64() + 5000;
    while (!quitRequested.load())
    {
        WaitForSingleObject(bufferDone, 20);
        for (int i = 0; i < opt.buffers; ++i)
            if (headers[i].dwFlags & WHDR_DONE)
                renderInto(i, false);

        if (opt.verbose && GetTickCount64() >= nextReport)
        {
            nextReport += 5000;
            std::printf("msgs A/B/C/D: %llu/%llu/%llu/%llu  sysex: "
                        "%llu/%llu/%llu/%llu  dropped: %llu\n",
                        (unsigned long long)midi.messages(0),
                        (unsigned long long)midi.messages(1),
                        (unsigned long long)midi.messages(2),
                        (unsigned long long)midi.messages(3),
                        (unsigned long long)midi.sysexes(0),
                        (unsigned long long)midi.sysexes(1),
                        (unsigned long long)midi.sysexes(2),
                        (unsigned long long)midi.sysexes(3),
                        (unsigned long long)midi.dropped());
            std::printf("timing: sample-accurate, clock %s, period ratio %.6f, "
                        "early-clamped %llu\n",
                        eventClock.locked() ? "locked" : "not locked",
                        eventClock.periodRatio(),
                        (unsigned long long)eventClock.earlyClamped());
        }
    }

    waveOutReset(out);
    for (int i = 0; i < opt.buffers; ++i)
        waveOutUnprepareHeader(out, &headers[i], sizeof(WAVEHDR));
    waveOutClose(out);
    timeEndPeriod(1);
    midi.stop();
    plugin.unload();
    CloseHandle(bufferDone);

    return 0;
}
