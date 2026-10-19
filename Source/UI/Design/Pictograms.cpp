#include "UI/Design/Pictograms.h"

#include <array>

namespace halo::pictograms
{
    namespace
    {
        // Centre-origin coordinates on a 16 x 16 grid (-8..8). Dots are
        // zero-length strokes ("M x,y L x,y.01"), drawn by the round caps.
        juce::Path build (Pict p)
        {
            auto svg = [] (const char* d) { return juce::Drawable::parseSVGPath (d); };
            juce::Path out;
            auto circle = [&out] (float cx, float cy, float r) { out.addEllipse (cx - r, cy - r, 2 * r, 2 * r); };
            auto rect = [&out] (float x, float y, float w, float h, float r) { out.addRoundedRectangle (x, y, w, h, r); };
            auto add = [&out, &svg] (const char* d) { out.addPath (svg (d)); };

            switch (p)
            {
                case Pict::play:      add ("M-4.5,-6.2 L6,0 L-4.5,6.2 Z"); break;
                case Pict::stop:      rect (-5.0f, -5.0f, 10.0f, 10.0f, 1.0f); break;
                case Pict::rec:       circle (0.0f, 0.0f, 5.2f); break;
                case Pict::load:      rect (-7.0f, -4.6f, 14.0f, 10.6f, 1.8f); add ("M-3.4,-0.6 L0,2.6 L3.4,-0.6 M0,2.6 L0,-7.6"); break;
                case Pict::edit:      add ("M-8,3.6 L-5.4,3.6 L-3.6,-4.4 L-0.9,6.2 L1.8,-7.1 L3.6,3.6 L8,3.6"); break;
                case Pict::chop:      circle (-4.4f, 4.4f, 2.6f); circle (4.4f, 4.4f, 2.6f); add ("M-2.6,2.6 L5.3,-7.1 M2.6,2.6 L-5.3,-7.1"); break;
                case Pict::capture:   circle (-4.4f, -1.0f, 3.9f); circle (4.4f, -1.0f, 3.9f); add ("M-8,5.6 L8,5.6 M-4.4,-1 L-4.4,-0.99 M4.4,-1 L4.4,-0.99"); break;
                case Pict::rack:      rect (-8.0f, -3.6f, 5.0f, 7.2f, 1.0f); rect (3.0f, -3.6f, 5.0f, 7.2f, 1.0f); add ("M-3,0 L3,0 M-1.3,-1.6 L0.4,0 L-1.3,1.6"); break;
                case Pict::backup:    rect (-8.0f, -5.6f, 16.0f, 11.2f, 1.6f); circle (-3.4f, -1.4f, 1.7f); circle (3.4f, -1.4f, 1.7f);
                                      add ("M-1.7,-1.4 L1.7,-1.4 M-4.6,5.6 L-3.4,2.6 L3.4,2.6 L4.6,5.6"); break;
                case Pict::monitor:   add ("M-6.4,3 L-6.4,0 A6.4,6.4 0 0 1 6.4,0 L6.4,3"); rect (-7.6f, 1.4f, 3.0f, 5.6f, 1.0f); rect (4.6f, 1.4f, 3.0f, 5.6f, 1.0f); break;
                case Pict::grab:      add ("M6.6,1.2 A6.6,6.6 0 1 1 3.2,-5.6 M3.2,-5.6 L-0.8,-6.6 M3.2,-5.6 L3.4,-1.6"); break;
                case Pict::usb:       rect (-3.6f, -7.6f, 7.2f, 6.2f, 0.8f); add ("M-1.6,-5.6 L-1.6,-4.2 M1.6,-5.6 L1.6,-4.2 M-5.4,-1.4 L5.4,-1.4 L5.4,2.6 A5.4,5,0,0,1,-5.4,2.6 Z M0,7.6 L0,7.4"); break;
                case Pict::plugX:     add ("M-3,-7.4 L-3,-4 M3,-7.4 L3,-4 M-5.4,-4 L5.4,-4 L5.4,-0.4 A5.4,5,0,0,1,-5.4,-0.4 Z M0,4.6 L0,7.6 M3.4,3.6 L7.4,7.6 M7.4,3.6 L3.4,7.6"); break;
                case Pict::lockClosed:rect (-5.6f, -1.0f, 11.2f, 8.4f, 1.5f); add ("M-3.6,-1 L-3.6,-3.6 A3.6,3.6 0 0 1 3.6,-3.6 L3.6,-1"); break;
                case Pict::lockOpen:  rect (-5.6f, -1.0f, 11.2f, 8.4f, 1.5f); add ("M-3.6,-1 L-3.6,-3.6 A3.6,3.6 0 0 1 3.6,-3.6 L3.6,-5.6"); break;
                case Pict::triangle:  add ("M0,-6.8 L7.6,6.4 L-7.6,6.4 Z M0,-1.8 L0,2 M0,4.2 L0,4.21"); break;
                case Pict::info:      circle (0.0f, 0.0f, 7.0f); add ("M0,-0.6 L0,3.8 M0,-3.6 L0,-3.59"); break;
                case Pict::folder:    add ("M-8,-5.4 L-3,-5.4 L-1.4,-3.2 L8,-3.2 L8,6 L-8,6 Z"); break;
                case Pict::eye:       add ("M-8,0 C-4,-6 4,-6 8,0 C4,6 -4,6 -8,0 Z"); circle (0.0f, 0.0f, 2.4f); break;
                case Pict::ear:       add ("M-4.6,-1.6 A5,5.4 0 1 1 4.4,1.6 C3.2,3.4 1.4,3.8 0.8,5.6 C0.2,7.4 -2.8,7.8 -3.8,5.6 M-1.6,-1.6 A2,2.2 0 1 1 1.8,0.4"); break;
                case Pict::speaker:   add ("M-7,-3 L-3,-3 L2,-7 L2,7 L-3,3 L-7,3 Z M5,-3 C7,-1 7,1 5,3"); break;
                case Pict::feed:      circle (0.0f, 0.0f, 7.0f); add ("M-3.6,-0.8 L-3.6,-0.79 M3.6,-0.8 L3.6,-0.79 M0,-3.6 L0,-3.59 M-2.4,2.8 L-2.4,2.81 M2.4,2.8 L2.4,2.81"); break;
                case Pict::clock:     circle (0.0f, 0.0f, 7.0f); add ("M0,-4 L0,0 L3,2"); break;
                case Pict::device:    rect (-8.0f, -5.4f, 16.0f, 10.8f, 1.6f); add ("M-5,-2.4 L1,-2.4 M-5,1.6 L-4.99,1.6 M-2,1.6 L-1.99,1.6 M1,1.6 L1.01,1.6"); circle (4.8f, 0.0f, 1.6f); break;
                case Pict::pads:      for (int r = 0; r < 2; ++r) for (int c = 0; c < 3; ++c) rect (-7.6f + (float) c * 5.4f, -5.0f + (float) r * 5.6f, 4.0f, 4.0f, 0.8f); break;
                case Pict::file:      add ("M-5,-7.4 L2,-7.4 L6,-3.4 L6,7.4 L-5,7.4 Z M2,-7.4 L2,-3.4 L6,-3.4"); break;
                case Pict::upload:    add ("M0,4 L0,-7 M-4.4,-2.6 L0,-7 L4.4,-2.6 M-6,7.2 L6,7.2"); break;
                case Pict::exportOut: add ("M-6,0 L-6,6.6 L6,6.6 L6,0 M0,3 L0,-7.4 M-3.6,-3.8 L0,-7.4 L3.6,-3.8"); break;
                case Pict::caret:     add ("M-4.6,2.6 L0,-2.4 L4.6,2.6"); break;
                case Pict::arrowUp:   add ("M0,7 L0,-7 M-5,-2 L0,-7 L5,-2"); break;
                case Pict::arrowDown: add ("M0,-7 L0,7 M-5,2 L0,7 L5,2"); break;
                case Pict::arrowLeft: add ("M7,0 L-7,0 M-2,-5 L-7,0 L-2,5"); break;
                case Pict::arrowRight:add ("M-7,0 L7,0 M2,-5 L7,0 L2,5"); break;
                case Pict::enter:     add ("M6,-6 L6,2 L-6,2 M-2.6,-1.4 L-6,2 L-2.6,5.4"); break;
                case Pict::check:     add ("M-6,0.4 L-2,4.4 L6,-4.8"); break;
                case Pict::cross:     add ("M-5,-5 L5,5 M5,-5 L-5,5"); break;
                case Pict::chevronLeft:  add ("M2,-5 L-3,0 L2,5"); break;
                case Pict::chevronRight: add ("M-2,-5 L3,0 L-2,5"); break;
                case Pict::none:
                case Pict::count:
                default: break;
            }
            return out;
        }

