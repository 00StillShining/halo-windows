// Known answers come from decoded captures of TE's own tool (see docs/protocol.md).
#include <doctest/doctest.h>

#include "Core/TeProtocol.h"

using namespace halo;
using te::Bytes;

TEST_CASE ("GREET request matches a captured EP-133 frame")
{
    // F0 00 20 76 33 40 77 14 01 F7 = devId 0x33, request id 2964, cmd 1, no payload.
    CHECK (te::buildRequest (0x33, 2964, te::greet, {}) == Bytes { 0xF0, 0x00, 0x20, 0x76, 0x33, 0x40, 0x77, 0x14, 0x01, 0xF7 });
}

TEST_CASE ("PUT INIT payload and frame match a captured upload")
{
    // Raw payload from TE's tool uploading "1_kick_01" (11272 bytes, mono) to slot 1.
    const Bytes raw { 0x02, 0x00, 0x05, 0x00, 0x01, 0x03, 0xE8, 0x00, 0x00, 0x2C, 0x08, 0x31, 0x5F, 0x6B,
                      0x69, 0x63, 0x6B, 0x5F, 0x30, 0x31, 0x00, 0x7B, 0x22, 0x63, 0x68, 0x61, 0x6E, 0x6E,
                      0x65, 0x6C, 0x73, 0x22, 0x3A, 0x31, 0x7D };
    CHECK (te::putInit (1, 0x2C08, "1_kick_01", "{\"channels\":1}") == raw);

    const auto frame = te::buildRequest (0x33, 1555, te::file, raw);
    const Bytes head { 0xF0, 0x00, 0x20, 0x76, 0x33, 0x40, 0x6C, 0x13, 0x05, 0x40, 0x02, 0x00, 0x05, 0x00, 0x01, 0x03, 0x68 };
    REQUIRE (frame.size() > head.size());
    CHECK (Bytes (frame.begin(), frame.begin() + (long) head.size()) == head);
    CHECK (frame.back() == 0xF7);
    CHECK (frame.size() == 10 + raw.size() + (raw.size() + 6) / 7);
}

TEST_CASE ("DFU is refused, whatever the payload")
{
    CHECK (te::buildRequest (0x3C, 1, te::dfuCommand, { 0x01, 0x01, 0x00, 0xC8 }).empty());
    CHECK (te::buildRequest (0x3C, 5000, te::greet, {}).empty());
}

TEST_CASE ("responses: raw status byte, packed body, request id echo")
{
    // A response to rid 2964 (byte 6 = 0x20 | 0x17), cmd 1, status 0, body "a:b".
    auto body = sysex::pack7 ({ 'a', ':', 'b' });
    Bytes m { 0xF0, 0x00, 0x20, 0x76, 0x3C, 0x40, 0x37, 0x14, 0x01, 0x00 };
    m.insert (m.end(), body.begin(), body.end());
    m.push_back (0xF7);
    const auto f = te::parseFrame (m);
    REQUIRE (f.has_value());
    CHECK (f->kind == te::Frame::Kind::response);
    CHECK (f->deviceId == 0x3C);
    CHECK (f->requestId == 2964);
    CHECK (f->command == te::greet);
    CHECK (f->status == te::ok);
    CHECK (f->body == Bytes { 'a', ':', 'b' });
}

TEST_CASE ("debug frames and events are told apart from responses")
{
    const Bytes debug { 0xF0, 0x00, 0x20, 0x76, 0x3C, 0x33, 'e', 'r', 'r', 0xF7 };
    CHECK (te::parseFrame (debug)->kind == te::Frame::Kind::debug);
    CHECK (te::parseFrame (debug)->text == "err");

    const Bytes event { 0xF0, 0x00, 0x20, 0x76, 0x3C, 0x40, 0x00, 0x05, 0x05, 0x00, 0x03, 0xF7 };
    CHECK (te::parseFrame (event)->kind == te::Frame::Kind::event);
    CHECK_FALSE (te::parseFrame ({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }).has_value());
}

TEST_CASE ("GREET reply parsing")
{
    const std::string s = "product:EP-40;mode:normal;sku:TE032AS006;os_version:2.5.1;serial:ABC";
    const auto kv = te::parseGreet (Bytes (s.begin(), s.end()));
    CHECK (kv.at ("product") == "EP-40");
    CHECK (kv.at ("mode") == "normal");
    CHECK (kv.at ("sku") == "TE032AS006");
    CHECK (kv.at ("os_version") == "2.5.1");
}

TEST_CASE ("payload builders: list, meta, get, delete use big-endian ids")
{
    CHECK (te::fileInit() == Bytes { 0x01, 0x01, 0x00, 0x40, 0x00, 0x00 });
    CHECK (te::list (1000, 2) == Bytes { 0x04, 0x00, 0x02, 0x03, 0xE8 });
    CHECK (te::metaGet (1000, 0) == Bytes { 0x07, 0x02, 0x03, 0xE8, 0x00, 0x00 });
    CHECK (te::getInit (12) == Bytes { 0x03, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00 });
    CHECK (te::deleteFile (11) == Bytes { 0x06, 0x00, 0x0B });
    // Captured pad assignment: 07 01 14 5B {"sym":2}\0 on fid 5211.
    const auto set = te::metaSet (5211, "{\"sym\":2}");
    CHECK (Bytes (set.begin(), set.begin() + 4) == Bytes { 0x07, 0x01, 0x14, 0x5B });
    CHECK (set.back() == 0x00);
}

TEST_CASE ("list, init, get and meta replies")
{
    // page 0, then slot 5: flags 0x1D, 4096 bytes, "005.pcm".
    Bytes body { 0x00, 0x00, 0x00, 0x05, 0x1D, 0x00, 0x00, 0x10, 0x00, '0', '0', '5', '.', 'p', 'c', 'm', 0x00 };
    const auto page = te::parseList (body);
    REQUIRE (page.has_value());
    REQUIRE (page->entries.size() == 1);
    CHECK (page->entries[0].fid == 5);
    CHECK (page->entries[0].size == 4096);
    CHECK (page->entries[0].name == "005.pcm");
    CHECK_FALSE (page->entries[0].isDirectory());
    CHECK (te::parseList ({ 0x00, 0x03 })->last);

    CHECK (te::parseInit ({ 0x0C, 0x00, 0x00, 0x02, 0x00 })->chunkSize == 512);

    const auto info = te::parseGetInit ({ 0x00, 0x05, 0x1D, 0x00, 0x00, 0x10, 0x00, 'k', 0x00 });
    CHECK (info->size == 4096);
    CHECK (info->name == "k");

    const auto meta = te::parseMetaPage ({ 0x00, 0x00, '{', '}', 0x00 });
    CHECK (meta->text == "{}");
    CHECK (meta->last);
}

TEST_CASE ("names and pad addresses")
{
    CHECK (te::normalizeName ("My \"Kick\"/01 Loud & Long!") == "my kick01 loud &");
    CHECK (te::padFid (1, 0, 0) == 3201); // project 1, group A, pad "7"
    CHECK (te::padFid (3, 2, 10) == 5411); // project 3, group C, pad "0"
    CHECK (te::projectFid (6) == 8000);
}
