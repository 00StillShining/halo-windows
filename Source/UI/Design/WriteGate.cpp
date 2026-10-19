#include "UI/Design/WriteGate.h"
#include "UI/Design/Primitives/ConfirmCard.h"

namespace halo
{
    juce::String WriteSpec::headerFor (Kind k)
    {
        const auto ep = "EP" + juce::String::charToString ((juce::juce_wchar) 0x2013) + "40";
        switch (k)
        {
            case Kind::send:        return "SEND TO " + ep + "?";
            case Kind::sendToPads:  return "SEND TO PADS?";
            case Kind::assign:      return "ASSIGN TO PAD?";
            case Kind::deleteSound: return "DELETE FROM " + ep + "?";
        }
        return {};
    }

    WriteGate::WriteGate (juce::Component& h, Environment e) : host (h), env (std::move (e)) {}

    WriteGate::~WriteGate()
    {
        *alive = false;
        if (card != nullptr)
            host.removeChildComponent (card.get());
        card.reset();
        closed.clear();
    }

    bool WriteGate::available() const
    {
        const bool unlocked = env.writesUnlocked && env.writesUnlocked();
        const bool connected = env.connected && env.connected();
        const bool busy = env.writes != nullptr && env.writes->isBusy();
        return unlocked && connected && ! busy && env.writes != nullptr;
    }

    juce::String WriteGate::reason() const
    {
        const auto ep = "ep" + juce::String::charToString ((juce::juce_wchar) 0x2013) + "40";
        if (! (env.connected && env.connected()))
            return "The " + ep + " is not connected, so nothing was written.";
        if (env.writes != nullptr && env.writes->isBusy())
            return "Busy: sending to the " + ep + ". Wait for it to finish.";
        if (! (env.writesUnlocked && env.writesUnlocked()))
            return "Writes are locked: there's no complete backup of this " + ep + " (BACKUPS, Ctrl+6).";
        return {};
    }

    void WriteGate::request (WriteSpec spec, std::function<void (GatedWriter)> action)
    {
        // Upstream gating makes a locked request unreachable; never show a
        // confirm for a write that can't happen.
        if (! available())
        {
            DBG ("WriteGate: refused a confirm while the gate is closed: " + reason());
            return;
        }
        if (spec.title.isEmpty())
            spec.title = WriteSpec::headerFor (spec.kind);
        closeCard();
        pending = std::move (action);
        card = std::make_unique<ConfirmCard> (*this, std::move (spec));
        if (env.setStageHidden)
            env.setStageHidden (true);
        host.addAndMakeVisible (*card);
        card->setBounds (host.getLocalBounds());
        card->toFront (true);
        juce::Component::SafePointer<ConfirmCard> safe (card.get());
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->isShowing())
                safe->grabKeyboardFocus();
        });
        card->grabKeyboardFocus();
    }

    void WriteGate::closeCard()
    {
        if (card == nullptr)
            return;
        // Defer the deletion: we may be inside one of the card's own callbacks.
        card->setVisible (false);
        host.removeChildComponent (card.get());
        closed.push_back (std::move (card));
        juce::MessageManager::callAsync ([this, token = std::weak_ptr<bool> (alive)]
        {
            if (! token.expired())
                closed.clear();
        });
        pending = nullptr;
        if (env.setStageHidden)
            env.setStageHidden (false);
    }

    void WriteGate::confirmed()
    {
        // Last check: rescan the backups synchronously, then the gate.
        if (env.forceRescan)
            env.forceRescan();
        if (card != nullptr)
            card->refreshGate();
        if (! available())
            return; // the card stays open and shows the reason; nothing is written
        auto action = std::move (pending);
        closeCard();
        if (action)
            action (GatedWriter (*this));
    }

    //==========================================================================
    bool GatedWriter::check (const std::function<void (Ep40Writer::Result)>& done) const
    {
        if (gate->available())
            return true;
        if (done)
            done (Ep40Writer::Result { false, 0, gate->reason() });
        return false;
    }

    void GatedWriter::upload (const AudioData& audio, const juce::String& name, int slot, std::function<void (Ep40Writer::Result)> done,
                              Ep40Writer::BytesProgress onBytes) const
    {
        if (check (done))
            gate->env.writes->upload (audio, name, slot, std::move (done), std::move (onBytes));
    }

    void GatedWriter::uploadToPads (std::vector<Ep40Writer::PadJob> jobs, int project, std::function<void (const juce::String&)> onProgress,
                                    std::function<void (Ep40Writer::Result)> done) const
    {
        if (check (done))
            gate->env.writes->uploadToPads (std::move (jobs), project, std::move (onProgress), std::move (done));
    }

    void GatedWriter::assignPad (int project, int group, int gridIndex, int slot, std::function<void (Ep40Writer::Result)> done) const
    {
        if (check (done))
            gate->env.writes->assignPad (project, group, gridIndex, slot, std::move (done));
    }

    void GatedWriter::deleteSound (int slot, std::function<void (Ep40Writer::Result)> done) const
    {
        if (check (done))
            gate->env.writes->deleteSound (slot, std::move (done));
    }
}
