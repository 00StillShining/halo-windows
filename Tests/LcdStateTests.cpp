#include <doctest/doctest.h>

#include "Core/LcdState.h"

#include <optional>

using namespace halo;

namespace
{
    // Every combination of the fields that can light something.
    template <typename Fn>
    void forEachState (Fn&& fn)
    {
        for (int feed = 0; feed < 3; ++feed)
            for (int mode = 0; mode < 5; ++mode)
                for (int group = -1; group < 4; ++group)
                    for (int pad = -1; pad < 12; ++pad)
                        for (int playing = 0; playing < 2; ++playing)
                            for (int pulse = 0; pulse < 2; ++pulse)
                                for (int value : { 0, 7, 40, 120, 999 })
                                {
                                    DisplayState s;
                                    s.feedMode = (FeedMode) feed;
                                    s.mode = (DisplayMode) mode;
                                    s.value = value;
                                    s.activeGroup = group < 0 ? std::nullopt : std::optional<int> (group);
                                    s.activePadIndex = pad < 0 ? std::nullopt : std::optional<int> (pad);
                                    s.isPlaying = playing != 0;
                                    s.clockPulse = pulse != 0;
                                    s.leftMeter = 0.5f;
                                    s.rightMeter = 0.25f;
                                    fn (s);
                                }
    }
}

TEST_CASE ("LCD: nothing lights without a backing DisplayState field")
{
    forEachState ([] (const DisplayState& s)
    {
        const auto lcd = toLcdState (s);
        if (s.feedMode == FeedMode::waiting)
        {
            for (bool on : lcd.lit)
                CHECK_FALSE (on);
            CHECK (lcd.meters == std::array<float, 4> {});
            return;
        }
        for (int i = 0; i < (int) LcdIcon::count; ++i)
        {
            const auto icon = (LcdIcon) i;
            bool backed = false;
            if (i <= (int) LcdIcon::system) backed = (int) s.mode == i;
            else if (icon == LcdIcon::play) backed = s.isPlaying;
            else if (icon == LcdIcon::bpm) backed = s.clockPulse;
            else if (icon >= LcdIcon::groupA && icon <= LcdIcon::groupD)
                backed = s.activeGroup && *s.activeGroup == i - (int) LcdIcon::groupA;
            else if (icon == LcdIcon::numpad) backed = s.activePadIndex.has_value();
            CHECK_MESSAGE (lcd.lit[(size_t) i] == backed, "icon " << i);
        }
    });
}

TEST_CASE ("LCD: every preview frame differs from the live frame in backlight and digits")
{
    forEachState ([] (const DisplayState& s)
    {
        if (s.feedMode != FeedMode::live)
            return;
        auto preview = s;
        preview.feedMode = FeedMode::preview;
        const auto live = toLcdState (s), shown = toLcdState (preview);
        CHECK (shown.backlight < live.backlight);
        CHECK (shown.text != live.text);
        CHECK (shown.text == std::array<char, 3> { 'S', 'I', 'M' });
        for (char c : live.text)
            CHECK ((c == ' ' || (c >= '0' && c <= '9')));
    });
}

TEST_CASE ("LCD: provenance treatment per feed")
{
    auto s = DisplayState::liveIdle();
    s.value = 120;
    auto lcd = toLcdState (s);
    CHECK (lcd.backlight == doctest::Approx (LcdState::liveBacklight));
    CHECK (lcd.text == std::array<char, 3> { '1', '2', '0' });
    s.value = 72;
    CHECK (toLcdState (s).text == std::array<char, 3> { ' ', '7', '2' });

    lcd = toLcdState (DisplayState::waiting());
    CHECK (lcd.backlight == doctest::Approx (LcdState::waitingBacklight));
    CHECK (lcd.text == std::array<char, 3> { '_', '_', '_' });

    lcd = toLcdState (DisplayState::previewFrame (5));
    CHECK (lcd.backlight == doctest::Approx (LcdState::previewBacklight));
    CHECK (lcd.text == std::array<char, 3> { 'S', 'I', 'M' });
}

TEST_CASE ("LCD: every character the state can produce has a glyph")
{
    for (char c : { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'S', 'I', 'M', '_' })
        CHECK_MESSAGE (lcdGlyph (c) != 0, c);
    CHECK (lcdGlyph (' ') == 0);
    // No two digits look alike.
    for (char a = '0'; a <= '9'; ++a)
        for (char b = (char) (a + 1); b <= '9'; ++b)
            CHECK (lcdGlyph (a) != lcdGlyph (b));
    CHECK (lcdGlyph ('8') != lcdGlyph ('0'));
}
