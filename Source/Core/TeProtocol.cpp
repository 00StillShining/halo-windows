#include "Core/TeProtocol.h"

#include <algorithm>
#include <cctype>

namespace halo::te
{
    namespace
    {
        void put16 (Bytes& b, std::uint16_t v) { b.push_back ((std::uint8_t) (v >> 8)); b.push_back ((std::uint8_t) v); }
        void put32 (Bytes& b, std::uint32_t v)
        {
            for (int s = 24; s >= 0; s -= 8)
                b.push_back ((std::uint8_t) (v >> s));
        }
        std::uint16_t get16 (const Bytes& b, size_t i) { return (std::uint16_t) ((b[i] << 8) | b[i + 1]); }
        std::uint32_t get32 (const Bytes& b, size_t i)
        {
            return ((std::uint32_t) b[i] << 24) | ((std::uint32_t) b[i + 1] << 16) | ((std::uint32_t) b[i + 2] << 8) | b[i + 3];
        }
        // Reads a NUL-terminated string at i; advances i past the NUL.
        std::string getString (const Bytes& b, size_t& i)
        {
            std::string s;
            while (i < b.size() && b[i] != 0)
                s += (char) b[i++];
            if (i < b.size())
                ++i;
            return s;
        }
        bool isTeHeader (const Bytes& m)
        {
            return m.size() >= 10 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x76 && m.back() == 0xF7;
        }
    }

    Bytes buildRequest (std::uint8_t deviceId, int requestId, std::uint8_t command, const Bytes& raw)
    {
        if (command == dfuCommand || requestId < 0 || requestId > 4095 || deviceId > 0x7F)
            return {};
        Bytes m { 0xF0, 0x00, 0x20, 0x76, deviceId, 0x40,
                  (std::uint8_t) (0x40 | 0x20 | ((requestId >> 7) & 0x1F)),
                  (std::uint8_t) (requestId & 0x7F), command };
        const auto packed = sysex::pack7 (raw);
        m.insert (m.end(), packed.begin(), packed.end());
        m.push_back (0xF7);
        return m;
    }

    std::optional<Frame> parseFrame (const Bytes& m)
    {
        if (! isTeHeader (m))
            return std::nullopt;

        Frame f;
        f.deviceId = m[4];
        if (m[5] == 0x33) // firmware debug/log text
        {
            f.kind = Frame::Kind::debug;
            for (size_t i = 6; i + 1 < m.size(); ++i)
                f.text += (char) (m[i] & 0x7F);
            return f;
        }
        if (m[5] != 0x40)
            return std::nullopt;

        f.requestId = ((m[6] & 0x1F) << 7) | (m[7] & 0x7F);
        f.command = m[8];
        const bool isResponse = (m[6] & 0x20) != 0;
        f.kind = isResponse ? Frame::Kind::response : Frame::Kind::event;

        size_t bodyStart = 9;
        if (isResponse)
        {
            if (m.size() < 11)
                return std::nullopt;
            f.status = m[9]; // raw, never packed
            bodyStart = 10;
        }
        f.body = sysex::unpack7 (Bytes (m.begin() + (long) bodyStart, m.end() - 1));
        return f;
    }

    //==============================================================================
    Bytes fileInit (bool subscribe, std::uint32_t maxResponse)
    {
        Bytes b { 0x01, (std::uint8_t) (subscribe ? 0x01 : 0x00) };
        put32 (b, maxResponse);
        return b;
    }

    Bytes list (std::uint16_t node, std::uint16_t page)
    {
        Bytes b { 0x04 };
        put16 (b, page);
        put16 (b, node);
        return b;
    }

    Bytes metaGet (std::uint16_t fid, std::uint16_t page)
    {
        Bytes b { 0x07, 0x02 };
        put16 (b, fid);
        put16 (b, page);
        return b;
    }

    Bytes metaSet (std::uint16_t fid, const std::string& json)
    {
        Bytes b { 0x07, 0x01 };
        put16 (b, fid);
        b.insert (b.end(), json.begin(), json.end());
        b.push_back (0x00);
        return b;
    }

    Bytes stat (std::uint16_t fid)
    {
        Bytes b { 0x0B };
        put16 (b, fid);
        return b;
    }

    Bytes getInit (std::uint16_t fid, std::uint32_t offset)
    {
        Bytes b { 0x03, 0x00 };
        put16 (b, fid);
        put32 (b, offset);
        return b;
    }

