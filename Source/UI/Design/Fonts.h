#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace halo
{
    // The embedded OFL faces (docs/redesign/plan.md section 2.2), registered
    // once from binary data and kept for the app's lifetime. Owned by
    // HaloApplication (and by the UI test runner); every text role comes from
    // here. Sizes are em sizes (withPointHeight), tracking is in em, and type
    // never scales with the window.
    class HaloFonts
    {
    public:
        enum class Face
        {
            lexendGigaLight, lexendExaMedium, lexendExaSemiBold,
            interLight, interRegular, interMedium, interSemiBold,
            jetBrainsMono, departureMono,
            count
        };

        // Type roles, in the order of Source/Core/UiFontMetrics.h's kRoles.
        enum class Role { label, title, tag, ui, uiStatus, uiLight, body, bodyStrong, print, printMicro, mono, pixel };

        HaloFonts();
        ~HaloFonts();

        static HaloFonts* instance() noexcept { return current; }

        int registeredCount() const noexcept { return registered; }
        juce::Typeface::Ptr typeface (Face) const;
        // Family names of the registered faces, for the LookAndFeel's
        // getTypefaceForFont mapping (stock widgets get Inter Tight).
        juce::Typeface::Ptr typefaceForFamily (const juce::String& family, bool bold) const;

        // A role at its table size and tracking.
        static juce::Font font (Role);
        // A role at another em size (e.g. a 13 px status value); the role's tracking is kept.
        static juce::Font font (Role, float em);
        // Any face at any em size and tracking (in em).
        static juce::Font face (Face, float em, float trackingEm = 0.0f);

        // JUCE's kerning factor is relative to the font height (ascent +
        // descent); tracking is specified in em. This converts.
        static float kerningFactor (const juce::Font&, float em, float trackingEm);

        static float roleEm (Role);
        static float roleTracking (Role);

    private:
        static HaloFonts* current;
        std::array<juce::Typeface::Ptr, (size_t) Face::count> faces;
        int registered = 0;

        JUCE_DECLARE_NON_COPYABLE (HaloFonts)
    };

    using Role = HaloFonts::Role;
}
