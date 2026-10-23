#include <doctest/doctest.h>

#include "Core/Ep40EntityNames.h"
#include "Core/HmeshFormat.h"
#include "Core/HtexFormat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>

using namespace halo;

namespace
{
    using Bytes = std::vector<std::byte>;

    void put8 (Bytes& b, std::uint8_t v) { b.push_back ((std::byte) v); }
    void put16 (Bytes& b, std::uint16_t v) { put8 (b, (std::uint8_t) v); put8 (b, (std::uint8_t) (v >> 8)); }
    void put32 (Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) put8 (b, (std::uint8_t) (v >> (8 * i))); }
    void putF (Bytes& b, float f) { std::uint32_t v; std::memcpy (&v, &f, 4); put32 (b, v); }

    struct V3Spec
    {
        std::uint8_t vertexMaterial = 1, partMaterial = 0, slot = 1;
        std::uint16_t ledFor = 0xFFFF;
        std::uint32_t lastIndex = 2;
        float boundsMinZ = -0.008f;
        float pivotX = 0.0f;
        bool extraChunk = false;
    };

    // Two materials; parts pad_7 (one triangle, slot 1) and halo_ring (anchor, no triangles).
    Bytes makeV3 (const V3Spec& s = {})
    {
        Bytes head;
        for (float v : { -0.088f, -0.12f, s.boundsMinZ, 0.088f, 0.12f, 0.016f })
            putF (head, v);
        put32 (head, 2); put32 (head, 2); put32 (head, 3); put32 (head, 3);

        Bytes matl;
        for (int m = 0; m < 2; ++m)
        {
            char name[16] {};
            std::snprintf (name, sizeof name, "mat%d", m);
            for (char c : name) put8 (matl, (std::uint8_t) c);
            for (float v : { 0.7f, 0.6f, 0.5f, m == 0 ? 0.62f : 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f })
                putF (matl, v);
            put32 (matl, m == 1 ? (std::uint32_t) HmeshMaterial::decalTarget : 0u);
        }

        Bytes part;
        auto addPart = [&] (const char* name, std::uint8_t kind, std::uint8_t flags, std::uint8_t slot, std::uint8_t material,
                            std::uint32_t first, std::uint32_t count, std::uint32_t base, std::uint32_t verts, std::uint16_t ledFor)
        {
            put16 (part, (std::uint16_t) std::strlen (name));
            for (const char* c = name; *c != 0; ++c) put8 (part, (std::uint8_t) *c);
            while (part.size() % 4 != 0) put8 (part, 0);
            put8 (part, kind); put8 (part, flags); put8 (part, slot); put8 (part, material);
            for (float v : { s.pivotX, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0026f, 0.0f, 0.0f, 0.0f, 0.01f, 0.01f, 0.01f })
                putF (part, v);
            put32 (part, first); put32 (part, count); put32 (part, base); put32 (part, verts);
            put16 (part, ledFor); put16 (part, 0);
        };
        addPart ("pad_7", HmeshPart::pad, HmeshPart::castsShadow | HmeshPart::moving, s.slot, s.partMaterial, 0, 3, 0, 3, s.ledFor);
        addPart ("halo_ring", HmeshPart::anchor, 0, 0, 0, 3, 0, 3, 0, 0xFFFF);

        Bytes vert;
        const float positions[3][3] { { 0, 0, 0 }, { 0.01f, 0, 0 }, { 0, 0.01f, 0.005f } };
        for (const auto& p : positions)
        {
            putF (vert, p[0]); putF (vert, p[1]); putF (vert, p[2]);
            const auto n = Hmesh::encodeNormal (0, 0, 1);
            put16 (vert, (std::uint16_t) n[0]); put16 (vert, (std::uint16_t) n[1]);
            put16 (vert, 0); put16 (vert, 65535);
            put8 (vert, s.vertexMaterial); put8 (vert, 1); put8 (vert, 0); put8 (vert, 255);
        }
        Bytes indx;
        put32 (indx, 0); put32 (indx, 1); put32 (indx, s.lastIndex);

        std::vector<std::pair<std::string, Bytes>> chunks { { "HEAD", head }, { "MATL", matl }, { "PART", part },
                                                            { "VERT", vert }, { "INDX", indx } };
        if (s.extraChunk)
            chunks.insert (chunks.begin() + 2, { "ZZZZ", Bytes (13, std::byte { 7 }) });
        return chunkfile::write ("HMSH", 3, chunks);
    }

    // A v1 or v2 file: halo_ring below the case, then pad_7 (one triangle).
    Bytes makeLegacy (std::uint32_t version)
    {
        Bytes b;
        for (char c : { 'H', 'M', 'S', 'H' }) put8 (b, (std::uint8_t) c);
        put32 (b, version);
        put32 (b, 2);
        const std::size_t floats = version == 1 ? 12 : 14;
        auto addPart = [&] (const char* name, float z, float roughness)
        {
            put16 (b, (std::uint16_t) std::strlen (name));
            for (const char* c = name; *c != 0; ++c) put8 (b, (std::uint8_t) *c);
            for (int i = 0; i < 16; ++i) putF (b, (i % 5 == 0) ? 1.0f : 0.0f); // identity
            for (int i = 0; i < 3; ++i) putF (b, 0.0f);
            put32 (b, 3);
            for (int v = 0; v < 3; ++v)
            {
                const float f[14] { 0.01f * (float) v, 0.0f, z, 0, 0, 1, 0.8f, 0.8f, 0.8f, 0, 0, 0, roughness, 0 };
                for (std::size_t k = 0; k < floats; ++k) putF (b, f[k]);
            }
            put32 (b, 3); put32 (b, 0); put32 (b, 1); put32 (b, 2);
        };
        addPart ("halo_ring", -0.0094f, 0.5f);
        addPart ("pad_7", -0.008f, 0.4f);
        return b;
    }

    Bytes readFile (const char* path)
    {
        Bytes data;
        if (std::FILE* f = std::fopen (path, "rb"))
        {
            std::byte buffer[65536];
            for (size_t n; (n = std::fread (buffer, 1, sizeof buffer, f)) > 0;)
                data.insert (data.end(), buffer, buffer + n);
            std::fclose (f);
        }
        return data;
    }
}

