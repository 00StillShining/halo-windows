#include <doctest/doctest.h>

#include "Core/SysEx.h"

using namespace halo::sysex;

TEST_CASE ("identity request is the standard universal message")
{
    CHECK (identityRequest() == Bytes { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 });
}

TEST_CASE ("identity reply with a 3-byte manufacturer (Teenage Engineering)")
{
    const Bytes reply { 0xF0, 0x7E, 0x01, 0x06, 0x02, 0x00, 0x20, 0x76, 0x33, 0x00, 0x40, 0x01, 0x02, 0x05, 0x01, 0x00, 0xF7 };
    const auto id = parseIdentityReply (reply);
    REQUIRE (id.has_value());
    CHECK (id->isTeenageEngineering());
    CHECK (id->deviceId == 0x01);
    CHECK (id->family == 0x33);
    CHECK (id->model == (0x40 | (0x01 << 7)));
    CHECK (id->version == Bytes { 0x02, 0x05, 0x01, 0x00 });
}

TEST_CASE ("identity reply with a 1-byte manufacturer, and junk is rejected")
{
    const Bytes oneByte { 0xF0, 0x7E, 0x00, 0x06, 0x02, 0x41, 0x10, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF7 };
    REQUIRE (parseIdentityReply (oneByte).has_value());
    CHECK (parseIdentityReply (oneByte)->manufacturer == Bytes { 0x41 });
    CHECK_FALSE (parseIdentityReply ({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }).has_value());
    CHECK_FALSE (parseIdentityReply ({}).has_value());
}

TEST_CASE ("7-bit packing round-trips any bytes and never sets bit 7")
{
    Bytes data;
    for (int i = 0; i < 300; ++i)
        data.push_back ((std::uint8_t) ((i * 37 + 11) & 0xFF));
    const auto packed = pack7 (data);
    for (auto b : packed)
        CHECK (b < 0x80);
    CHECK (packed.size() == data.size() + (data.size() + 6) / 7);
    CHECK (unpack7 (packed) == data);

    // Worked example: FF 01 80 -> MSB byte 0b101 = 05, then 7F 01 00.
    CHECK (pack7 ({ 0xFF, 0x01, 0x80 }) == Bytes { 0x05, 0x7F, 0x01, 0x00 });
}

TEST_CASE ("hex formatting truncates long messages")
{
    CHECK (toHex ({ 0xF0, 0x7E, 0xF7 }) == "F0 7E F7");
    CHECK (toHex ({ 1, 2, 3, 4 }, 2) == "01 02 ... (4 bytes)");
}
