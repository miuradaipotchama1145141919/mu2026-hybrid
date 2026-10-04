#include "SgRouting.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main()
{
    const std::array<std::uint8_t, 9> sg {
        0xf0, 0x43, 0x10, 0x5d, 0x50, 0x00, 0x42, 0x00, 0xf7
    };
    const std::array<std::uint8_t, 9> xg {
        0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7
    };
    const std::array<std::uint8_t, 4> truncated {
        0xf0, 0x43, 0x10, 0x5d
    };
    expect(hybrid::isSgConfiguration(sg), "SG model SysEx is detected");
    expect(!hybrid::isSgConfiguration(xg), "ordinary XG SysEx is ignored");
    expect(!hybrid::isSgConfiguration(truncated),
           "truncated SG SysEx is ignored");

    expect(hybrid::sgOwnsNote(0x00643c90, 0x0001),
           "routed channel note-on belongs to SG");
    expect(hybrid::sgOwnsNote(0x00003c80, 0x0001),
           "routed channel note-off belongs to SG");
    expect(hybrid::sgOwnsNote(0x00003c90, 0x0001),
           "zero-velocity routed note-on belongs to SG");
    expect(!hybrid::sgOwnsNote(0x00643c91, 0x0001),
           "unrouted channel note remains in XG");
    expect(!hybrid::sgOwnsNote(0x00075bb0, 0x0001),
           "controllers continue to XG");

    expect(hybrid::isSgBankSelect(0x006200b0), "bank MSB 98 selects SG");
    expect(hybrid::isSgBankSelect(0x006200b3), "bank MSB 98 on channel 4");
    expect(hybrid::isSgBankSelect(0x005200b0), "bank MSB 82 selects SG");
    expect(hybrid::isSgBankSelect(0x005200b5), "bank MSB 82 on channel 6");
    expect(!hybrid::isSgBankSelect(0x002100b0), "VL bank MSB 33 is not SG");
    expect(!hybrid::isSgBankSelect(0x005100b0), "VL bank MSB 81 is not SG");
    expect(!hybrid::isSgBankSelect(0x006100b0), "VL bank MSB 97 is not SG");
    expect(!hybrid::isSgBankSelect(0x000062b0), "CC 98 value 0 is not SG");
    expect(!hybrid::isSgBankSelect(0x006220b0), "bank LSB is not SG");
    expect(!hybrid::isSgBankSelect(0x000062c0), "program change is not SG");

    auto mask = hybrid::updateSgBankMask(0x006200b0, 0);
    expect(mask == 0x0001, "SG bank sets its channel bit");
    mask = hybrid::updateSgBankMask(0x006200b3, mask);
    expect(mask == 0x0009, "second SG channel is added");
    mask = hybrid::updateSgBankMask(0x005200b5, mask);
    expect(mask == 0x0029, "MSB 82 also adds its channel");
    mask = hybrid::updateSgBankMask(0x000000b5, mask);
    expect(mask == 0x0009, "leaving MSB 82 clears the channel bit");
    mask = hybrid::updateSgBankMask(0x000000b0, mask);
    expect(mask == 0x0008, "leaving the SG bank clears the channel bit");
    expect(hybrid::updateSgBankMask(0x00075bb0, mask) == mask,
           "other controllers leave the mask alone");
    expect(hybrid::updateSgBankMask(0x000005c3, mask) == mask,
           "program change leaves the mask alone");
    expect(hybrid::updateSgBankMask(0x00643c93, mask) == mask,
           "notes leave the mask alone");
    return failures == 0 ? 0 : 1;
}