TEST_CASE ("hmesh v3: a hand-built file round-trips")
{
    std::string error;
    const auto data = makeV3();
    const auto mesh = Hmesh::parse (data, error);
    REQUIRE_MESSAGE (mesh.has_value(), error);
    CHECK (mesh->version == 3);
    CHECK (mesh->boundsMin[2] == doctest::Approx (-0.008f));
    REQUIRE (mesh->materials.size() == 2);
    CHECK (mesh->materials[0].roughness == doctest::Approx (0.62f));
    CHECK (mesh->materials[1].flags == HmeshMaterial::decalTarget);
    CHECK (mesh->materials[0].name == "mat0");
    REQUIRE (mesh->parts.size() == 2);
    const auto* pad = mesh->find ("pad_7");
    REQUIRE (pad != nullptr);
    CHECK (pad->kind == HmeshPart::pad);
    CHECK (pad->slot == 1);
    CHECK (pad->travel == doctest::Approx (0.0026f));
    CHECK (pad->indexCount == 3);
    CHECK (mesh->find ("halo_ring")->indexCount == 0);
    REQUIRE (mesh->vertices.size() == 3);
    CHECK (mesh->vertices[2].position[2] == doctest::Approx (0.005f));
    CHECK (mesh->vertices[0].uv[1] == 65535);
    const auto n = Hmesh::decodeNormal (mesh->vertices[0].normal[0], mesh->vertices[0].normal[1]);
    CHECK (n[2] == doctest::Approx (1.0f));
}

TEST_CASE ("hmesh v3: unknown chunks are skipped")
{
    std::string error;
    V3Spec s;
    s.extraChunk = true;
    CHECK (Hmesh::parse (makeV3 (s), error).has_value());
}

