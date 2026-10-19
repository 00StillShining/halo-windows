// Ported from HaloTests/EP40MIDIMappingTests.swift, plus parser tests.
#include <doctest/doctest.h>

#include "Core/Ep40MidiMapping.h"
#include "Core/MidiEvent.h"

#include <algorithm>
#include <set>

using namespace halo;

TEST_CASE ("group boundaries")
{
    CHECK (ep40::groupForNote (36) == 0);
    CHECK (ep40::groupForNote (47) == 0);
    CHECK (ep40::groupForNote (48) == 1);
    CHECK (ep40::groupForNote (59) == 1);
    CHECK (ep40::groupForNote (60) == 2);
    CHECK (ep40::groupForNote (71) == 2);
    CHECK (ep40::groupForNote (72) == 3);
    CHECK (ep40::groupForNote (83) == 3);
}

TEST_CASE ("every pad note maps to its group")
{
    for (int note = 36; note <= 83; ++note)
        CHECK (ep40::groupForNote ((std::uint8_t) note) == (note - 36) / 12);
}

TEST_CASE ("notes outside the pad range have no group or grid index")
{
    for (std::uint8_t note : { 35, 84, 0, 127 })
    {
        CHECK_FALSE (ep40::groupForNote (note).has_value());
        CHECK_FALSE (ep40::gridIndexForNote (note).has_value());
    }
}

TEST_CASE ("group count covers all notes")
{
    CHECK (ep40::groupCount * ep40::padsPerGroup == 48);
    CHECK (ep40::padNoteHigh - ep40::padNoteLow + 1 == 48);
}

TEST_CASE ("grid table is a permutation of 0...11")
{
    std::set<int> values (ep40::midiOffsetToGrid.begin(), ep40::midiOffsetToGrid.end());
    CHECK (values.size() == 12);
    CHECK (*values.begin() == 0);
    CHECK (*values.rbegin() == 11);
}

TEST_CASE ("note order . 0 ENTER 1-9 lands on the right labelled pad (confirmed on hardware)")
{
    const char* noteOrder[] { ".", "0", "ENTER", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    for (int offset = 0; offset < 12; ++offset)
    {
        const auto grid = ep40::gridIndexForNote ((std::uint8_t) (36 + offset));
        REQUIRE (grid.has_value());
        CHECK (ep40::gridLabels[(std::size_t) *grid] == noteOrder[offset]);
    }
}

TEST_CASE ("grid index repeats across groups")
{
    for (int offset = 0; offset < 12; ++offset)
        for (int group = 1; group <= 3; ++group)
            CHECK (ep40::gridIndexForNote ((std::uint8_t) (36 + group * 12 + offset))
                   == ep40::gridIndexForNote ((std::uint8_t) (36 + offset)));
}

TEST_CASE ("Note On with velocity 0 is a Note Off")
{
    const std::uint8_t bytes[] { 0x90, 40, 0 };
    const auto e = parseMidi (bytes, 3, 0.0);
    REQUIRE (e.has_value());
    CHECK (e->kind == MidiKind::noteOff);
    CHECK (e->data1 == 40);
}

TEST_CASE ("Note On keeps channel, note and velocity")
{
    const std::uint8_t one[] { 0x92, 40, 110 };
    const auto e = parseMidi (one, 3, 1.5);
    REQUIRE (e.has_value());
    CHECK (e->kind == MidiKind::noteOn);
    CHECK (e->channel == 2);
    CHECK (e->data1 == 40);
    CHECK (e->data2 == 110);
    CHECK (e->timeSeconds == 1.5);

    const std::uint8_t minimal[] { 0x90, 36, 1 };
    CHECK (parseMidi (minimal, 3, 0.0)->kind == MidiKind::noteOn);
}

TEST_CASE ("realtime, transport and song position messages parse")
{
    const std::uint8_t clock[] { 0xF8 }, start[] { 0xFA }, cont[] { 0xFB }, stop[] { 0xFC };
    CHECK (parseMidi (clock, 1, 0)->kind == MidiKind::timingClock);
    CHECK (parseMidi (start, 1, 0)->kind == MidiKind::start);
    CHECK (parseMidi (cont, 1, 0)->kind == MidiKind::continuePlaying);
    CHECK (parseMidi (stop, 1, 0)->kind == MidiKind::stop);

    const std::uint8_t spp[] { 0xF2, 0x10, 0x01 }; // 16 + 128
    const auto e = parseMidi (spp, 3, 0);
    REQUIRE (e.has_value());
    CHECK (e->kind == MidiKind::songPosition);
    CHECK (e->songPosition == 144);
}

TEST_CASE ("unsupported or truncated messages are ignored")
{
    const std::uint8_t sysex[] { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    const std::uint8_t pitchBend[] { 0xE0, 0, 64 };
    const std::uint8_t truncated[] { 0x90, 40 };
    CHECK_FALSE (parseMidi (sysex, sizeof (sysex), 0).has_value());
    CHECK_FALSE (parseMidi (pitchBend, 3, 0).has_value());
    CHECK_FALSE (parseMidi (truncated, 2, 0).has_value());
    CHECK_FALSE (parseMidi (nullptr, 0, 0).has_value());
}
