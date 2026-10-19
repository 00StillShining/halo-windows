#include "Core/SysEx.h"

#include <cstdio>

namespace halo::sysex
{
    Bytes identityRequest()
    {
        return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    }

    std::optional<Identity> parseIdentityReply (const Bytes& m)
    {
        if (m.size() < 15 || m.front() != 0xF0 || m.back() != 0xF7 || m[1] != 0x7E || m[3] != 0x06 || m[4] != 0x02)
            return std::nullopt;

        Identity id;
        id.deviceId = m[2];
        size_t i = 5;
        if (m[i] == 0x00)
        {
            if (m.size() < 17)
                return std::nullopt;
            id.manufacturer = { m[i], m[i + 1], m[i + 2] };
            i += 3;
        }
        else
        {
            id.manufacturer = { m[i] };
            i += 1;
        }
        if (i + 8 >= m.size())
            return std::nullopt;
        id.family = (std::uint16_t) (m[i] | (m[i + 1] << 7));
        id.model = (std::uint16_t) (m[i + 2] | (m[i + 3] << 7));
        id.version = { m[i + 4], m[i + 5], m[i + 6], m[i + 7] };
        return id;
    }

    std::string toHex (const Bytes& b, std::size_t maxBytes)
    {
        std::string out;
        const size_t n = maxBytes > 0 && b.size() > maxBytes ? maxBytes : b.size();
        char hex[4];
        for (size_t i = 0; i < n; ++i)
        {
            std::snprintf (hex, sizeof (hex), "%02X", b[i]);
            if (i > 0)
                out += ' ';
            out += hex;
        }
        if (n < b.size())
            out += " ... (" + std::to_string (b.size()) + " bytes)";
        return out;
    }

    Bytes pack7 (const Bytes& in)
    {
        Bytes out;
        out.reserve (in.size() + in.size() / 7 + 1);
        for (size_t start = 0; start < in.size(); start += 7)
        {
            const size_t count = std::min<size_t> (7, in.size() - start);
            std::uint8_t msbs = 0;
            for (size_t k = 0; k < count; ++k)
                msbs |= (std::uint8_t) (((in[start + k] >> 7) & 1) << k);
            out.push_back (msbs);
            for (size_t k = 0; k < count; ++k)
                out.push_back ((std::uint8_t) (in[start + k] & 0x7F));
        }
        return out;
    }

    Bytes unpack7 (const Bytes& in)
    {
        Bytes out;
        out.reserve (in.size());
        for (size_t start = 0; start < in.size(); start += 8)
        {
            const std::uint8_t msbs = in[start];
            const size_t count = std::min<size_t> (7, in.size() - start - 1);
            for (size_t k = 0; k < count; ++k)
                out.push_back ((std::uint8_t) ((in[start + 1 + k] & 0x7F) | (((msbs >> k) & 1) << 7)));
        }
        return out;
    }
}
