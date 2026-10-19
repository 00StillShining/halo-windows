#include "UI/LoadPanel.h"
#include "Audio/SampleIO.h"
#include "UI/HaloColours.h"
#include "UI/HaloLookAndFeel.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
    }

    LoadPanel::LoadPanel (AppModel& m) : model (m)
    {
        exportFolderButton.onClick = []
        {
            const auto folder = SampleIO::exportFolder();
            folder.createDirectory();
            folder.revealToUser();
        };
        libraryButton.onClick = [this] { model.setMode (AppMode::capture); };
        // TE's official EP-40 guide (it links to their sample tool), opened in the
        // user's browser on request. The guide URL is the one cited in the Mac
        // app's research notes; the tool's own address wasn't confirmed.
        toolButton.onClick = [] { juce::URL ("https://teenage.engineering/guides/ep-40").launchInDefaultBrowser(); };
        for (auto* b : { &exportFolderButton, &libraryButton, &toolButton })
            addAndMakeVisible (b);
    }

    void LoadPanel::resized()
    {
        auto area = getLocalBounds().reduced (24, 20);
        textArea = area.removeFromTop (juce::jmin (area.getHeight() - 60, 520));
        auto buttons = area.removeFromTop (40);
        libraryButton.setBounds (buttons.removeFromLeft (110).reduced (2));
        exportFolderButton.setBounds (buttons.removeFromLeft (200).reduced (2));
        toolButton.setBounds (buttons.removeFromLeft (170).reduced (2));
    }

    void LoadPanel::paint (juce::Graphics& g)
    {
        auto area = textArea;
        HaloLookAndFeel::drawPanel (g, area.toFloat(), c.paper);
        area.reduce (20, 18);

        g.setFont (HaloType::label (11.0f));
        g.setColour (c.orange);
        g.drawText ("DEVICE LIBRARY: NOT READABLE YET", area.removeFromTop (18), juce::Justification::centredLeft);
        area.removeFromTop (8);

        g.setFont (HaloType::body (14.0f));
        g.setColour (c.ink);
        g.drawFittedText ("Reading the EP-40's sounds and pad assignments, and sending samples to it, use Teenage "
                          "Engineering's private USB protocol. Halo doesn't know it yet, so it shows nothing rather "
                          "than guess. (The Mac app filled this screen with mock data.)",
                          area.removeFromTop (72), juce::Justification::topLeft, 4);
        area.removeFromTop (12);

        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("HOW TO LOAD SAMPLES TODAY", area.removeFromTop (18), juce::Justification::centredLeft);
        area.removeFromTop (6);

        const juce::String steps[] {
            "1.  Record a take or GRAB in PLAY, or import a file in CAPTURE (Ctrl+4).",
            "2.  Open it in EDIT (Ctrl+3): trim, fade, pick an EP-40 QUALITY. Or CHOP it into slices.",
            "3.  EXPORT. Files go to Music\\Halo\\Export as 16-bit WAVs at EP-40 rates (CHOP adds a pads.txt).",
            "4.  Quit Halo first: Windows lets only one app use the EP-40's USB MIDI port at a time.",
            "5.  Open TE's EP Sample Tool and drag the exported files onto the sounds or pads you want.",
        };
        g.setFont (HaloType::body (13.5f));
        g.setColour (c.ink);
        for (const auto& step : steps)
            g.drawFittedText (step, area.removeFromTop (40), juce::Justification::topLeft, 2);

        area.removeFromTop (8);
        g.setFont (HaloType::mono (11.0f));
        g.setColour (c.inkSoft);
        g.drawFittedText ("EP-40 (TE docs): 999 sample slots, ~122 MB usable, 16-bit up to 46.875 kHz. "
                          "Halo's size estimates use those numbers; the device's own count is the final word.",
                          area.removeFromTop (40), juce::Justification::topLeft, 3);
    }
}