TEST_CASE ("hmesh v3: every malformation is rejected")
{
    std::string error;
    auto rejects = [&] (const Bytes& data) { return ! Hmesh::parse (data, error).has_value(); };

    auto bad = makeV3();
    bad[0] = std::byte { 'X' };
    CHECK (rejects (bad));
    bad = makeV3();
    bad[4] = std::byte { 4 }; // version 4
    CHECK (rejects (bad));
    bad = makeV3();
    bad.back() ^= std::byte { 1 }; // CRC
    CHECK (rejects (bad));

    V3Spec s;
    s.lastIndex = 3;                 // index past its part's vertices
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.vertexMaterial = 2;            // material out of range
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.partMaterial = 9;
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.slot = 40;                     // slot out of range
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.ledFor = 16;                   // ledFor out of range
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.pivotX = std::nanf ("");       // non-finite
    CHECK (rejects (makeV3 (s)));
    s = {};
    s.boundsMinZ = 1.0f;             // inverted bounds
    CHECK (rejects (makeV3 (s)));
}

TEST_CASE ("hmesh legacy: v2 keeps roughness, v1 gets 0.5, bounds leave out halo_ring")
{
    std::string error;
    const auto v2 = Hmesh::parse (makeLegacy (2), error);
    REQUIRE_MESSAGE (v2.has_value(), error);
    const auto* pad = v2->find ("pad_7");
    REQUIRE (pad != nullptr);
    CHECK (v2->materials[v2->vertices[pad->baseVertex].material].roughness == doctest::Approx (0.4f));
    CHECK (pad->slot == 1);
    CHECK (pad->kind == HmeshPart::pad);
    CHECK (v2->boundsMin[2] == doctest::Approx (-0.008f)); // not the ring's -0.0094
    CHECK ((v2->find ("halo_ring")->flags & HmeshPart::castsShadow) == 0);
    CHECK (v2->vertices[0].ao == 255);

    const auto v1 = Hmesh::parse (makeLegacy (1), error);
    REQUIRE_MESSAGE (v1.has_value(), error);
    for (const auto& m : v1->materials)
        CHECK (m.roughness == doctest::Approx (0.5f));
}

TEST_CASE ("hmesh: random truncations and byte flips never crash or yield an out-of-range index")
{
    std::mt19937 rng (40);
    int parsed = 0;
    for (int round = 0; round < 2000; ++round)
    {
        auto data = (round % 2 == 0) ? makeV3() : makeLegacy (2);
        if (round % 3 == 0)
            data.resize (std::uniform_int_distribution<size_t> (0, data.size()) (rng));
        else
            for (int k = 0; k < 1 + (int) (rng() % 4); ++k)
                data[std::uniform_int_distribution<size_t> (0, data.size() - 1) (rng)] ^= (std::byte) (1u + rng() % 255);
        std::string error;
        const auto mesh = Hmesh::parse (data, error);
        if (! mesh)
            continue;
        ++parsed;
        for (auto i : mesh->indices)
            REQUIRE (i < mesh->vertices.size());
        for (const auto& v : mesh->vertices)
        {
            REQUIRE (v.material < mesh->materials.size());
            REQUIRE (v.slot < Hmesh::maxSlots);
        }
        for (const auto& p : mesh->parts)
            REQUIRE ((std::uint64_t) p.firstIndex + p.indexCount <= mesh->indices.size());
    }
    MESSAGE ("parsed " << parsed << " of 2000 corrupted files (legacy files have no CRC)");
}

TEST_CASE ("EP-40 asset: contract names, unique slots, empty anchors")
{
#ifdef HALO_ASSET_DIR
    const auto data = readFile (HALO_ASSET_DIR "/stage/EP40.hmesh");
    REQUIRE (! data.empty());
    std::string error;
    const auto mesh = Hmesh::parse (data, error);
    REQUIRE_MESSAGE (mesh.has_value(), error);
    CHECK (mesh->version == 3);
    for (auto name : ep40::entityNames)
        CHECK_MESSAGE (mesh->find (name) != nullptr, "missing " << std::string (name));

    std::set<int> slots;
    for (const auto& p : mesh->parts)
    {
        if (p.kind == HmeshPart::pad || p.kind == HmeshPart::group)
        {
            CHECK (p.slot != 0);
            CHECK (slots.insert (p.slot).second);
        }
        if (p.kind == HmeshPart::anchor)
            CHECK (p.indexCount == 0);
    }
    CHECK (slots.size() == 16);
    // The floor sits at the case's lowest point: nothing hangs below it.
    CHECK (mesh->boundsMin[2] == doctest::Approx (-0.008f).epsilon (0.01));
    CHECK (mesh->indices.size() / 3 <= 150000);
#else
    MESSAGE ("HALO_ASSET_DIR not set");
#endif
}

