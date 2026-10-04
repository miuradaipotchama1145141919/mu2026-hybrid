#pragma once

#include "Common.h"

namespace standalone
{

    struct OfflineEvent
    {
        std::uint64_t frame{};
        std::uint32_t order{};
        int port{};
        std::vector<std::uint8_t> bytes;
    };

    struct OfflineFile
    {
        std::uint16_t ppqn{};
        std::vector<OfflineEvent> events;
    };

    struct TempoPoint
    {
        std::uint64_t tick{};
        std::uint32_t usPerQuarter{500000};
    };

    struct TrackState
    {
        std::uint64_t tick{};
        std::uint8_t runningStatus{};
        int port{};
    };

    struct ParseContext
    {
        const std::vector<std::uint8_t> &bytes;
        std::size_t position{};
        std::size_t end{};
        std::uint32_t nextOrder{};
        std::vector<OfflineEvent> events;
        std::vector<TempoPoint> tempos{{0, 500000}};
    };

    // Binary reading
    inline std::uint16_t readUint16(const std::vector<std::uint8_t> &bytes, std::size_t &position)
    {
        if (position + 2 > bytes.size())
            throw std::runtime_error("truncated MIDI file");

        const auto value = static_cast<std::uint16_t>((bytes[position] << 8) | bytes[position + 1]);
        position += 2;

        return value;
    }

    inline std::uint32_t readUint32(const std::vector<std::uint8_t> &bytes, std::size_t &position)
    {
        if (position + 4 > bytes.size())
            throw std::runtime_error("truncated MIDI file");

        const std::uint32_t value = (std::uint32_t(bytes[position]) << 24) | (std::uint32_t(bytes[position + 1]) << 16) | (std::uint32_t(bytes[position + 2]) << 8) | bytes[position + 3];
        position += 4;

        return value;
    }

    inline std::uint32_t readVarLength(const std::vector<std::uint8_t> &bytes, std::size_t &position)
    {
        std::uint32_t value = 0;

        for (int i = 0; i < 4; ++i)
        {
            if (position >= bytes.size())
                throw std::runtime_error("truncated MIDI VLQ");

            const std::uint8_t current = bytes[position++];
            value = (value << 7) | (current & 0x7f);

            if (!(current & 0x80))
                return value;
        }

        throw std::runtime_error("invalid MIDI VLQ");
    }

    // Track parsing
    inline std::vector<std::uint8_t> readShortMessage(ParseContext &context, std::uint8_t status, int dataCount, const char *errorText)
    {
        if (context.position + dataCount > context.end)
            throw std::runtime_error(errorText);

        std::vector<std::uint8_t> message(1 + dataCount);
        message[0] = status;

        for (int i = 0; i < dataCount; ++i)
            message[1 + i] = context.bytes[context.position++];

        return message;
    }

    inline void addEvent(ParseContext &context, const TrackState &track, std::vector<std::uint8_t> message)
    {
        context.events.push_back({track.tick, context.nextOrder++, track.port, std::move(message)});
    }

    // Returns true when the end of track meta event was read
    inline bool readMetaEvent(ParseContext &context, TrackState &track)
    {
        if (context.position >= context.end)
            throw std::runtime_error("truncated MIDI meta event");

        const std::uint8_t type = context.bytes[context.position++];
        const std::uint32_t length = readVarLength(context.bytes, context.position);

        if (context.position + length > context.end)
            throw std::runtime_error("truncated MIDI meta payload");

        const std::uint8_t *payload = context.bytes.data() + context.position;

        if (type == 0x51 && length == 3)
        {
            const std::uint32_t usPerQuarter = (std::uint32_t(payload[0]) << 16) | (std::uint32_t(payload[1]) << 8) | payload[2];

            context.tempos.push_back({track.tick, usPerQuarter});
        }
        else if (type == 0x21 && length >= 1)
        {
            track.port = payload[0] & 3;
        }

        context.position += length;

        return type == 0x2f;
    }

    inline void readSysexEvent(ParseContext &context, const TrackState &track, std::uint8_t status)
    {
        const std::uint32_t length = readVarLength(context.bytes, context.position);

        if (context.position + length > context.end)
            throw std::runtime_error("truncated MIDI SysEx");

        std::vector<std::uint8_t> message;

        if (status == 0xf0)
            message.push_back(0xf0);

        const auto payloadStart = context.bytes.begin() + context.position;
        message.insert(message.end(), payloadStart, payloadStart + length);

        if (status == 0xf0 && message.back() != 0xf7)
            message.push_back(0xf7);

        addEvent(context, track, std::move(message));
        context.position += length;
    }

