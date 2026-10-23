#include "UI/Design/Theme.h"

namespace halo
{
    namespace
    {
        Theme build (const tokens::Palette& p, const char* name, Theme::Skin skin)
        {
            Theme t;
            t.name = name;
            t.skin = skin;
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
            return t;
        }

        Theme& current()
        {
            static Theme t = Theme::graphite();
            return t;
        }
    }

    Theme Theme::graphite() { return build (tokens::graphite(), "graphite", Skin::graphite); }
    Theme Theme::field()    { return build (tokens::field(), "field", Skin::field); }

    Theme Theme::fromName (const juce::String& name)
    {
        return name == "field" ? field() : graphite();
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
        themeListeners().call ([] (ThemeListener& l) { l.themeChanged(); });
    }
}
