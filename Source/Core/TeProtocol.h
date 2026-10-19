#pragma once

#include "Core/SysEx.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Teenage Engineering's EP-series sample protocol (EP-40 / EP-133 / EP-1320):
// framing, payload builders and reply parsers. Pure and tested; see
// docs/protocol.md for the spec and sources. Only the commands Halo needs
// exist here. DFU (firmware) has no builder on purpose, and buildRequest
// refuses it.
namespace halo::te
{
    using sysex::Bytes;

    enum Command : std::uint8_t { greet = 0x01, echo = 0x02, file = 0x05 };
    inline constexpr std::uint8_t dfuCommand = 0x03; // never sent

    enum Status : std::uint8_t { ok = 0, error = 1, unknownCommand = 2, badRequest = 3, interim = 64 };

    inline constexpr std::uint16_t soundsNode = 1000;
    inline constexpr std::uint16_t projectsNode = 2000;
    inline constexpr int maxPutDataBytes = 433; // for the 512-byte chunk size both EP-133 and EP-40 report

    // F0 00 20 76 <devId> 40 <flags> <rid> <cmd> <packed payload> F7.
    // Returns an empty frame for refused commands (DFU) or bad ids.
    Bytes buildRequest (std::uint8_t deviceId, int requestId, std::uint8_t command, const Bytes& rawPayload);

    struct Frame
    {
        enum class Kind { response, event, debug };
        Kind kind = Kind::response;
        std::uint8_t deviceId = 0;
        int requestId = 0;
        std::uint8_t command = 0;
        std::uint8_t status = 0;  // responses only (sent raw, not packed)
        Bytes body;               // unpacked
        std::string text;         // debug frames: the firmware's ASCII
    };

    // Nullopt for anything that isn't a TE frame.
    std::optional<Frame> parseFrame (const Bytes&);

    // ---- payloads (raw, before packing) -------------------------------------
    Bytes fileInit (bool subscribe = true, std::uint32_t maxResponse = 4u * 1024 * 1024);
    Bytes list (std::uint16_t node, std::uint16_t page);
    Bytes metaGet (std::uint16_t fid, std::uint16_t page);
    Bytes metaSet (std::uint16_t fid, const std::string& json);
    Bytes stat (std::uint16_t fid);
    Bytes getInit (std::uint16_t fid, std::uint32_t offset = 0);
    Bytes getData (std::uint16_t page);
    Bytes putInit (std::uint16_t slot, std::uint32_t size, const std::string& name, const std::string& json);
    Bytes putData (std::uint16_t page, const std::uint8_t* data, std::size_t size);
    Bytes deleteFile (std::uint16_t fid);

    // ---- reply parsers --------------------------------------------------------
    // "product:EP-40;mode:normal;sku:TE032AS006;os_version:2.5.1;..."
    std::map<std::string, std::string> parseGreet (const Bytes& body);

    struct Entry
    {
        std::uint16_t fid = 0;
        std::uint8_t flags = 0;
        std::uint32_t size = 0;
        std::string name;
        bool isDirectory() const noexcept { return (flags & 0x02) != 0; }
    };

    struct ListPage
    {
        std::uint16_t page = 0;
        std::vector<Entry> entries;
        bool last = false; // body of 2 bytes or less ends a listing
    };
    std::optional<ListPage> parseList (const Bytes& body);

    struct InitReply
    {
        std::uint32_t chunkSize = 0;
    };
    std::optional<InitReply> parseInit (const Bytes& body);

    struct GetInfo
    {
        std::uint16_t fid = 0;
        std::uint8_t flags = 0;
        std::uint32_t size = 0;
        std::string name;
    };
    std::optional<GetInfo> parseGetInit (const Bytes& body);

    struct DataPage
    {
        std::uint16_t page = 0;
        Bytes data;
    };
    std::optional<DataPage> parseDataPage (const Bytes& body);

    // META GET pages: JSON text chunks, ended by a trailing NUL or an empty page.
    struct MetaPage
    {
        std::uint16_t page = 0;
        std::string text;
        bool last = false;
    };
    std::optional<MetaPage> parseMetaPage (const Bytes& body);

    // ---- names and addresses --------------------------------------------------
    // TE's rule: printable ASCII only, no \ " /, at most 16 characters, lower case.
    std::string normalizeName (const std::string&);

    // Project 1-9, group 0-3 (A-D), grid index 0-11 (7 8 9 / 4 5 6 / 1 2 3 / . 0 ENTER).
    std::uint16_t padFid (int project, int group, int gridIndex);
    std::uint16_t projectFid (int project);
}
