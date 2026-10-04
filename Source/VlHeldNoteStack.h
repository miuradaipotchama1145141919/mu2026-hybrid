#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hybrid {

struct VlHeldNote {
    std::uint8_t note {};
    std::uint8_t velocity {};
};

class VlHeldNoteStack {
public:
    static constexpr std::size_t capacity = 32;

    // Recording
    void clear() noexcept
    {
        noteCount = 0;
        soundingNote = -1;
    }

    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
    {
        if (noteCount == capacity) {
            for (std::size_t i = 1; i < capacity; ++i)
                heldNotes[i - 1] = heldNotes[i];

            --noteCount;
        }

        heldNotes[noteCount++] = { note, velocity };
        soundingNote = note;
    }

    template <typename Holds>
    [[nodiscard]] std::optional<VlHeldNote> noteOff(
        std::uint8_t note, Holds&& holds) noexcept
    {
        removeLatest(note);

        if (soundingNote != note)
            return std::nullopt;

        soundingNote = -1;

        while (noteCount != 0 && !holds(heldNotes[noteCount - 1].note))
            --noteCount;

        if (noteCount == 0)
            return std::nullopt;

        const VlHeldNote resumeNote = heldNotes[noteCount - 1];
        soundingNote = resumeNote.note;

        return resumeNote;
    }

    // State
    [[nodiscard]] std::size_t size() const noexcept { return noteCount; }

    [[nodiscard]] int sounding() const noexcept { return soundingNote; }

private:
    void removeLatest(std::uint8_t note) noexcept
    {
        for (std::size_t i = noteCount; i-- > 0;) {
            if (heldNotes[i].note != note)
                continue;

            for (std::size_t next = i + 1; next < noteCount; ++next)
                heldNotes[next - 1] = heldNotes[next];

            --noteCount;
            return;
        }
    }

    std::array<VlHeldNote, capacity> heldNotes {};
    std::size_t noteCount {};
    int soundingNote {-1};
};

} // namespace hybrid