    inline void readTrack(ParseContext &context)
    {
        TrackState track;

        while (context.position < context.end)
        {
            track.tick += readVarLength(context.bytes, context.position);

            if (context.position >= context.end)
                break;

            std::uint8_t status = context.bytes[context.position++];

            if (status < 0x80)
            {
                if (!track.runningStatus)
                    throw std::runtime_error("MIDI data without running status");

                --context.position;
                status = track.runningStatus;
            }
            else if (status < 0xf0)
            {
                track.runningStatus = status;
            }

            if (status == 0xff)
            {
                if (readMetaEvent(context, track))
                    break;
            }
            else if (status == 0xf0 || status == 0xf7)
            {
                readSysexEvent(context, track, status);
            }
            else if (status >= 0xf0)
            {
                const int dataCount = (status == 0xf1 || status == 0xf3) ? 1 : (status == 0xf2 ? 2 : 0);

                addEvent(context, track, readShortMessage(context, status, dataCount, "truncated MIDI system event"));
            }
            else
            {
                const std::uint8_t kind = status & 0xf0;
                const int dataCount = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;

                addEvent(context, track, readShortMessage(context, status, dataCount, "truncated MIDI channel event"));
            }
        }
    }

    // Timing
    inline double ticksToSeconds(const std::vector<TempoPoint> &tempos, std::uint64_t target, std::uint16_t division)
    {
        double seconds = 0.0;
        std::uint64_t previousTick = 0;
        std::uint32_t usPerQuarter = 500000;

        for (const TempoPoint &tempo : tempos)
        {
            if (tempo.tick > target)
                break;

            if (tempo.tick > previousTick)
                seconds += double(tempo.tick - previousTick) * double(usPerQuarter) / 1000000.0 / division;

            previousTick = tempo.tick;
            usPerQuarter = tempo.usPerQuarter;
        }

        if (target > previousTick)
            seconds += double(target - previousTick) * double(usPerQuarter) / 1000000.0 / division;

        return seconds;
    }

    // File loading
    inline OfflineFile loadOfflineMidi(const std::filesystem::path &path, double rate)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file)
            throw std::runtime_error("cannot open MIDI file: " + path.string());

        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});

        if (bytes.size() < 14 || std::memcmp(bytes.data(), "MThd", 4) != 0)
            throw std::runtime_error("not a Standard MIDI File");

        std::size_t position = 4;
        const std::uint32_t headerLength = readUint32(bytes, position);

        if (headerLength < 6 || position + headerLength > bytes.size())
            throw std::runtime_error("bad MIDI header");

        const std::uint16_t format = readUint16(bytes, position);
        const std::uint16_t trackCount = readUint16(bytes, position);
        const std::uint16_t division = readUint16(bytes, position);
        position = 8 + headerLength;

        if (format > 1)
            throw std::runtime_error("only MIDI formats 0 and 1 are supported");

        if ((division & 0x8000) || division == 0)
            throw std::runtime_error("SMPTE/invalid MIDI division is not supported");

        ParseContext context{bytes};
        context.position = position;

        for (unsigned track = 0; track < trackCount; ++track)
        {
            if (context.position + 8 > bytes.size() || std::memcmp(bytes.data() + context.position, "MTrk", 4) != 0)
                throw std::runtime_error("missing MIDI track chunk");

            context.position += 4;
            const std::uint32_t length = readUint32(bytes, context.position);

            if (context.position + length > bytes.size())
                throw std::runtime_error("truncated MIDI track");

            context.end = context.position + length;
            readTrack(context);
            context.position = context.end;
        }

        std::stable_sort(context.tempos.begin(), context.tempos.end(),
                         [](const TempoPoint &left, const TempoPoint &right)
                         { return left.tick < right.tick; });

        std::stable_sort(context.events.begin(), context.events.end(),
                         [](const OfflineEvent &left, const OfflineEvent &right)
                         {
                             return left.frame != right.frame ? left.frame < right.frame : left.order < right.order;
                         });

        OfflineFile result;
        result.ppqn = division;
        result.events.reserve(context.events.size());

        for (OfflineEvent &event : context.events)
        {
            const double seconds = ticksToSeconds(context.tempos, event.frame, division);
            const auto frame = static_cast<std::uint64_t>(std::llround(seconds * rate));

            result.events.push_back({frame, event.order, event.port, std::move(event.bytes)});
        }

        return result;
    }

    // WAV writing
    inline void writeWav16(const std::filesystem::path &path, const std::vector<std::int16_t> &pcm, std::uint32_t rate)
    {
        std::ofstream file(path, std::ios::binary);

        if (!file)
            throw std::runtime_error("cannot create WAV: " + path.string());

        const auto byteCount = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));

        const auto writeUint16 = [&](std::uint16_t value)
        {
            const char data[2] = {char(value), char(value >> 8)};
            file.write(data, 2);
        };

        const auto writeUint32 = [&](std::uint32_t value)
        {
            const char data[4] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24)};
            file.write(data, 4);
        };

        file.write("RIFF", 4);
        writeUint32(36 + byteCount);
        file.write("WAVEfmt ", 8);
        writeUint32(16);
        writeUint16(1);
        writeUint16(2);
        writeUint32(rate);
        writeUint32(rate * 4);
        writeUint16(4);
        writeUint16(16);
        file.write("data", 4);
        writeUint32(byteCount);
        file.write(reinterpret_cast<const char *>(pcm.data()), byteCount);
    }

} // namespace standalone