    Bytes getData (std::uint16_t page)
    {
        Bytes b { 0x03, 0x01 };
        put16 (b, page);
        return b;
    }

    Bytes putInit (std::uint16_t slot, std::uint32_t size, const std::string& name, const std::string& json)
    {
        Bytes b { 0x02, 0x00, 0x05 }; // flags 0x05 = READ|FILE (0x04 alone is rejected)
        put16 (b, slot);
        put16 (b, soundsNode);
        put32 (b, size);
        b.insert (b.end(), name.begin(), name.end());
        b.push_back (0x00);
        b.insert (b.end(), json.begin(), json.end()); // no trailing NUL after the JSON
        return b;
    }

    Bytes putData (std::uint16_t page, const std::uint8_t* data, std::size_t size)
    {
        Bytes b { 0x02, 0x01 };
        put16 (b, page);
        if (data != nullptr)
            b.insert (b.end(), data, data + size);
        return b;
    }

    Bytes deleteFile (std::uint16_t fid)
    {
        // The plain form `06 <fid>`. The "06 02 <fid>" form some docs show is wrong
        // and can address the wrong slot.
        Bytes b { 0x06 };
        put16 (b, fid);
        return b;
    }

    //==============================================================================
    std::map<std::string, std::string> parseGreet (const Bytes& body)
    {
        std::map<std::string, std::string> out;
        std::string text;
        for (auto c : body)
            if (c != 0)
                text += (char) c;
        size_t start = 0;
        while (start <= text.size())
        {
            const auto end = std::min (text.find (';', start), text.size());
            const auto pair = text.substr (start, end - start);
            if (const auto colon = pair.find (':'); colon != std::string::npos)
                out[pair.substr (0, colon)] = pair.substr (colon + 1);
            start = end + 1;
        }
        return out;
    }

    std::optional<ListPage> parseList (const Bytes& body)
    {
        if (body.size() < 2)
            return ListPage { 0, {}, true };
        ListPage p;
        p.page = get16 (body, 0);
        p.last = body.size() <= 2;
        size_t i = 2;
        while (i + 7 <= body.size())
        {
            Entry e;
            e.fid = get16 (body, i);
            e.flags = body[i + 2];
            e.size = get32 (body, i + 3);
            i += 7;
            e.name = getString (body, i);
            p.entries.push_back (std::move (e));
        }
        return p;
    }

    std::optional<InitReply> parseInit (const Bytes& body)
    {
        if (body.size() < 5)
            return std::nullopt;
        return InitReply { get32 (body, 1) };
    }

    std::optional<GetInfo> parseGetInit (const Bytes& body)
    {
        if (body.size() < 7)
            return std::nullopt;
        GetInfo g;
        g.fid = get16 (body, 0);
        g.flags = body[2];
        g.size = get32 (body, 3);
        size_t i = 7;
        g.name = getString (body, i);
        return g;
    }

    std::optional<DataPage> parseDataPage (const Bytes& body)
    {
        if (body.size() < 2)
            return std::nullopt;
        return DataPage { get16 (body, 0), Bytes (body.begin() + 2, body.end()) };
    }

    std::optional<MetaPage> parseMetaPage (const Bytes& body)
    {
        if (body.size() < 2)
            return MetaPage { 0, {}, true };
        MetaPage p;
        p.page = get16 (body, 0);
        p.last = body.size() <= 2 || body.back() == 0x00;
        for (size_t i = 2; i < body.size(); ++i)
            if (body[i] != 0)
                p.text += (char) body[i];
        return p;
    }

    //==============================================================================
    std::string normalizeName (const std::string& in)
    {
        std::string out;
        for (unsigned char c : in)
        {
            if (c < 0x20 || c > 0x7E || c == '\\' || c == '"' || c == '/')
                continue;
            out += (char) std::tolower (c);
            if (out.size() == 16)
                break;
        }
        return out;
    }

    std::uint16_t projectFid (int project)
    {
        return (std::uint16_t) (3000 + (std::clamp (project, 1, 9) - 1) * 1000);
    }

    std::uint16_t padFid (int project, int group, int gridIndex)
    {
        return (std::uint16_t) (projectFid (project) + 200 + std::clamp (group, 0, 3) * 100 + std::clamp (gridIndex, 0, 11) + 1);
    }
}
