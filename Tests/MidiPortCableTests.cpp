#include "MidiPortCable.h"

#include <cstdint>
#include <cstdio>

namespace {

bool expect(bool condition, const char* description)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", description);
    return condition;
}

} // namespace

int main()
{
    hybrid::MidiPortCable cable;
    bool passed = true;

    passed &= expect(cable.port() == 0 && cable.isPrimary(),
                     "starts on port A");
    passed &= expect(!cable.observe(0x90, 60),
                     "an ordinary note is not a cable selector");
    passed &= expect(cable.isPrimary(),
                     "an ordinary note leaves the port unchanged");

    passed &= expect(cable.observe(0xf5, 2) && cable.port() == 1
                         && !cable.isPrimary(),
                     "F5 02 selects port B");
    passed &= expect(cable.observe(0xf5, 4) && cable.port() == 3,
                     "F5 04 selects port D");
    passed &= expect(!cable.observe(0xb0, 7) && cable.port() == 3,
                     "channel messages do not change the selected port");
    passed &= expect(cable.observe(0xf5, 1) && cable.isPrimary(),
                     "F5 01 returns to port A");

    cable.observe(0xf5, 3);
    passed &= expect(cable.observe(0xf5, 0) && cable.port() == 2,
                     "F5 00 is a selector but is ignored (port unchanged)");
    passed &= expect(cable.observe(0xf5, 5) && cable.port() == 2,
                     "F5 05 is a selector but is ignored (port unchanged)");
    passed &= expect(cable.observe(0xf5, 0x7f) && cable.port() == 2,
                     "F5 7F is a selector but is ignored (port unchanged)");

    cable.reset();
    passed &= expect(cable.isPrimary(), "reset returns to port A");

    passed &= expect(hybrid::MidiPortCable::selector(0)[0] == 0xf5
                         && hybrid::MidiPortCable::selector(0)[1] == 1,
                     "selector for port A is F5 01");
    passed &= expect(hybrid::MidiPortCable::selector(3)[1] == 4,
                     "selector for port D is F5 04");
    passed &= expect(hybrid::MidiPortCable::selector(9)[1] == 1,
                     "an out-of-range port falls back to A");

    return passed ? 0 : 1;
}
