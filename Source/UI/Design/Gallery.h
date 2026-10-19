#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Debug fixture pages for the design system (plan sections 6.1, R2a-R3):
    // HALO_UI_GALLERY=1 shows every primitive in every state from fixture
    // data, under a permanent yellow band "FIXTURE · gallery data, not device
    // state", so nothing here can be mistaken for the real thing.
    // HALO_UI_GALLERY=<n> picks the page (1 palette, type, keys; 2 encoders,
    // cells, pads, meters; 3 confirm cards).
    std::unique_ptr<juce::Component> createGallery (int page);
}
