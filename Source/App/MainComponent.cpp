#include "App/MainComponent.h"

namespace halo
{
    MainComponent::MainComponent (AppModel& m)
        : model (m),
          writeGate (*this, WriteGate::Environment {
              [this] { return model.writesUnlocked(); },
              [this] { return model.canTalkToDevice(); },
              [this] { const auto b = model.latestBackup(); return b ? Ep40Backup::describe (*b) : juce::String(); },
              [this] { model.backup().forceRescan(); },
              [this] (bool hide) { stage.setVisible (! hide && ! model.editor().chopOpen); },
              deviceWrites.get() }),
          statusBar (m), stage (m), rail (m, writeGate), modeBar (m)
    {
        setLookAndFeel (&lookAndFeel);
        // Desktop windows (popup menus, alert windows, tooltips) get it too.
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        addAndMakeVisible (statusBar);
        addAndMakeVisible (stage);
        addAndMakeVisible (rail);
        addAndMakeVisible (modeBar);

        addChildComponent (chopSurface);
        rail.onPreferredWidthChanged = [this] { resized(); };
        model.editor().addChangeListener (this);
        setWantsKeyboardFocus (true);
        setSize (HaloMetrics::windowDefaultWidth, HaloMetrics::windowDefaultHeight);

       #if JUCE_DEBUG
        // Debug-only capture hooks (like the Mac's DD-015 gate hooks), so UI
        // states can be screenshotted without clicking: HALO_RAIL=open,
        // HALO_MODE=1...6.
        const auto railHook = juce::SystemStats::getEnvironmentVariable ("HALO_RAIL", {});
        if (railHook == "open" || railHook == "closed")
            rail.setExpanded (railHook == "open");
        const int modeIndex = juce::SystemStats::getEnvironmentVariable ("HALO_MODE", "1").getIntValue() - 1;
        constexpr AppMode modes[] { AppMode::play, AppMode::load, AppMode::edit,
                                    AppMode::capture, AppMode::rack, AppMode::backups };
        if (modeIndex > 0 && modeIndex < 6)
            model.setMode (modes[modeIndex]);
       #endif
    }

    MainComponent::~MainComponent()
    {
        model.editor().removeChangeListener (this);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // CHOP opens over the stage; the 3D model comes back when it closes.
        const bool chop = model.editor().chopOpen;
        if (chop != chopSurface.isVisible())
        {
            chopSurface.setVisible (chop);
            stage.setVisible (! chop);
            if (chop)
                chopSurface.grabKeyboardFocus();
            resized();
        }
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        statusBar.setBounds (area.removeFromTop (HaloMetrics::statusBarHeight));
        modeBar.setBounds (area.removeFromBottom (HaloMetrics::modeBarHeight));
        rail.setBounds (area.removeFromRight (rail.preferredWidth (getWidth())));
        stage.setBounds (area);
        chopSurface.setBounds (area);
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        // Ctrl+1...6 switch modes (Cmd+1...6 on the Mac); Ctrl+M monitor; Ctrl+R record.
        if (key.getModifiers().isCtrlDown())
        {
            if (key.getKeyCode() == 'M') { model.toggleMonitor(); return true; }
            if (key.getKeyCode() == 'R') { model.toggleRecording(); return true; }
            if (key.getKeyCode() == 'G') { model.grabNow(); return true; }

            constexpr AppMode modes[] { AppMode::play, AppMode::load, AppMode::edit,
                                        AppMode::capture, AppMode::rack, AppMode::backups };
            const int index = key.getKeyCode() - '1';
            if (index >= 0 && index < 6)
            {
                model.setMode (modes[index]);
                return true;
            }
        }
        return false;
    }
}
