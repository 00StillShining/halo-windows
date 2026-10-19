#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// SysEx building blocks shared by the device protocol (pure, tested).
namespace halo::sysex
{
    using Bytes = std::vector<std::uint8_t>;

    // Universal Non-Realtime Identity Request: F0 7E 7F 06 01 F7. Standard MIDI;
    // every device may answer, nothing on the device changes.
    Bytes identityRequest();

    struct Identity
    {
        std::uint8_t deviceId = 0;
        Bytes manufacturer;           // 1 or 3 bytes (00 20 76 = Teenage Engineering)
        std::uint16_t family = 0;     // LSB first on the wire
        std::uint16_t model = 0;      // LSB first on the wire
        Bytes version;                // 4 bytes, meaning is manufacturer-specific
        bool isTeenageEngineering() const { return manufacturer == Bytes { 0x00, 0x20, 0x76 }; }
    };

    // F0 7E <dev> 06 02 <mfr 1|3> <fam lo> <fam hi> <mod lo> <mod hi> <v1 v2 v3 v4> F7
    std::optional<Identity> parseIdentityReply (const Bytes&);

    std::string toHex (const Bytes&, std::size_t maxBytes = 0);

    // TE's 7-bit packing (classic "7 data bytes behind 1 MSB byte", LSB first):
    // for each group of up to 7 bytes, one byte holds their top bits (bit i =
    // MSB of byte i), followed by the 7 low-7-bit bytes.
    Bytes pack7 (const Bytes&);
    Bytes unpack7 (const Bytes&);
}
