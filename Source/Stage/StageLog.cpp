#include "Stage/StageLog.h"

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace halo
{
    long long StageLog::processId()
    {
       #if JUCE_WINDOWS
        return (long long) _getpid();
       #else
        return (long long) getpid();
       #endif
    }

    StageLog::StageLog()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_LOG", {});
        path = env.isNotEmpty()
                 ? juce::File (env)
                 : juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("halo-stage-" + juce::String (StageLog::processId()) + ".log");
        path.getParentDirectory().createDirectory();
        path.deleteFile();
        out = std::make_unique<juce::FileOutputStream> (path);
        if (out->failedToOpen())
            out.reset();
        startMs = juce::Time::getMillisecondCounterHiRes();
    }

    void StageLog::line (const juce::String& text)
    {
        const auto stamp = juce::String ((juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001, 3);
        DBG ("stage: " << text);
        const juce::ScopedLock sl (lock);
        if (out == nullptr)
            return;
        out->writeText ("[" + stamp + "] " + text + "\n", false, false, nullptr);
        out->flush();
    }
}
