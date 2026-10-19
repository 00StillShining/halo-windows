#pragma once

#include "Core/UiTokens.h"
#include "UI/HaloColours.h"

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <array>

namespace halo
{
    // The redesign's colour roles (docs/redesign/plan.md sections 2.1 and 5.1),
    // built from Source/Core/UiTokens.h. Read it with theme(); switch with
    // setTheme(), which also refreshes the legacy palette that not-yet-rebuilt
    // panels read through legacyColours() and broadcasts a change.
    struct Theme
    {
        enum class Skin { graphite, field };

        struct Screen
        {
            juce::Colour bg, panel, wave, waveSel, grid, gridDot, rule, frame, mute, dim, ink, hot;
            std::array<juce::Colour, 8> ramp;
            juce::Colour paper, paperHi, paperShade, tapeEmpty, tapeWound, ledOff, hatch;
            juce::Colour stepA, stepB, droppedTail, neutralKey;
        };
        struct Encoder { juce::Colour line, fill, onFill, cap, skirt; };
        struct Honest { juce::Colour mock, preview, danger, info; };
        struct Deck
        {
            juce::Colour top, bottom, edge, bezel, key, keyDown, keyLip, keyShift, keyShiftText,
                         print, printCap, printDim, grille, ledOn, ledOffDeck;
            bool brushed = false;
        };

        juce::String name;
        Skin skin = Skin::graphite;
        // true: the whole app still wears today's graph paper (R1's default,
        // so the plumbing lands with no visual change). false: black glass,
        // and legacy panels use the legacy-dark role mapping.
        bool legacyLook = false;

        Screen scr;
        std::array<Encoder, 4> enc;
        Honest honest;
        Deck deck;
        tokens::Legacy legacy;

        static Theme graphite();
        static Theme field();
        static Theme legacyPaper(); // today's look, everywhere
        static Theme fromName (const juce::String&); // HALO_THEME: graphite | field | legacy

        const Encoder& encoder (int index) const { return enc[(size_t) juce::jlimit (0, 3, index)]; }
    };

    const Theme& theme();
    void setTheme (const Theme&);

    // Top-level components listen to repaint once on a theme change.
    struct ThemeListener
    {
        virtual ~ThemeListener() = default;
        virtual void themeChanged() = 0;
    };
    void addThemeListener (ThemeListener*);
    void removeThemeListener (ThemeListener*);

    inline juce::Colour rgb (tokens::Rgb c) { return juce::Colour (0xff000000u | c); }

    // Hook for the 3D stage's look (3D plan M1, StageLook): once that header is
    // merged, Theme provides the dark stage preset the redesign asks for
    // (plan section 4.3) and MainComponent hands it to StageComponent::setLook.
   #if __has_include("Stage/StageLook.h")
    #define HALO_HAS_STAGE_LOOK 1
   #endif
}
