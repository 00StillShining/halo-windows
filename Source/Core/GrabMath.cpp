#include "Core/GrabMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace halo::grab
{
    double framesPerBeat (double sampleRate, double bpm)
    {
        return bpm > 0.0 ? sampleRate * 60.0 / bpm : 0.0;
    }

    Window barWindow (double sampleRate, double bpm, int beatsPerBar, int bars,
                      std::int64_t lastDownbeatFrame, std::int64_t oldestResident)
    {
        const auto perBar = (std::int64_t) std::llround (framesPerBeat (sampleRate, bpm) * std::max (1, beatsPerBar));
        if (perBar <= 0)
            return { lastDownbeatFrame, lastDownbeatFrame, 0 };

        int n = std::max (1, bars);
        while (n > 0 && lastDownbeatFrame - n * perBar < oldestResident)
            --n;
        const auto start = lastDownbeatFrame - n * perBar;
        return { std::max (start, oldestResident), lastDownbeatFrame, n };
    }

    Window secondsWindow (double sampleRate, double seconds, std::int64_t now, std::int64_t oldestResident)
    {
        const auto want = (std::int64_t) std::llround (seconds * sampleRate);
        return { std::max (now - want, oldestResident), now, 0 };
    }

    int snapToZeroCrossing (const std::vector<float>& mono, int target, int radius)
    {
        const int n = (int) mono.size();
        if (n <= 1)
            return std::clamp (target, 0, std::max (n, 0));

        const int clamped = std::clamp (target, 0, n);
        const int lo = std::max (1, clamped - std::max (0, radius));
        const int hi = std::min (n - 1, clamped + std::max (0, radius));
        int best = -1, bestDistance = std::numeric_limits<int>::max();
        for (int i = lo; i <= hi; ++i)
            if (mono[(size_t) i - 1] < 0.0f && mono[(size_t) i] >= 0.0f && std::abs (i - clamped) < bestDistance)
            {
                bestDistance = std::abs (i - clamped);
                best = i;
            }
        return best >= 0 ? best : clamped;
    }

    std::optional<double> estimateBpm (const std::vector<float>& mono, double sampleRate, double minBpm, double maxBpm)
    {
        if (sampleRate <= 0.0 || (double) mono.size() <= sampleRate)
            return std::nullopt;

        const int hop = std::max (1, (int) std::lround (sampleRate * 0.010)); // ~10 ms
        const int hopCount = (int) mono.size() / hop;
        if (hopCount <= 8)
            return std::nullopt;

        std::vector<float> energy ((size_t) hopCount);
        for (int k = 0; k < hopCount; ++k)
        {
            float sum = 0.0f;
            for (int i = 0; i < hop; ++i)
            {
                const float s = mono[(size_t) (k * hop + i)];
                sum += s * s;
            }
            energy[(size_t) k] = sum;
        }

        // Half-wave-rectified first difference: onset novelty.
        std::vector<float> novelty ((size_t) hopCount, 0.0f);
        double noveltyPower = 0.0;
        for (int k = 1; k < hopCount; ++k)
        {
            novelty[(size_t) k] = std::max (0.0f, energy[(size_t) k] - energy[(size_t) k - 1]);
            noveltyPower += (double) novelty[(size_t) k] * novelty[(size_t) k];
        }
        if (noveltyPower <= 0.0)
            return std::nullopt;

        const double hopSeconds = (double) hop / sampleRate;
        const int minLag = std::max (1, (int) std::lround (60.0 / maxBpm / hopSeconds));
        const int maxLag = std::min (hopCount - 1, (int) std::lround (60.0 / minBpm / hopSeconds));
        if (minLag > maxLag)
            return std::nullopt;

        int bestLag = -1;
        double bestScore = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (int k = lag; k < hopCount; ++k)
                acc += (double) novelty[(size_t) k] * novelty[(size_t) (k - lag)];
            if (acc > bestScore)
            {
                bestScore = acc;
                bestLag = lag;
            }
        }

        // The winning lag must carry a real share of the novelty energy.
        if (bestLag <= 0 || bestScore <= noveltyPower * 0.05)
            return std::nullopt;

        const double bpm = 60.0 / ((double) bestLag * hopSeconds);
        if (bpm < minBpm || bpm > maxBpm)
            return std::nullopt;
        return bpm;
    }

    std::string filename (const Kind& kind, const Stamp& t)
    {
        char stamp[32];
        std::snprintf (stamp, sizeof (stamp), "%04d-%02d-%02d-%02d%02d-%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);

        char spec[48];
        if (kind.clocked)
            std::snprintf (spec, sizeof (spec), "%02dbars-%dbpm", kind.bars, kind.bpm);
        else if (kind.estBpm)
            std::snprintf (spec, sizeof (spec), "%ds-est%dbpm", kind.seconds, *kind.estBpm);
        else
            std::snprintf (spec, sizeof (spec), "%ds", kind.seconds);

        return std::string ("grab-") + stamp + "-" + spec + ".wav";
    }

    namespace
    {
        std::optional<int> bpmValue (std::string token)
        {
            if (token.rfind ("est", 0) == 0)
                token = token.substr (3);
            if (token.size() <= 3 || token.compare (token.size() - 3, 3, "bpm") != 0)
                return std::nullopt;
            try { return std::stoi (token.substr (0, token.size() - 3)); }
            catch (...) { return std::nullopt; }
        }

        bool endsWith (const std::string& s, const std::string& suffix)
        {
            return s.size() >= suffix.size() && s.compare (s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }

    std::optional<std::string> titleFromFilename (const std::string& name)
    {
        auto stem = name;
        if (const auto dot = stem.rfind ('.'); dot != std::string::npos)
            stem = stem.substr (0, dot);

        std::vector<std::string> parts;
        std::stringstream ss (stem);
        for (std::string part; std::getline (ss, part, '-');)
            parts.push_back (part);

        // grab | yyyy | MM | dd | HHmm | ss | spec [| bpm]
        if (parts.size() < 7 || parts[0] != "grab" || parts[4].size() != 4)
            return std::nullopt;

        const auto when = parts[1] + "-" + parts[2] + "-" + parts[3] + " " + parts[4].substr (0, 2) + "." + parts[4].substr (2);
        const auto& spec = parts[6];
        const auto bpm = parts.size() >= 8 ? bpmValue (parts[7]) : std::nullopt;
        const std::string dot = " \xC2\xB7 "; // " · " in UTF-8

        try
        {
            if (endsWith (spec, "bars"))
                return "GRAB" + dot + when + dot + std::to_string (std::stoi (spec.substr (0, spec.size() - 4))) + " BARS"
                       + (bpm ? dot + std::to_string (*bpm) + " BPM" : std::string());
            if (endsWith (spec, "s"))
                return "GRAB" + dot + when + dot + std::to_string (std::stoi (spec.substr (0, spec.size() - 1))) + " S"
                       + (bpm ? dot + "~" + std::to_string (*bpm) + " BPM (EST)" : std::string());
        }
        catch (...)
        {
        }
        return std::nullopt;
    }
}
