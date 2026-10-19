#include "UI/Design/Theme.h"

namespace halo
{
    namespace
    {
        juce::Colour argb (tokens::Argb c) { return juce::Colour (c); }

        Theme build (const tokens::Palette& p, const tokens::Legacy& legacy, const char* name, Theme::Skin skin, bool legacyLook)
        {
            Theme t;
            t.name = name;
            t.skin = skin;
            t.legacyLook = legacyLook;
            const auto& s = p.scr;
            t.scr = { rgb (s.bg), rgb (s.panel), rgb (s.wave), rgb (s.waveSel), rgb (s.grid), rgb (s.gridDot), rgb (s.rule),
                      rgb (s.frame), rgb (s.mute), rgb (s.dim), rgb (s.ink), rgb (s.hot), {},
                      rgb (s.paper), rgb (s.paperHi), rgb (s.paperShade), rgb (s.tapeEmpty), rgb (s.tapeWound),
                      rgb (s.ledOff), rgb (s.hatch), rgb (s.stepA), rgb (s.stepB), rgb (s.droppedTail), rgb (s.neutralKey) };
            for (size_t i = 0; i < s.ramp.size(); ++i)
                t.scr.ramp[i] = rgb (s.ramp[i]);
            for (size_t i = 0; i < 4; ++i)
            {
                const auto& e = p.enc[i];
                t.enc[i] = { rgb (e.line), rgb (e.fill), rgb (e.onFill), rgb (e.cap), rgb (e.skirt) };
            }
            t.honest = { rgb (p.honest.mock), rgb (p.honest.preview), rgb (p.honest.danger), rgb (p.honest.info) };
            const auto& d = p.deck;
            t.deck = { rgb (d.deckTop), rgb (d.deckBottom), rgb (d.deckEdge), rgb (d.glassBezel), rgb (d.key), rgb (d.keyDown),
                       rgb (d.keyLip), rgb (d.keyShift), rgb (d.keyShiftText), rgb (d.print), rgb (d.printCap), rgb (d.printDim),
                       rgb (d.grille), rgb (d.ledOn), rgb (d.ledOffDeck), d.brushed };
            t.legacy = legacy;
            return t;
        }

        Theme& current()
        {
            static Theme t = Theme::legacyPaper();
            return t;
        }

        void publishLegacy (const tokens::Legacy& l)
        {
            auto& c = legacyColours();
            c = HaloColours { argb (l.canvas), argb (l.gridMinor), argb (l.gridMajor), argb (l.paper), argb (l.paperHigh),
                              argb (l.metal), argb (l.ink), argb (l.inkSoft), argb (l.orange), argb (l.orangeHot),
                              argb (l.riddimGreen), argb (l.lcdGreen), argb (l.warning) };
            c.dark = l.dark;
        }
    }

    Theme Theme::graphite()    { return build (tokens::graphite(), tokens::legacyDark(), "graphite", Skin::graphite, false); }
    Theme Theme::field()       { return build (tokens::field(), tokens::legacyDark(), "field", Skin::field, false); }
    Theme Theme::legacyPaper() { return build (tokens::graphite(), tokens::legacyPaper(), "legacy", Skin::graphite, true); }

    Theme Theme::fromName (const juce::String& name)
    {
        if (name == "field")    return field();
        if (name == "graphite") return graphite();
        return legacyPaper();
    }

    const Theme& theme() { return current(); }

    namespace
    {
        juce::ListenerList<ThemeListener>& themeListeners()
        {
            static juce::ListenerList<ThemeListener> list;
            return list;
        }
    }

    void addThemeListener (ThemeListener* l)    { themeListeners().add (l); }
    void removeThemeListener (ThemeListener* l) { themeListeners().remove (l); }

    void setTheme (const Theme& t)
    {
        current() = t;
        publishLegacy (t.legacy);
        themeListeners().call ([] (ThemeListener& l) { l.themeChanged(); });
    }
}
