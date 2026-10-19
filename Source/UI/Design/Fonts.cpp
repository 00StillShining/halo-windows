#include "UI/Design/Fonts.h"
#include "App/DevLog.h"
#include "Core/UiFontMetrics.h"

#include "HaloFontAssets.h"

namespace halo
{
    HaloFonts* HaloFonts::current = nullptr;

    namespace
    {
        // Binary-data names (juce_add_binary_data drops the dash) in Face order.
        constexpr const char* resourceNames[] {
            "LexendGigaLight_ttf", "LexendExaMedium_ttf", "LexendExaSemiBold_ttf",
            "InterTightLight_ttf", "InterTightRegular_ttf", "InterTightMedium_ttf", "InterTightSemiBold_ttf",
            "JetBrainsMonoRegular_ttf", "DepartureMonoRegular_ttf",
        };
        static_assert (std::size (resourceNames) == (size_t) HaloFonts::Face::count);

        // Role -> face, matching tools/fonts/roles.json.
        constexpr HaloFonts::Face roleFaces[] {
            HaloFonts::Face::lexendExaMedium,   // label
            HaloFonts::Face::lexendGigaLight,   // title
            HaloFonts::Face::lexendExaSemiBold, // tag
            HaloFonts::Face::interRegular,      // ui
            HaloFonts::Face::interRegular,      // uiStatus
            HaloFonts::Face::interLight,        // uiLight
            HaloFonts::Face::interRegular,      // body
            HaloFonts::Face::interSemiBold,     // bodyStrong
            HaloFonts::Face::interRegular,      // print
            HaloFonts::Face::interMedium,       // printMicro
            HaloFonts::Face::jetBrainsMono,     // mono
            HaloFonts::Face::departureMono,     // pixel
        };
        static_assert (std::size (roleFaces) == fontmetrics::kRoles.size());

        const fontmetrics::Role& metrics (Role r) { return *fontmetrics::kRoles[(size_t) r]; }
    }

    HaloFonts::HaloFonts()
    {
        jassert (current == nullptr);
        current = this;
        for (size_t i = 0; i < faces.size(); ++i)
        {
            int size = 0;
            if (const auto* data = HaloFontAssets::getNamedResource (resourceNames[i], size); data != nullptr && size > 0)
                faces[i] = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            if (faces[i] != nullptr)
                ++registered;
            else
                DevLog::line (juce::String ("fonts: couldn't register ") + resourceNames[i] + ", falling back to Segoe UI / Cascadia Mono");
        }
        DevLog::line ("fonts: " + juce::String (registered) + " faces registered");
        jassert (registered == (int) Face::count);
    }

    HaloFonts::~HaloFonts()
    {
        if (current == this)
            current = nullptr;
    }

    juce::Typeface::Ptr HaloFonts::typeface (Face f) const
    {
        return faces[(size_t) f];
    }

    juce::Typeface::Ptr HaloFonts::typefaceForFamily (const juce::String& family, bool bold) const
    {
        if (family.startsWithIgnoreCase ("Inter Tight"))
            return faces[(size_t) (bold ? Face::interSemiBold : Face::interRegular)];
        if (family.startsWithIgnoreCase ("Lexend Exa"))
            return faces[(size_t) (bold ? Face::lexendExaSemiBold : Face::lexendExaMedium)];
        if (family.startsWithIgnoreCase ("Lexend Giga"))
            return faces[(size_t) Face::lexendGigaLight];
        if (family.startsWithIgnoreCase ("JetBrains Mono"))
            return faces[(size_t) Face::jetBrainsMono];
        if (family.startsWithIgnoreCase ("Departure Mono"))
            return faces[(size_t) Face::departureMono];
        return nullptr;
    }

    float HaloFonts::kerningFactor (const juce::Font& font, float em, float trackingEm)
    {
        const float height = font.getHeight();
        return height > 0.0f ? trackingEm * em / height : 0.0f;
    }

    float HaloFonts::roleEm (Role r)       { return metrics (r).em; }
    float HaloFonts::roleTracking (Role r) { return metrics (r).tracking; }

    juce::Font HaloFonts::face (Face f, float em, float trackingEm)
    {
        juce::Typeface::Ptr tf = current != nullptr ? current->typeface (f) : nullptr;
        juce::FontOptions options;
        if (tf != nullptr)
        {
            options = juce::FontOptions().withTypeface (tf);
        }
        else
        {
            const bool mono = f == Face::jetBrainsMono || f == Face::departureMono;
            const bool bold = f == Face::lexendExaSemiBold || f == Face::interSemiBold;
            options = juce::FontOptions (mono ? "Cascadia Mono" : "Segoe UI", 12.0f, bold ? juce::Font::bold : juce::Font::plain);
        }
        juce::Font font (options.withPointHeight (em));
        if (trackingEm != 0.0f)
            font.setExtraKerningFactor (kerningFactor (font, em, trackingEm));
        return font;
    }

    juce::Font HaloFonts::font (Role r)
    {
        return face (roleFaces[(size_t) r], metrics (r).em, metrics (r).tracking);
    }

    juce::Font HaloFonts::font (Role r, float em)
    {
        return face (roleFaces[(size_t) r], em, metrics (r).tracking);
    }
}