TEST_CASE ("EP-40 keys atlas: an SDF with full mips, and every pad's top prints ink")
{
#ifdef HALO_ASSET_DIR
    const auto atlasData = readFile (HALO_ASSET_DIR "/stage/ep40_keys_sdf.htex");
    const auto meshData = readFile (HALO_ASSET_DIR "/stage/EP40.hmesh");
    REQUIRE (! atlasData.empty());
    REQUIRE (! meshData.empty());
    std::string error;
    const auto atlas = Htex::parse (atlasData, error);
    REQUIRE_MESSAGE (atlas.has_value(), error);
    CHECK (atlas->format == Htex::Format::rgba8);
    CHECK ((atlas->flags & Htex::sdf) != 0);
    CHECK (std::string (atlas->channels.data(), 4) == "GOWC");
    size_t levels = 1;
    for (auto w = atlas->width, h = atlas->height; w > 1 || h > 1; w = std::max (1u, w / 2), h = std::max (1u, h / 2))
        ++levels;
    CHECK (atlas->mips.size() == levels);

    const auto mesh = Hmesh::parse (meshData, error);
    REQUIRE_MESSAGE (mesh.has_value(), error);
    int pads = 0;
    for (const auto& part : mesh->parts)
    {
        if (part.kind != HmeshPart::pad && part.kind != HmeshPart::group)
            continue;
        ++pads;
        // The UV box of the cap's top face, and the strongest ink inside it.
        float u0 = 1, v0 = 1, u1 = 0, v1 = 0;
        for (auto i = part.baseVertex; i < part.baseVertex + part.vertexCount; ++i)
        {
            const auto& v = mesh->vertices[i];
            if ((v.mask & 1) == 0 || Hmesh::decodeNormal (v.normal[0], v.normal[1])[2] < 0.9f)
                continue;
            const float u = v.uv[0] / 65535.0f, w = v.uv[1] / 65535.0f;
            u0 = std::min (u0, u); u1 = std::max (u1, u); v0 = std::min (v0, w); v1 = std::max (v1, w);
        }
        REQUIRE_MESSAGE (u1 > u0, part.name << ": no decal top");
        int strongest = 0;
        for (auto y = (std::uint32_t) (v0 * atlas->height); y < (std::uint32_t) (v1 * atlas->height); ++y)
            for (auto x = (std::uint32_t) (u0 * atlas->width); x < (std::uint32_t) (u1 * atlas->width); ++x)
                for (auto c : atlas->texel8 (0, x, y))
                    strongest = std::max (strongest, (int) c);
        CHECK_MESSAGE (strongest > 140, part.name << ": no legend on the cap");
    }
    CHECK (pads == 16);
#else
    MESSAGE ("HALO_ASSET_DIR not set");
#endif
}

TEST_CASE ("EP-40 face atlas: the deck silkscreen is a two-ink SDF with ink in both inks")
{
#ifdef HALO_ASSET_DIR
    const auto data = readFile (HALO_ASSET_DIR "/stage/ep40_face_sdf.htex");
    REQUIRE (! data.empty());
    std::string error;
    const auto face = Htex::parse (data, error);
    REQUIRE_MESSAGE (face.has_value(), error);
    CHECK (face->format == Htex::Format::rg8);
    CHECK ((face->flags & Htex::sdf) != 0);
    // 176 x 240 mm (the whole top, port edge to front edge) at 8 texels per mm.
    CHECK (face->width == 1408);
    CHECK (face->height == 1920);
    int strongest[2] {};
    for (std::uint32_t y = 0; y < face->height; y += 2)
        for (std::uint32_t x = 0; x < face->width; x += 2)
        {
            const auto t = face->texel8 (0, x, y);
            strongest[0] = std::max (strongest[0], (int) t[0]);
            strongest[1] = std::max (strongest[1], (int) t[1]);
        }
    CHECK (strongest[0] > 140); // green: parameter names, brackets, Y
    CHECK (strongest[1] > 140); // orange: GAIN and X
#else
    MESSAGE ("HALO_ASSET_DIR not set");
#endif
}
