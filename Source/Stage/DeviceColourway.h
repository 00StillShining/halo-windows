#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace halo
{
    // The LCD's own colours (Stage/DisplayRenderer). TE's backlit green screen,
    // or a black-glass screen like the app's.
    struct LcdPalette
    {
        juce::Colour background { juce::Colour::fromFloatRGBA (0.045f, 0.37f, 0.25f, 1.0f) };
        juce::Colour digitWell  { juce::Colour::fromFloatRGBA (0.025f, 0.20f, 0.13f, 0.96f) };
        juce::Colour segmentOff { juce::Colour::fromFloatRGBA (0.10f, 0.28f, 0.20f, 0.85f) };
        juce::Colour segmentOn  { juce::Colour::fromFloatRGBA (0.90f, 0.98f, 0.91f, 1.0f) };
        juce::Colour glow       { juce::Colour::fromFloatRGBA (0.76f, 1.0f, 0.82f, 0.75f) };
        // Icons: TE's own colours (the atlas dimmed, lit ones full), or tinted
        // flat: off icons in iconOff, lit ones in iconOn.
        bool tintIcons = false;
        bool authoredAtlas = true;    // draw the PNG with its own green (TE); else icons only
        float atlasOpacity = 0.27f;
        juce::Colour iconOff, iconOn;
    };

    // The device's colourway (owner decision, reversing plan D7: the device's
    // colours may follow the app's design). Applied at runtime, with no
    // re-bake: plastic albedo and roughness by material role, the decal inks
    // of the keys atlas and the face silkscreen, the LCD palette and the pad
    // cue. Graphite is the app's default (owner's pick); TE's original
    // ("te"), field and black glass stay selectable.
    //
    // Materials are named "role:palette" by the model compiler
    // (tools/model/usdz_to_hmesh.py material_role): roles shell, body, side,
    // well, plate, pad, group, key, knob, fader, insert, port, label, spkr,
    // screen, led; palettes cream, label, key white, key beige, green, orange,
    // gap, stem, glass, led off.
    struct DeviceColourway
    {
        struct Rule
        {
            std::string role, palette;   // "*" matches any
            juce::Colour colour;         // sRGB
            float roughness = -1.0f;     // < 0: keep the model's
        };

        juce::String name { "te" };
        std::vector<Rule> rules;         // first match wins
        // Decal inks (sRGB). Keys atlas channels: TE's green, orange, white,
        // charcoal; face silkscreen: green, orange.
        std::array<juce::Colour, 4> keyInks { juce::Colour (0xff00503A), juce::Colour (0xffFF681D),
                                              juce::Colour (0xffF4F2EC), juce::Colour (0xff2B2B28) };
        std::array<juce::Colour, 2> faceInks { juce::Colour (0xff00503A), juce::Colour (0xffFF681D) };
        LcdPalette lcd;
        juce::Colour cue { 0xffFA5A16 };  // the pad underglow (D3)

        struct Override { juce::Colour colour; float roughness; };

        // The override for a material named "role:palette", if any.
        std::optional<Override> lookup (const std::string& materialName) const
        {
            const auto colon = materialName.find (':');
            const auto role = materialName.substr (0, colon);
            const auto palette = colon == std::string::npos ? std::string() : materialName.substr (colon + 1);
            for (const auto& r : rules)
                if ((r.role == "*" || r.role == role) && (r.palette == "*" || r.palette == palette))
                    return Override { r.colour, r.roughness };
            return std::nullopt;
        }

        static DeviceColourway te() { return {}; }

        // The app's graphite palette (Core/UiTokens.h): graphite body, dark
        // keys, cream and ink legends, the four encoder colours as accents
        // (orange where TE has orange, blue where TE has green, ochre VOLUME,
        // grey stems), a black-glass LCD with cream segments.
        static DeviceColourway graphite()
        {
            DeviceColourway c;
            c.name = "graphite";
            const juce::Colour deck (0xff1D1D20), deckLow (0xff121214), deckHigh (0xff2C2C30),
                               key (0xff2B2C30), keyHigh (0xff34353A), cream (0xffCDD0C2), ink (0xffF7F5F5),
                               blue (0xff4D7EA9), ochre (0xffB38E5E), grey (0xffA7B1B8), orange (0xffEF3E24);
            c.rules = {
                { "screen", "green", juce::Colour (0xff000005), 0.15f },
                { "screen", "glass", juce::Colour (0xff101012), -1.0f },
                { "shell", "*", deckLow, 0.50f },   { "body", "*", deck, 0.55f },
                { "side", "cream", deck, 0.55f },   { "side", "green", deckLow, 0.45f },
                { "label", "*", deckHigh, 0.45f },  { "spkr", "label", deckHigh, 0.50f },
                { "spkr", "key beige", deckLow, 0.70f },
                { "pad", "*", key, 0.34f },         { "group", "*", keyHigh, 0.34f },
                { "key", "key white", key, 0.34f }, { "key", "key beige", keyHigh, 0.34f },
                { "*", "orange", orange, -1.0f },   { "*", "green", blue, -1.0f },
                { "plate", "label", deckHigh, 0.40f },
                { "knob", "key white", ochre, -1.0f }, { "*", "stem", grey, -1.0f },
                { "insert", "label", deckHigh, -1.0f }, { "insert", "key beige", keyHigh, -1.0f },
                { "port", "label", deckHigh, -1.0f },
            };
            c.keyInks = { cream, orange, ink, ink };
            c.faceInks = { cream, orange };
            c.lcd = blackGlassLcd (cream, true);
            c.cue = orange;
            return c;
        }

        // OP-1 field: a light aluminium-grey body with dark keys, ink legends,
        // the encoder colours as accents, and a black OLED-like LCD keeping
        // TE's coloured icons.
        static DeviceColourway field()
        {
            DeviceColourway c;
            c.name = "field";
            const juce::Colour alu (0xffBCC2C6), aluLight (0xffD9DDDF), shell (0xff2C2C30),
                               key (0xff2B2C30), keyHigh (0xff34353A), ink (0xffF7F5F5), dark (0xff1D1D20),
                               blue (0xff4D7EA9), ochre (0xffB38E5E), grey (0xffA7B1B8), orange (0xffEF3E24);
            c.rules = {
                { "screen", "green", juce::Colour (0xff000005), 0.15f },
                { "screen", "glass", juce::Colour (0xff101012), -1.0f },
                { "shell", "*", shell, 0.45f },     { "body", "*", alu, 0.40f },
                { "side", "cream", alu, 0.40f },    { "side", "green", shell, 0.45f },
                { "label", "*", aluLight, 0.40f },  { "spkr", "label", alu, 0.40f },
                { "spkr", "key beige", dark, 0.70f },
                { "pad", "*", key, 0.34f },         { "group", "*", keyHigh, 0.34f },
                { "key", "key white", key, 0.34f }, { "key", "key beige", keyHigh, 0.34f },
                { "*", "orange", orange, -1.0f },   { "*", "green", blue, -1.0f },
                { "plate", "label", shell, 0.40f },
                { "knob", "key white", ochre, -1.0f }, { "*", "stem", grey, -1.0f },
                { "insert", "label", shell, -1.0f }, { "insert", "key beige", dark, -1.0f },
                { "port", "label", aluLight, -1.0f },
            };
            // Legends on the dark keys, plates and inserts print light; the
            // deck's silkscreen dark on the aluminium.
            c.keyInks = { ink, orange, ink, ink };
            c.faceInks = { dark, orange };
            c.lcd = blackGlassLcd (ink, false);
            c.cue = orange;
            return c;
        }

        // Black glass: near-black body and keys, ink legends, orange only on
        // RECORD, SAMPLE and the X knob.
        static DeviceColourway blackGlass()
        {
            DeviceColourway c;
            c.name = "black glass";
            const juce::Colour glass (0xff000005), body (0xff0B0B0D), key (0xff17171A), keyHigh (0xff1D1D20),
                               plate (0xff121214), ink (0xffF7F5F5), grey (0xffA7B1B8), orange (0xffEF3E24);
            c.rules = {
                { "shell", "*", glass, 0.30f },     { "body", "*", body, 0.28f },
                { "side", "*", body, 0.28f },       { "label", "*", plate, 0.30f },
                { "spkr", "*", plate, 0.40f },
                { "pad", "*", key, 0.28f },         { "group", "*", keyHigh, 0.28f },
                { "key", "orange", orange, -1.0f }, { "knob", "orange", orange, -1.0f },
                { "key", "*", key, 0.28f },         { "plate", "*", plate, 0.30f },
                { "knob", "stem", grey, -1.0f },    { "knob", "*", keyHigh, 0.30f },
                { "fader", "stem", grey, -1.0f },
                { "insert", "*", plate, 0.30f },    { "port", "label", plate, -1.0f },
                { "port", "*", glass, -1.0f },
                { "screen", "green", glass, 0.15f },
                { "screen", "glass", juce::Colour (0xff0A0A0C), -1.0f },
            };
            c.keyInks = { ink, orange, ink, ink };
            c.faceInks = { grey, orange };
            c.lcd = blackGlassLcd (ink, true);
            c.cue = orange;
            return c;
        }

        static DeviceColourway named (const juce::String& n)
        {
            const auto s = n.trim().toLowerCase().removeCharacters (" _-");
            if (s == "graphite") return graphite();
            if (s == "field") return field();
            if (s == "blackglass" || s == "black") return blackGlass();
            return te();
        }

    private:
        static LcdPalette blackGlassLcd (juce::Colour segment, bool tint)
        {
            LcdPalette p;
            p.background = juce::Colour (0xff050507);
            p.digitWell = juce::Colour (0xff000005);
            p.segmentOff = juce::Colour (0xff17181B);
            p.segmentOn = segment;
            p.glow = segment.withAlpha (0.55f);
            p.tintIcons = tint;
            p.authoredAtlas = false;
            p.atlasOpacity = tint ? 1.0f : 0.22f;
            p.iconOff = juce::Colour (0xff1C1D20);
            p.iconOn = segment;
            return p;
        }
    };
}
