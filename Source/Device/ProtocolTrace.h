#pragma once

#include "App/HaloPaths.h"
#include "Core/SysEx.h"

#include <juce_core/juce_core.h>

namespace halo
{
    // Append-only log of every SysEx message to and from the EP-40, with
    // timestamps, at %APPDATA%\Halo\protocol-trace.log (port of ProtocolTrace.swift,
    // which had no producer on the Mac). It's the evidence for protocol work.
    class ProtocolTrace
    {
    public:
        static juce::File file()
        {
            return HaloPaths::protocolTrace();
        }

        void log (const char* direction, const sysex::Bytes& bytes, const juce::String& note = {})
        {
            const auto f = file();
            f.getParentDirectory().createDirectory();
            if (f.getSize() > 8 * 1024 * 1024) // keep it bounded
                f.moveFileTo (f.withFileExtension ("old.log"));
            f.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S")
                          + juce::String::formatted (".%03d ", juce::Time::getCurrentTime().getMilliseconds())
                          + direction + " " + juce::String (sysex::toHex (bytes, 4096))
                          + (note.isNotEmpty() ? "  # " + note : juce::String()) + "\n");
        }
    };
}
