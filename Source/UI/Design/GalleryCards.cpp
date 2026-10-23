// Gallery page 5 (R3): the four write confirms and the dead-confirm state,
// from fixture data under the permanent FIXTURE band. Nothing here can write:
// the fake device side only counts.
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/ConfirmCard.h"
#include "UI/Design/WriteGate.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

        struct NoWrites : DeviceWrites
        {
            void upload (const AudioData&, const juce::String&, int, std::function<void (Ep40Writer::Result)>, Ep40Writer::BytesProgress) override {}
            void uploadToPads (std::vector<Ep40Writer::PadJob>, int, std::function<void (const juce::String&)>, std::function<void (Ep40Writer::Result)>) override {}
            void assignPad (int, int, int, int, std::function<void (Ep40Writer::Result)>) override {}
            void deleteSound (int, std::function<void (Ep40Writer::Result)>) override {}
            bool isBusy() const override { return false; }
        };

        // One quadrant holding one card.
        class Holder : public juce::Component
        {
        public:
            Holder (WriteSpec spec, bool deadAfterOpen)
            {
                gate = std::make_unique<WriteGate> (*this, WriteGate::Environment {
                    [this] { return unlocked; },
                    [] { return true; },
                    [] { return "backup 18 oct 2026 21:04" + dot + "complete" + dot + "477 sounds"; },
                    [] {},
                    [] (bool) {},
                    &writes });
                spec.title = WriteSpec::headerFor (spec.kind);
                pendingSpec = std::move (spec);
                dead = deadAfterOpen;
            }

            void resized() override
            {
                if (gate->openCard() == nullptr && getWidth() > 0)
                {
                    gate->request (pendingSpec, [] (GatedWriter) {});
                    if (dead)
                    {
                        unlocked = false; // the backup vanished while the card was open
                        if (auto* card = gate->openCard())
                            card->refreshGate();
                    }
                }
                if (auto* card = gate->openCard())
                    card->setBounds (getLocalBounds());
            }

        private:
            NoWrites writes;
            std::unique_ptr<WriteGate> gate;
            WriteSpec pendingSpec;
            bool unlocked = true, dead = false;
        };

        class CardsPage : public juce::Component
        {
        public:
            CardsPage()
            {
                setOpaque (true);
                WriteSpec send;
                send.kind = WriteSpec::Kind::send;
                send.confirmLabel = "send";
                send.body = "Upload \"LONGSONG\" (1.72 MB) to empty slot 912 in USER?\n\nIt's auto-trimmed to the first 20 s of your selection.\n"
                            "Takes about 40 s. Nothing on the EP-40 is overwritten.";
                send.from = "longsong";
                send.to = "slot 912";
                send.estimateSeconds = 40.0;
                send.estimateText = "about 40 s";

                WriteSpec pads;
                pads.kind = WriteSpec::Kind::sendToPads;
                pads.confirmLabel = "send";
                pads.body = "Upload 12 slices (5.21 MB) into empty USER slots, and assign them to group A pads in project 3?\n\n"
                            "7 slice(s) are cut to 4.0 s.\nTakes about 2 min (the EP-40 takes about 40 KB/s; MONO or a LO quality is quicker).\n"
                            "No sound is overwritten. The pads' old sounds stay in the library.";
                pads.from = "12 slices";
                pads.to = "group a, project 3";
                pads.estimateSeconds = 125.0;
                pads.estimateText = "about 2 min";

                WriteSpec assign;
                assign.kind = WriteSpec::Kind::assign;
                assign.confirmLabel = "assign";
                assign.body = "Put \"BIGKICK\" on pad A 7 in project 3? It currently plays \"KICK 2\". Nothing is deleted: \"KICK 2\" stays in the library.";
                assign.from = "BIGKICK";
                assign.to = "pad a 7, project 3";

                WriteSpec del;
                del.kind = WriteSpec::Kind::deleteSound;
                del.destructive = true;
                del.confirmLabel = "delete";
                del.body = "Delete sound 912 \"LONGSONG\" from the EP-40?\n\nYour Halo backup still has a copy. Pads that use it will be silent.";
                del.from = "sound 912";
                del.to = "deleted";

                holders.add (new Holder (send, false));
                holders.add (new Holder (pads, false));
                holders.add (new Holder (assign, true));   // dead confirm: the gate closed while open
                holders.add (new Holder (del, false));
                for (auto* h : holders)
                    addAndMakeVisible (h);
                setSize (1440, 900);
            }

            void resized() override
            {
                const int w = getWidth() / 2, h = (getHeight() - 30) / 2;
                for (int i = 0; i < holders.size(); ++i)
                    holders[i]->setBounds ((i % 2) * w, 30 + (i / 2) * h, w, h);
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (theme().scr.bg);
                auto band = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 26.0f);
                g.setColour (theme().scr.panel);
                g.fillRect (band);
                paint::text (g, "FIXTURE" + dot + "GALLERY DATA, NOT DEVICE STATE" + dot + "THE FOUR WRITE CONFIRMS" + dot + "ASSIGN SHOWN DEAD (GATE CLOSED WHILE OPEN)",
                             band.reduced (24.0f, 0.0f), HaloFonts::font (Role::tag), theme().scr.ink);
            }

        private:
            juce::OwnedArray<Holder> holders;
        };
    }

    std::unique_ptr<juce::Component> createCardsGalleryPage() { return std::make_unique<CardsPage>(); }
}
