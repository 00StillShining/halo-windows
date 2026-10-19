#include "App/AppModel.h"
#include "App/MainComponent.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/HaloColours.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, AppModel& model)
            : DocumentWindow (name, HaloColours::graphPaper.paper, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (model), true);
            setResizable (true, true);
            setResizeLimits (HaloMetrics::windowMinimumWidth, HaloMetrics::windowMinimumHeight, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    class HaloApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise (const juce::String& commandLine) override
        {
            // HALO_MOCK_MIDI=1 (or --mock-midi) swaps the EP-40 for a synthetic
            // source, for development without hardware. Always labelled MOCK.
            const bool mock = commandLine.contains ("--mock-midi")
                           || juce::SystemStats::getEnvironmentVariable ("HALO_MOCK_MIDI", {}) == "1";

            model = std::make_unique<AppModel> (mock);
            window = std::make_unique<MainWindow> (getApplicationName(), *model);
            model->start();

           #if JUCE_DEBUG
            // Debug-only verification hooks: HALO_MONITOR_DB sets the level,
            // HALO_AUTOMONITOR=1 starts the route, HALO_AUTORECORD=<seconds>
            // records one take of that length.
            const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
            // HALO_OPEN=<path>[|chop]: open a file in EDIT (or CHOP) without importing it.
            if (const auto open = juce::StringArray::fromTokens (env ("HALO_OPEN"), "|", {}); open.size() > 0)
            {
                Library::Item item;
                item.file = juce::File (open[0]);
                item.title = item.file.getFileNameWithoutExtension();
                model->openInEditor (item, open[1] == "chop");

                // HALO_AUTOEXPORT=edit|slices: export once the file has loaded.
                const auto what = env ("HALO_AUTOEXPORT");
                if (what.isNotEmpty())
                    juce::Timer::callAfterDelay (3000, [this, what]
                    {
                        if (what == "slices") model->editor().exportSlices();
                        else model->editor().exportEdit();
                    });
            }
            // HALO_DEVTEST=download:<slot>: once the EP-40 library is synced, download
            // one sound into build\devtest-<slot>.wav (never into the library).
            // HALO_DEVTEST=writetest: the first guarded write round trip. Needs a
            // complete backup. Uploads a test tone to an empty USER slot, verifies
            // size and bytes, does a pad round trip that restores the original
            // sound, deletes the tone, and logs every step to build\writetest.txt.
            if (env ("HALO_DEVTEST") == "writetest")
                juce::Timer::callAfterDelay (15000, [this] { runWriteTest(); });

            // HALO_DEVTEST=backup: start a full (read-only) backup once synced.
            if (env ("HALO_DEVTEST") == "backup")
                juce::Timer::callAfterDelay (15000, [this] { model->backup().start(); });
            if (const auto test = env ("HALO_DEVTEST"); test.startsWith ("download:"))
            {
                const int slot = test.fromFirstOccurrenceOf (":", false, false).getIntValue();
                juce::Timer::callAfterDelay (15000, [this, slot]
                {
                    model->device().download (slot, [slot] (std::optional<AudioData> audio, juce::String error)
                    {
                        const auto out = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("devtest-" + juce::String (slot) + ".wav");
                        if (audio)
                            SampleIO::writeWav16 (*audio, out);
                        out.withFileExtension ("txt").replaceWithText (audio ? "ok " + juce::String (audio->frameCount()) + " frames" : "error " + error);
                    });
                });
            }
            if (env ("HALO_MONITOR_DB").isNotEmpty())
                model->monitor().setGainDb (env ("HALO_MONITOR_DB").getDoubleValue());
            if (const auto wanted = env ("HALO_OUTPUT"); wanted.isNotEmpty())
                for (const auto& name : model->monitor().outputNames())
                    if (name.containsIgnoreCase (wanted))
                    {
                        model->monitor().setOutputName (name);
                        break;
                    }
            if (env ("HALO_AUTOMONITOR") == "1")
            {
                // HALO_AUTOGRAB=<seconds>[,bars]: GRAB once after that long.
                const auto autoGrab = juce::StringArray::fromTokens (env ("HALO_AUTOGRAB"), ",", {});
                if (autoGrab.size() > 0 && autoGrab[0].getIntValue() > 0)
                {
                    if (autoGrab.size() > 1)
                        model->grabber().bars = autoGrab[1].getIntValue();
                    juce::Timer::callAfterDelay (1500 + autoGrab[0].getIntValue() * 1000, [this] { model->grabNow(); });
                }

                juce::Timer::callAfterDelay (1500, [this, seconds = env ("HALO_AUTORECORD").getIntValue()]
                {
                    model->toggleMonitor();
                    if (seconds > 0)
                    {
                        model->toggleRecording();
                        juce::Timer::callAfterDelay (seconds * 1000, [this] { model->toggleRecording(); });
                    }
                });
            }
           #endif
        }

        void shutdown() override
        {
            window.reset();
            model.reset();
        }

        void systemRequestedQuit() override { quit(); }

    private:
       #if JUCE_DEBUG
        void runWriteTest()
        {
            auto out = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile ("writetest.txt");
            out.replaceWithText ({});
            auto step = [out] (const juce::String& line) { out.appendText (juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + line + "\n"); };

            auto& lib = model->device();
            auto& writer = model->deviceWriter();
            if (! model->writesUnlocked())
                return step ("ABORT: writes are locked (no complete backup, or not synced)");

            // A 0.3 s, 440 Hz tone at the device's native rate, and the exact bytes expected back.
            AudioData tone;
            tone.sampleRate = lib.info().nativeRate;
            tone.channels.assign (1, std::vector<float> ((size_t) (tone.sampleRate * 0.3)));
            auto expected = std::make_shared<te::Bytes>();
            for (size_t i = 0; i < tone.channels[0].size(); ++i)
            {
                const float v = 0.5f * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / (float) tone.sampleRate);
                tone.channels[0][i] = v;
                const auto s = (std::int16_t) juce::roundToInt (v * 32767.0f);
                expected->push_back ((std::uint8_t) (s & 0xFF));
                expected->push_back ((std::uint8_t) ((s >> 8) & 0xFF));
            }
            step ("upload: " + juce::String ((int) expected->size()) + " bytes, free slot "
                  + juce::String (lib.firstFreeSlot (Ep40Writer::userFirstSlot, Ep40Writer::userLastSlot)));

            writer.upload (tone, "halo test tone", 0, [this, step, expected] (Ep40Writer::Result up)
            {
                step (juce::String ("1 upload + STAT verify: ") + (up.ok ? "OK " : "FAIL ") + up.message);
                if (! up.ok)
                    return;
                const int slot = up.slot;
                model->device().downloadRaw ((std::uint16_t) slot, [this, step, expected, slot] (std::optional<te::Bytes> back, juce::String error)
                {
                    const bool same = back && *back == *expected;
                    step (juce::String ("2 download compare: ") + (same ? "OK identical " : "FAIL ") + juce::String (back ? (int) back->size() : 0) + " bytes " + error);

                    // Pad round trip on the first pad that has a sound.
                    auto& lib = model->device();
                    int group = -1, grid = -1, original = 0;
                    for (int g = 0; g < 4 && group < 0; ++g)
                        for (int i = 0; i < 12; ++i)
                            if (lib.padSound (g, i) > 0) { group = g; grid = i; original = lib.padSound (g, i); break; }
                    const int project = lib.activeProject();
                    if (group < 0)
                        return step ("3 pad round trip: SKIPPED (no assigned pad found)");

                    auto& writer = model->deviceWriter();
                    writer.assignPad (project, group, grid, slot, [this, step, project, group, grid, original, slot] (Ep40Writer::Result a)
                    {
                        auto& writer = model->deviceWriter();
                        writer.readPad (project, group, grid, [this, step, project, group, grid, original, slot, a] (int now)
                        {
                            step ("3 pad P" + juce::String (project) + " " + juce::String (ep40::groupLabels[(size_t) group].data())
                                  + juce::String (ep40::gridLabels[(size_t) grid].data()) + " set to test sound: "
                                  + (a.ok && now == slot ? "OK" : "FAIL") + " (reads " + juce::String (now) + ")");
                            auto& writer = model->deviceWriter();
                            writer.assignPad (project, group, grid, original, [this, step, project, group, grid, original, slot] (Ep40Writer::Result r)
                            {
                                auto& writer = model->deviceWriter();
                                writer.readPad (project, group, grid, [this, step, original, slot, r] (int now2)
                                {
                                    step (juce::String ("4 pad restored to its original sound ") + juce::String (original) + ": "
                                          + (r.ok && now2 == original ? "OK" : "FAIL") + " (reads " + juce::String (now2) + ")");
                                    model->deviceWriter().deleteSound (slot, [step] (Ep40Writer::Result d)
                                    {
                                        step (juce::String ("5 delete test sound + confirm gone: ") + (d.ok ? "OK " : "FAIL ") + d.message);
                                        step ("DONE");
                                    });
                                });
                            });
                        });
                    });
                });
            });
        }
       #endif

        std::unique_ptr<AppModel> model;
        std::unique_ptr<MainWindow> window;
    };
}

START_JUCE_APPLICATION (halo::HaloApplication)
