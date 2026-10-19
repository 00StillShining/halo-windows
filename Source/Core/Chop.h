#pragma once

#include "Core/SampleCore.h"

#include <string>
#include <vector>

// CHOP core: port of OnsetDetector.swift (spectral-flux onsets, adaptive peak
// pick), SliceSet and the pad-layout half of SendToPadsPlan.swift.
//
// The Mac planner also assigned sample-slot numbers from a MOCK device
// library. That needs the real device library (protocol, Phase 0B), so this
// port plans only what is real: which pads the slices land on, and exact bytes.
namespace halo::chop
{
    struct OnsetParameters
    {
        double sensitivity = 0.5;          // 0 = few onsets ... 1 = many
        double minSliceMilliseconds = 40.0;
        int windowSize = 1024;              // power of two
        int hopSize = 512;
    };

    struct Onset
    {
        int frame = 0;
        float strength = 0.0f; // spectral-flux peak height
    };

    std::vector<Onset> detectOnsetsWithStrength (const std::vector<float>& mono, double sampleRate, const OnsetParameters&);
    std::vector<int> detectOnsets (const std::vector<float>& mono, double sampleRate, const OnsetParameters&);

    // The strongest maxCuts onsets (each at least minGap from the others),
    // returned in time order. Fewer if fewer were detected.
    std::vector<int> strongestCuts (const std::vector<Onset>&, int maxCuts, int minGap);

    // Cuts that divide the source into `slices` equal parts.
    std::vector<int> evenCuts (int sourceFrames, int slices);

    struct Slice
    {
        int index = 0;
        int startFrame = 0;
        int endFrame = 0; // exclusive
        int frameCount() const noexcept { return endFrame > startFrame ? endFrame - startFrame : 0; }
    };

    // Sorted, unique interior cut points that divide the source into slices.
    class SliceSet
    {
    public:
        SliceSet() = default;
        SliceSet (int sourceFrames, const std::vector<int>& onsets);

        int sourceFrames() const noexcept { return frames; }
        const std::vector<int>& cuts() const noexcept { return cutPoints; }
        int sliceCount() const noexcept { return frames > 0 ? (int) cutPoints.size() + 1 : 0; }
        std::vector<Slice> slices() const;
        int sliceAt (int frame) const noexcept;

        // Edits keep every slice at least minGap frames long.
        bool addCut (int frame, int minGap);
        void removeCut (int index);
        void moveCut (int index, int frame, int minGap);
        void clear() { cutPoints.clear(); }

    private:
        int frames = 0;
        std::vector<int> cutPoints;
    };

    struct PadWrite
    {
        int sliceIndex = 0;
        int group = 0;      // 0..3 = A..D
        int gridIndex = 0;  // 0..11, row-major from 7 8 9
        std::string name;   // "AMEN.01"
        long long payloadBytes = 0;
    };

    struct PadPlan
    {
        std::vector<PadWrite> writes;
        int overflowCount = 0; // slices with no pad left in the group
        long long totalBytes = 0;
        double longestSeconds = 0.0; // of the slices placed on pads, after any cap
        int tooLongCount = 0;        // placed slices still over maxSoundSeconds
        int trimmedCount = 0;        // placed slices shortened by the cap
        int group = 0;
        int startGridIndex = 0;
    };

    // Lay slices onto consecutive pads of one group from a start pad, with
    // exact bytes at the chosen treatment.
    // maxSliceFrames > 0 caps every slice's length (its start stays put; the
    // tail is cut) — AUTO-TRIM.
    PadPlan planPads (const std::vector<Slice>&, int channelCount, Treatment, double sourceRate,
                      int group, int startGridIndex, const std::string& baseName, int maxSliceFrames = 0);

    // The longest per-slice length (seconds) such that every slice is at most
    // maxSeconds and all of them together fit budgetSeconds of audio. Slices
    // already shorter than the cap keep their full length.
    double capToFit (const std::vector<double>& sliceSeconds, double maxSeconds, double budgetSeconds);

    // Micro-fade used on every slice by default (4 ms in, 4 ms out).
    SamplePrep microFadePrep (double sampleRate);
}
