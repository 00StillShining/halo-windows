#include "UI/Design/Primitives/ConfirmCard.h"
#include "UI/Design/WriteGate.h"

#include <doctest/doctest.h>

#include <optional>

using namespace halo;

namespace
{
    struct FakeWrites : DeviceWrites
    {
        int uploads = 0, padUploads = 0, assigns = 0, deletes = 0;
        bool busy = false;

        void upload (const AudioData&, const juce::String&, int, std::function<void (Ep40Writer::Result)> done, Ep40Writer::BytesProgress) override
        { ++uploads; if (done) done ({ true, 912, "ok" }); }
        void uploadToPads (std::vector<Ep40Writer::PadJob>, int, std::function<void (const juce::String&)>, std::function<void (Ep40Writer::Result)> done) override
        { ++padUploads; if (done) done ({ true, 0, "ok" }); }
        void assignPad (int, int, int, int, std::function<void (Ep40Writer::Result)> done) override
        { ++assigns; if (done) done ({ true, 0, "ok" }); }
        void deleteSound (int, std::function<void (Ep40Writer::Result)> done) override
        { ++deletes; if (done) done ({ true, 0, "ok" }); }
        bool isBusy() const override { return busy; }
        int total() const { return uploads + padUploads + assigns + deletes; }
    };

    struct Rig
    {
        juce::Component host;
        FakeWrites writes;
        bool unlocked = true, connected = true, stageHidden = false;
        int rescans = 0;
        std::function<void()> onRescan;
        WriteGate gate { host, WriteGate::Environment {
            [this] { return unlocked; },
            [this] { return connected; },
            [] { return juce::String ("backup 18 oct 2026 21:04, complete, 477 sounds"); },
            [this] { ++rescans; if (onRescan) onRescan(); },
            [this] (bool hidden) { stageHidden = hidden; },
            &writes } };

        Rig() { host.setSize (1180, 720); }

        static WriteSpec spec (WriteSpec::Kind k)
        {
            WriteSpec s;
            s.kind = k;
            s.body = "Body copy.";
            s.confirmLabel = k == WriteSpec::Kind::deleteSound ? "delete" : k == WriteSpec::Kind::assign ? "assign" : "send";
            s.destructive = k == WriteSpec::Kind::deleteSound;
            return s;
        }

        // An action that writes the way its page does.
        static std::function<void (GatedWriter)> writeFor (WriteSpec::Kind k)
        {
            return [k] (GatedWriter w)
            {
                switch (k)
                {
                    case WriteSpec::Kind::send:        w.upload (AudioData {}, "x", 0, nullptr); break;
                    case WriteSpec::Kind::sendToPads:  w.uploadToPads ({}, 3, nullptr, nullptr); break;
                    case WriteSpec::Kind::assign:      w.assignPad (3, 0, 0, 912, nullptr); break;
                    case WriteSpec::Kind::deleteSound: w.deleteSound (912, nullptr); break;
                }
            };
        }
    };

    constexpr WriteSpec::Kind allKinds[] { WriteSpec::Kind::send, WriteSpec::Kind::sendToPads,
                                           WriteSpec::Kind::assign, WriteSpec::Kind::deleteSound };
}

TEST_CASE ("writegate: every write kind opens a card, and only a confirm writes")
{
    for (auto kind : allKinds)
    {
        Rig rig;
        rig.gate.request (Rig::spec (kind), Rig::writeFor (kind));
        auto* card = rig.gate.openCard();
        REQUIRE (card != nullptr);
        CHECK (card->spec().title == WriteSpec::headerFor (kind));
        CHECK (rig.stageHidden);            // airspace: the GL stage hides under the card
        CHECK (rig.writes.total() == 0);    // opening writes nothing

        card->pressConfirm();
        CHECK (rig.gate.openCard() == nullptr);
        CHECK (rig.rescans == 1);           // a forced backup rescan before the write
        CHECK (rig.writes.total() == 1);
        CHECK_FALSE (rig.stageHidden);
    }
}

