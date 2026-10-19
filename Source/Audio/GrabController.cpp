#include "Audio/GrabController.h"
#include "Audio/SessionRecorder.h"
#include "Core/GrabMath.h"

namespace halo
{
    GrabController::GrabController() = default;

    GrabController::~GrabController()
    {
        *alive = false;
    }

    void GrabController::setTransportPlaying (bool isPlaying)
    {
        if (playing == isPlaying)
            return;
        playing = isPlaying;
        lastDownbeat.reset(); // the next real downbeat re-anchors the bar grid
    }

    void GrabController::grab (std::shared_ptr<RollingCaptureBuffer> history, double sampleRate, std::optional<double> bpm)
    {
        if (busy)
            return;

        if (history == nullptr || sampleRate <= 0.0)
        {
            Result r;
            r.kind = Result::Kind::notMonitoring;
            r.message = "GRAB needs the monitor running";
            if (onFinished)
                onFinished (r);
            return;
        }

        busy = true;
        const auto downbeat = canGrabBars (bpm) ? lastDownbeat : std::nullopt;
        juce::Thread::launch ([this, alive = alive, history, sampleRate, bpm, downbeat, b = bars, s = seconds]
        {
            auto result = run (history, sampleRate, bpm, downbeat, b, s);
            juce::MessageManager::callAsync ([this, alive, result]
            {
                if (! *alive)
                    return;
                busy = false;
                if (onFinished)
                    onFinished (result);
            });
        });
    }

    GrabController::Result GrabController::run (std::shared_ptr<RollingCaptureBuffer> history, double sampleRate,
                                                std::optional<double> bpm, std::optional<std::int64_t> downbeat,
                                                int bars, int seconds)
    {
        Result result;
        const auto head = history->nowFrame();
        const auto oldest = history->oldestResidentFrame();

        // Clocked bar window when a real downbeat gives at least one whole bar;
        // otherwise the last N seconds.
        bool clocked = false;
        auto window = grab::secondsWindow (sampleRate, seconds, head, oldest);
        if (bpm && downbeat)
        {
            const auto w = grab::barWindow (sampleRate, *bpm, 4, bars, std::min (*downbeat, head), oldest);
            if (w.bars > 0)
            {
                window = w;
                clocked = true;
            }
        }

        const auto minFrames = (std::int64_t) (sampleRate * 0.25); // under a quarter second isn't a loop
        if (window.frames() < minFrames)
        {
            result.kind = Result::Kind::emptyHistory;
            result.message = "Not enough history yet";
            return result;
        }

        const auto frames = (int) window.frames();
        std::vector<float> l ((size_t) frames), r ((size_t) frames);
        if (! history->snapshot (window.startFrame, window.endFrame, l.data(), r.data())
            && ! history->snapshot (window.startFrame, window.endFrame, l.data(), r.data()))
        {
            result.message = "History moved: try again";
            return result;
        }

        // Trim both ends to rising zero crossings so the loop is seamless.
        std::vector<float> mono ((size_t) frames);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (l[i] + r[i]);
        const int radius = std::min (frames / 4, std::max (1, (int) (sampleRate * 0.010)));
        const int startIdx = grab::snapToZeroCrossing (mono, 0, radius);
        const int endIdx = grab::snapToZeroCrossing (mono, frames, radius);
        const bool trimOk = endIdx - startIdx >= minFrames;
        const int s = trimOk ? startIdx : 0;
        const int e = trimOk ? endIdx : frames;
        const int finalFrames = e - s;

        grab::Kind kind;
        if (clocked)
        {
            kind.clocked = true;
            kind.bars = window.bars;
            kind.bpm = (int) std::lround (*bpm);
        }
        else
        {
            kind.seconds = std::max (1, (int) std::lround (finalFrames / sampleRate));
            const std::vector<float> loop (mono.begin() + s, mono.begin() + e);
            if (const auto est = grab::estimateBpm (loop, sampleRate))
                kind.estBpm = (int) std::lround (*est);
        }

        const auto now = juce::Time::getCurrentTime();
        const grab::Stamp stamp { now.getYear(), now.getMonth() + 1, now.getDayOfMonth(),
                                  now.getHours(), now.getMinutes(), now.getSeconds() };
        const auto folder = SessionRecorder::recordingsFolder();
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (juce::String (grab::filename (kind, stamp)).upToLastOccurrenceOf (".wav", false, false), ".wav");

        {
            std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
            juce::WavAudioFormat wav;
            auto writer = out != nullptr ? wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                                         .withSampleRate (sampleRate)
                                                                         .withNumChannels (2)
                                                                         .withBitsPerSample (24))
                                         : nullptr;
            const float* channels[] { l.data() + s, r.data() + s };
            if (writer == nullptr || ! writer->writeFromFloatArrays (channels, 2, finalFrames))
            {
                result.message = "Couldn't write the grab";
                return result;
            }
        } // writer destroyed here: the WAV header is final before anyone reads it

        result.file = file;
        result.seconds = finalFrames / sampleRate;
        result.askedBars = bars;
        result.gotBars = clocked ? window.bars : 0;
        result.kind = clocked && window.bars < bars ? Result::Kind::truncated : Result::Kind::grabbed;
        return result;
    }
}
