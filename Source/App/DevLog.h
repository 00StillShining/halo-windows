#pragma once

#include <juce_core/juce_core.h>

namespace halo
{
    // Debug-only line log for the screenshot harness (tools/dev/ui_shots.ps1):
    // HALO_UI_LOG=<file> appends one line per event ("HALO_UI_READY ...",
    // "fonts: 9 faces registered"). Without the variable it only goes to the
    // debugger output. Release builds compile it away.
    struct DevLog
    {
        static void line (const juce::String& text)
        {
           #if JUCE_DEBUG
            juce::Logger::outputDebugString (text);
            static const juce::File file = []
            {
                const auto path = juce::SystemStats::getEnvironmentVariable ("HALO_UI_LOG", {}).trim().unquoted();
                return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
            }();
            if (file != juce::File())
                file.appendText (text + "\n", false, false, "\n");
           #else
            juce::ignoreUnused (text);
           #endif
        }
    };
}
