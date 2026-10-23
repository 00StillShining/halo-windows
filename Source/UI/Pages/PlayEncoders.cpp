#include "UI/Pages/PlayEncoders.h"
#include "Core/GrabMath.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/Gauges.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::String plusMinus = juce::String::charToString ((juce::juce_wchar) 0x00B1);
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);

        juce::String signedDb (double db)
        {
            const int v = juce::roundToInt (db);
            return (v < 0 ? minus : juce::String()) + juce::String (std::abs (v));
        }
    }

    EncoderBinding levelBinding (AppModel& model)
    {
        // The one monitor level (H7: strip, rail and meters agree).
        {
            constexpr double floorDb = MonitorGain::silenceFloorDb;
            EncoderBinding e;
            e.label = "LEVEL";
            e.getNorm = [&model] { return (model.monitorGainDb() - floorDb) / -floorDb; };
            e.setNorm = [&model] (double n) { model.setMonitorGainDb (std::round ((floorDb + n * -floorDb) * 2.0) / 2.0); };
            e.reset = [&model] { model.setMonitorGainDb (MonitorGain::defaultDb); };
            e.step = 1.0 / -floorDb;       // 1 db per detent
            e.fine = 0.5 / -floorDb;       // alt: 0.5 db (the slider's resolution)
            e.coarse = 6.0 / -floorDb;     // ctrl: 6 db
            e.format = [&model] { return signedDb (model.monitorGainDb()); };
            e.unit = [] { return juce::String ("db"); };
            e.caption = [&model]
            {
                if (model.monitorGainDb() <= MonitorGain::silenceFloorDb)
                    return juce::String ("muted");
                return model.monitor().isRunning() ? juce::String ("what you hear") : juce::String ("monitor off");
            };
            e.hint = [] { return "wheel " + plusMinus + "1 db" + dot + "alt " + plusMinus + "0.5" + dot + "ctrl " + plusMinus + "6"; };
            e.pressLabel = "monitor";
            e.press = [&model] { model.toggleMonitor(); };
            e.pressAvailable = [&model] { return model.monitor().isRunning() || model.monitor().ep40InputName().isNotEmpty(); };
            e.gauge = [&model] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                const double n = (model.monitorGainDb() - floorDb) / -floorDb;
                gauges::ladder (g, r.withSizeKeepingCentre (r.getWidth(), 12.0f), 20, juce::roundToInt (n * 20.0), c);
            };
            return e;
        }
    }

    EncoderBinding grabBinding (AppModel& model, int encoderIndex)
    {
        // GRAB length: bars while a clock runs with the transport playing, else seconds.
        {
            EncoderBinding e;
            e.label = "GRAB";
            e.steps = 3;
            auto index = [&model]
            {
                const bool bars = model.grabUsesBars();
                const int value = bars ? model.grabber().bars : model.grabber().seconds;
                for (int i = 0; i < 3; ++i)
                    if ((bars ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i]) == value)
                        return i;
                return 1;
            };
            e.getNorm = [index] { return index() / 2.0; };
            e.setNorm = [&model] (double n)
            {
                const int i = juce::jlimit (0, 2, (int) std::lround (n * 2.0));
                model.setGrabLength (model.grabUsesBars() ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i]);
            };
            e.reset = [&model] { model.setGrabLength (model.grabUsesBars() ? 8 : 10); };
            e.format = [&model] { return juce::String (model.grabUsesBars() ? model.grabber().bars : model.grabber().seconds); };
            e.unit = [&model] { return juce::String (model.grabUsesBars() ? "bars" : "s"); };
            e.caption = [&model] { return juce::String (model.grabUsesBars() ? "clock running" : "no clock: seconds"); };
            e.hint = [] { return "wheel: 4" + dot + "8" + dot + "16 bars (5" + dot + "10" + dot + "30 s)"; };
            e.pressLabel = "grab";
            e.press = [&model] { model.grabNow(); };
            e.pressAvailable = [&model] { return model.monitor().isRunning() && ! model.grabber().isBusy(); };
            e.gauge = [&model, index, encoderIndex] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                const bool bars = model.grabUsesBars();
                const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 12.0f);
                const int at = index();
                const float w = r.getWidth() / 3.0f;
                for (int i = 0; i < 3; ++i)
                {
                    const auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getCentreY() - 9.0f, w, 18.0f);
                    const auto text = juce::String (bars ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i]);
                    if (i == at)
                    {
                        g.setColour (c);
                        g.fillRoundedRectangle (cell.reduced (3.0f, 0.0f), 3.0f);
                        paint::text (g, text, cell, font, theme().encoder (encoderIndex).onFill, juce::Justification::centred);
                    }
                    else
                    {
                        paint::text (g, text, cell, font, theme().scr.dim, juce::Justification::centred);
                    }
                }
            };
            return e;
        }
    }

    std::array<std::optional<EncoderBinding>, 4> playBindings (AppModel& model, StageComponent& stage)
    {
        std::array<std::optional<EncoderBinding>, 4> b;
        b[0] = levelBinding (model); // 1. LEVEL
        b[1] = grabBinding (model, 1); // 2. GRAB length

        // 3. ORBIT: yaw on the turn, tilt on the shift layer; press = hero view.
        {
            constexpr float lo = StageComponent::orbitMinYaw, hi = StageComponent::orbitMaxYaw;
            const juce::String degree = juce::String::charToString ((juce::juce_wchar) 0x00B0);
            EncoderBinding e;
            e.label = "ORBIT";
            e.getNorm = [&stage] { return (double) ((stage.orbitYaw() - lo) / (hi - lo)); };
            e.setNorm = [&stage] (double n) { stage.setOrbit (lo + (float) n * (hi - lo), stage.orbitPitch()); };
            e.reset = [&stage] { stage.resetOrbit(); };
            e.step = 2.0 / (hi - lo);   // 2 degrees per detent
            e.fine = 0.5 / (hi - lo);
            e.coarse = 20.0 / (hi - lo);
            e.format = [&stage] { return juce::String (juce::roundToInt (stage.orbitYaw())); };
            e.unit = [degree] { return degree; };
            e.caption = [&stage, degree] { return juce::String (juce::roundToInt (stage.orbitPitch())) + degree + " tilt"; };
            e.hint = [] { return "wheel " + plusMinus + "2" + juce::String::charToString ((juce::juce_wchar) 0x00B0) + dot + "shift: tilt" + dot + "press: hero view"; };
            e.pressLabel = "reset view";
            e.press = [&stage] { stage.resetOrbit(); };
            e.shiftLabel = "TILT";
            e.shiftTurn = [&stage] (int detents) { stage.setOrbit (stage.orbitYaw(), stage.orbitPitch() + 2.0f * (float) detents); };
            e.shiftPressLabel = "top view";
            e.shiftPress = [&stage] { stage.setOrbit (stage.orbitYaw(), 89.0f); };
            e.gauge = [&stage] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                gauges::timeline (g, r, (stage.orbitYaw() - lo) / (hi - lo), 10, c);
            };
            b[2] = e;
        }

        // 4. ECHO MIX on the monitor route's dub rack; press engages / bypasses it.
        {
            auto& p = model.rack().parameters();
            EncoderBinding e;
            e.label = "ECHO MIX";
            e.getNorm = [&p] { return (double) p.echoMix.load(); };
            e.setNorm = [&p] (double n) { p.echoMix.store ((float) n); };
            e.reset = [&p] { p.echoMix.store (0.35f); };
            e.step = 0.01;
            e.format = [&p] { return juce::String (juce::roundToInt (p.echoMix.load() * 100.0f)); };
            e.caption = [&p, &model]
            {
                if (! p.engaged.load())
                    return juce::String ("rack bypassed");
                return model.monitor().isRunning() ? juce::String ("rack engaged") : juce::String ("engaged" + dot + "monitor off");
            };
            e.tag = [&p] { return juce::String (p.engaged.load() ? "ENGAGED" : "BYPASSED"); };
            e.tagOn = [&p] { return p.engaged.load(); };
            e.hint = [] { return "wheel " + plusMinus + "1" + dot + "press: engage or bypass the rack"; };
            e.pressLabel = "engage";
            e.press = [&p] { p.engaged.store (! p.engaged.load()); };
            e.gauge = [&p] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                gauges::tickDial (g, r, p.echoMix.load(), c);
            };
            b[3] = e;
        }
        return b;
    }
}
