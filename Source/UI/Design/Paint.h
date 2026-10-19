#pragma once

#include "UI/Design/Fonts.h"
#include "UI/Design/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo::paint
{
    // Shared painters for the redesign (docs/redesign/plan.md section 2.5).
    // Inside the glass everything is flat: no gradients, glows, blurs or
    // shadows (the ConfirmCard's hard offset shadow is the one exception).

    // ---- Geometry -------------------------------------------------------------
    float physicalScale (const juce::Graphics&);
    // Snap a coordinate to the physical pixel grid (centre of a pixel when
    // `centre`, for 1 px lines).
    float snap (float v, float scale, bool centre = false);
    // Hairlines: exactly one physical pixel, on the pixel grid.
    void hairlineH (juce::Graphics&, float x0, float x1, float y, juce::Colour);
    void hairlineV (juce::Graphics&, float x, float y0, float y1, juce::Colour);

    // ---- Text -----------------------------------------------------------------
    float textWidth (const juce::Font&, const juce::String&);
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, juce::Colour,
               juce::Justification = juce::Justification::centredLeft);
    // Draws left-aligned at x with the text's baseline at `baseline`; returns the right edge.
    float textAt (juce::Graphics&, const juce::String&, float x, float baseline, const juce::Font&, juce::Colour);
    // Middle-ellipsis to fit a width ("speakers (re...tek)").
    juce::String middleEllipsis (const juce::String&, const juce::Font&, float maxWidth);
    // Wrapped paragraph; returns its height for that width (never ellipsised).
    float paragraphHeight (const juce::String&, const juce::Font&, float width, float lineSpacing = 4.0f);
    void paragraph (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, juce::Colour,
                    float lineSpacing = 4.0f, juce::Justification = juce::Justification::topLeft);

    // ---- Lamps (7 px dots, plan section 2.1 lamp vocabulary) ------------------
    void lamp (juce::Graphics&, juce::Point<float> centre, juce::Colour, float diameter = 7.0f);
    void hollowLamp (juce::Graphics&, juce::Point<float> centre, juce::Colour, float diameter = 7.0f, float stroke = 1.5f);
    // An LED on the deck: dot plus a solid 35% halo disc (never a blur).
    void deckLed (juce::Graphics&, juce::Point<float> centre, juce::Colour, float diameter = 5.0f);

    // ---- Tags (20 px, radius 2.5, `tag` type) ---------------------------------
    enum class TagStyle { filled, outline, mock, preview, danger, hatched };
    float tagWidth (const juce::String&, TagStyle = TagStyle::filled);
    // Draws a tag with its top-left at `topLeft`; returns its bounds.
    juce::Rectangle<float> tag (juce::Graphics&, juce::Point<float> topLeft, const juce::String&, TagStyle,
                                juce::Colour fill = {}, juce::Colour textColour = {}, float height = 20.0f);

    // ---- Textures -------------------------------------------------------------
    // 45 degree lines "not kept / bypassed / dropped / incomplete".
    void hatch (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float pitch = 6.0f, float thickness = 1.1f);
    // Yellow / black caution stripes (MOCK).
    void hazard (juce::Graphics&, juce::Rectangle<float>, float pitch = 14.0f);
    void dashedRect (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float thickness = 1.5f,
                     float on = 6.0f, float off = 4.0f, float radius = 0.0f);
    // The only shadow in the glass: hard, unblurred, 3,3 px.
    void hardShadow (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour = juce::Colours::black);

    // ---- Dot timeline ---------------------------------------------------------
    // A row of dots: used (filled ramp3), this operation (hot), free (hollow).
    // `danger` outlines free dots in crimson (e.g. memory above 90%).
    void dotTimeline (juce::Graphics&, juce::Rectangle<float>, int total, int used, int current,
                      bool dangerFree = false, float diameter = 5.0f);

    // ---- Keys -----------------------------------------------------------------
    enum class KeyState { normal, selected, disabled };
    // A flat step key (radius 3, 2 px black lip); shade = paired row shading.
    void stepKey (juce::Graphics&, juce::Rectangle<float>, const juce::String&, KeyState, bool alternateShade,
                  bool hovered = false, bool focused = false);
    // Soft-key pill: filled in the encoder's fill colour when it has a press,
    // otherwise a dim empty capsule.
    void pill (juce::Graphics&, juce::Rectangle<float>, const juce::String&, int encoder, bool available,
               bool hovered = false, bool pressed = false);
}
