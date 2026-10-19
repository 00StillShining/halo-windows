#include "UI/Design/Primitives/StageFrame.h"
#include "Core/HonestState.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
    }

    StageFrame::StageFrame (AppModel& m, juce::Component& s) : model (m), stage (s)
    {
        setOpaque (true);
        addAndMakeVisible (stage);
        model.addListener (this);
        startTimer (1000); // connection text, cheap
    }

    StageFrame::~StageFrame()
    {
        model.removeListener (this);
        removeChildComponent (&stage);
    }

    void StageFrame::displayChanged (const DisplayState& s)
    {
        if (s.feedMode != lastFeed)
        {
            lastFeed = s.feedMode;
            repaint();
        }
    }

    juce::Rectangle<int> StageFrame::stageBounds() const
    {
        return getLocalBounds().withTrimmedTop (chromeHeight).reduced (band);
    }

    void StageFrame::resized()
    {
        stage.setBounds (stageBounds());
    }

    void StageFrame::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        g.fillAll (t.scr.bg);
        const bool mock = model.isMock();
        const bool preview = model.display().feedMode == FeedMode::preview || ! model.isConnected();

        // Chrome row: what this is, the honest tabs, how to drive it.
        auto row = getLocalBounds().toFloat().removeFromTop ((float) chromeHeight);
        const auto ui = HaloFonts::font (Role::ui);
        const auto labelFont = HaloFonts::font (Role::label);
        float x = row.getX();
        pictograms::draw (g, Pict::play, { x, row.getCentreY() - 6.0f, 12.0f, 12.0f }, t.scr.ink, 1.4f);
        x += 18.0f;
        paint::text (g, "PLAY", { x, row.getY(), 60.0f, row.getHeight() }, labelFont, t.scr.ink);
        x += paint::textWidth (labelFont, "PLAY") + 8.0f;
        const auto what = "ep" + enDash + "40 riddim" + dot + (mock ? "mock midi" : model.isConnected() ? "live midi" : "no ep" + enDash + "40 connected" + dot + "connect over usb");
        const auto hint = "drag orbit" + dot + "wheel zoom" + dot + "double-click reset";

        // Tabs: the long forms when there's room, the short forms otherwise.
        const juce::String mockLong = "MOCK SOURCE" + dot + "SYNTHETIC EP" + enDash + "40 MIDI, NOT REAL HARDWARE";
        const juce::String previewLong = "PREVIEW" + dot + "DEMO DISPLAY FRAMES, NOT THE DEVICE";
        const float whatW = paint::textWidth (ui, what) + 16.0f;
        const float hintW = paint::textWidth (ui, hint) + 8.0f;
        auto tabsWidth = [&] (bool longForm)
        {
            float w = 0.0f;
            if (mock) w += paint::tagWidth (longForm ? mockLong : "MOCK SOURCE", paint::TagStyle::mock) + 8.0f;
            if (preview) w += paint::tagWidth (longForm ? previewLong : "PREVIEW", paint::TagStyle::preview) + 8.0f;
            return w;
        };
        const float room = row.getRight() - x;
        const bool showWhat = room >= whatW + tabsWidth (false);
        const bool longTabs = room >= (showWhat ? whatW : 0.0f) + tabsWidth (true) + hintW;
        const bool showHint = room >= (showWhat ? whatW : 0.0f) + tabsWidth (longTabs) + hintW;
        if (showWhat)
        {
            paint::text (g, what, { x, row.getY(), whatW, row.getHeight() }, ui, t.scr.dim);
            x += whatW;
        }
        const float tagTop = row.getCentreY() - 9.0f;
        if (mock)
            x = paint::tag (g, { x, tagTop }, longTabs ? mockLong : juce::String ("MOCK SOURCE"), paint::TagStyle::mock, {}, {}, 18.0f).getRight() + 8.0f;
        if (preview)
            x = paint::tag (g, { x, tagTop }, longTabs ? previewLong : juce::String ("PREVIEW"), paint::TagStyle::preview, {}, {}, 18.0f).getRight() + 8.0f;
        if (showHint)
            paint::text (g, hint, row.withLeft (row.getRight() - hintW), ui, t.scr.dim, juce::Justification::centredRight);

        // The band around the stage cell.
        const auto cell = stageBounds().toFloat();
        const auto outer = cell.expanded ((float) band);
        if (mock)
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path ring;
            ring.addRectangle (outer);
            ring.addRectangle (preview ? cell.expanded (3.0f) : cell);
            ring.setUsingNonZeroWinding (false);
            g.reduceClipRegion (ring);
            paint::hazard (g, outer, 14.0f);
        }
        if (preview)
        {
            const float inset = mock ? 1.5f : 4.0f;
            paint::dashedRect (g, cell.expanded (inset), t.honest.preview, 1.5f, 6.0f, 4.0f);
        }
        if (! mock && ! preview)
        {
            g.setColour (t.scr.rule);
            g.drawRect (cell.expanded (1.0f), 1.0f);
        }

        // The poster stands in for the GL child while that is hidden.
        if (! stage.isVisible())
        {
            if (poster.isValid())
                g.drawImage (poster, cell, juce::RectanglePlacement::centred);
            else
            {
                g.setColour (t.scr.panel);
                g.fillRect (cell);
                paint::text (g, "stage paused while a card is open", cell, ui, t.scr.dim, juce::Justification::centred);
            }
        }
    }
}
