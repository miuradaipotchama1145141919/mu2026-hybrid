#include "VlVoiceAllocator.h"

#include <cassert>

int main()
{
    hybrid::VlVoiceAllocator<8> allocator;
    assert(!allocator.hasExplicitConfiguration());

    const auto legacyFirst = allocator.noteOn(0, 60);
    const auto legacyLegato = allocator.noteOn(0, 64);
    assert(legacyFirst.voice == 0);
    assert(legacyLegato.voice == legacyFirst.voice);
    assert(legacyLegato.stoleActiveNote);
    assert(allocator.noteOff(0, 60) == legacyFirst.voice);
    assert(allocator.noteOff(0, 64) == legacyFirst.voice);

    allocator.reset();
    for (std::uint8_t voice = 0; voice < 8; ++voice)
        assert(allocator.configureVoice(voice, 0));
    assert(allocator.hasExplicitConfiguration());
    for (std::uint8_t note = 60; note < 68; ++note) {
        const auto allocation = allocator.noteOn(0, note);
        assert(allocation.voice == static_cast<std::size_t>(note - 60));
        assert(!allocation.stoleActiveNote);
    }
    const auto stolen = allocator.noteOn(0, 72);
    assert(stolen.voice == 0);
    assert(!stolen.reassigned);
    assert(stolen.stoleActiveNote);
    assert(allocator.noteOff(0, 60) == allocator.noVoice);

    allocator.reset();
    assert(allocator.configureVoice(3, 2));
    const auto first = allocator.noteOn(2, 64);
    const auto second = allocator.noteOn(2, 64);
    assert(first.voice == 3);
    assert(second.voice == first.voice);
    assert(allocator.noteOff(2, 64) == first.voice);
    assert(allocator.noteOff(2, 64) == first.voice);

    allocator.reset();
    assert(allocator.configureVoice(2, 4));
    assert(allocator.configureVoice(5, 4));
    assert(allocator.noteOn(4, 60).voice == 2);
    assert(allocator.noteOn(4, 64).voice == 5);
    assert(allocator.noteOn(3, 67).voice == allocator.noVoice);

    allocator.reset();
    const auto released = allocator.noteOn(0, 60);
    assert(released.voice == 0);
    allocator.releaseChannel(0);
    assert(!allocator.active(0));
    assert(allocator.channel(0) == 0);

    allocator.reset();
    assert(allocator.noteOn(0, 60).voice == 0);
    assert(allocator.noteOn(0, 64).voice == 0);
    assert(allocator.holds(0, 60));
    assert(allocator.holds(0, 64));
    assert(!allocator.holds(0, 65));
    assert(!allocator.holds(1, 60));
    assert(allocator.noteOff(0, 64) == 0);
    assert(allocator.holds(0, 60));
    assert(!allocator.holds(0, 64));
    assert(allocator.active(0));
    assert(allocator.noteOff(0, 60) == 0);
    assert(!allocator.holds(0, 60));
    assert(!allocator.active(0));

    allocator.reset();
    assert(allocator.noteOn(0, 60).voice == 0);
    assert(allocator.noteOn(0, 60).voice == 0);
    assert(allocator.noteOff(0, 60) == 0);
    assert(allocator.holds(0, 60));
    assert(allocator.noteOff(0, 60) == 0);
    assert(!allocator.holds(0, 60));

    allocator.reset();
    assert(allocator.configureVoice(2, 4));
    assert(allocator.configureVoice(5, 4));
    assert(allocator.noteOn(4, 60).voice == 2);
    assert(allocator.noteOn(4, 64).voice == 5);
    assert(allocator.holds(2, 60));
    assert(!allocator.holds(2, 64));
    assert(allocator.holds(5, 64));
    assert(allocator.noteOn(4, 67).voice == 2);
    assert(!allocator.holds(2, 60));
    assert(allocator.holds(2, 67));
}
