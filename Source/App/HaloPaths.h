#pragma once

#include <juce_core/juce_core.h>

namespace halo
{
    // Every folder and file Halo writes outside the repo, in one place
    // (docs/redesign/plan.md section 6.1):
    //   Music\Halo\Recordings, Music\Halo\Export, Music\Halo\From EP-40
    //   Documents\Halo\Backups
    //   %APPDATA%\Halo\settings.xml (also the local library index), protocol-trace.log
    //
    // Debug builds honour HALO_DATA_ROOT=<folder>: everything above moves under
    // that folder (Music\, Documents\, AppData\), so screenshot and test runs
    // never touch the owner's real data. Release builds ignore it.
    struct HaloPaths
    {
        // The sandbox root, or an invalid File when Halo uses the real folders.
        static juce::File sandboxRoot()
        {
           #if JUCE_DEBUG
            static const juce::File root = []
            {
                const auto path = juce::SystemStats::getEnvironmentVariable ("HALO_DATA_ROOT", {}).trim().unquoted();
                return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
            }();
            return root;
           #else
            return {};
           #endif
        }

        static bool sandboxed() { return sandboxRoot() != juce::File(); }

        static juce::File musicHalo()    { return base (juce::File::userMusicDirectory, "Music").getChildFile ("Halo"); }
        static juce::File recordings()   { return musicHalo().getChildFile ("Recordings"); }
        static juce::File exports()      { return musicHalo().getChildFile ("Export"); }
        static juce::File fromEp40()     { return musicHalo().getChildFile ("From EP-40"); }
        static juce::File backups()      { return base (juce::File::userDocumentsDirectory, "Documents").getChildFile ("Halo").getChildFile ("Backups"); }
        static juce::File appData()      { return base (juce::File::userApplicationDataDirectory, "AppData").getChildFile ("Halo"); }
        static juce::File settingsFile() { return appData().getChildFile ("settings.xml"); }
        static juce::File protocolTrace(){ return appData().getChildFile ("protocol-trace.log"); }

    private:
        static juce::File base (juce::File::SpecialLocationType type, const char* sandboxName)
        {
            if (sandboxed())
                return sandboxRoot().getChildFile (sandboxName);
            return juce::File::getSpecialLocation (type);
        }
    };
}
