#pragma once

#include "UI/Design/Pictograms.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // A 44 px action key (plan section 2.5): label plus pictogram, width =
    // text + 2 x 16 px (never fixed, so it never clips).
    //  neutral      #1C1D21 cap, ink label (export, show, audition, out...)
    //  primary      paper cap, black label (back up now)
    //  deviceWrite  paper cap, crimson write LED + up arrow (send, assign);
    //               caption "writes to the ep-40 · asks first"
    //  lockedWrite  dim outline, padlock instead of the LED; not clickable;
    //               caption "back up the ep-40 first (backups, ctrl 6)"
    //  destructive  crimson outline + triangle (filled only inside the confirm)
    //  busy         dim; not clickable; caption "busy: sending to the ep-40"
    // A disabled key always looks dead; its reason is printed by its page.
    class ActionKey : public juce::Button
    {
    public:
        enum class Variant { neutral, primary, deviceWrite, lockedWrite, destructive, busy };

        explicit ActionKey (const juce::String& label = {}, Pict icon = Pict::none, Variant = Variant::neutral);

        void setLabel (const juce::String&);
        void setIcon (Pict);
        void setVariant (Variant);
        Variant getVariant() const noexcept { return variant; }

        int preferredWidth() const;
        static constexpr int height = 44;
        static juce::String captionFor (Variant);

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String label;
        Pict icon = Pict::none;
        Variant variant = Variant::neutral;
    };
}
