// HaloUiTests: doctest with JUCE's GUI modules initialised and the embedded
// fonts registered, for tests that need real typefaces, components or the
// message thread (plan section 5.1).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "UI/Design/Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    halo::HaloFonts fonts;

    doctest::Context context (argc, argv);
    return context.run();
}