TEST_CASE ("writegate: cancel and Esc write nothing; a fresh Enter cancels; Tab then Enter confirms")
{
    Rig rig;
    rig.gate.request (Rig::spec (WriteSpec::Kind::send), Rig::writeFor (WriteSpec::Kind::send));
    rig.gate.openCard()->pressCancel();
    CHECK (rig.gate.openCard() == nullptr);

    rig.gate.request (Rig::spec (WriteSpec::Kind::send), Rig::writeFor (WriteSpec::Kind::send));
    rig.gate.openCard()->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (rig.gate.openCard() == nullptr);

    rig.gate.request (Rig::spec (WriteSpec::Kind::send), Rig::writeFor (WriteSpec::Kind::send));
    rig.gate.openCard()->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    CHECK (rig.gate.openCard() == nullptr);
    CHECK (rig.writes.total() == 0);

    rig.gate.request (Rig::spec (WriteSpec::Kind::send), Rig::writeFor (WriteSpec::Kind::send));
    auto* card = rig.gate.openCard();
    CHECK_FALSE (card->confirmHasFocus());   // cancel has default focus
    card->keyPressed (juce::KeyPress (juce::KeyPress::tabKey));
    CHECK (card->confirmHasFocus());
    card->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    CHECK (rig.gate.openCard() == nullptr);
    CHECK (rig.writes.uploads == 1);
}

TEST_CASE ("writegate: the gate closing while the card is open kills the confirm")
{
    struct Case { const char* what; std::function<void (Rig&)> close; const char* reasonWord; };
    const Case cases[] {
        { "writes locked", [] (Rig& r) { r.unlocked = false; }, "locked" },
        { "disconnected", [] (Rig& r) { r.connected = false; }, "not connected" },
        { "a send started", [] (Rig& r) { r.writes.busy = true; }, "Busy" },
    };
    for (const auto& c : cases)
    {
        INFO (c.what);
        Rig rig;
        rig.gate.request (Rig::spec (WriteSpec::Kind::assign), Rig::writeFor (WriteSpec::Kind::assign));
        auto* card = rig.gate.openCard();
        REQUIRE (card != nullptr);
        CHECK (card->confirmIsLive());

        c.close (rig);
        card->refreshGate(); // the card does this at 2 Hz
        CHECK_FALSE (card->confirmIsLive());
        CHECK (card->shownReason().contains (c.reasonWord));

        card->pressConfirm();
        CHECK (rig.writes.total() == 0);
        CHECK (rig.gate.openCard() == card); // still open, showing why

        card->keyPressed (juce::KeyPress (juce::KeyPress::tabKey));
        CHECK_FALSE (card->confirmHasFocus()); // a dead key can't take focus
    }
}

TEST_CASE ("writegate: the confirm's own rescan can close the gate")
{
    Rig rig;
    rig.onRescan = [&rig] { rig.unlocked = false; }; // the backup vanished from disk
    rig.gate.request (Rig::spec (WriteSpec::Kind::deleteSound), Rig::writeFor (WriteSpec::Kind::deleteSound));
    auto* card = rig.gate.openCard();
    card->pressConfirm();
    CHECK (rig.rescans == 1);
    CHECK (rig.writes.total() == 0);
    REQUIRE (rig.gate.openCard() != nullptr);
    CHECK_FALSE (rig.gate.openCard()->confirmIsLive());
}

TEST_CASE ("writegate: the gated writer checks again at the moment of the write")
{
    Rig rig;
    std::optional<GatedWriter> later;
    rig.gate.request (Rig::spec (WriteSpec::Kind::send), [&later] (GatedWriter w) { later = w; }); // e.g. rendering first
    rig.gate.openCard()->pressConfirm();
    REQUIRE (later.has_value());

    rig.connected = false; // unplugged while rendering
    bool failed = false;
    later->upload (AudioData {}, "x", 0, [&failed] (Ep40Writer::Result r) { failed = ! r.ok && r.message.isNotEmpty(); });
    CHECK (failed);
    CHECK (rig.writes.uploads == 0);
}

TEST_CASE ("writegate: a closed gate never opens a card")
{
    Rig rig;
    rig.unlocked = false;
    rig.gate.request (Rig::spec (WriteSpec::Kind::send), Rig::writeFor (WriteSpec::Kind::send));
    CHECK (rig.gate.openCard() == nullptr);
    CHECK (rig.writes.total() == 0);
}
