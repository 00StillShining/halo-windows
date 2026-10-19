#include "App/AppModel.h"
#include "App/MainComponent.h"
#include "UI/HaloColours.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AppModel& model)
            : DocumentWindow (name, HaloColours::graphPaper.paper, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (model), true);
            setResizable (true, true);
            setResizeLimits (HaloMetrics::windowMinimumWidth, HaloMetrics::windowMinimumHeight, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    class HaloApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise (const juce::String& commandLine) override
        {
            // HALO_MOCK_MIDI=1 (or --mock-midi) swaps the EP-40 for a synthetic
            // source, for development without hardware. Always labelled MOCK.
            const bool mock = commandLine.contains ("--mock-midi")
                           || juce::SystemStats::getEnvironmentVariable ("HALO_MOCK_MIDI", {}) == "1";

            model = std::make_unique<AppModel> (mock);
            window = std::make_unique<MainWindow> (getApplicationName(), *model);
            model->start();
        }

        void shutdown() override
        {
            window.reset();
            model.reset();
        }

        void systemRequestedQuit() override { quit(); }

    private:
        std::unique_ptr<AppModel> model;
        std::unique_ptr<MainWindow> window;
    };
}

START_JUCE_APPLICATION (halo::HaloApplication)
