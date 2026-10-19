#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // LOAD mode. The Mac app showed the device's sound slots and pad
    // assignments from MOCK data, because reading them needs the EP-40's
    // private protocol. This shows the honest state, and the working path for
    // today: prepare in EDIT / CHOP, export, load with TE's EP Sample Tool.
    class LoadPanel : public juce::Component
    {
    public:
        explicit LoadPanel (AppModel&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        AppModel& model;
        juce::TextButton exportFolderButton { "OPEN EXPORT FOLDER" }, libraryButton { "LIBRARY" }, toolButton { "TE EP-40 GUIDE" };
        juce::Rectangle<int> textArea;
    };
}
