#pragma once

#include <juce_core/juce_core.h>

namespace halo
{
    // The stage's own log (renderer, GPU, tier, demotions, dev-hook output).
    // HALO_STAGE_LOG names the file; the default is %TEMP%\halo-stage-<pid>.log,
    // so a daily instance and a MOCK instance never write to the same file.
    // Thread safe: the GL thread and the message thread both write to it.
    class StageLog
    {
    public:
        StageLog();

        void line (const juce::String& text);
        juce::File file() const { return path; }

        static long long processId();

    private:
        juce::File path;
        juce::CriticalSection lock;
        std::unique_ptr<juce::FileOutputStream> out;
        double startMs = 0.0;
    };
}
