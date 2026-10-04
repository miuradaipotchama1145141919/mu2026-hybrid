#include "VlHeldNoteStack.h"
#include "VlVoiceAllocator.h"

#include <cassert>

namespace {

struct Harness {
    hybrid::VlVoiceAllocator<8> allocator;
    hybrid::VlHeldNoteStack stack;

    void on(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
    {
        const auto allocation = allocator.noteOn(channel, note);
        assert(allocation.voice == 0);
        stack.noteOn(note, velocity);
    }

    std::optional<hybrid::VlHeldNote> off(std::uint8_t channel,
                                          std::uint8_t note)
    {
        const auto voice = allocator.noteOff(channel, note);
        assert(voice == 0);
        auto resume = stack.noteOff(note, [&](std::uint8_t held) {
            return allocator.holds(voice, held);
        });
        if (!allocator.active(voice))
            stack.clear();
        return resume;
    }
};

} // namespace

int main()
{
    {
        Harness h;
        h.on(0, 60, 90);
        h.on(0, 64, 100);
        const auto resume = h.off(0, 64);
        assert(resume && resume->note == 60 && resume->velocity == 90);
        assert(h.stack.sounding() == 60);
        assert(!h.off(0, 60));
        assert(h.stack.sounding() == -1);
        assert(h.stack.size() == 0);
    }

    {
        Harness h;
        h.on(0, 60, 90);
        h.on(0, 64, 100);
        assert(!h.off(0, 60));
        assert(h.stack.sounding() == 64);
        assert(!h.off(0, 64));
    }

    {
        Harness h;
        h.on(0, 60, 80);
        h.on(0, 64, 81);
        h.on(0, 67, 82);
        auto resume = h.off(0, 67);
        assert(resume && resume->note == 64 && resume->velocity == 81);
        resume = h.off(0, 64);
        assert(resume && resume->note == 60 && resume->velocity == 80);
        assert(!h.off(0, 60));
    }

    {
        Harness h;
        h.on(0, 60, 80);
        h.on(0, 64, 81);
        h.on(0, 67, 82);
        assert(!h.off(0, 64));
        const auto resume = h.off(0, 67);
        assert(resume && resume->note == 60);
    }

    {
        Harness h;
        h.on(0, 60, 70);
        h.on(0, 60, 71);
        const auto resume = h.off(0, 60);
        assert(resume && resume->note == 60);
        assert(!h.off(0, 60));
    }

    {
        hybrid::VlHeldNoteStack stack;
        for (std::uint8_t note = 0; note < 40; ++note)
            stack.noteOn(note, 64);
        assert(stack.size() == hybrid::VlHeldNoteStack::capacity);
        assert(stack.sounding() == 39);
        const auto resume = stack.noteOff(39, [](std::uint8_t note) {
            return note == 38;
        });
        assert(resume && resume->note == 38);
    }

    return 0;
}