        const std::array<juce::Path, (size_t) Pict::count>& cache()
        {
            static const auto paths = []
            {
                std::array<juce::Path, (size_t) Pict::count> a;
                for (size_t i = 0; i < a.size(); ++i)
                    a[i] = build ((Pict) i);
                return a;
            }();
            return paths;
        }
    }

    const juce::Path& path (Pict p)
    {
        return cache()[(size_t) juce::jlimit (0, (int) Pict::count - 1, (int) p)];
    }

    void draw (juce::Graphics& g, Pict p, juce::Rectangle<float> box, juce::Colour colour, float strokePx, bool filled)
    {
        const auto& shape = path (p);
        if (shape.isEmpty() || box.isEmpty())
            return;
        const float scale = juce::jmin (box.getWidth(), box.getHeight()) / 16.0f;
        const auto t = juce::AffineTransform::scale (scale).translated (box.getCentreX(), box.getCentreY());
        g.setColour (colour);
        if (filled)
            g.fillPath (shape, t);
        g.strokePath (shape, juce::PathStrokeType (strokePx / scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
    }

    const char* name (Pict p)
    {
        static constexpr const char* names[] {
            "none", "play", "load", "edit", "chop", "capture", "rack", "backup",
            "stop", "rec", "monitor", "grab", "usb", "plug-x", "lock", "unlock", "triangle", "info",
            "folder", "eye", "ear", "speaker", "feed", "clock", "device", "pads", "file", "upload", "export",
            "caret", "up", "down", "left", "right", "enter", "check", "cross", "prev", "next",
        };
        static_assert (std::size (names) == (size_t) Pict::count);
        return names[(size_t) juce::jlimit (0, (int) Pict::count - 1, (int) p)];
    }
}
