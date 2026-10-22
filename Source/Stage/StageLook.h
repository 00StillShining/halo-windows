#pragma once

#include "Core/RingState.h"
#include "Core/ToneCurves.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace halo
{
    // How the stage's surroundings look: the only coupling between the 3D stage
    // and the app's theme (docs/redesign/riddim-3d-plan.md, D7 and section 3.1).
    //
    // Held by value: StageComponent::setLook copies it under the stage's lock and
    // the GL thread picks it up on its next frame. Device plastic colours are NOT
    // look tokens; they are facts of the hardware and live in the model.
    //
    // OVERLAY RULE (plan section 3.9): the stage is a native GL child window with
    // component painting off, so no JUCE component can draw over it. Redesign
    // overlays (stageClip treatments, opxy-* mockups) must sit beside the stage
    // or be drawn in GL by the stage itself.
    inline constexpr int kRingStateCount = 7; // RingState::Kind, in declaration order

    struct StageLook
    {
        // The backdrop the stage clears to; the stage border equals it exactly.
        juce::Colour backdrop;
        // The floor around the device: a real plane, with graph paper (the grid
        // colours) round the device fading to this colour toward the stage edge.
        juce::Colour floor;
        juce::Colour gridMinor, gridMajor; // alpha = line strength; transparent = plain floor
        float gridCellPt = 16.0f;          // Low tier screen grid: points per minor cell, a major line every 4
        float floorGridMm = 5.0f;          // the perspective floor's graph paper: minor cell (mm), a major line every 5
        juce::Colour shadowTint;           // what the contact shadow darkens toward

        // The ring pool: one colour per RingState::Kind (disconnected is never drawn).
        std::array<juce::Colour, kRingStateCount> ring;

        float exposure = 1.0f;
        ToneMapper toneMapper = ToneMapper::neutral;
        float keyIntensity = 1.0f, rimIntensity = 1.0f, fillIntensity = 1.0f;
        float floorReflection = 0.0f; // 0 = off (Paper); about 0.15 (Dark), from M8
        // Key light falling on the floor around the device, so a dark floor still
        // shows the contact shadow (0 = the floor is the backdrop exactly).
        float floorSpot = 0.0f;
        juce::Image floorTexture; // by value; the M3 perspective floor (graph paper by default, O2)

        static juce::Colour ringColour (const StageLook& look, RingState::Kind kind)
        {
            return look.ring[(size_t) kind];
        }

        // Default: DD-GATE Palette A "graph paper", today's values exactly.
        static StageLook paper()
        {
            StageLook l;
            l.backdrop = juce::Colour (0xffAEB9BE);
            l.floor = l.backdrop;
            l.gridMinor = juce::Colour (0xffCAD3D6).withAlpha (0.52f);
            l.gridMajor = juce::Colour (0xff869399).withAlpha (0.34f);
            l.shadowTint = juce::Colour (0xff111315);
            l.ring = ringPalette (juce::Colour (0xffFF5A1F), juce::Colour (0xffD94A24));
            return l;
        }

        // OP-XY near-black. The multiply shadow has nothing to darken on a black
        // floor, so a soft key-light spot around the device carries the contact.
        static StageLook dark()
        {
            StageLook l;
            l.backdrop = juce::Colour (0xff060607);
            l.floor = l.backdrop;
            l.gridMinor = juce::Colour (0xff38383F).withAlpha (0.0f);
            l.gridMajor = juce::Colour (0xff38383F).withAlpha (0.0f);
            l.shadowTint = juce::Colour (0xff000000);
            l.ring = ringPalette (juce::Colour (0xffFF5A1F), juce::Colour (0xffFF3B1F));
            l.floorSpot = 0.11f;
            l.floorReflection = 0.15f;
            return l;
        }

        static StageLook named (const juce::String& name)
        {
            return name.equalsIgnoreCase ("dark") ? dark() : paper();
        }

    private:
        // Halo's ring is orange whatever it says (HaloRingRig.swift); an error
        // is the warning red. Levels and pulses come from RingState, not colour.
        static std::array<juce::Colour, kRingStateCount> ringPalette (juce::Colour orange, juce::Colour warning)
        {
            std::array<juce::Colour, kRingStateCount> r;
            r.fill (orange);
            r[(size_t) RingState::Kind::disconnected] = juce::Colours::transparentBlack;
            r[(size_t) RingState::Kind::error] = warning;
            return r;
        }
    };
}
