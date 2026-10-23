// HaloLcdPreview: renders the replica LCD texture for a few DisplayStates to
// PNG files, with no window (plan M9 review tool).
//
//     HaloLcdPreview <out-dir>
//
// Writes lcd-live.png (tempo 120, MAIN, playing, group B, a pad), lcd-live72.png,
// lcd-waiting.png and lcd-preview.png (the demo, frame 9).
#include "Stage/DisplayRenderer.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInit; // the message manager ImageCache's timer needs; no window
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: HaloLcdPreview <out-dir>\n");
        return 2;
    }
    const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    dir.createDirectory();
    halo::DisplayRenderer renderer;

    auto save = [&dir] (const juce::Image& image, const char* name)
    {
        auto file = dir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, out))
        {
            std::fprintf (stderr, "could not write %s\n", file.getFullPathName().toRawUTF8());
            return false;
        }
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        return true;
    };

    auto live = halo::DisplayState::liveIdle();
    live.value = 120;
    live.mode = halo::DisplayMode::main;
    live.isPlaying = true;
    live.clockPulse = true;
    live.activeGroup = 1;
    live.activePadIndex = 4;
    live.leftMeter = 0.7f;
    live.rightMeter = 0.4f;
    bool ok = save (renderer.render (live), "lcd-live.png");
    auto live72 = halo::DisplayState::liveIdle();
    live72.value = 72;
    ok &= save (renderer.render (live72), "lcd-live72.png");
    ok &= save (renderer.render (halo::DisplayState::waiting()), "lcd-waiting.png");
    ok &= save (renderer.render (halo::DisplayState::previewFrame (9)), "lcd-preview.png");
    return ok ? 0 : 1;
}
