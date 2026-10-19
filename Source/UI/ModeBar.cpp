#include "UI/ModeBar.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
        constexpr AppMode modes[] { AppMode::play, AppMode::load, AppMode::edit,
                                    AppMode::capture, AppMode::rack, AppMode::backups };
    }

    juce::String ModeBar::title (AppMode mode)
    {
        switch (mode)
        {
            case AppMode::play:    return "PLAY";
            case AppMode::load:    return "LOAD";
            case AppMode::edit:    return "EDIT";
            case AppMode::capture: return "CAPTURE";
            case AppMode::rack:    return "RACK";
            case AppMode::backups: return "BACKUPS";
        }
        return {};
    }

    ModeBar::ModeBar (AppModel& m) : model (m)
    {
        int index = 1;
        for (auto mode : modes)
        {
            auto* button = buttons.add (new juce::TextButton (title (mode) + "  CTRL " + juce::String (index++)));
            button->setClickingTogglesState (false);
            button->onClick = [this, mode] { model.setMode (mode); };
            addAndMakeVisible (button);
        }
        model.addListener (this);
        statusChanged();
    }

    ModeBar::~ModeBar()
    {
        model.removeListener (this);
    }

    void ModeBar::statusChanged()
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (modes[i] == model.mode(), juce::dontSendNotification);
    }

    void ModeBar::paint (juce::Graphics& g)
    {
        g.fillAll (c.metal.withAlpha (0.55f));
        g.setColour (c.ink.withAlpha (0.16f));
        g.fillRect (getLocalBounds().removeFromTop (1));
    }

    void ModeBar::resized()
    {
        auto area = getLocalBounds().reduced (24, 10);
        const int slot = area.getWidth() / juce::jmax (1, buttons.size());
        for (auto* button : buttons)
            button->setBounds (area.removeFromLeft (slot).withSizeKeepingCentre (juce::jmin (180, slot - 16), 36));
    }
}
