#pragma once

#include "Stage/StageLog.h"

#include <juce_core/juce_core.h>

namespace halo
{
    // The stage's GLSL sources (Source/Stage/Shaders/*), embedded through the
    // HaloShaders binary-data target. Each source is returned ready to compile:
    //   #version 150
    //   #define HALO_HDR 0|1
    //   const float <name> = <value>;   for every HALO_TC in Core/ToneCurveConstants.inc
    //   the file, with #include "x.glsl" lines replaced by x.glsl (once each)
    //
    // Debug builds hot-reload from HALO_STAGE_SHADER_DIR when it is set: sources
    // are read from that folder (ToneCurveConstants.inc too, if it is there) and
    // changed() reports edits so the stage can rebuild its programs.
    class ShaderLibrary
    {
    public:
        explicit ShaderLibrary (StageLog&);

        juce::String source (const juce::String& file, bool hdr) const;

        bool fromDisk() const { return directory != juce::File(); }
        bool changed(); // true once after any watched file changes on disk

    private:
        juce::String raw (const juce::String& file) const;
        juce::String toneConstants() const;
        void expand (const juce::String& file, juce::StringArray& included, juce::String& out) const;
        juce::Time newestOnDisk() const;

        StageLog& log;
        juce::File directory;
        juce::Time lastSeen;
    };
}
