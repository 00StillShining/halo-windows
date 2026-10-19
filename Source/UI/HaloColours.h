#pragma once

#include <juce_graphics/juce_graphics.h>

namespace halo
{
    // Legacy design tokens (port of HaloColors.swift / HaloMetrics.swift).
    // Forwarding shim during the redesign (docs/redesign/plan.md section 5.1):
    // panels that aren't rebuilt yet read their roles from legacyColours(),
    // which the Theme fills: today's graph paper with HALO_THEME=legacy, or
    // the legacy-dark mapping onto the black glass. R13 deletes this file.
    struct HaloColours
    {
        juce::Colour canvas, gridMinor, gridMajor, paper, paperHigh, metal,
                     ink, inkSoft, orange, orangeHot, riddimGreen, lcdGreen, warning;
        bool dark = false; // the legacy-dark mapping is active

        static const HaloColours graphPaper; // fixed values (the 3D stage until its StageLook lands)
    };

    inline const HaloColours HaloColours::graphPaper {
            juce::Colour (0xffAEB9BE),
            juce::Colour (0xffCAD3D6).withAlpha (0.52f),
            juce::Colour (0xff869399).withAlpha (0.34f),
            juce::Colour (0xffE6E8E5),
            juce::Colour (0xffF4F4EF),
            juce::Colour (0xffA6B0B4),
            juce::Colour (0xff111315),
            juce::Colour (0xff343A3D),
            juce::Colour (0xffFF5A1F),
            juce::Colour (0xffFF6A2A),
            juce::Colour (0xff405C4C),
            juce::Colour (0xffA6D39F),
            juce::Colour (0xffD94A24),
    };

    // The live legacy palette (see above). Bind it once per file:
    //     const auto& c = legacyColours();
    // The reference stays valid; setTheme() updates the values in place.
    inline HaloColours& legacyColours()
    {
        static HaloColours colours = HaloColours::graphPaper;
        return colours;
    }

    // The "no value" placeholder. Built from its code point: juce::String treats
    // narrow literals as ASCII, so a raw UTF-8 dash would render as mojibake.
    inline const juce::String noValue = juce::String::charToString ((juce::juce_wchar) 0x2014);

    struct HaloMetrics
    {
        static constexpr int unit = 8;
        static constexpr float radiusSmall = 2.0f;
        static constexpr float radiusPanel = 4.0f;
        static constexpr int statusBarHeight = 64;
        static constexpr int modeBarHeight = 56;
        static constexpr int railSpineWidth = 28;
        static constexpr int railPlayWidth = 300;
        static constexpr int windowDefaultWidth = 1440, windowDefaultHeight = 900;
        static constexpr int windowMinimumWidth = 1180, windowMinimumHeight = 720;
        static constexpr int panelShadowX = 3, panelShadowY = 6;
    };

    // Typography (HaloTypography.swift). The Mac uses SF Pro / SF Mono; on
    // Windows the closest system faces are Segoe UI and Cascadia Mono.
    struct HaloType
    {
        static juce::Font wordmark (float size = 34.0f) { return juce::FontOptions ("Segoe UI", size, juce::Font::bold); }
        static juce::Font body (float size = 13.0f)     { return juce::FontOptions ("Segoe UI", size, juce::Font::plain); }

        static juce::Font mono (float size = 12.0f, bool bold = false)
        {
            return juce::Font (juce::FontOptions (monoFace(), size, bold ? juce::Font::bold : juce::Font::plain));
        }

        // Uppercase, tracked label style.
        static juce::Font label (float size = 11.0f, bool bold = true)
        {
            return mono (size, bold).withExtraKerningFactor (0.11f);
        }

        static juce::String monoFace()
        {
            static const juce::String face = []
            {
                const auto names = juce::Font::findAllTypefaceNames();
                for (auto candidate : { "Cascadia Mono", "Consolas" })
                    if (names.contains (candidate))
                        return juce::String (candidate);
                return juce::Font::getDefaultMonospacedFontName();
            }();
            return face;
        }
    };
}
